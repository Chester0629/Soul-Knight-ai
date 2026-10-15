#!/usr/bin/env bash
# P2.18 in-game spawn sweep (not part of evidence_chain.sh: ~35 game launches, a few minutes).
# Usage (Git Bash, repo root, Debug build):  bash tools/p218_seed_sweep.sh <out dir>
# Writes <out dir>/seed_sweep.txt (floor 0, seeds 1-30: enemies and how many overlap a blocker at
# scene frame 3) and <out dir>/boss_sweep.txt (boss floors of seeds 1 2 3 5 7: the boss line of
# every floor-5 level, via SK_FORCE_CLEAR=10 chaining floors).
# FAILS (exit 1, last line says FAIL) on: a game run with a non-zero exit code or a timeout, a seed
# with no enemy rows, a boss seed with no boss row, fewer than 100 enemy rows or 8 boss rows in total
# (empty output can never pass), or any row with blocked=1.
cd "$(dirname "$0")/.." || exit 1
OUT="${1:-docs/evidence/p2.18}"
mkdir -p "$OUT"
G="${P218_GAME:-./build/Debug/SoulKnight.exe}" # P218_GAME: test hook (tools/tests/test_p218_sweep.py swaps in a fake game)
[ -x "$G" ] || { echo "FAIL: no game binary $G"; exit 1; }
TMP="$(mktemp)"
FAILS=0
fail() { echo "FAIL: $1"; FAILS=$((FAILS+1)); }

total=0; blocked=0; floors=0
: > "$OUT/seed_sweep.txt"
for seed in $(seq 1 30); do
  SK_SEED=$seed SK_SKIP_MENU=1 SK_MUTE=1 SK_DT_MS=20 SK_MAX_FRAMES=5 SK_TRACE_POS=3 timeout 120 "$G" > "$TMP" 2>&1
  rc=$?
  [ "$rc" -eq 0 ] || fail "seed $seed exited $rc"
  n=$(grep -c "PosTrace: frame 3 enemy" "$TMP")
  b=$(grep "PosTrace: frame 3 enemy" "$TMP" | grep -c "blocked=1")
  [ "$n" -ge 1 ] || fail "seed $seed produced no enemy rows"
  total=$((total+n)); blocked=$((blocked+b)); floors=$((floors+1))
  echo "seed $seed enemies $n blocked $b" >> "$OUT/seed_sweep.txt"
done
echo "SWEEP floors=$floors enemies=$total blocked=$blocked" >> "$OUT/seed_sweep.txt"
tail -1 "$OUT/seed_sweep.txt"
[ "$total" -ge 100 ] || fail "only $total enemy rows (need >= 100)"
[ "$blocked" -eq 0 ] || fail "$blocked enemies overlap a blocker"

bosses=0; bblocked=0
: > "$OUT/boss_sweep.txt"
for seed in 1 2 3 5 7; do
  SK_SEED=$seed SK_SKIP_MENU=1 SK_MUTE=1 SK_DT_MS=20 SK_FORCE_CLEAR=10 SK_MAX_FRAMES=100 SK_TRACE_POS=3 timeout 120 "$G" > "$TMP" 2>&1
  rc=$?
  [ "$rc" -eq 0 ] || fail "boss seed $seed exited $rc"
  rows=$(grep -E "PosTrace: frame 3 boss|Level [A-G][0-9] " "$TMP" | awk '/Level/{lvl=$4} /boss/{print lvl " " $0}' | sed 's/\[info\] //')
  seedRows=0
  while IFS= read -r line; do
    [ -n "$line" ] || continue
    echo "seed $seed $line" >> "$OUT/boss_sweep.txt"
    bosses=$((bosses+1)); seedRows=$((seedRows+1))
    case "$line" in *"blocked=1"*) bblocked=$((bblocked+1));; esac
  done <<< "$rows"
  [ "$seedRows" -ge 1 ] || fail "boss seed $seed produced no boss row"
done
echo "BOSS SWEEP bosses=$bosses blocked=$bblocked" >> "$OUT/boss_sweep.txt"
tail -1 "$OUT/boss_sweep.txt"
[ "$bosses" -ge 8 ] || fail "only $bosses boss rows (need >= 8)"
[ "$bblocked" -eq 0 ] || fail "$bblocked bosses overlap a blocker"
rm -f "$TMP"
if [ "$FAILS" -eq 0 ]; then echo "P218 SWEEP: OK" | tee "$OUT/sweep_status.txt"; else echo "P218 SWEEP: FAIL ($FAILS)" | tee "$OUT/sweep_status.txt"; exit 1; fi
