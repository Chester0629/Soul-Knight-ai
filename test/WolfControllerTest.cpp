#include <gtest/gtest.h>

#include <vector>

#include "combat/WolfController.hpp"

using Game::WolfController;
using AtkPrefab = Game::WolfController::AtkPrefab;

// NOLINTBEGIN(readability-magic-numbers)

// ---- RunReflection (wander draw) -----------------------------------------

TEST(WolfControllerTest, WanderRollInRange) {
    WolfController w;
    w.SetSeed(123);
    for (int i = 0; i < 128; ++i) {
        const int r = w.RunReflection();
        EXPECT_GE(r, 0);
        EXPECT_LT(r, WolfController::kWanderRerollCeiling); // Range(0,10) max EXCL
        EXPECT_EQ(r, w.LastWanderRoll()); // stored as last wander roll
    }
}

TEST(WolfControllerTest, WanderIsDeterministic) {
    WolfController a;
    WolfController b;
    a.SetSeed(7);
    b.SetSeed(7);
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(a.RunReflection(), b.RunReflection());
    }
}

TEST(WolfControllerTest, WanderConsumesExactlyOneDrawInOrder) {
    // RunReflection draws Range(0,10) once; a reference stream from the same seed
    // must match draw-for-draw.
    WolfController w;
    Game::RGRandom ref;
    w.SetSeed(808);
    ref.SetRandomSeed(808);
    for (int i = 0; i < 32; ++i) {
        EXPECT_EQ(w.RunReflection(), ref.Range(0, 10));
    }
}

TEST(WolfControllerTest, LastWanderRollSentinelBeforeAnyDraw) {
    WolfController w;
    w.SetSeed(1);
    EXPECT_EQ(w.LastWanderRoll(), -1); // no draw taken yet
    w.RunReflection();
    EXPECT_GE(w.LastWanderRoll(), 0);
}

// ---- Scout (target = master, dispatches RunReflection) -------------------

TEST(WolfControllerTest, ScoutTargetsMaster) {
    WolfController w;
    w.SetSeed(1);
    EXPECT_FALSE(w.TargetIsMaster());
    w.Scout();
    EXPECT_TRUE(w.TargetIsMaster()); // target_obj = master_tf
}

TEST(WolfControllerTest, ScoutDrawsThroughRunReflection) {
    // Scout takes no draw of its own; it returns RunReflection's single draw.
    // A Scout stream must equal a RunReflection stream from the same seed.
    WolfController scoutWolf;
    WolfController wanderWolf;
    scoutWolf.SetSeed(99);
    wanderWolf.SetSeed(99);
    for (int i = 0; i < 64; ++i) {
        const int s = scoutWolf.Scout();
        const int r = wanderWolf.RunReflection();
        EXPECT_EQ(s, r);
        EXPECT_EQ(s, scoutWolf.LastWanderRoll());
    }
}

TEST(WolfControllerTest, ScoutRollInRange) {
    WolfController w;
    w.SetSeed(31337);
    for (int i = 0; i < 128; ++i) {
        const int r = w.Scout();
        EXPECT_GE(r, 0);
        EXPECT_LT(r, WolfController::kWanderRerollCeiling);
    }
}

// ---- EndCycle (zero move_direction; target_obj untouched) ----------------

TEST(WolfControllerTest, EndCycleZeroesMoveDirAndLeavesTargetLatch) {
    // FAITHFUL: WolfController__EndCycle @ game_full.c:626917 writes ONLY
    // move_direction = Vector2.zero (0x50/0x54, lines 626934-626935); it does
    // NOT touch target_obj (0x1C). So after Scout() points the target at the
    // master, EndCycle must leave the targets-master latch UNCHANGED.
    WolfController w;
    w.SetSeed(1);
    EXPECT_FALSE(w.MoveDirZeroed());
    w.Scout();
    EXPECT_TRUE(w.TargetIsMaster());
    w.EndCycle();                       // move_direction = Vector2.zero only
    EXPECT_TRUE(w.MoveDirZeroed());     // the one real write
    EXPECT_TRUE(w.TargetIsMaster());    // target_obj (0x1C) is untouched
}

TEST(WolfControllerTest, EndCycleTakesNoRngDraw) {
    // EndCycle has no rg_random draw; the stream after EndCycle must equal an
    // untouched stream.
    WolfController cycled;
    WolfController quiet;
    cycled.SetSeed(456);
    quiet.SetSeed(456);
    cycled.EndCycle();
    cycled.EndCycle();
    EXPECT_EQ(cycled.RunReflection(), quiet.RunReflection());
}

// ---- OnAtk (prefab selection) --------------------------------------------

TEST(WolfControllerTest, OnAtkSelectsBullet1WhenNotStrengthened) {
    WolfController w;
    w.SetSeed(1);
    EXPECT_FALSE(w.Strengthen());
    EXPECT_EQ(w.OnAtk(), AtkPrefab::Bullet1); // strengthen == false -> bullet1
}

TEST(WolfControllerTest, OnAtkSelectsBullet2WhenStrengthened) {
    WolfController w;
    w.SetSeed(1);
    w.SetStrengthen(true);
    EXPECT_TRUE(w.Strengthen());
    EXPECT_EQ(w.OnAtk(), AtkPrefab::Bullet2); // strengthen == true -> bullet2
}

TEST(WolfControllerTest, OnAtkTakesNoRngDrawAndDoesNotMutateState) {
    WolfController used;
    WolfController quiet;
    used.SetSeed(2024);
    quiet.SetSeed(2024);
    used.OnAtk();
    used.SetStrengthen(true);
    used.OnAtk();
    // selection is a pure read: no draw consumed, and OnAtk writes no state.
    // Scout was never called, so the targets-master latch is still false.
    EXPECT_FALSE(used.TargetIsMaster());
    EXPECT_EQ(used.RunReflection(), quiet.RunReflection());
}

// ---- Full interleaved replay determinism ----------------------------------

TEST(WolfControllerTest, FullStreamReplayIsDeterministic) {
    // Interleave Scout (1 draw via RunReflection), RunReflection (1 draw),
    // EndCycle (0 draws) and OnAtk (0 draws); replay from the same seed ->
    // identical sequence (lockstep guarantee).
    auto run = [](int seed) {
        WolfController w;
        w.SetSeed(seed);
        std::vector<int> trace;
        for (int i = 0; i < 24; ++i) {
            trace.push_back(w.Scout());         // 1 draw
            trace.push_back(w.RunReflection()); // 1 draw
            w.EndCycle();                        // 0 draws
            w.SetStrengthen((i & 1) != 0);
            trace.push_back(w.OnAtk() == AtkPrefab::Bullet2 ? 1 : 0); // 0 draws
        }
        return trace;
    };
    const auto a = run(2718);
    const auto b = run(2718);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i], b[i]);
    }
}

TEST(WolfControllerTest, DifferentSeedsDiverge) {
    WolfController a;
    WolfController b;
    a.SetSeed(1);
    b.SetSeed(424242);
    bool diverged = false;
    for (int i = 0; i < 64; ++i) {
        if (a.RunReflection() != b.RunReflection()) {
            diverged = true;
        }
    }
    EXPECT_TRUE(diverged);
}

// NOLINTEND(readability-magic-numbers)
