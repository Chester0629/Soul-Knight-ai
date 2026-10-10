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

# require <name> <regex>...: every regex must match the case's excerpt; one FAIL per missing
# event, so STATUS: OK can never hide an excerpt that lost its key lines (R23: H7 kept only
# three gate lines and still passed).
require() {
  local name="$1"; shift
  local re
  for re in "$@"; do
    grep -qE "$re" "$E/$name.txt" || fail "$name missing required event: $re"
  done
}

# require_order <name> <regex>...: the regexes must match in this order (each one on a line
# after the previous match); one FAIL naming the first event that is missing or out of order.
require_order() {
  local name="$1"; shift
  local re after=0 ln
  for re in "$@"; do
    ln=$(PAT="$re" awk -v start="$after" 'NR > start && $0 ~ ENVIRON["PAT"] { print NR; exit }' "$E/$name.txt")
    if [ -z "$ln" ]; then fail "$name missing or out-of-order event: $re"; return; fi
    after=$ln
  done
}

# require_count <name> <regex> <min>: at least <min> excerpt lines match.
require_count() {
  local n; n=$(grep -cE "$2" "$E/$1.txt")
  [ "$n" -ge "$3" ] || fail "$1 needs >= $3 lines matching '$2', got $n"
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
require_order H1 "hero c12 on floor 0" "Level B1 \\(1-1\\)" "Floor 0 CLEARED" "hero c12 on floor 1" "Level B2 \\(1-2\\)"
run_case H2 "TransferGate|GameScene: hero" 6 SK_SKIP_MENU=1 SK_GATE_TELEPORT=200 SK_MAX_FRAMES=400
require_order H2 "TransferGate: open in room" "TransferGate: entered -> next floor" "hero c01 on floor 1"
echo "=== H2neg (no SK_GATE_TELEPORT: floors visited must be 1) ==="
env SK_SKIP_MENU=1 SK_MAX_FRAMES=400 timeout "$CASE_TIMEOUT" "$G" > "$E/H2neg.log" 2>&1 || fail "H2neg exited $?"
grep -c "GameScene: hero" "$E/H2neg.log" | tee "$E/H2neg.txt"
[ "$(cat "$E/H2neg.txt")" = "1" ] || fail "H2neg floors != 1"
# P3.0 (data speeds at 32 px/unit): enemies now really walk and their bullets really fly, so the
# stationary, never-dodging hero in this case used to die on the third wave (frame ~240) before
# the last forced kill (frame 300) could clear the room. KILL_EVERY 90 -> 45 shortens each wave's
# exposure (kills at 120/165/210); the wave sequence and every require_order below are unchanged.
run_case H3 "RGRoomX: room 1|EnemyMaker: room 1" 12 SK_SKIP_MENU=1 SK_ROOM_TELEPORT=60,1 SK_FORCE_KILL=120 SK_FORCE_KILL_EVERY=45 SK_MAX_FRAMES=700
require_order H3 "part spawned 9 \\(15 pending\\)" "level 3 points [0-9]+ -> 24 planned, wave size 9" "StartRoom -> Active" "part spawned 9 \\(6 pending\\)" "part spawned 6 \\(0 pending\\)" "room 1 cleared"
run_case H4 "Reward:|Pickup:" 6 SK_SKIP_MENU=1 SK_FORCE_KILL=120 SK_MAX_FRAMES=700
require_count H4 "Reward: .* dropped [0-9]+ pickups" 3
run_case H5 "died|EndScene: game" 3 SK_SKIP_MENU=1 SK_FORCE_DIE=100 SK_MAX_FRAMES=300 SK_SHOT="$E/H5.png"
check_png "$E/H5.png"
require_order H5 "Player died on floor 0 -> EndScene" "EndScene: game over at 1-1"
run_case H6 "Shop:" 5 SK_SKIP_MENU=1 SK_COINS=30 SK_SHOP_BUY=100 SK_MAX_FRAMES=130
require_order H6 "vending_machine_i at room [0-9]+ sells .health_pot_big. for 10 coins" "Shop: bought health_pot_big for 10 "
# H7: 447 frames only just covered five floors (the v1.28 run stopped on floor 3 and the
# old excerpt still passed); 700 frames, a 20-line excerpt, and explicit boss events.
run_case H7 "Level B5|BossRoom:|Boss defeated|Chest: boss|TransferGate: open" 20 SK_SKIP_MENU=1 SK_FORCE_CLEAR=90 SK_FORCE_KILL=30 SK_MAX_FRAMES=700
# The gate must open in the BOSS room itself (R25: a later floor's gate line satisfied the
# generic pattern even when the boss-room gate line was deleted).
H7_BOSS_ROOM=$(sed -nE 's/.*BossRoom: room ([0-9]+) uses.*/\1/p' "$E/H7.txt" | head -1)
require_order H7 "Level B5 \\(1-5\\)" "BossRoom: room [0-9]+ uses r1_boss" "Boss defeated!" "Chest: boss chest chest_boss" "TransferGate: open in room ${H7_BOSS_ROOM:-NONE} at"
run_case H8 "Statue:" 4 SK_SKIP_MENU=1 SK_EVENT_ROOM=statue SK_COINS=30 SK_STATUE_WORSHIP=100 SK_MAX_FRAMES=130
require_order H8 "Statue: worshipped statue_0[0-9] .* -> statue_index [0-9]"
run_case H9 "Buff:" 4 SK_SKIP_MENU=1 SK_FORCE_BUFF=30,fire SK_MAX_FRAMES=200
require_order H9 "Buff: Fire applied to 61453 for 2\\.0+s" "Buff: Fire ended on 61453"
run_case H9armor "Buff:" 4 SK_SKIP_MENU=1 SK_FORCE_BUFF=30,armor SK_MAX_FRAMES=600
require_order H9armor "Buff: Armor applied to 61453 for 6\\.0+s" "Buff: Armor ended on 61453"
# H10 keeps 40 lines (was 6): a bullet-triggered Fire buff logs one "applied"
# line per landed hit, so a 6-line excerpt was all "applied" and truncated the
# "ended" lines that prove the P3.1 expiry path actually runs.
# item 6 (R18/Codex block): SK_FORCE_SKILL (a HELD press) can never show "ended"
# for a toggle-type skill (C04/C08), because the toggle's only end trigger is a
# SECOND, DISTINCT press -- and a held button is never released to make one.
# SK_FORCE_SKILL_PULSE=60,300 presses for exactly one frame at 60 and again at
# 300 (release in between), so c04/c08 show "activated" (frame 60) then "ended"
# (frame 300, the pulse's 2nd press toggles the shield/cut window off); c10/c13
# show "activated" then their OWN "ended" via in_skill_time auto-end well before
# frame 300 (5.0s in_skill_time / 0.02s fixed step = 250 fixed steps; the sim runs
# at kFixedStepMs=20ms == the same 60fps-ish cadence GameScene drives it at here,
# so 250 fixed steps land well inside the 240-frame gap between the two pulses) --
# the 2nd pulse at 300 is then a harmless no-op for them (TryActivateSkill's
# !in_skill gate has nothing to re-enter or close).
# P3.3.c: c03/c05/c07/c11/c12 join the loop. "ended" is asserted ONLY for the heroes whose
# in_skill really closes within the 600 frames of this run:
#   c03 -- Invoke("CreateTunder", 0.4 s) -> RoleSkillEnd (ends ~0.4 s after each press, even
#          with no enemy to strike, DISASM 0x264bf4);
#   c07 -- ends when the 6th bat is back (BatBack -> RoleSkillEnd; with no enemy every bat
#          misses and the 6-bat flock is home in ~0.62 s).
# For the others only "activated" (plus the effect cue) is asserted, by construction:
#   c05, c11 -- INSTANT ultimates: RoleSkillEnd is called at the tail of RoleSkill itself
#               (DISASM 0x2661e8 / 0x26af90), so in_skill is never true across a frame
#               boundary and GameScene's InSkill() edge detector never sees an "ended";
#   c12      -- RoleSkillEnd (game_typed.c:3736-3755) restarts the cooldown but never clears
#               in_skill (T-49), so the hero stays in_skill after the first summon cast.
# c05/c07/c11/c12 additionally log their world-effect cue ("Skill: <c> effect ...").
for c in c03 c04 c05 c07 c08 c10 c11 c12 c13; do
  run_case "H12_$c" "Skill:" 6 SK_SKIP_MENU=1 SK_MUTE=1 SK_START_CHAR=$c SK_FORCE_SKILL_PULSE=60,300 SK_MAX_FRAMES=600
  grep -q "Skill: $c activated" "$E/H12_$c.txt" || fail "H12_$c has no activated line"
  case "$c" in
    c05|c11|c12) ;; # activated-only (see above)
    *) grep -q "Skill: $c ended" "$E/H12_$c.txt" || fail "H12_$c has no ended line" ;;
  esac
  case "$c" in
    c05|c07|c11|c12) grep -q "Skill: $c effect" "$E/H12_$c.txt" || fail "H12_$c has no effect cue line" ;;
  esac
done
run_case H10 "Buff:" 40 SK_SKIP_MENU=1 SK_FORCE_WEAPON=weapon_013 SK_AUTOWALK=1 SK_FORCE_HIT=0 SK_MAX_FRAMES=600
require_order H10 "Buff: Fire applied to [0-9]+ for 2\.0+s" "Buff: Fire ended on [0-9]+"
run_case H11 "Audio:" 40 SK_SKIP_MENU=1 SK_MUTE=1 SK_AUTOWALK=1 SK_AUTOFIRE=1 SK_FORCE_HIT=30 SK_FORCE_KILL=120 SK_MAX_FRAMES=300
require_order H11 "Audio: bgm bgm_" "Audio: sfx fx_gun_"
require_count H11 "Audio: sfx enermy_die" 1
echo "=== V-S r2_boss3 (seed 1: variant D, floor 9 room 3; zoom 1.5 partial) ==="
run_case r2boss3_seed "Level D5|BossRoom: room 3 uses r2_boss3|SK_ROOM_TELEPORT -> room 3 centre" 40 \
  SK_SEED=1 SK_SKIP_MENU=1 SK_FORCE_CLEAR=25 SK_ROOM_TELEPORT=12,3 SK_MAX_FRAMES=245 SK_SHOT="$E/r2_boss3.png"
check_png "$E/r2_boss3.png"
require_order r2boss3_seed "Level D5" "BossRoom: room 3 uses r2_boss3" "SK_ROOM_TELEPORT -> room 3 centre"
echo "=== V-S r2_boss3 whole room (SK_ZOOM=0.6) ==="
run_case r2boss3_full "Level D5|BossRoom: room 3 uses r2_boss3|SK_ROOM_TELEPORT -> room 3 centre|SK_ZOOM override" 40 \
  SK_SEED=1 SK_SKIP_MENU=1 SK_FORCE_CLEAR=25 SK_ROOM_TELEPORT=12,3 SK_ZOOM=0.6 SK_MAX_FRAMES=245 SK_SHOT="$E/r2_boss3_full.png"
check_png "$E/r2_boss3_full.png"
require_order r2boss3_full "BossRoom: room 3 uses r2_boss3" "SK_ROOM_TELEPORT -> room 3 centre"
require r2boss3_full "SK_ZOOM override"
echo "=== P2.15 boss pool by the floor's real biome (seed 5: F5 volcano -> boss11; seed 1: A5 forest, D5 graveyard) ==="
run_case p215_boss11 "Level F5|BossPool: biome 3-volcano -> boss11|SK_ROOM_TELEPORT -> room 4 centre|SK_ROOM_TELEPORT offset" 40   SK_SEED=5 SK_SKIP_MENU=1 SK_FORCE_CLEAR=25 SK_ROOM_TELEPORT=12,4,0,-160 SK_MAX_FRAMES=365 SK_SHOT="$E/p215_boss11.png"
check_png "$E/p215_boss11.png"
require_order p215_boss11 "Level F5" "BossPool: biome 3-volcano -> boss11" "SK_ROOM_TELEPORT -> room 4 centre"
require p215_boss11 "SK_ROOM_TELEPORT offset \(0,-160\)"
run_case p215_boss11_full "Level F5|BossPool: biome 3-volcano -> boss11|SK_ROOM_TELEPORT -> room 4 centre|SK_ROOM_TELEPORT offset|SK_ZOOM override" 40   SK_SEED=5 SK_SKIP_MENU=1 SK_FORCE_CLEAR=25 SK_ROOM_TELEPORT=12,4,0,-160 SK_ZOOM=0.5 SK_MAX_FRAMES=365 SK_SHOT="$E/p215_boss11_full.png"
check_png "$E/p215_boss11_full.png"
require_order p215_boss11_full "Level F5" "BossPool: biome 3-volcano -> boss11" "SK_ROOM_TELEPORT -> room 4 centre"
require p215_boss11_full "SK_ROOM_TELEPORT offset \(0,-160\)"
run_case p215_forest "Level A5|BossPool: biome 1-forest -> boss0[78]|SK_ROOM_TELEPORT -> room 3 centre|SK_ROOM_TELEPORT offset" 40   SK_SEED=1 SK_SKIP_MENU=1 SK_FORCE_CLEAR=25 SK_ROOM_TELEPORT=12,3,0,-160 SK_MAX_FRAMES=120 SK_SHOT="$E/p215_forest.png"
check_png "$E/p215_forest.png"
require_order p215_forest "Level A5" "BossPool: biome 1-forest -> boss0[78]" "SK_ROOM_TELEPORT -> room 3 centre"
require p215_forest "SK_ROOM_TELEPORT offset \(0,-160\)"
run_case p215_graveyard "Level D5|BossPool: biome 2-graveyard -> boss0[34]|SK_ROOM_TELEPORT -> room 3 centre|SK_ROOM_TELEPORT offset" 40   SK_SEED=1 SK_SKIP_MENU=1 SK_FORCE_CLEAR=25 SK_ROOM_TELEPORT=12,3,0,-160 SK_MAX_FRAMES=245 SK_SHOT="$E/p215_graveyard.png"
check_png "$E/p215_graveyard.png"
require_order p215_graveyard "Level D5" "BossPool: biome 2-graveyard -> boss0[34]" "SK_ROOM_TELEPORT -> room 3 centre"
require p215_graveyard "SK_ROOM_TELEPORT offset \(0,-160\)"
git rev-parse --short HEAD | tee "$E/head.txt"
rm -f "$E"/*.log
if [ "${#FAILS[@]}" -eq 0 ]; then
  echo "STATUS: OK (all steps passed) $(date -u +%Y-%m-%dT%H:%M:%SZ)" | tee "$E/status.txt"
else
  echo "STATUS: FAILED (${#FAILS[@]}): ${FAILS[*]} $(date -u +%Y-%m-%dT%H:%M:%SZ)" | tee "$E/status.txt"
  exit 1
fi
