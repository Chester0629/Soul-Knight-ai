#include <gtest/gtest.h>

#include <array>
#include <vector>

#include <glm/glm.hpp>

#include "RealBodyDriver.hpp"

#include "data/GameData.hpp"

using sktest::RealBodyDriver;

// NOLINTBEGIN(readability-magic-numbers)

// =============================================================================
// Real-collision verification of the "enter room / lock / clear / reach boss"
// integration that P1/P2 previously only LOG-verified through the SK_AUTOWALK
// ContainsPoint threshold (a false positive for body-in-interior -- the gap the
// air-wall bug hid in). Every "arrived / entered / reached" assertion here is on
// the body's ACTUAL position via real BlocksAny collision + Room::ContainsPointInset
// (deep interior), NEVER the ContainsPoint threshold. See RealBodyDriver.hpp.
//
// Tests 1-5 prove the five driver capabilities (real movement + assertion); the
// three canonically-named tests (EntersAndLocks / ClearsAndReopen / ReachesBoss)
// are the real verifications that replace the autowalk-log weak checks.
// =============================================================================

namespace {
constexpr float kPitch = RealBodyDriver::kPitch; // 1312

const char *kResourceRoot = RESOURCE_DIR; // CMake-injected (as GameDataTest).

// E/W door flag helpers (index {E, -y, W, +y}).
constexpr std::array<int, 4> kEast{1, 0, 0, 0};
constexpr std::array<int, 4> kWest{0, 0, 1, 0};
constexpr std::array<int, 4> kWestEast{1, 0, 1, 0};
} // namespace

// --- Capability 1: real-body navigation reaches the INTERIOR (not threshold) --
TEST(RealBodyDriverTest, NavigatesToRoomInterior) {
    RealBodyDriver d(1);
    const int start = d.AddRoom({0.0F, 0.0F}, 15, kEast, 0);
    const int dest = d.AddRoom({kPitch, 0.0F}, 15, kWest, 0); // empty -> no lock
    d.SetPlayer(d.RoomCenter(start));

    ASSERT_TRUE(d.NavigateTo(d.RoomCenter(dest), /*tol=*/24.0F, /*maxFrames=*/600))
        << "real body failed to traverse corridor into the destination room";
    // Real proof: the body is DEEP in the interior, not merely past the rect edge.
    EXPECT_TRUE(d.BodyDeepInside(dest))
        << "body reached only the threshold, not the interior";
    EXPECT_GT(glm::length(d.PlayerPos() - d.RoomCenter(start)), kPitch * 0.5F)
        << "body never actually left the start room";
}

// --- Capability 1 (around an obstacle): waypoint route succeeds where a blind
//     straight line dies on the wall (proves real routing, not straight-line). --
TEST(RealBodyDriverTest, NavigatesAroundObstacle) {
    // A vertical wall (x in [-16,16], y in [-100,100]) straddles the straight path.
    const glm::vec2 from{-200.0F, 0.0F};
    const glm::vec2 to{200.0F, 0.0F};

    RealBodyDriver straight(2);
    straight.AddRoom({0.0F, 0.0F}, 25, {0, 0, 0, 0}, 0);
    straight.AddWall({0.0F, 0.0F}, {32.0F, 200.0F});
    straight.SetPlayer(from);
    EXPECT_FALSE(straight.NavigateTo(to, 24.0F, 400))
        << "straight line should be blocked by the wall (proves the obstacle bites)";

    RealBodyDriver routed(2);
    routed.AddRoom({0.0F, 0.0F}, 25, {0, 0, 0, 0}, 0);
    routed.AddWall({0.0F, 0.0F}, {32.0F, 200.0F});
    routed.SetPlayer(from);
    EXPECT_TRUE(routed.NavigateWaypoints({{0.0F, 220.0F}, to}, 24.0F, 600))
        << "waypoint route over the wall should reach the target";
}

// --- Capability 2 / RealBodyEntersAndLocksRoom -------------------------------
TEST(RealBodyDriverTest, RealBodyEntersAndLocksRoom) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::EnemyDef *edef = gd.FindEnemy("EnemyAI01");
    ASSERT_NE(edef, nullptr);

    RealBodyDriver d(7);
    const int start = d.AddRoom({0.0F, 0.0F}, 15, kEast, 0);
    const int room = d.AddRoom({kPitch, 0.0F}, 15, kWest, 1);
    d.AddEnemy(room, *edef, d.RoomCenter(room), 1234); // live hostile -> will lock
    d.SetPlayer(d.RoomCenter(start));

    // Reaching the interior at all PROVES no air-wall pin (pre-fix this would pin
    // at the doorway and NavigateTo would fail).
    ASSERT_TRUE(d.NavigateTo(d.RoomCenter(room), 24.0F, 800));
    EXPECT_TRUE(d.BodyDeepInside(room)) << "body not in interior";
    EXPECT_FALSE(d.DoorOpen(room)) << "door must seal behind the body (locked)";

    // Sealed in: the body cannot walk back out to the start room.
    const bool escaped = d.NavigateTo(d.RoomCenter(start), 24.0F, 400);
    EXPECT_FALSE(escaped) << "locked room must contain the body";
    EXPECT_EQ(d.PlayerRoomId(), room) << "body should still be inside the locked room";
}

// --- Capability 3 / RealBodyAimsAndKillsEnemy --------------------------------
TEST(RealBodyDriverTest, RealBodyAimsAndKillsEnemy) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::EnemyDef *edef = gd.FindEnemy("EnemyAI01");
    const Game::WeaponDef *wdef = gd.FindWeapon("Gun001");
    ASSERT_NE(edef, nullptr);
    ASSERT_NE(wdef, nullptr);

    RealBodyDriver d(11);
    const int room = d.AddRoom({0.0F, 0.0F}, 15, {0, 0, 0, 0}, 0);
    d.AddEnemy(room, *edef, glm::vec2{60.0F, 0.0F}, 1);
    d.EquipPlayer(*wdef, 5);
    d.SetPlayer(d.RoomCenter(room));

    ASSERT_EQ(d.LiveEnemyCount(room), 1);
    EXPECT_TRUE(d.ClearRoomCombat(room, /*maxFrames=*/400))
        << "driver could not aim+fire to kill the enemy";
    EXPECT_EQ(d.LiveEnemyCount(room), 0);
}

// --- Capability 4 / RealBodyClearsRoomAndDoorsReopen -------------------------
TEST(RealBodyDriverTest, RealBodyClearsRoomAndDoorsReopen) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::EnemyDef *edef = gd.FindEnemy("EnemyAI01");
    const Game::WeaponDef *wdef = gd.FindWeapon("Gun001");
    ASSERT_NE(edef, nullptr);
    ASSERT_NE(wdef, nullptr);

    RealBodyDriver d(13);
    const int start = d.AddRoom({0.0F, 0.0F}, 15, kEast, 0);
    const int room = d.AddRoom({kPitch, 0.0F}, 15, kWest, 1); // reward room (type 1)
    d.AddEnemy(room, *edef, d.RoomCenter(room), 99);
    d.EquipPlayer(*wdef, 5);
    d.SetPlayer(d.RoomCenter(start));

    ASSERT_TRUE(d.NavigateTo(d.RoomCenter(room), 24.0F, 800));
    ASSERT_FALSE(d.DoorOpen(room)) << "room must lock on entry";

    ASSERT_TRUE(d.ClearRoomCombat(room, 400)) << "could not clear the room";
    EXPECT_EQ(d.LiveEnemyCount(room), 0);
    EXPECT_TRUE(d.DoorOpen(room)) << "doors must reopen on clear";
    EXPECT_TRUE(d.RewardGranted(room)) << "reward gate (room_type==1) must fire";

    // Cleared -> the body can now walk back out to the start room.
    EXPECT_TRUE(d.NavigateTo(d.RoomCenter(start), 24.0F, 800))
        << "cleared room must reopen the exit";
}

// --- Capability 5 / RealBodyReachesBossRoom ----------------------------------
// A 3-room E-W chain: the boss room is TWO hops away -- single-hop SK_AUTOWALK
// (adjacent-room target only) cannot reach it; the driver multi-hops via waypoints.
TEST(RealBodyDriverTest, RealBodyReachesBossRoom) {
    RealBodyDriver d(17);
    const int start = d.AddRoom({0.0F, 0.0F}, 15, kEast, 0);
    const int mid = d.AddRoom({kPitch, 0.0F}, 15, kWestEast, 0); // empty passage
    const int boss = d.AddRoom({2.0F * kPitch, 0.0F}, 15, kWest, 0);
    (void)mid;
    d.AddBoss(boss, d.RoomCenter(boss), /*maxHp=*/500, /*seed=*/9000, "BossAI01");
    d.SetPlayer(d.RoomCenter(start));

    ASSERT_TRUE(d.NavigateWaypoints(
        {d.RoomCenter(mid), d.RoomCenter(boss)}, 24.0F, /*maxFramesPerLeg=*/900))
        << "driver failed to multi-hop to the far boss room";
    EXPECT_TRUE(d.BodyDeepInside(boss)) << "body not in the boss-room interior";
    EXPECT_TRUE(d.BossAlive());

    // Hold in the boss room so the boss wakes and fires (real sim combat).
    for (int f = 0; f < 200 && d.EnemyBulletCount() == 0; ++f) {
        d.Frame(d.RoomCenter(boss), /*moving=*/false, /*firing=*/false,
                glm::vec2{0.0F});
    }
    EXPECT_GT(d.EnemyBulletCount(), 0) << "boss should fire in the boss room";
    EXPECT_TRUE(d.BossAlive());
}

// NOLINTEND(readability-magic-numbers)
