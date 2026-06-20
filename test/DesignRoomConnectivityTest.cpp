#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <vector>

#include "data/GameData.hpp"
#include "world/FloorBlock.hpp"
#include "world/RoomGen.hpp"
#include "world/Room.hpp"

using Game::DesignRoomDef;
using Game::FloorBlock;
using Game::GameData;
using Game::RoomGen;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
const char *kResourceRoot = RESOURCE_DIR;

// Build a room in DESIGN mode with the prefab's obj_index==0 walls stamped (exactly
// what GameScene feeds RoomGen for a design slot).
RoomGen BuildDesign(const DesignRoomDef &def, int seed,
                    const std::array<int, 4> &entrance) {
    RoomGen::Options o;
    o.randomRoom = false;
    o.roomWidth = def.width;
    o.roomHeight = def.height;
    o.proceduralObstacles = false;
    for (const auto &ob : def.obstacles) {
        if (ob.objIndex == 0) {
            o.designSolidCells.emplace_back(ob.x, ob.y);
        }
    }
    return RoomGen(seed, entrance, o);
}
} // namespace

// === Phase 3 gate #3: spawn-not-trapped + door-reachable on REAL prefab =======
// obstacles, every design room, multiple seeds, both square and 21-wide. The P1
// seed-3 "spawn wedged in a pocket" risk is AMPLIFIED once real (dense) obstacles
// land, so this asserts on ALL 108 rooms, not a sample: ConnectedFloorCells (the
// door-flood the live spawn uses) must stay non-trivial -- i.e. no real design
// room seals its own interior off from its doors.
TEST(DesignRoomConnectivityTest, EveryRealDesignRoomDoorReachableNoTrappedSpawn) {
    GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    ASSERT_EQ(gd.DesignRooms().size(), 108u);

    const std::array<int, 4> allDoors = {1, 1, 1, 1};
    int wideChecked = 0;       // 21-wide / 21-tall rooms actually exercised
    int denseChecked = 0;      // rooms with a heavy wall load
    for (const auto &entry : gd.DesignRooms()) {
        const DesignRoomDef &def = entry.second;
        int wallCells = 0;
        for (const auto &ob : def.obstacles) {
            if (ob.objIndex == 0) {
                ++wallCells;
            }
        }
        if (def.width == 21 || def.height == 21) {
            ++wideChecked;
        }
        if (wallCells >= 20) {
            ++denseChecked;
        }
        // The shell (CreateFloor/CreateWall) is seeded, so sweep seeds: the
        // design walls are fixed but the surrounding floor noise varies.
        for (int seed : {1, 2, 3, 7, 12345}) {
            const RoomGen rg = BuildDesign(def, seed, allDoors);
            const auto cells = FloorBlock::ConnectedFloorCells(rg);
            // The live spawn is the median of THIS set; it must be non-trivial so
            // the player never spawns wedged with no door-reachable floor.
            ASSERT_GT(cells.size(), 8u)
                << entry.first << " seed=" << seed << " w=" << def.width
                << " h=" << def.height << " walls=" << wallCells
                << " -- interior sealed off from its doors";
            // Every returned cell is genuinely walkable (never a stamped wall).
            for (const auto &c : cells) {
                ASSERT_FALSE(Game::Room::IsSolidCell(rg.At(c.first, c.second)))
                    << entry.first << " connected cell is solid";
            }
        }
    }
    EXPECT_GT(wideChecked, 0) << "must exercise 21-wide/tall design rooms";
    EXPECT_GT(denseChecked, 0) << "must exercise heavily-walled design rooms";
}

// NOLINTEND(readability-magic-numbers)
