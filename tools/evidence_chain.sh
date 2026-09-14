#!/usr/bin/env bash
# Evidence chain for docs/MASTER_PLAN.md section 10: build, full suite, data gate,
# headless cases H1-H8 (section 1.1) with real logs, and the boss-room visuals.
# Usage (Git Bash, repo root):  tools/evidence_chain.sh <tag>   -> docs/evidence/<tag>/
# Every step is checked: a failed build/ctest/gate, a non-zero or timed-out game run,
# an empty log or an empty PNG counts as a failure; the script exits non-zero if any
# step failed and prints a STATUS line listing them.
set -u
cd "$(dirname "$0")/.." || exit 1
TAG="${1:-latest}"
E="docs/evidence/$TAG"; mkdir -p "$E"
rm -f "$E"/*.png "$E"/*.txt "$E"/*.log   # never accept a previous run's products
G=./build/Debug/SoulKnight.exe
CASE_TIMEOUT=120
FAILS=()

fail() { FAILS+=("$1"); echo "FAIL: $1"; }

# run_case <name> <grep-regex> <max-lines> [env assignments...]
# Runs the game with the given env under a timeout, keeps the full log in $E/<name>.log,
# and writes the filtered excerpt to $E/<name>.txt. Fails on non-zero exit, timeout, or
# an empty excerpt.
run_case() {
  local name="$1" re="$2" max="$3"; shift 3
  echo "=== $name ==="
  env "$@" timeout "$CASE_TIMEOUT" "$G" > "$E/$name.log" 2>&1
  local rc=$?
  if [ "$rc" -eq 124 ]; then fail "$name timed out after ${CASE_TIMEOUT}s"; fi
  if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then fail "$name exited $rc"; fi
  grep -E "$re" "$E/$name.log" | sed -n "1,${max}p" > "$E/$name.txt"
  cat "$E/$name.txt"
  [ -s "$E/$name.txt" ] || fail "$name produced no matching log lines"
}

check_png() { # <path>
  if [ ! -s "$1" ]; then fail "$(basename "$1") missing or empty"; else echo "$(basename "$1"): $(stat -c %s "$1") bytes"; fi
}

echo "=== CONFIGURE ==="
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Debug > "$E/configure.log" 2>&1 || fail "configure"
tail -1 "$E/configure.log"
echo "=== BUILD GAME ==="
cmake --build build --config Debug --target SoulKnight > "$E/build_game.log" 2>&1 || fail "build SoulKnight"
grep -E " error|SoulKnight.vcxproj ->" "$E/build_game.log"
echo "=== BUILD TESTS ==="
cmake --build build --config Debug --target SoulKnightTests > "$E/build_tests.log" 2>&1 || fail "build SoulKnightTests"
grep -E " error|SoulKnightTests.vcxproj ->" "$E/build_tests.log"
echo "=== CTEST ==="
ctest --test-dir build -C Debug -E Tests_NOT_BUILT > "$E/ctest.log" 2>&1 || fail "ctest"
grep -E "tests passed|tests failed" "$E/ctest.log" | tee "$E/ctest.txt"
[ -s "$E/ctest.txt" ] || fail "ctest produced no summary line"
grep -qE "^100% tests passed, 0 tests failed" "$E/ctest.txt" || fail "ctest reports failures"
echo "=== GATE ==="
python tools/validate_data.py > "$E/gate.log" 2>&1 || fail "validate_data"
tail -1 "$E/gate.log" | tee "$E/gate.txt"
grep -q "GATE: GREEN" "$E/gate.txt" || fail "gate not GREEN"
echo "=== GATE BOUNDARY TEST (T-31) ==="
python -m unittest discover -s tools/tests > "$E/gate_tests.log" 2>&1 || fail "gate boundary unittest"
tail -3 "$E/gate_tests.log" | tee "$E/gate_tests.txt"
[ -x "$G" ] || { fail "no game binary"; echo "STATUS: FAILED (${#FAILS[@]}): ${FAILS[*]} $(date -u +%Y-%m-%dT%H:%M:%SZ)" | tee "$E/status.txt"; exit 1; }
echo "=== WARM-UP (first launch of a fresh build can take minutes: AV scan / shader cache) ==="
WSTART=$(date +%s)
env SK_SKIP_MENU=1 SK_MUTE=1 SK_MAX_FRAMES=1 timeout 600 "$G" > /dev/null 2>&1 || fail "warm-up exited $?"
echo "warm-up took $(( $(date +%s) - WSTART )) s"

run_case H1 "GameScene: hero|Level |CLEARED" 8 SK_START_CHAR=c12 SK_FORCE_CLEAR=60 SK_MAX_FRAMES=200
run_case H2 "TransferGate|GameScene: hero" 6 SK_SKIP_MENU=1 SK_GATE_TELEPORT=200 SK_MAX_FRAMES=400
echo "=== H2neg (no SK_GATE_TELEPORT: floors visited must be 1) ==="
env SK_SKIP_MENU=1 SK_MAX_FRAMES=400 timeout "$CASE_TIMEOUT" "$G" > "$E/H2neg.log" 2>&1 || fail "H2neg exited $?"
grep -c "GameScene: hero" "$E/H2neg.log" | tee "$E/H2neg.txt"
[ "$(cat "$E/H2neg.txt")" = "1" ] || fail "H2neg floors != 1"
run_case H3 "RGRoomX: room 1|EnemyMaker: room 1" 12 SK_SKIP_MENU=1 SK_ROOM_TELEPORT=60,1 SK_FORCE_KILL=120 SK_FORCE_KILL_EVERY=90 SK_MAX_FRAMES=700
run_case H4 "Reward:|Pickup:" 6 SK_SKIP_MENU=1 SK_FORCE_KILL=120 SK_MAX_FRAMES=700
run_case H5 "died|EndScene: game" 3 SK_SKIP_MENU=1 SK_FORCE_DIE=100 SK_MAX_FRAMES=300 SK_SHOT="$E/H5.png"
check_png "$E/H5.png"
run_case H6 "Shop:" 5 SK_SKIP_MENU=1 SK_COINS=30 SK_SHOP_BUY=100 SK_MAX_FRAMES=130
run_case H7 "Level B5|BossRoom:|Boss defeated|Chest: boss|TransferGate: open" 12 SK_SKIP_MENU=1 SK_FORCE_CLEAR=90 SK_FORCE_KILL=30 SK_MAX_FRAMES=447
run_case H8 "Statue:" 4 SK_SKIP_MENU=1 SK_EVENT_ROOM=statue SK_COINS=30 SK_STATUE_WORSHIP=100 SK_MAX_FRAMES=130
run_case H9 "Buff:" 4 SK_SKIP_MENU=1 SK_FORCE_BUFF=30,fire SK_MAX_FRAMES=200
run_case H9armor "Buff:" 4 SK_SKIP_MENU=1 SK_FORCE_BUFF=30,armor SK_MAX_FRAMES=600
# H10 keeps 40 lines (was 6): a bullet-triggered Fire buff logs one "applied"
# line per landed hit, so a 6-line excerpt was all "applied" and truncated the
# "ended" lines that prove the P3.1 expiry path actually runs.
run_case H10 "Buff:" 40 SK_SKIP_MENU=1 SK_FORCE_WEAPON=weapon_013 SK_AUTOWALK=1 SK_FORCE_HIT=0 SK_MAX_FRAMES=600
grep -q "Buff: Fire ended" "$E/H10.txt" \
  || fail "H10: no 'Buff: Fire ended' line -- the bullet-triggered Fire buff never expired"
run_case H11 "Audio:" 40 SK_SKIP_MENU=1 SK_MUTE=1 SK_AUTOWALK=1 SK_AUTOFIRE=1 SK_FORCE_HIT=30 SK_FORCE_KILL=120 SK_MAX_FRAMES=300
echo "=== V-S r2_boss3 (seed 1: variant D, floor 9 room 3; zoom 1.5 partial) ==="
run_case r2boss3_seed "Level D5|BossRoom: room 3 uses r2_boss3|SK_ROOM_TELEPORT -> room 3 centre" 40 \
  SK_SEED=1 SK_SKIP_MENU=1 SK_FORCE_CLEAR=25 SK_ROOM_TELEPORT=12,3 SK_MAX_FRAMES=245 SK_SHOT="$E/r2_boss3.png"
check_png "$E/r2_boss3.png"
echo "=== V-S r2_boss3 whole room (SK_ZOOM=0.6) ==="
run_case r2boss3_full "Level D5|BossRoom: room 3 uses r2_boss3|SK_ROOM_TELEPORT -> room 3 centre|SK_ZOOM override" 40 \
  SK_SEED=1 SK_SKIP_MENU=1 SK_FORCE_CLEAR=25 SK_ROOM_TELEPORT=12,3 SK_ZOOM=0.6 SK_MAX_FRAMES=245 SK_SHOT="$E/r2_boss3_full.png"
check_png "$E/r2_boss3_full.png"
git rev-parse --short HEAD | tee "$E/head.txt"
rm -f "$E"/*.log
if [ "${#FAILS[@]}" -eq 0 ]; then
  echo "STATUS: OK (all steps passed) $(date -u +%Y-%m-%dT%H:%M:%SZ)" | tee "$E/status.txt"
else
  echo "STATUS: FAILED (${#FAILS[@]}): ${FAILS[*]} $(date -u +%Y-%m-%dT%H:%M:%SZ)" | tee "$E/status.txt"
  exit 1
fi
