#include <gtest/gtest.h>

#include <array>

#include <glm/glm.hpp>

#include "world/FloorBlock.hpp"
#include "world/Room.hpp"
#include "world/RoomGen.hpp"

using Game::FloorBlock;
using Game::Room;
using Game::RoomGen;

// NOLINTBEGIN(readability-magic-numbers)

namespace {

constexpr int kCells = 15;
constexpr float kCellPx = 32.0F;                          // GameScene kCellPx
constexpr float kPitch = static_cast<float>(FloorBlock::kBlock) * kCellPx; // 1312

RoomGen MakeRoom(const std::array<int, 4> &ent) {
    RoomGen::Options o;
    o.randomRoom = false;
    o.roomWidth = kCells;
    o.roomHeight = kCells;
    o.wallLevel = 1;
    o.obstacleLevel = 1;
    return RoomGen(4242, ent, o);
}

// World position of a block-local cell whose room block is centred at `origin`.
// The 41-cell block is centred, so block cell (bx,by) maps to
// origin + (bx - (kBlock-1)/2, by - (kBlock-1)/2) * cellPx -- identical to the
// room-local CellToWorld GameScene already uses (offset 13 + room centre 7 = 20).
glm::vec2 WorldOf(int bx, int by, glm::vec2 origin) {
    const float c = static_cast<float>(FloorBlock::kBlock - 1) * 0.5F; // 20
    return origin + glm::vec2{(static_cast<float>(bx) - c) * kCellPx,
                              (static_cast<float>(by) - c) * kCellPx};
}

} // namespace

// === Seam 2: corridors are NEUTRAL -- a player in a corridor is in no room, so
// the clear-room door seal never arms there (otherwise a player walking a corridor
// past a still-locked room could be wrongly trapped, or trap themselves). This is
// asserted, not left as a "corridor is outside the AABB by construction" note.
TEST(CorridorNeutralityTest, RoomInteriorIsContained) {
    const std::array<int, 4> east = {1, 0, 0, 0};
    const RoomGen rg = MakeRoom(east);
    const Room room = Room::FromRoomGen(rg, kCellPx, glm::vec2{0.0F, 0.0F});

    EXPECT_TRUE(room.ContainsPoint(room.Center())) << "room centre";
    EXPECT_TRUE(room.ContainsPoint(WorldOf(20, 20, {0, 0}))) << "block centre cell";
    // The room's east door edge (block col 27) is still inside the room AABB.
    EXPECT_TRUE(room.ContainsPoint(WorldOf(27, 20, {0, 0}))) << "door edge cell";
}

TEST(CorridorNeutralityTest, CorridorCellsAreInNoRoom) {
    const std::array<int, 4> east = {1, 0, 0, 0};
    const std::array<int, 4> west = {0, 0, 1, 0};
    const RoomGen ra = MakeRoom(east);
    const RoomGen rb = MakeRoom(west);
    const glm::vec2 originA{0.0F, 0.0F};
    const glm::vec2 originB{kPitch, 0.0F}; // B one block east of A
    const Room roomA = Room::FromRoomGen(ra, kCellPx, originA);
    const Room roomB = Room::FromRoomGen(rb, kCellPx, originB);

    // Every carved east-corridor cell of A (block cols 28..40, the junction +
    // floor) must be OUTSIDE both room AABBs -> ContainsPoint false for both, so
    // the locked-room seal cannot arm while the player is on the corridor.
    for (int bx = 28; bx <= 40; ++bx) {
        const glm::vec2 cell = WorldOf(bx, 20, originA);
        EXPECT_FALSE(roomA.ContainsPoint(cell)) << "corridor x=" << bx << " in A";
        EXPECT_FALSE(roomB.ContainsPoint(cell)) << "corridor x=" << bx << " in B";
    }

    // The junction (28) is the FIRST cell past A's east AABB edge: cell 27 is in
    // A, cell 28 is in neither -- the exact room->corridor neutrality boundary.
    EXPECT_TRUE(roomA.ContainsPoint(WorldOf(27, 20, originA)));
    EXPECT_FALSE(roomA.ContainsPoint(WorldOf(28, 20, originA)));

    // The mid-seam point between the two rooms is in neither room.
    EXPECT_FALSE(roomA.ContainsPoint(glm::vec2{kPitch * 0.5F, 0.0F}));
    EXPECT_FALSE(roomB.ContainsPoint(glm::vec2{kPitch * 0.5F, 0.0F}));
}

// NOLINTEND(readability-magic-numbers)
