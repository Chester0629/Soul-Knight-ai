#!/usr/bin/env bash
# Evidence chain for docs/MASTER_PLAN.md section 10: build, full suite, headless
# cases H1-H7 (section 1.1) with real logs, and the boss-room visual sample.
# Usage (Git Bash, repo root):  tools/evidence_chain.sh <tag>   -> docs/evidence/<tag>/
set -u -o pipefail
cd "$(dirname "$0")/.." || exit 1
TAG="${1:-latest}"
E="docs/evidence/$TAG"; mkdir -p "$E"
G=./build/Debug/SoulKnight.exe
echo "=== CONFIGURE ==="; cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Debug 2>&1 | tail -1
echo "=== BUILD GAME ==="; cmake --build build --config Debug 2>&1 | grep -E " error|SoulKnight.vcxproj ->" | head -10
echo "=== BUILD TESTS ==="; cmake --build build --config Debug --target SoulKnightTests 2>&1 | grep -E " error|SoulKnightTests.vcxproj ->" | head -10
echo "=== CTEST ==="; ctest --test-dir build -C Debug -E Tests_NOT_BUILT 2>&1 | grep -E "tests passed|tests failed" | tee "$E/ctest.txt"
echo "=== GATE ==="; python tools/validate_data.py 2>&1 | tail -1 | tee "$E/gate.txt"
[ -x "$G" ] || { echo "no game binary"; exit 1; }
echo "=== H1 ==="; SK_START_CHAR=c12 SK_FORCE_CLEAR=60 SK_MAX_FRAMES=200 timeout 120 $G 2>&1 | grep -E "GameScene: hero|Level |CLEARED" | head -8 | tee "$E/H1.txt"
echo "=== H2 ==="; SK_SKIP_MENU=1 SK_GATE_TELEPORT=200 SK_MAX_FRAMES=400 timeout 120 $G 2>&1 | grep -E "TransferGate|GameScene: hero" | head -6 | tee "$E/H2.txt"
echo "H2-neg floors:"; SK_SKIP_MENU=1 SK_MAX_FRAMES=400 timeout 120 $G 2>&1 | grep -c "GameScene: hero" | tee "$E/H2neg.txt"
echo "=== H3 ==="; SK_SKIP_MENU=1 SK_ROOM_TELEPORT=60,1 SK_FORCE_KILL=120 SK_FORCE_KILL_EVERY=90 SK_MAX_FRAMES=700 timeout 120 $G 2>&1 | grep -E "RGRoomX: room 1|EnemyMaker: room 1" | head -12 | tee "$E/H3.txt"
echo "=== H4 ==="; SK_SKIP_MENU=1 SK_FORCE_KILL=120 SK_MAX_FRAMES=700 timeout 120 $G 2>&1 | grep -E "Reward:|Pickup:" | head -6 | tee "$E/H4.txt"
echo "=== H5 ==="; SK_SKIP_MENU=1 SK_FORCE_DIE=100 SK_MAX_FRAMES=300 SK_SHOT="$E/H5.png" timeout 120 $G 2>&1 | grep -E "died|EndScene: game" | head -3 | tee "$E/H5.txt"
echo "=== H6 ==="; SK_SKIP_MENU=1 SK_COINS=30 SK_SHOP_BUY=100 SK_MAX_FRAMES=130 timeout 120 $G 2>&1 | grep -E "Shop:" | head -5 | tee "$E/H6.txt"
echo "=== H7 ==="; SK_SKIP_MENU=1 SK_FORCE_CLEAR=90 SK_FORCE_KILL=30 SK_MAX_FRAMES=447 timeout 120 $G 2>&1 | grep -E "Level B5|BossRoom:|Boss defeated|Chest: boss|TransferGate: open" | sed -n '/Level B5/,$p' | head -6 | tee "$E/H7.txt"
echo "=== STATUE (P1.12 statues table) ==="; SK_SKIP_MENU=1 SK_EVENT_ROOM=statue SK_COINS=30 SK_STATUE_WORSHIP=100 SK_MAX_FRAMES=130 timeout 120 $G 2>&1 | grep -E "Statue:" | head -4 | tee "$E/statue.txt"
echo "=== V-S r2_boss3 (seed 1: variant D, floor 9 room 3) ==="
SK_SEED=1 SK_SKIP_MENU=1 SK_FORCE_CLEAR=25 SK_ROOM_TELEPORT=12,3 SK_MAX_FRAMES=245 SK_SHOT="$E/r2_boss3.png" timeout 120 $G 2>&1 | grep -E "Level D5|BossRoom: room 3 uses r2_boss3" | tail -2 | tee "$E/r2boss3_seed.txt"
echo "=== V-S r2_boss3 whole room (SK_ZOOM) ==="
SK_SEED=1 SK_SKIP_MENU=1 SK_FORCE_CLEAR=25 SK_ROOM_TELEPORT=12,3 SK_ZOOM=0.6 SK_MAX_FRAMES=245 SK_SHOT="$E/r2_boss3_full.png" timeout 120 $G >/dev/null 2>&1; ls -la "$E/r2_boss3_full.png" | awk '{print $5}'
git rev-parse --short HEAD | tee "$E/head.txt"
echo "STATUS: all steps ran (pipefail on; a failed grep shows as empty file)"
