# AGENTS.md - Retro Engine v5 (Sonic Mania) core for Chimera

This repository builds the RSDKv5 decompilation (Retro Engine v5) with the
Sonic Mania decompilation's logic compiled in, as a core for Chimera
(https://github.com/ToolAssisted-run/chimera). It is a GAME core
(`"kind": "game"`). It produces one file, `rsdkv5.chimeraCore`. The game's
data file is never in the repository or the package: the user provides it.

## Layout

- `extern/RSDKv5`: the engine's decompilation, a submodule. Never edited in
  place. `extern/SonicMania`: the game's logic, compiled in unmodified.
- `extern/{SDL,ogg,theora}`: library submodules.
- `patches/`: numbered patches for `extern/RSDKv5`, applied by
  `waterbox/apply-patches.sh`.
- `waterbox/sources.mk`: the sources, defines and link wraps both builds
  share. `native.mk`, `guest.mk`: the two builds.
- `waterbox/rsdk-driver.cpp`: the machine (a frame, the sound, the picture).
  `rsdk-files.c`: every file the engine opens. `seams.c`: what SDL is
  answered with. `detmath.c`: the math functions. `game-state.cpp`: the
  memory domains and properties; `mania-state.c`: where Sonic Mania keeps a
  player's rings, lives and score, from the game's own headers. `wbx-entry.c`: the guest's exports.
- `waterbox/waterbox.config`, `file_slots.json`, `default_keybinds.json`,
  `package-licenses.json`: the declarations.
- `waterbox/run-gate.sh`: the core's tests. `waterbox/tests/`: the frontend
  tests. There is no test game: the game's logic is compiled in.
- `build/`: every output. `tests/roms-local/`: game data, ignored by git.

## Commands, in order

```
git submodule update --init --recursive
cd waterbox
./run-gate.sh -m <miniBox> [-g <Data.rsdk>]
./tests/run-frontend.sh --chimera-root <chimera> [-d <Data.rsdk>]
./build-package.sh -m <miniBox>
```

`docs/BUILDING.md` has the requirements and the contract tests.

## Rules

- Upstream is changed only by a numbered patch. The decompilations'
  licences (LICENSE-RSDK.txt, LICENSE-SonicMania.txt) cover the patches and
  the game's logic: keep them in the repository and the package, keep
  `RSDK_AUTOBUILD` defined, distribute no game asset. Sonic Mania's logic is
  compiled with `-fno-ipa-icf` (upstream: the game compares function
  addresses); keep it.
- Native and sandbox must stay the same machine: anything that reads a
  clock, a thread, the C library's math or rand is answered by the core.
- Every new test is broken on purpose once and seen to fail.
- No game data in the repository, in CI or in an issue.
- Plain language and ASCII in documents. Commits: `type(scope): what`, a
  short body with the numbers, explicit paths staged.
