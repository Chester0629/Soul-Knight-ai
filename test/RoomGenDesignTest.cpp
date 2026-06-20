#include <gtest/gtest.h>

#include <array>
#include <utility>
#include <vector>

#include "world/FloorBlock.hpp"
#include "world/RoomGen.hpp"

using Game::FloorBlock;
using Game::RoomGen;

// NOLINTBEGIN(readability-magic-numbers)

// === Phase 3 (3.1): design-room obstacle branch of CreateObstacle =============
//
// Decision #2 = b': the RoomGen shell (perimeter/floor/door-cells) stays; for a
// design slot the procedural obstacle layer (Phase A/B) is REPLACED by the
// prefab's obj_index==0 wall markers (designSolidCells). The branch is gated on
// Options::proceduralObstacles, which DEFAULTS to true -- so the procedural path
// and every RoomGenGoldenTest are byte-identical (proven there; this file pins
// the new design branch).

namespace {

RoomGen::Options Design(int w, int h, std::vector<std::pair<int, int>> walls) {
    RoomGen::Options o;
    o.randomRoom = false;
    o.roomWidth = w;
    o.roomHeight = h;
    o.proceduralObstacles = false;       // design mode: no procedural obstacles
    o.designSolidCells = std::move(walls);
    return o;
}

const std::array<int, 4> kAllDoors = {1, 1, 1, 1};
const std::array<int, 4> kNoDoors = {0, 0, 0, 0};

int CountCode(const RoomGen &r, int code) {
    int n = 0;
    for (int x = 0; x < r.Width(); ++x) {
        for (int y = 0; y < r.Height(); ++y) {
            if (r.At(x, y) == code) {
                ++n;
            }
        }
    }
    return n;
}

} // namespace

// The supplied interior wall cells become solid (code 1); every other interior
// cell stays open floor (code 0). No procedural big/small obstacles appear.
TEST(RoomGenDesignTest, StampsDesignWallsNoProceduralObstacles) {
    // r1_1's real 7x7 centre block: grid x,y in [4,10].
    std::vector<std::pair<int, int>> block;
    for (int x = 4; x <= 10; ++x) {
        for (int y = 4; y <= 10; ++y) {
            block.emplace_back(x, y);
        }
    }
    const RoomGen r(20240607, kNoDoors, Design(15, 15, block));

    // Every block cell is solid (code 1).
    for (const auto &c : block) {
        EXPECT_EQ(r.At(c.first, c.second), 1) << "block cell " << c.first << ","
                                              << c.second;
    }
    EXPECT_EQ(CountCode(r, 1), 49) << "exactly the 49 stamped walls are solid";
    // No procedural destructibles (code 2) or big-obstacle markers (8/9).
    EXPECT_EQ(CountCode(r, 2), 0);
    EXPECT_EQ(CountCode(r, 8), 0);
    EXPECT_EQ(CountCode(r, 9), 0);
}

// FloorList (spawn/connectivity source) excludes the stamped walls but includes
// the open interior around them -- the room is walkable around the block.
TEST(RoomGenDesignTest, FloorListExcludesDesignWalls) {
    std::vector<std::pair<int, int>> block;
    for (int x = 4; x <= 10; ++x) {
        for (int y = 4; y <= 10; ++y) {
            block.emplace_back(x, y);
        }
    }
    const RoomGen r(7, kNoDoors, Design(15, 15, block));
    for (const auto &fc : r.FloorList()) {
        const bool inBlock = fc.first >= 4 && fc.first <= 10 && fc.second >= 4 &&
                             fc.second <= 10;
        EXPECT_FALSE(inBlock) << "floor cell inside the wall block: " << fc.first
                              << "," << fc.second;
    }
    EXPECT_GT(r.FloorList().size(), 0u);
    // Empty design (no walls) leaves the whole interior walkable -> more floor.
    const RoomGen empty(7, kNoDoors, Design(15, 15, {}));
    EXPECT_GT(empty.FloorList().size(), r.FloorList().size());
}

// A design wall that collides with a door (11), aisle (-2) or border (-1) is
// NOT stamped: the shell + 5-wide door band the corridor seam needs stay intact.
TEST(RoomGenDesignTest, DoesNotOverwriteDoorAisleOrBorder) {
    // Build with all doors, then try to stamp walls over the entire grid.
    std::vector<std::pair<int, int>> everywhere;
    for (int x = 0; x < 21; ++x) {
        for (int y = 0; y < 15; ++y) {
            everywhere.emplace_back(x, y);
        }
    }
    const RoomGen r(7, kAllDoors, Design(21, 15, everywhere));
    // Door cells (11) survive -- the room is still enterable on every side.
    EXPECT_GT(CountCode(r, 11), 0) << "door cells must survive design stamping";
    // Aisle cells (-2) survive (the carved corridor path into the room).
    EXPECT_GT(CountCode(r, -2), 0) << "aisle cells must survive";
    // Border (-1) is untouched (only code-0 floor is converted).
    EXPECT_GT(CountCode(r, -1), 0) << "border ring must survive";
}

// Design mode must still produce a connected, door-reachable interior: a wall
// block in the centre leaves a walkable ring that reaches every carved door.
TEST(RoomGenDesignTest, DesignRoomStaysDoorReachable) {
    std::vector<std::pair<int, int>> block;
    for (int x = 5; x <= 9; ++x) {
        for (int y = 5; y <= 9; ++y) {
            block.emplace_back(x, y);
        }
    }
    const RoomGen r(12345, kAllDoors, Design(15, 15, block));
    // ConnectedFloorCells flood-fills from the doors over walkable cells; with a
    // central block it must still reach a non-trivial interior (not just doors).
    const auto cells = FloorBlock::ConnectedFloorCells(r);
    EXPECT_GT(cells.size(), 20u) << "central block must leave a reachable ring";
    // None of the reachable cells is inside the wall block.
    for (const auto &c : cells) {
        const bool inBlock = c.first >= 5 && c.first <= 9 && c.second >= 5 &&
                             c.second <= 9;
        EXPECT_FALSE(inBlock);
    }
}

// NOLINTEND(readability-magic-numbers)
