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
#                draws a 320-pixel picture
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
refuse none "Data.rsdk) is not there" "no data file"
refuse junk "is not a Retro Engine v5 data file" "a file of zeros"
refuse wide "goes from 320 to 424" "screenWidth 640"

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
