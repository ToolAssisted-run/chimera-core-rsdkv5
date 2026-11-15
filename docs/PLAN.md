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

## Developer menu, save data, four players, player name (2026-10-17, chimera#302, user-decided)

The owner chose these four from a user's list.

- **Developer menu**: a Developer Menu setting writes devMenu=y into the
  engine's settings file and adds a Dev Menu control. The engine opens the
  menu when Escape goes down, in its SDL event loop (ProcessEvents), which
  this driver does not run: the players' buttons are set in the keyboard
  state instead. So a press of Dev Menu calls what Escape calls there,
  OpenDevMenu or CloseDevMenu, before the frame. With the setting on the
  game also runs as a developer's build (the engine's PLATFORM_DEV), as
  under the decompilation. The control is live only with the setting on.
- **Save data**: the files the game writes (SaveData.bin and any other but
  Settings.ini and the engine's log) are the Export Save Data group, and a
  Save data slot puts them back before the seal. Settings.ini is refused:
  it is the project's settings.
- **Players 3 and 4**: a Players setting, 2 by default. At 4, players 3 and
  4 get the twelve buttons each (the engine's keyboard devices 3 and 4, as
  the Plus content's competition mode takes four players). At 2 those
  buttons are not live, so a project made before keeps its inputs.
- **Player name**: a Player Name setting, written as username= into the
  engine's settings file (printable characters, no brackets, semicolons or
  line breaks, at most 31). The main menu shows it at the bottom left.

Tests (each failed with its part taken out): `save data` (the route writes
SaveData.bin, 65,536 bytes; given back, the save select shows slot 1 in
Green Hill where a fresh game shows a new game; Settings.ini is refused;
native = sandbox), `developer menu` (the picture changes with the setting
on, not with it off; native = sandbox), `players` (with Players 4, player
3's Start on the title does what player 1's does; with 2 it does nothing;
native = sandbox) and `settings` for the name (CHIMERA draws another main
menu; native = sandbox).

## Mods (owner, 2026-11-15, issue 590)

The owner's decision, Chimera issue 590: mods as a project slot. The
decompilation's mod loader is turned on; a project holds mods as zips of the
decompilation's mod folders, each recorded by its SHA-1, loaded at power-on
and part of the machine, so a movie replays with the same mods. Nothing is
downloaded. What a mod writes stays inside the machine, never on the
computer's disk. A mod with compiled code of its own is refused with a text
that names it. The model is the Cemu core's Graphic Packs slot. No change to
Chimera.

**What a mod is.** A folder with a mod.ini (its name, author, version and
options) and the files it replaces or adds, at the same paths as in the
data file: Data/Palettes, Data/Sprites, Data/Stages, Data/Game/GameConfig.bin
and so on. A Data/Game/Game.xml adds to the game's configuration (a title,
palettes, objects, sound effects, stages) on top of the data file's. A mod
can also carry compiled code (mod.ini's LogicFile: a Windows or Linux
library) that the engine loads to change the game's logic. The decompilation
has no script language for a v5 game: Sonic Mania's logic is compiled code,
compiled into this core, so a mod changes it only with a LogicFile. (The
engine's Scripts folders and TxtScripts are for the v3 and v4 games, which
this core does not play.)

**The slot.** `mods`, "Mods", in `file_slots.json`: any number of zips
(`min` 0, `max` -1, format `zip`). A zip holds one or more mod folders, each
with its mod.ini, at its root or deeper; a zip with mod.ini at its root is
one mod, named as the zip. The project records each zip by its name and
SHA-1, in the slot's order, as it records any slot's file.

**How they are loaded** (`waterbox/rsdk-files.c`, `waterbox/mod-fs.hpp`,
`patches/0003`). At Init, before the seal, each zip is read (miniz, which
the engine already has) and every mod's files are unpacked into one block of
sealed memory, read-only from then on and in no savestate, as the folder
mods/<the mod's folder>/ of a "mods" folder that exists only in the
machine. The core writes the engine's own modconfig.ini there (every mod on,
in order). Then upstream's own mod loader runs as it does on a PC. It finds
its mods folder with std::filesystem, which the sandbox cannot answer (its
files have no folders); patch 0003 has ModAPI.cpp take the core's
`mod-fs.hpp` instead, which answers the same questions (is there a file or
a folder at this path, what does this folder hold) from the unpacked mods,
as upstream's Android build answers them with its own. Both builds use it,
so the native reference reads no disk either. A mod's files are then opened
through the core's file layer like the data file.

**The order** follows the slot: the mods of its first zip first. Upstream's
own order file, the mods folder's modconfig.ini, is used inside a zip: when
a zip has one beside its mod folders (a zip of a whole mods folder), its
order is that zip's order and a mod it turns off is left out; else the
folders are taken by name. Between zips the slot decides, since the project
records that order and the window shows it. The engine then does what it
does with its own list: where two mods have the same file, the one that
comes first is used (the top of the developer menu's Mods page). Every
mod's Game.xml is read, the first one's first, each adding to what the
others added; where two set one thing (the game's title), the later one's
stays, as upstream does.

**Refused,** each with a text in all 26 languages that names the zip, and
the mod where there is one: a file that is not a zip; a zip with no mod.ini;
two mods with one folder name; a mod for another version of the engine
(TargetVersion or ForceVersion other than 5); mods larger than the room left
in sealed memory (1024 MiB less the data file, about 825 MiB with Sonic
Mania's); and a mod with compiled code (a LogicFile). That code is a
library built for Windows or Linux that the engine would load with the
computer's dynamic loader. The sandbox has no dynamic loader and runs only
code built for it, so such a mod cannot work here, and loading its data
without its code would be another mod than its author made.

**What a mod writes.** The mod loader writes three kinds of file into the
mods folder: a mod's modConfig.cfg (when its mod.ini names a ConfigFile, as
it loads), a mod's modSettings.ini and the folder's modconfig.ini (when the
developer menu's Mods page saves). They are files in guest memory like the
game's saves, kept by their whole path, so each mod has its own; a savestate
carries them. They are not in Export Save Data: they are the mod loader's,
written again from the mods' own files. A mod's RedirectSaveRAM puts the
game's saves in its folder: there they are the game's saves under their own
names (SaveData.bin), exported and taken back by the Save data slot as
always.

**No mods: the machine as before.** Built with the mod loader, the engine
behaves the same when there are no mods except in two places, both changed
by patch 0003 for this build only: the mod loader drew a loading screen
("Mod loading done!") into the game's screen at power-on, which stayed in
the first frames (the machine shows nothing while it starts, so the core
leaves it out, mods or not), and the developer menu gained a Mods entry,
which moved Exit down (it is there only when the project has mods). The
Mods page lets a player turn mods off and on and reorder them, inside the
machine: a movie records the presses, and the project's slot stays what it
was.

**Measured** (2026-11-15, jaffanator01, in the chimera-dev container,
Ubuntu 24.04, GCC 13.3). Sonic Mania's data file is not on jaffanator01 or
on the machine the work was done from, so nothing here played Sonic Mania.
The tests use content of their own, made when they run
(`waterbox/tests/make-mods.py`): a data file of one file, a GameConfig.bin
whose title is CHIMERA PACK, and mods that replace it or add to it. The
engine runs it with nothing to play; with the Developer Menu setting on,
Dev Menu opens the engine's developer menu, which draws the game's title,
so what a mod changes is in the picture. The pictures were looked at:
CHIMERA PACK with no mods; ALPHA MOD with alpha.zip first, BETA MOD with
beta.zip first; TWO MOD for a zip whose modconfig.ini puts Two first; XML
MOD with the version PACK still the data file's for a Game.xml; the Mods
page listing Alpha Mod and Beta Mod, both on; and after alpha is turned off
there and the game restarted, BETA MOD.

- run-gate.sh, without the game: 21 passed (5 of them the tests there
  were, 16 new), the game's tests skipped. The new: a mod's GameConfig.bin
  and Game.xml change the game; the slot's order and a zip's modconfig.ini;
  two mods native = sandbox; the five refusals, in both builds; the
  modconfig.ini the engine writes read back after the restart, and none of
  the mod loader's files in Export Save Data; under strace, neither build
  opens a file on the disk to write (but the harness's own picture) or
  makes, removes or renames one, along a route on which the engine writes
  alpha's modConfig.cfg and modconfig.ini twice (seen under gdb: every
  write lands in the core's file layer); along that route, native = sandbox
  = a state saved and loaded before every frame = a state saved at frame
  75, 100 or 150 and loaded into a new host; and with no mods the digests
  (picture, sound, frame lengths, lag, memory) of the commit before, with
  the developer menu opened and moved through and without, in both builds.
- run-frontend.sh, without the game: the package, and engine:mods: a
  project made by chimera-run with alpha.zip and beta.zip in its Mods slot
  and that route as its movie: Game State, Players and the last picture are
  the native reference's after 240 frames, straight, with a state around
  every frame, from a state carried into another process, and replaying the
  movie chimera-run recorded, made into a project again; the project names
  the zips in order by SHA-1. 2 passed, the game skipped.
- Chimera's contract tests on the package: 24 passed.
- Seen to fail, once each, by breaking the code: the slot ignored (the
  change, the order, the refusals and the read-back fail); the engine's
  modconfig.ini written in reverse; a zip's modconfig.ini ignored; the
  LogicFile check off; the mod loader's writes also tried on the disk (the
  strace test names the open); its writes kept by their last name only (the
  read-back); its files let into Export Save Data; the order made to follow
  where the host put the mods (native = sandbox, and the order, fail
  natively); a count of the engine's writes kept outside the machine's
  state deciding which GameConfig.bin is read (the state saved at frame 100
  and loaded into a new host fails, at 75 and 150 not, as expected); the
  Mods entry always in the developer menu (no mods = before); the slot left
  out of the package (engine:mods: chimera-run refuses the project). A
  break that only the test of a state around every frame would catch was
  not found: a state saved and loaded in the same host gives back what was
  just saved.

Not tried: Sonic Mania's own data with any mod (the data file was not
there), and so no mod of the community, no sprite, palette or stage of a
real mod in play, and none of the core's tests with the game (`-g`) on
this build; a mod larger than a few hundred bytes; Windows, macOS and
aarch64. The no-mods digests were measured on x86_64; that test is skipped
on aarch64.

## Not done

- Other releases' data (Steam, consoles, Sonic Origins Plus): not tried.
- The engine's v3 and v4 modes (v5U plays those games' data too, as Sonic
  Origins does) are built in but were not made to work: the v3 test game
  showed a black screen. The rsdkv3 and rsdkv4 cores play those games.
- Mods with compiled code of their own (a LogicFile): refused (above).
- Mods with Sonic Mania itself: not tried; the data file was not at hand
  when the mod loader was turned on (2026-11-15).
