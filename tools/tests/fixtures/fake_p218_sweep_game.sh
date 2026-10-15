#!/usr/bin/env bash
seed="$SK_SEED"
if [ "${FAKE_EXIT:-0}" != "0" ]; then exit "$FAKE_EXIT"; fi
if [ -n "$SK_FORCE_CLEAR" ]; then
  if [ "$seed" = "${FAKE_NO_BOSS_SEED:-none}" ]; then echo " [info] Level A1 (1-1) nothing"; exit 0; fi
  for lvl in A5 D5; do
    echo " [info] Level $lvl (1-5) biome x"
    echo " [info] PosTrace: frame 3 boss room 3 (1.0,2.0) r=19.2 alive=1 blocked=${FAKE_BOSS_BLOCKED:-0}"
  done
else
  if [ "$seed" = "${FAKE_NO_ENEMY_SEED:-none}" ]; then echo " [info] nothing"; exit 0; fi
  for i in 0 1 2 3 4; do
    echo " [info] PosTrace: frame 3 enemy $i room 1 e_x (1.0,2.0) r=12.8 alive=1 blocked=${FAKE_ENEMY_BLOCKED:-0}"
  done
fi
