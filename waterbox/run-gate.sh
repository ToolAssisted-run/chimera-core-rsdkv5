#!/bin/sh
# run-gate.sh - the Retro Engine v5 (Sonic Mania) core's tests: the native
# reference and core.wbx are the same machine.
#
# The game's logic is compiled into the core, so there is no test game to
# write: what needs a game runs on Sonic Mania's data file, given with -g and
# never in the repository, and is skipped without it. Every test says what it
# compared. The tests:
#
# Without the game (anywhere, CI included):
#   refusals     no data file, a file that is not one, a screen width out of
#                range: each says why, in both builds
#   clock        the guest has no time() of its own: the one it calls is the
#                core's (the setting)
#   firmware     waterbox.config's firmware is the file the core opens
#
# With the game (-g <Data.rsdk>), on a route from power-on: Start on the
# title, A on Mania Mode, A on No Save, then right and a jump through the
# Angel Island opening into Green Hill (5200 frames, the opening video among
# them):
#   equivalence  run-native and run-wbx: every frame's picture, sound, length
#                and lag, the frame count and the memory domains
#   rerecord     the sandbox saved and loaded before every frame = without
#   session      saved during the opening video and again in play, a new
#                host, loaded, finished = without
#   turbo        the first half not drawn: the rest the same, and the first
#                half's pictures really not drawn
#   input        Start on the title reaches the main menu; without it the
#                title stays
#   pokes        Player 1's position written before a frame is the player's
#                position in the game
#   variables    Sonic Mania's own: in Green Hill Player 1 is Sonic with 3
#                lives in Mania mode; rings, lives and score written before a
#                frame are the game's from then on
#   settings     the random seed reaches the game; the Plus content setting,
#                off by default, turns the Plus title on; screenWidth 320
#                draws a 320-pixel picture; the player name is drawn on the
#                main menu
#   save data    SaveData.bin exported, given back and read by the game;
#                Settings.ini refused as save data
#   developer menu  Dev Menu opens it with the setting on, does nothing off
#   players      players 3 and 4 there with Players 4, not with 2
#   teeth        the equivalence comparison sees a one-frame difference
#
# usage: run-gate.sh [-m <miniBox dir>] [-g <Data.rsdk>]
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
mb="${MINIBOX_DIR:-$HOME/chimera/extern/chimera-common-minibox}"
game=""
while getopts "m:g:" opt; do
	case "$opt" in
		m) mb="$OPTARG" ;;
		g) game="$OPTARG" ;;
		*) exit 2 ;;
	esac
done
mb="$(cd "$mb" && pwd)"

echo "== build (miniBox $mb)"
mkdir -p "$root/build"
make -C "$here" -f native.mk MB="$mb" -j"$(nproc)" > "$root/build/gate-native.log" 2>&1 || {
	tail -20 "$root/build/gate-native.log"; echo "native build failed"; exit 1; }
make -C "$here" -f guest.mk MB="$mb" -j"$(nproc)" > "$root/build/gate-guest.log" 2>&1 || {
	tail -20 "$root/build/gate-guest.log"; echo "guest build failed"; exit 1; }
native="$root/build/native/run-native"
wbx="$root/build/native/run-wbx"
core="$root/build/guest/core.wbx"

work="$root/build/gate"
rm -rf "$work"
mkdir -p "$work"
fails=0
pass() { echo "PASS $*"; }
fail() { echo "FAIL $*"; fails=$((fails + 1)); }

# the digests only (miniBox's own log lines go to the same stdout)
digests() { grep -E '^[A-Za-z]+(\[[^]]*\])?=' "$1" || true; }
same() { # same <test> <a> <b> <what>
	digests "$2" > "$2.d"
	digests "$3" > "$3.d"
	if cmp -s "$2.d" "$3.d"; then
		pass "$1: $4"
	else
		fail "$1: $4"; diff "$2.d" "$3.d" | head -20
	fi
}
value() { digests "$1" | sed -n "s/^$2=//p"; }
# a traced property's value at a frame (a trace line: frame rate read values...)
at() { awk -v f="$2" -v col="$3" '$1 == f { print $(3 + col) }' "$1"; }
size() { python3 -c "import sys; d = open(sys.argv[1], 'rb').read(18); print('%dx%d' % (d[12] | d[13] << 8, d[14] | d[15] << 8))" "$1" 2>/dev/null || echo none; }

echo "== refusals"
refuse() { # refuse <dir> <expected text> <what>
	out="$("$native" "$work/$1" --frames 1 2>&1 || true)"
	outw="$("$wbx" "$core" "$work/$1" --frames 1 2>&1 || true)"
	if echo "$out" | grep -q "loadError=.*$2" && echo "$outw" | grep -q "loadError=.*$2"; then
		pass "refusals: $3 - $(echo "$out" | sed -n 's/^loadError=//p')"
	else
		fail "refusals: $3"; echo "$out" | tail -2
	fi
}
mkdir -p "$work/none" "$work/junk" "$work/wide"
head -c 100000 /dev/zero > "$work/junk/Data.rsdk"
head -c 100000 /dev/zero > "$work/wide/Data.rsdk"
printf '{"screenWidth": 640}' > "$work/wide/settings"
refuse none "error.game_s_data_file_data_rsdk" "no data file"
refuse junk "error.\\(is_not_a_retro_engine_v5\\|engine_did_not_start_the\\)" "a file of zeros"
refuse wide "error.setting_screenwidth_is_it" "screenWidth 640"

echo "== clock"
if nm "$core" | grep -qE ' (T|W|t) (time|__real_time)$'; then
	fail "clock: the guest links a time() of the C library's"
else
	pass "clock: time() is only the core's (the randomSeed setting); SDL's counters are the frame count"
fi

echo "== firmware"
fw="$(python3 -c "import json, sys; print(' '.join(f['id'] for f in json.load(open(sys.argv[1]))['firmware']))" "$here/waterbox.config")"
if [ "$fw" = "Data.rsdk" ] && grep -q 'const char \*name = "Data.rsdk";' "$here/rsdk-files.c"; then
	pass "firmware: waterbox.config's one firmware, Data.rsdk, is the name the core opens"
else
	fail "firmware: waterbox.config declares '$fw'"
fi

if [ -n "$game" ]; then
	echo "== Sonic Mania ($game)"
	mkdir -p "$work/game"
	cp "$game" "$work/game/Data.rsdk"
	route="--press 1400:S:5 --press 1700:A:5 --press 2000:A:5 --press 2300:A:5 --press 2600:A:5 --press 3000:R:2200 --press 3300:A:10"
	n=5200
	props="Engine.State,Scene.Category,Scene.List Position,Player 1.X,Player 1.Character,Player 1.Lives,Player 1.Rings,Game.Mode,Player 2.Character"
	"$native" "$work/game" --frames $n $route --trace "$work/g.t" --trace-props "$props" > "$work/g.n" 2>/dev/null || true
	"$wbx" "$core" "$work/game" --frames $n $route > "$work/g.w" 2>/dev/null || true
	same equivalence "$work/g.w" "$work/g.n" "native = sandbox, $n frames (lag $(value "$work/g.n" lagFrames); scene at the end: category $(at "$work/g.t" $((n - 1)) 2), position $(at "$work/g.t" $((n - 1)) 3))"
	"$wbx" "$core" "$work/game" --frames $n $route --rerecord > "$work/g.r" 2> "$work/g.r.err" || true
	same rerecord "$work/g.r" "$work/g.w" "save+load before every frame = straight ($(sed -n 's/stateBytes=//p' "$work/g.r.err") state bytes)"
	for s in 2100 5000; do
		"$wbx" "$core" "$work/game" --frames $n $route --session-at $s > "$work/g.s$s" 2>/dev/null || true
		same session "$work/g.s$s" "$work/g.w" "saved at frame $s, loaded into a new host = straight"
	done

	"$native" "$work/game" --frames $n $route --turbo > "$work/g.turbo" 2>/dev/null || true
	digests "$work/g.turbo" | grep -v '^videoHash=' > "$work/turbo.d"
	digests "$work/g.n" | grep -v '^videoHash=' > "$work/n.d"
	if cmp -s "$work/turbo.d" "$work/n.d" && [ "$(value "$work/g.turbo" videoHash)" != "$(value "$work/g.n" videoHash)" ]; then
		pass "turbo: first half undrawn - the second half's pictures, the sound, the frames and memory the same; the first half's pictures not"
	else
		fail "turbo"; diff "$work/turbo.d" "$work/n.d" | head
	fi

	# the title is scene position T; Start there (frame 1400) leads elsewhere
	"$native" "$work/game" --frames 1700 --trace "$work/still.t" --trace-props "$props" > /dev/null 2>&1 || true
	t0="$(at "$work/g.t" 1399 3)"; t1="$(at "$work/g.t" 1699 3)"; s1="$(at "$work/still.t" 1699 3)"
	if [ -n "$t0" ] && [ "$t0" = "$s1" ] && [ "$t1" != "$t0" ]; then
		pass "input: Start on the title (scene position $t0) leads to scene position $t1; without it the title stays"
	else
		fail "input: title $t0, after Start $t1, without Start $s1"
	fi

	"$native" "$work/game" --frames 4952 $route --poke "4950:Player 1.X=10485760" --trace "$work/pk.t" --trace-props "Player 1.X" > /dev/null 2>&1 || true
	"$wbx" "$core" "$work/game" --frames 4952 $route --poke "4950:Player 1.X=10485760" --trace "$work/pk.w" --trace-props "Player 1.X" > /dev/null 2>&1 || true
	before="$(at "$work/g.t" 4950 4)"; after="$(at "$work/pk.t" 4950 1)"
	if [ -n "$after" ] && [ "$before" != "$after" ] && [ $((after - 10485760)) -le $((16 << 16)) ] && [ $((10485760 - after)) -le $((16 << 16)) ] && cmp -s "$work/pk.t" "$work/pk.w"; then
		pass "pokes: Player 1.X set to 160 pixels before frame 4950 (running in Green Hill) is $after after it, moved on from there (without the poke $before); both builds the same"
	else
		fail "pokes: without $before, with $after"
	fi

	vp="Player 1.Rings,Player 1.Lives,Player 1.Score"
	"$native" "$work/game" --frames 4991 $route --poke "4950:Player 1.Rings=50" --poke "4950:Player 1.Lives=9" --poke "4950:Player 1.Score=12340" --trace "$work/v.t" --trace-props "$vp" > /dev/null 2>&1 || true
	"$wbx" "$core" "$work/game" --frames 4991 $route --poke "4950:Player 1.Rings=50" --poke "4950:Player 1.Lives=9" --poke "4950:Player 1.Score=12340" --trace "$work/v.w" --trace-props "$vp" > /dev/null 2>&1 || true
	plain="$(at "$work/g.t" 4949 5) $(at "$work/g.t" 4949 6) $(at "$work/g.t" 4949 7) $(at "$work/g.t" 4949 8) $(at "$work/g.t" 4949 9)"
	poked="$(at "$work/v.t" 4950 1) $(at "$work/v.t" 4950 2) $(at "$work/v.t" 4950 3)"
	held="$(at "$work/v.t" 4990 1) $(at "$work/v.t" 4990 2) $(at "$work/v.t" 4990 3)"
	if [ "$plain" = "1 3 0 0 2" ] && [ "$poked" = "50 9 12340" ] && [ "$held" = "50 9 12340" ] && cmp -s "$work/v.t" "$work/v.w"; then
		pass "variables: in Green Hill Player 1 is Sonic (Character 1) with 3 lives and 0 rings in Mania mode, Player 2 Tails (2); rings 50, lives 9 and score 12340 written before frame 4950 are the game's 40 frames on; both builds the same"
	else
		fail "variables: character, lives, rings, mode, player 2 '$plain'; written '$poked', 40 frames on '$held'"
	fi

	mkdir -p "$work/seed7"
	cp "$game" "$work/seed7/Data.rsdk"
	printf '{"randomSeed": 7}' > "$work/seed7/settings"
	"$native" "$work/seed7" --frames $n $route > "$work/seed7/n" 2>/dev/null || true
	if [ "$(value "$work/seed7/n" domain\\[Players\\])$(value "$work/seed7/n" videoHash)" != "$(value "$work/g.n" domain\\[Players\\])$(value "$work/g.n" videoHash)" ]; then
		pass "settings: randomSeed 7 plays the route differently from 0 (the game's random numbers follow the seed)"
	else
		fail "settings: randomSeed 7 and 0 play the same route the same"
	fi
	# the Plus content: off by default (the title is Sonic Mania's); on, the
	# title is Sonic Mania Plus's
	mkdir -p "$work/plus"
	cp "$game" "$work/plus/Data.rsdk"
	printf '{"plus": true}' > "$work/plus/settings"
	"$native" "$work/plus" --frames 1300 > "$work/plus/n" 2>/dev/null || true
	"$wbx" "$core" "$work/plus" --frames 1300 > "$work/plus/w" 2>/dev/null || true
	"$native" "$work/game" --frames 1300 > "$work/plus/off" 2>/dev/null || true
	if [ -n "$(value "$work/plus/n" videoHash)" ] && [ "$(value "$work/plus/n" videoHash)" != "$(value "$work/plus/off" videoHash)" ]; then
		same settings "$work/plus/w" "$work/plus/n" "plus on draws another title (Sonic Mania Plus's) than off, the default; native = sandbox"
	else
		fail "settings: plus on and off draw the same first 1300 frames"
	fi
	mkdir -p "$work/narrow"
	cp "$game" "$work/narrow/Data.rsdk"
	printf '{"screenWidth": 320}' > "$work/narrow/settings"
	"$native" "$work/narrow" --frames 120 --screenshot 119:"$work/narrow/s.tga" > "$work/narrow/n" 2>&1 || true
	"$wbx" "$core" "$work/narrow" --frames 120 > "$work/narrow/w" 2>&1 || true
	if [ "$(size "$work/narrow/s.tga")" = 320x240 ]; then
		same settings "$work/narrow/w" "$work/narrow/n" "screenWidth 320 draws a 320x240 picture; native = sandbox"
	else
		fail "settings: screenWidth 320 drew $(size "$work/narrow/s.tga")"
	fi

	# Save data (chimera#302): the route plays in save slot 1, so the game
	# writes SaveData.bin; Export Save Data gives it; a project given it back
	# starts with it - the save select shows slot 1 in Green Hill, where a
	# fresh game shows a new game - and Settings.ini is refused as save data
	mkdir -p "$work/sd" "$work/sdin" "$work/sdin-out" "$work/sdx"
	"$native" "$work/game" --frames $n $route --savedata-out "$work/sd" > "$work/sd.n" 2>/dev/null || true
	cp "$game" "$work/sdin/Data.rsdk"
	cp "$work/sd/SaveData.bin" "$work/sdin/" 2>/dev/null || true
	printf '{"savedata":["SaveData.bin"]}' > "$work/sdin/slots"
	"$native" "$work/sdin" --frames 1 --savedata-out "$work/sdin-out" > "$work/sdin1.n" 2>/dev/null || true
	# Start on the title, A on the autosave notice, A on Mania Mode
	sel="--press 1400:S:5 --press 1700:A:5 --press 2000:A:5"
	# shellcheck disable=SC2086
	"$native" "$work/game" --frames 2260 $sel > "$work/fresh.n" 2>/dev/null || true
	# shellcheck disable=SC2086
	"$native" "$work/sdin" --frames 2260 $sel > "$work/seeded.n" 2>/dev/null || true
	# shellcheck disable=SC2086
	"$wbx" "$core" "$work/sdin" --frames 2260 $sel > "$work/seeded.w" 2>/dev/null || true
	cp "$game" "$work/sdx/Data.rsdk"
	cp "$work/sd/SaveData.bin" "$work/sdx/Settings.ini" 2>/dev/null || true
	printf '{"savedata":["Settings.ini"]}' > "$work/sdx/slots"
	"$native" "$work/sdx" --frames 1 > "$work/sdx.n" 2>&1 || true
	if [ ! -s "$work/sd/SaveData.bin" ]; then
		fail "save data: the route wrote no SaveData.bin to export"
	elif ! cmp -s "$work/sd/SaveData.bin" "$work/sdin-out/SaveData.bin"; then
		fail "save data: a project given SaveData.bin does not start with those bytes"
	elif [ "$(value "$work/fresh.n" videoHash)" = "$(value "$work/seeded.n" videoHash)" ]; then
		fail "save data: the save select with the save given is the fresh game's: the game did not read it"
	elif ! grep -q "error.save_data_is_not_one_this" "$work/sdx.n"; then
		fail "save data: Settings.ini given as save data was not refused ($(tail -1 "$work/sdx.n"))"
	else
		pass "save data: SaveData.bin ($(stat -c %s "$work/sd/SaveData.bin") bytes) exported, given back and read (the save select differs from a fresh game's), Settings.ini refused"
		same "save data" "$work/seeded.w" "$work/seeded.n" "save data given: native = sandbox, 2260 frames"
	fi

	# The developer menu (chimera#302): with the Developer Menu setting on,
	# Dev Menu (Escape) opens it; with the setting off the same press changes
	# nothing, as the control is not there
	for dm in on off; do
		mkdir -p "$work/dm-$dm"
		cp "$game" "$work/dm-$dm/Data.rsdk"
		if [ "$dm" = on ]; then printf '{"devMenu": true}' > "$work/dm-$dm/settings"; fi
		"$native" "$work/dm-$dm" --frames 1100 --press 1000:E:5 > "$work/dm-$dm.n" 2>/dev/null || true
	done
	"$wbx" "$core" "$work/dm-on" --frames 1100 --press 1000:E:5 > "$work/dm-on.w" 2>/dev/null || true
	"$native" "$work/dm-off" --frames 1100 > "$work/dm-none.n" 2>/dev/null || true
	if [ "$(value "$work/dm-on.n" videoHash)" = "$(value "$work/dm-off.n" videoHash)" ]; then
		fail "developer menu: Dev Menu with the setting on made no difference to the picture"
	elif [ "$(value "$work/dm-off.n" videoHash)" != "$(value "$work/dm-none.n" videoHash)" ]; then
		fail "developer menu: with the setting off, Dev Menu still changed the game"
	else
		pass "developer menu: Dev Menu opens it with the setting on, and does nothing with the setting off"
		same "developer menu" "$work/dm-on.w" "$work/dm-on.n" "developer menu: native = sandbox"
	fi

	# Players 3 and 4 (chimera#302): the menus answer every player, so with
	# Players 4 player 3's Start on the title does what player 1's does; with
	# Players 2, the default, player 3's buttons are not there and it does
	# nothing
	for pl in 2 4; do
		mkdir -p "$work/pl$pl"
		cp "$game" "$work/pl$pl/Data.rsdk"
		printf '{"players": "%s"}' "$pl" > "$work/pl$pl/settings"
		"$native" "$work/pl$pl" --frames 1650 --press 1400:P:5 > "$work/pl$pl-P.n" 2>/dev/null || true
	done
	"$native" "$work/pl4" --frames 1650 --press 1400:S:5 > "$work/pl4-S.n" 2>/dev/null || true
	"$native" "$work/pl2" --frames 1650 > "$work/pl2-none.n" 2>/dev/null || true
	"$wbx" "$core" "$work/pl4" --frames 1650 --press 1400:P:5 > "$work/pl4-P.w" 2>/dev/null || true
	if [ "$(value "$work/pl4-P.n" tailVideoHash)" != "$(value "$work/pl4-S.n" tailVideoHash)" ]; then
		fail "players: with Players 4, player 3's Start on the title did not do what player 1's does"
	elif [ "$(value "$work/pl2-P.n" tailVideoHash)" != "$(value "$work/pl2-none.n" tailVideoHash)" ]; then
		fail "players: with Players 2, player 3's Start still changed the game"
	else
		pass "players: with Players 4 player 3's Start leaves the title as player 1's does; with Players 2 it does nothing"
		same players "$work/pl4-P.w" "$work/pl4-P.n" "player 3's Start: native = sandbox"
	fi

	# The player name (chimera#302): the main menu shows it at the bottom
	# left, so the name CHIMERA draws another main menu than the game's own
	# (Start on the title, A on the autosave notice)
	mkdir -p "$work/name"
	cp "$game" "$work/name/Data.rsdk"
	printf '{"username": "CHIMERA"}' > "$work/name/settings"
	menu="--press 1400:S:5 --press 1700:A:5"
	# shellcheck disable=SC2086
	"$native" "$work/name" --frames 1960 $menu > "$work/name.n" 2>/dev/null || true
	# shellcheck disable=SC2086
	"$wbx" "$core" "$work/name" --frames 1960 $menu > "$work/name.w" 2>/dev/null || true
	# shellcheck disable=SC2086
	"$native" "$work/game" --frames 1960 $menu > "$work/noname.n" 2>/dev/null || true
	if [ -n "$(value "$work/name.n" tailVideoHash)" ] && [ "$(value "$work/name.n" tailVideoHash)" != "$(value "$work/noname.n" tailVideoHash)" ]; then
		same settings "$work/name.w" "$work/name.n" "username CHIMERA draws another main menu than the default name; native = sandbox"
	else
		fail "settings: username CHIMERA draws the same main menu as the default name"
	fi

	# a jump at frame 4950, running in Green Hill
	"$native" "$work/game" --frames $n $route --press 4950:A:1 > "$work/teeth" 2>/dev/null || true
	if digests "$work/teeth" | cmp -s - "$work/g.n.d"; then
		fail "teeth: a route one frame different digests the same"
	else
		pass "teeth: a route one frame different digests differently"
	fi
else
	echo "SKIP game: no Sonic Mania data file given (-g <Data.rsdk>); it is never in the repository"
fi

echo
if [ $fails -eq 0 ]; then
	echo "gate: all tests passed"
else
	echo "gate: $fails test(s) failed"
	exit 1
fi
