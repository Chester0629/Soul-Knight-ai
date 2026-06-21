#include <gtest/gtest.h>

#include <array>
#include <cmath>
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

// The "air-wall" door-transition regression (the case the 1919-test suite missed).
//
// This is a REAL-COLLISION integration test: a radius-16 body walks frame-by-frame
// from the corridor INTO a room through its door, using the exact GameScene movement
// (axis-separated slide vs Room walls + corridor flanks + the door seal) and the
// exact GameScene lock-arm gate (Room::ContainsPointInset(centre, inset)). When the
// room's clear-room seal arms WHILE the body still overlaps the entry door cell, the
// body is pinned at the threshold -- the air wall. The fix arms the seal only once
// the centre is inset past the seal band, so it closes BEHIND the body.
//
// The abstract cell-grid BFS / centreline tests cannot see this: they use a
// dimensionless point with the door permanently open. This drives a real body
// THROUGH a door that locks as it enters.
namespace {

constexpr float kCellPx = 32.0F;
constexpr float kRadius = 16.0F; // kPlayerRadius
// Mirror of GameScene's kLockArmInset = one door cell + the body radius + 1px to
// clear the inclusive circle-vs-AABB tangency at the exact boundary. The seal cells
// sit on the outermost ring (rect edge == door-cell outer face for any size), so a
// centre this far inside is provably clear of the seal.
constexpr float kLockArmInset = kCellPx + kRadius + 1.0F; // 49px
constexpr float kAutoWalkSpeed = 300.0F;                  // px/s (GameScene)

// A clean room: wall/obstacle level 0 -> CreateObstacle places NOTHING, so the
// interior is all floor and the only colliders that can block the body are the
// perimeter walls, the corridor flanks, and (when locked) the door seal. This
// isolates the door-transition behaviour from procedural interior obstacles.
RoomGen MakeCleanRoom(const std::array<int, 4> &ent, int w, int h) {
    RoomGen::Options o;
    o.randomRoom = false;
    o.roomWidth = w;
    o.roomHeight = h;
    o.wallLevel = 0;
    o.obstacleLevel = 0;
    return RoomGen(4242, ent, o);
}

glm::vec2 BlockWorld(int bx, int by, glm::vec2 origin) {
    return Room::CellToWorld(bx, by, FloorBlock::kBlock, FloorBlock::kBlock,
                             kCellPx, origin);
}

// The corridor flank-wall colliders for `dir` (mirrors GameScene's addCorridorWall:
// 1 cell outside the long sides of the 5-wide walkable strip).
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

struct WalkResult {
    glm::vec2 finalPos{0.0F, 0.0F};
    bool armed = false;
};

// Walk a radius-16 body from the `dir` corridor into a clean room at the origin,
// replicating GameScene::Update exactly:
//   * steer toward the room centre at kAutoWalkSpeed (the SK_AUTOWALK steer),
//   * resolve movement axis-separated against the live collider set,
//   * after moving, arm the seal once Room::ContainsPointInset(centre, armInset)
//     -- the lock gate. Once armed the door seal joins the collider set.
// `armInset == 0` reproduces the PRE-FIX gate (arm the instant the centre enters
// the rect == plain ContainsPoint); `armInset == kLockArmInset` is the fix.
WalkResult WalkIntoRoom(int dir, int w, int h, float armInset) {
    std::array<int, 4> ent{0, 0, 0, 0};
    ent[static_cast<std::size_t>(dir)] = 1;
    const RoomGen rg = MakeCleanRoom(ent, w, h);
    const glm::vec2 origin{0.0F, 0.0F};
    const Room room = Room::FromRoomGen(rg, kCellPx, origin);

    std::vector<Util::Collider> base; // walls + flanks (door OPEN)
    for (const Util::Collider &wl : room.Walls()) {
        base.push_back(wl);
    }
    AddFlanks(base, dir, rg, origin);

    std::vector<Util::Collider> seal; // the full door opening (added on arm)
    for (const auto &c : FloorBlock::DoorSealCells(rg)) {
        seal.push_back(Util::Collider::MakeAABB(
            Room::CellToWorld(c.first, c.second, rg.Width(), rg.Height(), kCellPx,
                              origin),
            glm::vec2{kCellPx, kCellPx}));
    }

    const float halfW = static_cast<float>(w) * kCellPx * 0.5F;
    const float halfH = static_cast<float>(h) * kCellPx * 0.5F;
    const float out = 2.0F * kCellPx; // start this far out in the corridor
    glm::vec2 pos{0.0F, 0.0F};
    switch (dir) {
    case FloorBlock::DIR_EAST:
        pos = {halfW + out, 0.0F};
        break;
    case FloorBlock::DIR_WEST:
        pos = {-halfW - out, 0.0F};
        break;
    case FloorBlock::DIR_NEG_Y:
        pos = {0.0F, -halfH - out};
        break;
    case FloorBlock::DIR_POS_Y:
        pos = {0.0F, halfH + out};
        break;
    default:
        break;
    }
    const glm::vec2 target = origin; // the room centre (where the hostile sits)

    WalkResult r;
    const float dt = 16.0F; // ms (~60 fps)
    for (int frame = 0; frame < 600; ++frame) {
        const glm::vec2 before = pos;
        const glm::vec2 d = target - before;
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        if (len <= 1.0F) {
            break; // reached the centre
        }
        const glm::vec2 after =
            before + (d / len) * (kAutoWalkSpeed * dt / 1000.0F);

        const auto blocked = [&](glm::vec2 p) {
            return BlockedBy(base, p) || (r.armed && BlockedBy(seal, p));
        };
        glm::vec2 resolved = before;
        if (!blocked({after.x, before.y})) {
            resolved.x = after.x;
        }
        if (!blocked({resolved.x, after.y})) {
            resolved.y = after.y;
        }
        pos = resolved;

        // Lock-arm gate (the room always "has a live hostile" here; Uncleared
        // until armed). This is the single line the air-wall fix changes.
        if (!r.armed && room.ContainsPointInset(pos, armInset)) {
            r.armed = true;
        }
    }
    r.finalPos = pos;
    return r;
}

struct Case {
    int dir;
    const char *name;
};
constexpr std::array<Case, 4> kDirs = {{{FloorBlock::DIR_EAST, "EAST"},
                                        {FloorBlock::DIR_NEG_Y, "NEG_Y"},
                                        {FloorBlock::DIR_WEST, "WEST"},
                                        {FloorBlock::DIR_POS_Y, "POS_Y"}}};
constexpr std::array<int, 2> kSizes = {15, 21}; // faithful banded room sizes

} // namespace

// PRE-FIX behaviour (arm at inset 0 == ContainsPoint == the old gate): the seal
// snaps shut while the body still overlaps the entry door cell, pinning it AT the
// threshold. The body never reaches the interior -- it is stuck within one body of
// the entry edge. This both characterises the bug and proves this harness detects
// it (so the post-fix PASS is meaningful). All four doors, both sizes.
TEST(DoorTransitionTest, PreFixEdgeArmPinsBodyAtDoorway) {
    for (const Case &c : kDirs) {
        for (int s : kSizes) {
            const WalkResult r = WalkIntoRoom(c.dir, s, s, /*armInset=*/0.0F);
            const Room room = Room::FromRoomGen(
                MakeCleanRoom([&] {
                    std::array<int, 4> e{0, 0, 0, 0};
                    e[static_cast<std::size_t>(c.dir)] = 1;
                    return e;
                }(), s, s),
                kCellPx, glm::vec2{0.0F, 0.0F});
            const float half = static_cast<float>(s) * kCellPx * 0.5F;

            EXPECT_TRUE(r.armed) << c.name << " size=" << s
                                 << ": the lock must still engage";
            // Pinned: never got inset past the seal band.
            EXPECT_FALSE(room.ContainsPointInset(r.finalPos, kLockArmInset))
                << c.name << " size=" << s
                << ": pre-fix body unexpectedly reached the interior";
            // Stuck near the entry edge, not the centre.
            EXPECT_GT(glm::distance(r.finalPos, glm::vec2{0.0F, 0.0F}),
                      half - 2.0F * kCellPx)
                << c.name << " size=" << s << ": expected a pin at the doorway";
        }
    }
}

// POST-FIX behaviour (arm at kLockArmInset deep): the body walks through the open
// door, the seal arms only once its centre is past the seal band (so it never
// catches the body), and the body reaches the room centre. The lock STILL engages
// (armed) -- the room still seals behind the player to force the fight. All four
// doors, both sizes.
//
// *** This is the assertion that FAILS before the fix and PASSES after: setting
// armInset to 0 here (the pre-fix gate) pins the body and this fails; the fix's
// kLockArmInset admits it. ***
TEST(DoorTransitionTest, FixInsetArmAdmitsBodyToInterior) {
    for (const Case &c : kDirs) {
        for (int s : kSizes) {
            const WalkResult r =
                WalkIntoRoom(c.dir, s, s, /*armInset=*/kLockArmInset);
            const Room room = Room::FromRoomGen(
                MakeCleanRoom([&] {
                    std::array<int, 4> e{0, 0, 0, 0};
                    e[static_cast<std::size_t>(c.dir)] = 1;
                    return e;
                }(), s, s),
                kCellPx, glm::vec2{0.0F, 0.0F});

            EXPECT_TRUE(r.armed)
                << c.name << " size=" << s
                << ": the lock must still engage once the body is deep in";
            // Got past the seal band (not pinned).
            EXPECT_TRUE(room.ContainsPointInset(r.finalPos, kLockArmInset))
                << c.name << " size=" << s
                << ": body pinned at the doorway (air wall) -- did not enter";
            // Actually reached the interior centre.
            EXPECT_LT(glm::distance(r.finalPos, glm::vec2{0.0F, 0.0F}),
                      2.0F * kCellPx)
                << c.name << " size=" << s << ": body did not reach the centre";
        }
    }
}

// NOLINTEND(readability-magic-numbers)
