# Retro Engine v5 (Sonic Mania) for Chimera

This repository builds the RSDKv5 decompilation, a decompilation of the
Retro Engine v5, with the Sonic Mania decompilation's game logic compiled
in, as a game core for [Chimera](https://github.com/ToolAssisted-run/chimera),
a frontend for tool-assisted speedruns. The core plays Sonic Mania frame by
frame, the same every time, with savestates, rewinding and movies, its
videos and two-player modes included.

The build makes one file, `rsdkv5.chimeraCore`. Put it in Chimera's `Cores`
folder.

## What you bring

The game's data file, `Data.rsdk`, as the project's firmware. This package
contains none of the game.

- The checked file is 199 MiB, SHA-1
  `ED5C790106A670E6FDF04CCD876D560B1B08C895`.
- Other releases' `Data.rsdk` files (Steam's, the consoles') are the same
  kind of file; none was tried. A file of your own can be used in the
  original's place, and the project then records its hash.

## Settings

Language, region, Plus content (off by default), screen width (320 to 424
pixels, 424 by default) and the random seed. Each is recorded in the
project.

## What it does not do yet

- The Plus content (the Plus title, Encore mode, Mighty and Ray) is off
  unless the project turns its Plus Content setting on, as the engine
  decompilation's licence requires (a setting off by default).
- Mods (the decompilation's mod loader) are off.

[docs/PLAN.md](docs/PLAN.md) says what was decided and measured,
[docs/BUILDING.md](docs/BUILDING.md) how to build and test it.

## Licence

This repository's own files are MIT. The engine is the RSDKv5 decompilation
(by Rubberduckycooly and chuliRMG, of the Retro Engine v5 by Evening Star),
under its own licence in [LICENSE-RSDK.txt](LICENSE-RSDK.txt), and the game's
logic is the Sonic Mania decompilation, under its own licence in
[LICENSE-SonicMania.txt](LICENSE-SonicMania.txt): no use for profit, credit
to their authors, downloadable content off, no game assets distributed. A
package built from here is under those terms. [LICENSE](LICENSE) says what
each part is under.
