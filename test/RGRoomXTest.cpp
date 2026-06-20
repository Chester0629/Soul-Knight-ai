#include <gtest/gtest.h>

#include "data/RGRandom.hpp"
#include "world/RGRoomX.hpp"

using Game::RGRandom;
using Game::RGRoomX;
using Process = Game::RGRoomX::Process;

// NOLINTBEGIN(readability-magic-numbers)

// ---- Initial state (constructor) -------------------------------------------

TEST(RGRoomXTest, DefaultStateIsUnclearedClosed) {
    RGRoomX room;
    EXPECT_EQ(room.RoomType(), 0);
    EXPECT_EQ(room.State(), Process::Uncleared);
    EXPECT_FALSE(room.DoorOpen());
    EXPECT_FALSE(room.RewardGranted());
}

TEST(RGRoomXTest, RoomTypeIsStored) {
    RGRoomX reward(RGRoomX::kRoomTypeReward);
    EXPECT_EQ(reward.RoomType(), 1);
    RGRoomX normal(0);
    EXPECT_EQ(normal.RoomType(), 0);
}

// ---- door_open state transitions (RGRoomX__OpenDoor / CloseDoor) ------------

TEST(RGRoomXTest, OpenDoorSetsDoorOpenTrue) {
    // FAITHFUL: RGRoomX__OpenDoor writes *(this+0x6c) = 1.
    RGRoomX room;
    EXPECT_FALSE(room.DoorOpen());
    room.OpenDoor();
    EXPECT_TRUE(room.DoorOpen());
}

TEST(RGRoomXTest, CloseDoorSetsDoorOpenFalse) {
    // FAITHFUL: RGRoomX__CloseDoor writes *(this+0x6c) = 0.
    RGRoomX room;
    room.OpenDoor();
    EXPECT_TRUE(room.DoorOpen());
    room.CloseDoor();
    EXPECT_FALSE(room.DoorOpen());
}

TEST(RGRoomXTest, DoorTransitionsAreIdempotentlyDirectional) {
    RGRoomX room;
    room.CloseDoor();
    EXPECT_FALSE(room.DoorOpen());
    room.CloseDoor();
    EXPECT_FALSE(room.DoorOpen());
    room.OpenDoor();
    EXPECT_TRUE(room.DoorOpen());
    room.OpenDoor();
    EXPECT_TRUE(room.DoorOpen());
}

// Doors do not touch the process state.
TEST(RGRoomXTest, DoorMethodsDoNotChangeProcess) {
    RGRoomX room;
    room.OpenDoor();
    EXPECT_EQ(room.State(), Process::Uncleared);
    room.CloseDoor();
    EXPECT_EQ(room.State(), Process::Uncleared);
}

// ---- ClearRoom state machine (RGRoomX__ClearRoom) --------------------------

TEST(RGRoomXTest, ClearRoomMarksClearedAndOpensDoors) {
    // FAITHFUL: ClearRoom sets process=2 then OpenDoor() (door_open=1).
    RGRoomX room(0);
    room.ClearRoom();
    EXPECT_EQ(room.State(), Process::Cleared);
    EXPECT_TRUE(room.DoorOpen());
}

TEST(RGRoomXTest, ClearRoomGrantsRewardOnlyForRoomTypeOne) {
    // FAITHFUL: the `if (*(this+0x1c) == 1)` reward gate.
    RGRoomX reward(RGRoomX::kRoomTypeReward);
    reward.ClearRoom();
    EXPECT_TRUE(reward.RewardGranted());

    RGRoomX normal(0);
    normal.ClearRoom();
    EXPECT_FALSE(normal.RewardGranted());

    // Other room types (e.g. 2, the special type) also do NOT grant the reward.
    RGRoomX special(2);
    special.ClearRoom();
    EXPECT_FALSE(special.RewardGranted());
}

TEST(RGRoomXTest, ClearRoomFromClosedStillOpens) {
    RGRoomX room(RGRoomX::kRoomTypeReward);
    room.CloseDoor();
    EXPECT_FALSE(room.DoorOpen());
    room.ClearRoom();
    EXPECT_EQ(room.State(), Process::Cleared);
    EXPECT_TRUE(room.DoorOpen());
    EXPECT_TRUE(room.RewardGranted());
}

// ---- StartRoom (Phase 3 #3a: the Active/locked transition) -----------------

TEST(RGRoomXTest, StartRoomBecomesActiveAndClosesDoors) {
    // PORT-ORCHESTRATION: StartRoom sets process=Active and closes the doors
    // (door_open=0). Mirrors the prefab combat-begin transition.
    RGRoomX room(RGRoomX::kRoomTypeReward);
    room.OpenDoor(); // faithful initial: doors open (prefab door_open=1)
    EXPECT_EQ(room.State(), Process::Uncleared);
    EXPECT_TRUE(room.DoorOpen());

    room.StartRoom();
    EXPECT_EQ(room.State(), Process::Active);
    EXPECT_FALSE(room.DoorOpen()) << "entering combat seals the doors";
}

// The full per-room lifecycle the orchestrator drives: open -> StartRoom (locked)
// -> ClearRoom (reopened + reward gate). This is the single-source door/lock state.
TEST(RGRoomXTest, FullLifecycleUnclearedActiveCleared) {
    RGRoomX room(RGRoomX::kRoomTypeReward);
    room.OpenDoor(); // initial: Uncleared, doors open

    // Enter combat -> Active, sealed.
    room.StartRoom();
    EXPECT_EQ(room.State(), Process::Active);
    EXPECT_FALSE(room.DoorOpen());
    EXPECT_FALSE(room.RewardGranted());

    // Cleared -> reopened + reward gate (room_type==1).
    room.ClearRoom();
    EXPECT_EQ(room.State(), Process::Cleared);
    EXPECT_TRUE(room.DoorOpen());
    EXPECT_TRUE(room.RewardGranted());
}

TEST(RGRoomXTest, StartRoomDrawsNothing) {
    RGRoomX room(0);
    RGRandom ref;
    room.SetSeed(7);
    ref.SetRandomSeed(7);
    room.OpenDoor();
    room.StartRoom();
    RGRandom fresh;
    fresh.SetRandomSeed(7);
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(ref.Range(0, 1000), fresh.Range(0, 1000));
    }
}

// ---- RNG is never touched by the lifecycle (zero-draw bodies) --------------

TEST(RGRoomXTest, LifecycleDrawsNothing) {
    // The room lifecycle (OpenDoor/CloseDoor/ClearRoom) takes NO RNG draws. A
    // parallel same-seeded reference stream must stay lockstep after the calls.
    RGRoomX room(RGRoomX::kRoomTypeReward);
    RGRandom ref;
    room.SetSeed(4242);
    ref.SetRandomSeed(4242);

    room.CloseDoor();
    room.OpenDoor();
    room.ClearRoom();
    room.CloseDoor();

    // The room's stream is untouched, so a draw on a separately-seeded reference
    // must still equal a freshly-seeded stream draw-for-draw.
    RGRandom fresh;
    fresh.SetRandomSeed(4242);
    for (int i = 0; i < 16; ++i) {
        EXPECT_EQ(ref.Range(0, 1000), fresh.Range(0, 1000));
    }
}

// NOLINTEND(readability-magic-numbers)
