#!/usr/bin/env bash
# Fake SoulKnight.exe for tools/tests/test_p218_sweep.py (P218 H3 A/B script). FAKE_MODE selects the ending:
#   normal   -> clears when SK_DT_MS is 20 or unset, else dies (a DeathTrace line)
#   exit3    -> exits 3 after printing a death line
#   neither  -> prints nothing useful and exits 0
# FAKE_ENV_DUMP (a file) receives one line per run with the SK_* variables the game actually saw.
if [ -n "${FAKE_ENV_DUMP:-}" ]; then
  echo "SK_DT_MS=${SK_DT_MS-<unset>} SK_FOO=${SK_FOO-<unset>} SK_SEED=${SK_SEED-<unset>}" >> "$FAKE_ENV_DUMP"
fi
case "${FAKE_MODE:-normal}" in
  exit3)
    echo " [info] DeathTrace: frame 100 rawMs 2000 simMs 2000 meanFrameMs 20.00 enemiesAlive 3 bossAlive 0"; exit 3 ;;
  neither)
    echo " [info] nothing happened"; exit 0 ;;
  *)
    if [ "${SK_DT_MS-20}" = "20" ]; then
      echo " [info] RGRoomX: room 1 cleared -> reward gate fired (room_type==1)"
    else
      echo " [info] DeathTrace: frame 100 rawMs 4000 simMs 4000 meanFrameMs 40.00 enemiesAlive 5 bossAlive 0"
    fi ;;
esac
