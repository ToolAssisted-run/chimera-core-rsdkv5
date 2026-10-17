#!/bin/bash
# The frontend half of the tests: the package, as Chimera loads it, playing
# Sonic Mania's data file (-d, or tests/roms-local/Data.rsdk; never in the
# repository): power-on into Green Hill, 5500 frames (its timer running by
# then). Without the
# data file only the package is built.
#
# - chimera-run (the engine, no frontend): the route from a movie file
#   and from a hand-written project. The Game State and Players
#   domains and the last picture must be the native reference's.
# - Chimera itself, headless under Mono on a private Xvfb: the same project
#   opened and played through Lua, its domains compared with the native
#   reference; the game's timer in the saved project's headers; the property
#   library (game.list/get/set) tried against the same bytes; the core's own
#   refusal reaching the frontend; the package's keybinds becoming the
#   defaults.
#
# Nothing is written under the Chimera checkout: the config, the data home,
# the Base path and the sandbox's log are all in build/frontend.
#
# Usage: ./run-frontend.sh [--chimera-root <path>] [-m <miniBox dir>] [-d <Data.rsdk>]
set -u

here="$(cd "$(dirname "$0")" && pwd)"
wb="$(cd "$here/.." && pwd)"
root="$(cd "$wb/.." && pwd)"
chimera_root=""
mb="${MINIBOX_DIR:-}"
game="$root/tests/roms-local/Data.rsdk"
while [ $# -gt 0 ]; do
	case "$1" in
		--chimera-root) chimera_root="$2"; shift ;;
		-m) mb="$2"; shift ;;
		-d) game="$2"; shift ;;
		*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done
if [ -z "$chimera_root" ]; then
	for candidate in "$root/../chimera" "$HOME/chimera"; do
		[ -d "$candidate" ] && { chimera_root="$candidate"; break; }
	done
fi
[ -n "$chimera_root" ] && [ -d "$chimera_root" ] || { echo "chimera checkout not found; pass --chimera-root" >&2; exit 1; }
chimera_root="$(cd "$chimera_root" && pwd)"
[ -n "$mb" ] || mb="$chimera_root/extern/chimera-common-minibox"

emu_exe="$chimera_root/build/Chimera.exe"
crun="$chimera_root/build/dll/chimera-run"
rn="$root/build/native/run-native"

work="$root/build/frontend"
rm -rf "$work"
mkdir -p "$work/data"

ok=0
failed=0
skipped=0
report() {
	printf "%-26s %-6s %s\n" "$1" "$2" "$3"
	case "$2" in PASS) ok=$((ok+1)) ;; SKIP) skipped=$((skipped+1)) ;; *) failed=$((failed+1)) ;; esac
}
printf "%-26s %-6s %s\n" "Check" "Result" "Detail"
printf "%-26s %-6s %s\n" "-----" "------" "------"

[ -x "$rn" ] || { echo "native reference not built (make -f native.mk)" >&2; exit 1; }

# ---------------------------------------------------------------- the package
if sh "$wb/build-package.sh" -m "$mb" -o "$work/pkg" > "$work/package.log" 2>&1; then
	report "package" PASS "$(grep 'package sha1' "$work/package.log")"
else
	report "package" FAIL "see build/frontend/package.log"; echo; echo "$ok ok, $failed failed"; exit 1
fi
pkg="$work/pkg/rsdkv5.chimeraCore"

if [ ! -f "$game" ]; then
	report "game" SKIP "no Sonic Mania data file at $game (-d); it is never in the repository"
	echo; echo "$ok ok, $failed failed, $skipped skipped"; exit 0
fi
# the data file, as the reference's work dir and as the project's firmware
mkdir -p "$work/native"
cp "$game" "$work/native/Data.rsdk"
data="$work/native/Data.rsdk"
settings='{}'
frames=5500
# the route: Start on the title, A on Mania Mode, A on No Save, then right
# (and a jump) through the Angel Island opening into Green Hill
movie="$work/route.movie"
python3 - "$movie" "$frames" <<'PY'
import sys
path, n = sys.argv[1], int(sys.argv[2])
rows = [""] * n
def press(at, keys, length):
    for f in range(at, min(at + length, n)):
        rows[f] += keys
press(1400, "S", 5); press(1700, "A", 5); press(2000, "A", 5); press(2300, "A", 5); press(2600, "A", 5)
press(3000, "R", 2200); press(3300, "A", 10)
open(path, "w").write("\n".join(rows) + "\n")
PY

# the native reference: the movie, its domains after the last frame and its last picture
"$rn" "$work/native" --frames "$frames" --movie "$movie" --dump-domain "Game State" "$work/native.gs.bin" \
	--screenshot "$((frames - 1)):$work/native.last.tga" > /dev/null 2>&1
"$rn" "$work/native" --frames "$frames" --movie "$movie" --dump-domain "Players" "$work/native.pl.bin" > /dev/null 2>&1

same() { cmp -s "$work/native.gs.bin" "$1" && cmp -s "$work/native.pl.bin" "$2"; }

python3 "$here/make-project.py" "$pkg" "$work/test.chimeraProject" "$frames" --data "$data" --movie "$movie" \
	--settings "$settings" --log-out "$work/test.log"

# ---------------------------------------------------------------- chimera-run
if [ ! -x "$crun" ]; then
	report "engine" SKIP "no chimera-run at $crun"
else
	: > "$work/empty.rom"
	( cd "$work" && "$crun" "$pkg" empty.rom test.log --settings "$settings" --firmware "Data.rsdk=$data" \
		--dump "Game State=$work/run.gs.bin" --dump "Players=$work/run.gv.bin" \
		--screenshot "$((frames - 1))=$work/run.last.tga" --meta "$work/run.meta" > "$work/run.log" 2>&1 )
	if grep -q '^status=OK' "$work/run.meta" 2>/dev/null && same "$work/run.gs.bin" "$work/run.gv.bin" &&
	   python3 "$here/compare-pictures.py" "$work/native.last.tga" "$work/run.last.tga" > "$work/run.pic" 2>&1; then
		report "engine:movie" PASS "$frames frames from a movie file: Game State, Players and the last picture are the reference's"
	else
		report "engine:movie" FAIL "$(grep -m1 detail= "$work/run.meta" 2>/dev/null) $(cat "$work/run.pic" 2>/dev/null) (build/frontend/run.log)"
	fi

	( cd "$work" && "$crun" --project "$work/test.chimeraProject" "$pkg" --firmware "Data.rsdk=$data" \
		--dump "Game State=$work/proj.gs.bin" --dump "Players=$work/proj.gv.bin" --meta "$work/proj.meta" > "$work/proj.log" 2>&1 )
	if grep -q '^status=OK' "$work/proj.meta" 2>/dev/null && same "$work/proj.gs.bin" "$work/proj.gv.bin"; then
		report "engine:project" PASS "a project with no files, the data file its firmware: the same machine"
	else
		report "engine:project" FAIL "$(grep -m1 detail= "$work/proj.meta" 2>/dev/null) (build/frontend/proj.log)"
	fi
fi

# ---------------------------------------------------------------- Chimera itself
if [ ! -f "$emu_exe" ]; then
	report "frontend" SKIP "Chimera not built: $emu_exe"
	echo; echo "$ok ok, $failed failed, $skipped skipped"; [ "$failed" -eq 0 ]; exit
fi
export LD_LIBRARY_PATH="$chimera_root/build/dll:$chimera_root/build:/usr/lib/x86_64-linux-gnu"
export MONO_CRASH_NOFILE=1 MONO_WINFORMS_XIM_STYLE=disabled ALSOFT_DRIVERS=null
export CHIMERA_DATA_HOME="$work/data" MINIBOX_LOG="$work/minibox.log"
xvfb_pid=""
cleanup() { [ -n "$xvfb_pid" ] && kill "$xvfb_pid" 2>/dev/null; }
trap cleanup EXIT
if [ -z "${DISPLAY:-}" ]; then
	command -v Xvfb > /dev/null || { report "frontend" SKIP "no Xvfb"; echo; echo "$ok ok, $failed failed, $skipped skipped"; exit 0; }
	for n in 90 91 92 93 94 95 96 97 98 99; do
		if [ ! -e "/tmp/.X11-unix/X$n" ]; then
			Xvfb ":$n" -screen 0 640x480x24 -nolisten tcp > "$work/xvfb.log" 2>&1 & xvfb_pid=$!
			export DISPLAY=":$n"; break
		fi
	done
	sleep 1
fi

# a config of its own, with the Base path (and so every folder Chimera
# writes by default) in the work dir
config="$work/config.ini"
echo 'client.exit()' > "$work/exit.lua"
( cd "$work" && timeout 120 mono "$emu_exe" --headless "--config=$config" "--lua=$work/exit.lua" ) > "$work/bootstrap.log" 2>&1
python3 - "$config" "$work" <<'EOF'
import json, sys
path, base = sys.argv[1], sys.argv[2]
c = json.load(open(path))
for e in c["PathEntries"]["Paths"]:
    if e["Type"] == "Base" and e["System"] == "Global_NULL":
        e["Path"] = base
json.dump(c, open(path, "w"), indent=2)
EOF
# what the home check counts from: the first start above makes an empty
# Movies folder under the Base path it starts with (the checkout's build/),
# before there is a config to point it elsewhere
touch "$work/configured"

gui() {
	# gui <tag> <config> <project> <frames>
	local tag="$1" cfg="$2" project="$3" nframes="$4"
	mkdir -p "$work/$tag"
	printf 'frames=%s\nout=%s\nmeta=%s\nsave=%s\n' "$nframes" "$work/$tag" "$work/$tag/meta.txt" "${GUI_SAVE:-0}" > "$work/$tag/job.txt"
	( cd "$work" && MINIHAWK_JOB="$work/$tag/job.txt" timeout 300 mono "$emu_exe" --headless "--config=$cfg" \
		"--core=$pkg" "--firmware=Data.rsdk=$data" "--project=$project" "--lua=$here/frontend.lua" ) > "$work/$tag.log" 2>&1
	echo $?
}
meta() { sed -n "s/^$2=//p" "$work/$1/meta.txt" 2>/dev/null; }

cp "$work/test.chimeraProject" "$work/gui.chimeraProject"
GUI_SAVE=1 gui gui "$config" "$work/gui.chimeraProject" "$frames" > /dev/null
if [ "$(meta gui status)" = "OK" ] && same "$work/gui/gamestate.bin" "$work/gui/players.bin"; then
	report "gui:project" PASS "the project opened in Chimera: after $(meta gui frames) frames ($(meta gui lag) lag) Game State and Players are the reference's"
else
	report "gui:project" FAIL "status $(meta gui status) $(meta gui detail) (build/frontend/gui.log)"
fi

# the game's own timer (the table's gameTimer, Stage.Time Ms): Chimera reads it
# after every frame, and the project saved at the movie's end carries it
"$rn" "$work/native" --frames "$frames" --movie "$movie" --trace "$work/igt.trace" --trace-props "Scene.Time Ms" > /dev/null 2>&1
want_ms="$(awk -v s=$((frames - 1)) '$1 == s { print $4 }' "$work/igt.trace")"
hdr() { python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['headers'].get(sys.argv[2], ''))" "$work/gui.chimeraProject" "$1"; }
want_text="$(printf '%02d:%02d.%03d' $((want_ms / 60000)) $((want_ms / 1000 % 60)) $((want_ms % 1000)))"
if [ "$(meta gui saved)" = "1" ] && [ -n "$want_ms" ] && [ "$want_ms" -gt 0 ] && [ "$(meta gui game_time_ms)" = "$want_ms" ] &&
   [ "$(hdr GameTimeMs)" = "$want_ms" ] && [ "$(hdr GameTime)" = "$want_text" ] && [ "$(hdr GameTimeFrame)" = "$frames" ]; then
	report "gui:game-time" PASS "the scene timer $want_text after $frames frames: game.get, and the saved project's GameTime headers, are the reference's"
else
	report "gui:game-time" FAIL "saved $(meta gui saved); reference $want_ms ms; lua $(meta gui game_time_ms); headers $(hdr GameTimeMs) '$(hdr GameTime)' @ $(hdr GameTimeFrame)"
fi

# the property library: the table is the one the core exports, a global
# variable read by its name is its bytes, a set takes, and a name the core
# does not have is nil
nprops="$("$rn" "$work/native" --frames 1 --props-json "$work/props.json" > /dev/null 2>&1; python3 -c "import json; print(len(json.load(open('$work/props.json'))['properties']))")"
if [ "$(meta gui game_list)" = "absent" ]; then
	report "gui:game-properties" SKIP "this Chimera has no game.* library"
elif [ "$(meta gui game_list)" = "$nprops" ] && [ "$(meta gui game_x)" = "$(meta gui game_x_bytes)" ] &&
     [ "$(meta gui game_set)" = "true" ] && [ "$(meta gui game_x_after_set)" = "9" ] && [ "$(meta gui game_unknown)" = "nil" ]; then
	report "gui:game-properties" PASS "game.list() has the core's $nprops; Player 1.X by name is its bytes ($(meta gui game_x)); set to 9 took"
else
	report "gui:game-properties" FAIL "list $(meta gui game_list)/$nprops, x $(meta gui game_x)/$(meta gui game_x_bytes), set $(meta gui game_set)->$(meta gui game_x_after_set) $(meta gui game_error)"
fi

# the core's own refusal reaches the frontend: a screen width the core does
# not take, which Chimera passes on as a number and only the core refuses
python3 - "$work/gui.chimeraProject" "$work/wide.chimeraProject" <<'EOF'
import json, sys
p = json.load(open(sys.argv[1]))
p["settings"]["screenWidth"] = 1000
json.dump(p, open(sys.argv[2], "w"), indent="\t")
EOF
code="$(gui refuse "$config" "$work/wide.chimeraProject" 10)"
if grep -q "the setting screenWidth is 1000; it goes from 320 to 424" "$work/refuse.log"; then
	report "gui:refusal" PASS "the core's load error is what Chimera shows (headless: exit $code with the text)"
else
	report "gui:refusal" FAIL "exit $code; $(grep -m1 -i 'screenwidth\|error' "$work/refuse.log" | cut -c1-100)"
fi

# the bindings the package ships become the frontend's defaults
python3 "$here/forget-controller.py" "$config" "$work/config.keys.ini" "Sonic Mania"
gui keys "$work/config.keys.ini" "$work/gui.chimeraProject" 1 > /dev/null
if [ "$(meta keys status)" = "OK" ] && python3 "$here/check-keybinds.py" "$work/config.keys.ini" "$wb/default_keybinds.json" "Sonic Mania" > "$work/keys.txt" 2>&1; then
	report "gui:keybinds" PASS "$(cat "$work/keys.txt")"
else
	report "gui:keybinds" FAIL "$(head -1 "$work/keys.txt" 2>/dev/null) (build/frontend/keys.log)"
fi

# nothing written under the Chimera checkout
if [ -z "$(find "$chimera_root/build" -maxdepth 2 -newer "$work/configured" \( -name 'config*.ini' -o -name 'Movies' -o -name 'Firmware' -o -name 'CoreCache' -o -name 'ChimeraMono_last*' \) 2>/dev/null)" ]; then
	report "gui:writes-nothing-home" PASS "after the first start: config, data home, Base path and logs all in build/frontend"
else
	report "gui:writes-nothing-home" FAIL "$(find "$chimera_root/build" -maxdepth 2 -newer "$work/configured" | head -3 | tr '\n' ' ')"
fi

echo
echo "$ok ok, $failed failed, $skipped skipped"
[ "$failed" -eq 0 ]
