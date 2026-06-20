#include <gtest/gtest.h>

#include <array>
#include <vector>

#include <glm/glm.hpp>

#include "Util/Collider.hpp"

#include "world/FloorBlock.hpp"
#include "world/Room.hpp"
#include "world/RoomGen.hpp"

using Game::FloorBlock;
using Game::Room;
using Game::RoomGen;

// NOLINTBEGIN(readability-magic-numbers)

// Physics-level (radius-16 circle vs AABB) test of the actual GameScene collision
// for an east/west room pair at the 41-cell (1312px) block pitch: the room walls
// (Room::FromRoomGen) + the corridor flank walls + (when locked) the door seal.
// This closes the gap the abstract cell-grid BFS cannot see: that the real circle
// can traverse room->corridor->room and that a locked room truly contains it.
namespace {

constexpr float kCellPx = 32.0F;
constexpr float kPitch = static_cast<float>(FloorBlock::kBlock) * kCellPx; // 1312
constexpr float kRadius = 16.0F;                                          // kPlayerRadius

RoomGen MakeRoom(const std::array<int, 4> &ent) {
    RoomGen::Options o;
    o.randomRoom = false;
    o.roomWidth = 15;
    o.roomHeight = 15;
    o.wallLevel = 1;
    o.obstacleLevel = 1;
    return RoomGen(4242, ent, o);
}

glm::vec2 BlockWorld(int bx, int by, glm::vec2 origin) {
    return Room::CellToWorld(bx, by, FloorBlock::kBlock, FloorBlock::kBlock,
                             kCellPx, origin);
}

// Append the corridor flank-wall colliders for `dir` (mirrors GameScene's
// addCorridorWall: 1 cell outside the long sides of the walkable strip).
void AddFlanks(std::vector<Util::Collider> &out, int dir, const RoomGen &rg,
               glm::vec2 origin) {
    const FloorBlock::Rect s = FloorBlock::CorridorStrip(dir, rg);
    const bool horiz =
        (dir == FloorBlock::DIR_EAST || dir == FloorBlock::DIR_WEST);
    const glm::vec2 cell{kCellPx, kCellPx};
    if (horiz) {
        for (int bx = s.x0; bx <= s.x1; ++bx) {
            out.push_back(Util::Collider::MakeAABB(BlockWorld(bx, s.y0 - 1, origin), cell));
            out.push_back(Util::Collider::MakeAABB(BlockWorld(bx, s.y1 + 1, origin), cell));
        }
    } else {
        for (int by = s.y0; by <= s.y1; ++by) {
            out.push_back(Util::Collider::MakeAABB(BlockWorld(s.x0 - 1, by, origin), cell));
            out.push_back(Util::Collider::MakeAABB(BlockWorld(s.x1 + 1, by, origin), cell));
        }
    }
}

bool BlockedBy(const std::vector<Util::Collider> &colliders, glm::vec2 pos) {
    const Util::Collider circle = Util::Collider::MakeCircle(pos, kRadius);
    for (const Util::Collider &c : colliders) {
        if (Util::Overlap(c, circle)) {
            return true;
        }
    }
    return false;
}

// The collision an east(A)/west(B) pair produces: both rooms' walls + both
// corridors' flank walls. (No door seal -> a cleared/neutral floor.)
std::vector<Util::Collider> BasePair(const RoomGen &a, const RoomGen &b,
                                     glm::vec2 oa, glm::vec2 ob) {
    std::vector<Util::Collider> c;
    for (const auto &w : Room::FromRoomGen(a, kCellPx, oa).Walls()) {
        c.push_back(w);
    }
    for (const auto &w : Room::FromRoomGen(b, kCellPx, ob).Walls()) {
        c.push_back(w);
    }
    AddFlanks(c, FloorBlock::DIR_EAST, a, oa);
    AddFlanks(c, FloorBlock::DIR_WEST, b, ob);
    return c;
}

} // namespace

// The radius-16 circle traverses the centreline room-A-door -> corridor -> seam ->
// room-B-door with NO collision (the real walk space matches the tested cell grid).
TEST(DoorSealTest, CircleTraversesCorridorCentrelineUnblocked) {
    const RoomGen a = MakeRoom({1, 0, 0, 0}); // east door
    const RoomGen b = MakeRoom({0, 0, 1, 0}); // west door
    const glm::vec2 oa{0.0F, 0.0F};
    const glm::vec2 ob{kPitch, 0.0F};
    const auto base = BasePair(a, b, oa, ob);

    // A's east door is at world x=224 (room-local col 14, row 7); B's west door at
    // x=1088. y=0 is the centre of both door openings AND the corridor band.
    for (float x = 224.0F; x <= 1088.0F; x += 16.0F) {
        EXPECT_FALSE(BlockedBy(base, glm::vec2{x, 0.0F}))
            << "centreline blocked at x=" << x;
    }
}

// The corridor IS walled on its long sides (flank cells block).
TEST(DoorSealTest, CorridorFlanksBlock) {
    const RoomGen a = MakeRoom({1, 0, 0, 0});
    const RoomGen b = MakeRoom({0, 0, 1, 0});
    const auto base = BasePair(a, b, glm::vec2{0, 0}, glm::vec2{kPitch, 0});
    // A flank cell block (34,17) -> world (448,-96); just outside the 5-wide band.
    EXPECT_TRUE(BlockedBy(base, BlockWorld(34, 17, glm::vec2{0, 0})));
    EXPECT_TRUE(BlockedBy(base, BlockWorld(34, 23, glm::vec2{0, 0})));
}

// A LOCKED room must contain: the door seal blocks the centre of the opening (the
// aisle band), not just the two code-11 flanks. With the old code-11-only seal a
// circle at the door centre (224,0) walks straight out; the full-opening seal
// blocks it. (And the door is open when NOT sealed.)
TEST(DoorSealTest, LockedDoorSealBlocksFullOpening) {
    const RoomGen a = MakeRoom({1, 0, 0, 0});
    const RoomGen b = MakeRoom({0, 0, 1, 0});
    const glm::vec2 oa{0.0F, 0.0F};
    const glm::vec2 ob{kPitch, 0.0F};
    auto base = BasePair(a, b, oa, ob);

    const glm::vec2 doorCentre{224.0F, 0.0F}; // A's east opening, centre row
    EXPECT_FALSE(BlockedBy(base, doorCentre)) << "unlocked: opening is passable";

    // Locked: add A's door seal (the cells GameScene seals while the room is the
    // active uncleared room). The seal MUST cover the centre of the opening.
    std::vector<Util::Collider> locked = base;
    for (const auto &c : FloorBlock::DoorSealCells(a)) {
        locked.push_back(Util::Collider::MakeAABB(
            Room::CellToWorld(c.first, c.second, a.Width(), a.Height(), kCellPx, oa),
            glm::vec2{kCellPx, kCellPx}));
    }
    EXPECT_TRUE(BlockedBy(locked, doorCentre))
        << "locked room leaks: the aisle-band centre of the door is not sealed";
}

// Symmetric counterpart to LockedDoorSealBlocksFullOpening: a CLEARED (unlocked)
// room must REOPEN the WHOLE door band, not a 2-cell subset -- else a player who
// clears a room is sealed in forever. GameScene models lock/unlock by toggling the
// ENTIRE m_RoomDoors[i] set (== DoorSealCells) on/off via m_LockedRoom, so:
//   base   = the unlocked floor (no door colliders at all), and
//   locked = base + the SAME DoorSealCells.
// Asserting base-passable <-> locked-blocked over the full 5-wide aisle band proves
// the unseal reopens exactly the set the seal closes (no asymmetric remnant).
TEST(DoorSealTest, ClearedRoomUnsealReopensFullBand) {
    const RoomGen a = MakeRoom({1, 0, 0, 0}); // east door
    const RoomGen b = MakeRoom({0, 0, 1, 0}); // west door
    const glm::vec2 oa{0.0F, 0.0F};
    const glm::vec2 ob{kPitch, 0.0F};

    const auto base = BasePair(a, b, oa, ob); // cleared / not the active room
    std::vector<Util::Collider> locked = base; // the active, uncleared room
    for (const auto &c : FloorBlock::DoorSealCells(a)) {
        locked.push_back(Util::Collider::MakeAABB(
            Room::CellToWorld(c.first, c.second, a.Width(), a.Height(), kCellPx, oa),
            glm::vec2{kCellPx, kCellPx}));
    }

    // A's east opening at world x=224; the 5-wide aisle band is rows cy..cy+4 ->
    // world y -64..64. Every band cell must flip together: open when cleared,
    // sealed when locked. (If unseal only reopened the two code-11 flanks at
    // y=+/-96, the centre rows here would stay blocked -> this catches it.)
    for (int y = -64; y <= 64; y += 32) {
        const glm::vec2 cell{224.0F, static_cast<float>(y)};
        EXPECT_FALSE(BlockedBy(base, cell))
            << "cleared room must reopen the aisle band, y=" << y;
        EXPECT_TRUE(BlockedBy(locked, cell))
            << "locked room must seal the aisle band, y=" << y;
    }
}

// NOLINTEND(readability-magic-numbers)
