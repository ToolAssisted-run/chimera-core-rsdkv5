# Plan and design log - Retro Engine v5 (Sonic Mania) core

Dated entries; the newest decisions are at the end of each section.

## The request (2026-10-17)

Chimera issue 295 asked for three cores: the Retro Engine v3, v4 and v5
decompilations. The owner asked for each to be analysed and built if it is
technically and legally sound, and for the work to stay local: nothing
pushed, no comment on the issue. One core per engine (the owner's choice).
This one plays Sonic Mania; the owner supplied its data file for testing.
The Sonic CD and Sonic 1 and 2 cores (chimera-core-rsdkv3, -rsdkv4) were
built first, and this one follows their design.

## Feasibility

- **Licence.** Two decompilations, each under its own licence
  (LICENSE-RSDK.txt for the engine, v2.1; LICENSE-SonicMania.txt for the
  game's logic, v1): copying, changing and distributing for public or
  private use, with no use for profit, credit, no game assets, the licence
  with modified source; the engine's also asks for the downloadable content
  off by default in any build given out. The core defines `RSDK_AUTOBUILD`,
  declares `commercialUse: no`, credits the authors and carries both texts.
- **The game is code.** Unlike v3 and v4, whose games are scripts in the
  data file, a v5 game's logic is a program. The Sonic Mania decompilation
  is compiled in (upstream's GAME_STATIC way), as the last Steam release
  (GAME_VERSION 6), with the engine pinned at the commit it pins.
- **One thread on one clock.** The engine's main loop is one frame a pass;
  the patch splits it into a start-up, a frame and a shutdown, since its
  body uses `continue` to skip a frame. The only thread is the one that
  loads a music track; the core runs the load at once (SDL_CreateThread is
  answered by calling the function). Videos are timed by the music's play
  position or the frame count.
- **The picture.** A software renderer into 16-bit screens; the SDL 2
  device then scales the screens, a video, or the two players' screens
  into its window. The core reads that window back from SDL's software
  renderer (SDL_RenderReadPixels), so the picture is upstream's own
  composition, videos included.
- **What a runner can test.** No game can be written for the tests: the
  logic compiled in is Sonic Mania's, which needs Sonic Mania's data. The
  runner proves the build, the declarations, the refusals and the contract
  tests; everything that plays runs where the data file is (new-core.md
  allows this).

## How the core is built (2026-10-17)

As the v3 core (real SDL 2 in its minimal configuration; the math
functions, rand and the clocks answered by the core), and:

- **Files** (rsdk-files.c): Data.rsdk from sealed memory; Settings.ini
  written from the project's settings; every file the engine or the game
  writes (saves, time attack records, options, the settings written back)
  a file in guest memory, created on write and read back by name. A file
  that does not start with `RSDKv5` is refused: the engine would otherwise
  take it for no data and start its developer menu.
- **Input**: the engine keeps a keyboard's state as a bit mask a player
  (SKU::keyState); the core sets players 1's and 2's from the buttons
  before each frame.
- **Sound**: the engine mixes in floats; the core runs the mixer for 735
  samples a frame and makes them 16-bit.
- **Dates**: time() is the random seed setting, and localtime() reads it as
  UTC (the dates the game writes into its records).
- **Mania's logic** is compiled with `-fno-ipa-icf`: upstream turns off
  MSVC's merging of identical functions because the game compares function
  addresses (not drowning on a Hydrocity pull chain), and GCC's -O2 merges
  them too.

## Measured (2026-10-17)

- Sonic Mania, the owner's data (199 MiB, SHA-1 ED5C7901...): SEGA logo,
  credits, the title, the attract video (Theora), the main menu, Mania Mode
  with No Save, the Angel Island opening and its Phantom Ruby cutscene, and
  Green Hill Zone act 1 (the title card, play, three deaths to the first
  Motobug, game over). Pictured frame by frame.
- 5200 frames from power-on into Green Hill: native = sandbox (picture,
  sound, frame lengths, lag, memory); rerecord (a save and a load before
  every frame) = straight; sessions saved during the opening video and in
  Green Hill, loaded into a new process, = straight. State: 41 MB.
- Speed: 4300 frames in 4 s in the sandbox; 4300 with a save and a load
  before each, 62 s.
- Core tests: 15 with the game (5 without it); frontend tests 9, with the
  game opened in Chimera itself; contract tests 20.
- Each new test was broken on purpose and seen to fail: the refusal of a
  file that is not a pack (before the check existed), turbo, input, the
  pokes, the random seed, teeth.

## The Plus content setting (user-decided, 2026-10-17)

The engine's licence asks for the downloadable content off by default in
any build given out, and allows "a configuration setting ... so long as it
is set to off by default". The build keeps RSDK_AUTOBUILD (which forced the
Plus flag off in two places, the dummy user core's construction and every
stage load); patches/0002 makes both places take the project's Plus Content
setting instead, false unless set. Measured on the owner's data: with it on
the title is Sonic Mania Plus's and the Plus start (the autosave notice)
follows; the core test "settings: plus" compares the first 1300 frames on
and off, and native with sandbox on (seen failing with the setting
ignored).

## Windows (2026-10-17)

The package played in Chimera on Windows (the 2026-10-16 nightly, headless,
with a configuration of its own), 5500 frames of Sonic Mania from power-on into Green Hill, through the frontend
test's witness: the memory domains after the last frame are the Linux
reference's, byte for byte. The witness now runs to a frame count rather
than a number of frames: on Windows a project opens two frames in, and a
project run pauses at the end of its movie.

## Sonic Mania's own variables (2026-10-17)

The property table now has, beside the engine's (the scene, its timer, the
two players' positions and speeds), what Sonic Mania itself keeps for a
player: Character (Sonic, Tails, Knuckles, and Mighty and Ray in the Plus
build), Rings, Lives, Score, Shield, Invincible Timer, Speed Shoes Timer,
Super State and Is Player, and in a Game group the game's mode (Mania,
Encore, Time Attack, Competition) and its continues. The offsets and the
named values come from the game's own headers: `waterbox/mania-state.c` is
compiled with them, so a change to the game's structures moves the table
with it. A player's fields are read only from an object that is the game's
Player; they are zero, and Is Player false, while it is not.

Rings, lives and score can be set. Shield and the two timers are read only:
a shield is also an object of its own, and invincibility and speed shoes
also start stars, music and speeds, so writing the number alone would give
half of one.

Measured: on the core tests' route, in Green Hill, Player 1 is Sonic with 3
lives and 0 rings in Mania mode and Player 2 is Tails; rings 50, lives 9 and
score 12340 written before frame 4950 are still the game's 40 frames later,
in both builds (the core test "variables", seen failing with the rings not
written back). In Chimera, Lua's game.describe names the character Sonic,
game.set takes Player 1.Rings and refuses Player 1.Shield (the frontend
test "gui:mania-variables", seen failing with Shield writable). 17 core
tests and 10 frontend tests pass with the game; the contract tests 20.

## Not done

- Other releases' data (Steam, consoles, Sonic Origins Plus): not tried.
- The engine's v3 and v4 modes (v5U plays those games' data too, as Sonic
  Origins does) are built in but were not made to work: the v3 test game
  showed a black screen. The rsdkv3 and rsdkv4 cores play those games.
- Mods: the mod loader is off.
