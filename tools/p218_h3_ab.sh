#!/usr/bin/env bash
# P2.18 H3 A/B (R39/R40): run the IDENTICAL H3 command on a base build and the current build.
# Usage (Git Bash): bash tools/p218_h3_ab.sh <base SoulKnight.exe> <current SoulKnight.exe> <out file>
# Both binaries must carry the same DeathTrace diagnostics line (the base one is built from a git
# worktree of 0912b66 patched with the same ~10 diagnostic lines, nothing else).
# H3 = SK_SKIP_MENU=1 SK_ROOM_TELEPORT=60,1 SK_FORCE_KILL=120 SK_FORCE_KILL_EVERY=45 SK_MAX_FRAMES=700,
# seed pinned to the default 20240607. Every other SK_* variable of the calling shell is REMOVED from
# the game's environment (env -u), and the "unset" dt case really has no SK_DT_MS at all (not an empty
# string). Per run it records: outcome (cleared / died), the frame, simulated time (sum of the frame
# deltas the game logic saw), wall time, enemies alive, mean frame time, whole-process wall ms/frame.
# FAILS (non-zero exit, last line 'P218 H3 A/B: FAIL') when ANY game run exits non-zero or times out,
# or ends with neither a death nor a clear (NEITHER); the final status line is the script's exit code.
set -u
if [ "$#" -ne 3 ]; then echo "usage: $0 <base exe> <current exe> <out file>"; exit 2; fi
BASE="$1"; CUR="$2"; OUT="$3"
SEED=20240607
# the H3 hooks (applied after every other SK_* is removed from the environment)
CASE=(SK_SEED=$SEED SK_SKIP_MENU=1 SK_MUTE=1 SK_ROOM_TELEPORT=60,1 SK_FORCE_KILL=120 SK_FORCE_KILL_EVERY=45 SK_MAX_FRAMES=700)
FAILS=0
fail() { echo "FAIL: $1"; FAILS=$((FAILS+1)); }
{
  echo "# H3 A/B: base 0912b66 (+DeathTrace diagnostics) vs current P2.18 working tree (+DeathTrace)"
  echo "# command: ${CASE[*]} [SK_DT_MS=<dt>] SoulKnight.exe ; all other SK_* removed from the environment"
  printf "%-8s %-9s %-4s %-8s %-6s %-9s %-9s %-10s %-9s %-12s\n" build SK_DT_MS run outcome frame simMs rawMs meanFrameMs enemiesAlive wallMsPerFrame
} > "$OUT" || { echo "FAIL: cannot write $OUT"; exit 1; }

# env -u arguments for every SK_* variable currently exported
UNSETS=()
while IFS= read -r v; do UNSETS+=(-u "$v"); done < <(env | sed -n 's/^\(SK_[A-Za-z0-9_]*\)=.*/\1/p')

run_one() { # build exe dt run
  local name="$1" exe="$2" dt="$3" run="$4" log t0 t1 wall rc
  log="$(mktemp)"; t0=$(date +%s%N)
  if [ "$dt" = "unset" ]; then
    env ${UNSETS[@]+"${UNSETS[@]}"} -u SK_DT_MS "${CASE[@]}" timeout 180 "$exe" > "$log" 2>&1
  else
    env ${UNSETS[@]+"${UNSETS[@]}"} "${CASE[@]}" SK_DT_MS="$dt" timeout 180 "$exe" > "$log" 2>&1
  fi
  rc=$?
  t1=$(date +%s%N); wall=$(( (t1 - t0) / 1000000 / 700 ))
  local line cl dl
  line=$(grep "DeathTrace:" "$log" | head -1)
  cl=$(grep -n "room 1 cleared" "$log" | head -1 | cut -d: -f1)
  dl=$(grep -n "DeathTrace:" "$log" | head -1 | cut -d: -f1)
  if [ "$rc" -ne 0 ]; then
    fail "$name dt=$dt run $run exited $rc (124 = timeout)"
    printf "%-8s %-9s %-4s %-8s %-6s %-9s %-9s %-10s %-9s %-12s\n" "$name" "$dt" "$run" "FAIL(rc=$rc)" "-" "-" "-" "-" "-" "$wall" >> "$OUT"
  elif [ -n "$cl" ] && { [ -z "$dl" ] || [ "$cl" -lt "$dl" ]; }; then
    printf "%-8s %-9s %-4s %-8s %-6s %-9s %-9s %-10s %-9s %-12s\n" "$name" "$dt" "$run" cleared "-" "-" "-" "-" "-" "$wall" >> "$OUT"
  elif [ -n "$line" ]; then
    local fr sim raw mean alive
    fr=$(echo "$line" | sed -E 's/.*frame ([0-9]+) .*/\1/'); raw=$(echo "$line" | sed -E 's/.*rawMs ([0-9]+) .*/\1/')
    sim=$(echo "$line" | sed -E 's/.*simMs ([0-9]+) .*/\1/'); mean=$(echo "$line" | sed -E 's/.*meanFrameMs ([0-9.]+) .*/\1/')
    alive=$(echo "$line" | sed -E 's/.*enemiesAlive ([0-9]+) .*/\1/')
    printf "%-8s %-9s %-4s %-8s %-6s %-9s %-9s %-10s %-9s %-12s\n" "$name" "$dt" "$run" died "$fr" "$sim" "$raw" "$mean" "$alive" "$wall" >> "$OUT"
  else
    fail "$name dt=$dt run $run ended with neither a death nor a clear (NEITHER)"
    printf "%-8s %-9s %-4s %-8s %-6s %-9s %-9s %-10s %-9s %-12s\n" "$name" "$dt" "$run" NEITHER "-" "-" "-" "-" "-" "$wall" >> "$OUT"
  fi
  rm -f "$log"
}
for dt in 20 30 40 80; do
  for name in base current; do
    exe="$BASE"; [ "$name" = current ] && exe="$CUR"
    for run in 1 2; do run_one "$name" "$exe" "$dt" "$run"; done
  done
done
for name in base current; do
  exe="$BASE"; [ "$name" = current ] && exe="$CUR"
  for run in 1 2 3; do run_one "$name" "$exe" unset "$run"; done
done
status() { if [ "$FAILS" -eq 0 ]; then STATUS="P218 H3 A/B: OK"; else STATUS="P218 H3 A/B: FAIL ($FAILS)"; fi; }
status
# Every step that can still fail (writing the status line, showing the table) is counted BEFORE the
# final line is printed, and the status is recomputed after each, so the last line can never say OK
# while the exit code is non-zero.
echo "# $STATUS" >> "$OUT" || { FAILS=$((FAILS+1)); status; }
cat "$OUT" || { FAILS=$((FAILS+1)); status; }
echo "$STATUS"
[ "$FAILS" -eq 0 ]
