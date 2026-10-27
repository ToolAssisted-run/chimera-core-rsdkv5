# Building and testing the Retro Engine v5 (Sonic Mania) core

## Requirements

Linux, x86-64 (CI uses `ubuntu-latest`). gcc and g++, make, python3, and a
Chimera checkout with miniBox built with its C++ guest toolchain:

```
git clone --recursive https://github.com/ToolAssisted-run/chimera.git <chimera>
mb=<chimera>/extern/chimera-common-minibox
meson setup "$mb/build/meson-linux" "$mb" && meson compile -C "$mb/build/meson-linux"
meson setup "$mb/build/meson-cpp" "$mb" -Dguest_cpp=true && meson compile -C "$mb/build/meson-cpp"
```

For the frontend tests, Chimera built with its window (its README, "Building";
`tools/ci-chimera.sh` does it). The window runs headless and needs no display.

## Sources

- `extern/RSDKv5`: the RSDKv5 decompilation, pinned at the commit the
  Sonic Mania decompilation pins, with its stb_vorbis submodule. Never
  edited in place: `patches/` is applied by `waterbox/apply-patches.sh`,
  which every build runs first.
- `extern/SonicMania`: the Sonic Mania decompilation, the game's logic,
  compiled in unmodified.
- `extern/SDL` (release-2.32.10), `extern/ogg` (v1.3.5), `extern/theora`
  (v1.1.1).

```
git submodule update --init --recursive
```

## Building

```
cd waterbox
make -f native.mk -j$(nproc) MB=<miniBox>   # build/native/run-native, run-wbx
make -f guest.mk -j$(nproc) MB=<miniBox>    # build/guest/core.wbx
./build-package.sh -m <miniBox>             # build/package/rsdkv5.chimeraCore
./build-package.sh -r <chimera>             # into <chimera>/build/Cores
```

The scripts find miniBox from `-m`, else `MINIBOX_DIR`, else
`~/chimera/extern/chimera-common-minibox`.

## Testing

```
./waterbox/run-gate.sh -m <miniBox>                       # the core's tests
./waterbox/run-gate.sh -m <miniBox> -g <Data.rsdk>        # and on Sonic Mania
./waterbox/tests/run-frontend.sh --chimera-root <chimera> -d <Data.rsdk>  # the frontend's
```

Then Chimera's contract tests against the package:

```
CHIMERA_CORES_DIR=<folder with the package> <chimera>/build/meson-linux/source/ui/test-packages
```

The game's logic is compiled into the core, so there is no test game:
without `-g`, `run-gate.sh` runs only the tests that need no game (the
refusals, the clock, the firmware name), and `run-frontend.sh` without the
data file only builds the package. With the data file they play 5200 and
5500 frames from power-on through the title, the menus, the opening video and
the Angel Island opening into Green Hill. Keep the data file outside the
repository, or in `tests/roms-local/` (`run-frontend.sh` looks there), which
git ignores.

## What CI does

`.github/workflows/chimera.yml`: builds Chimera and miniBox, runs
`run-gate.sh` and `run-frontend.sh` (without the game), builds the package
stamped with the commit, runs the contract tests, and publishes through
Chimera's `publish-core.yml`.

## Troubleshooting

- "not a Retro Engine v5 data file": the file does not start with
  `RSDKv5` (the engine itself would start its developer menu instead).
- A change to SDL's, the codecs' or the engine's flags rebuilds everything:
  every object depends on the flags it was built with.
- Native and sandbox differ: a math function the engine, the game or a
  codec calls is not wrapped (sources.mk MATH_FUNCS).
