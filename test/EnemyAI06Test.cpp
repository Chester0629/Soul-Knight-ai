#include <gtest/gtest.h>

#include <vector>

#include "combat/EnemyAI06.hpp"

using Game::EnemyAI06;

// NOLINTBEGIN(readability-magic-numbers)

// ---- Scout ----------------------------------------------------------------

TEST(EnemyAI06Test, ScoutRunsWhenAliveAndClearsTarget) {
    EnemyAI06 e;
    e.SetSeed(1);
    EXPECT_TRUE(e.Scout());      // not dead, not dizzy -> body runs
    EXPECT_FALSE(e.HasTarget()); // target_obj = null (0x7C)
}

TEST(EnemyAI06Test, ScoutGatedWhileDead) {
    EnemyAI06 e;
    e.SetSeed(1);
    e.SetDead(true);
    EXPECT_FALSE(e.Scout()); // dead -> gated
}

TEST(EnemyAI06Test, ScoutGatedWhileDizzy) {
    EnemyAI06 e;
    e.SetSeed(1);
    e.SetDizzy(true);
    EXPECT_FALSE(e.Scout()); // dizzy -> gated
}

TEST(EnemyAI06Test, ScoutTakesNoRngDraw) {
    // Scout's recoverable head has NO rg_random draw: an enemy that scouts must
    // stay in lockstep with one that only shoot-reflects.
    EnemyAI06 scouted;
    EnemyAI06 quiet;
    scouted.SetSeed(456);
    quiet.SetSeed(456);
    scouted.Scout();
    scouted.Scout();
    int rollScouted = -1;
    int rollQuiet = -1;
    EXPECT_TRUE(scouted.ShootReflection(rollScouted));
    EXPECT_TRUE(quiet.ShootReflection(rollQuiet));
    EXPECT_EQ(rollScouted, rollQuiet); // Scout consumed no draws
}

// ---- ShootReflection ------------------------------------------------------

TEST(EnemyAI06Test, ShootReflectionRollInRange) {
    EnemyAI06 e;
    e.SetSeed(123);
    for (int i = 0; i < 128; ++i) {
        int roll = -1;
        EXPECT_TRUE(e.ShootReflection(roll));
        EXPECT_GE(roll, 0);
        EXPECT_LT(roll, EnemyAI06::kShootRerollCeiling); // Range(0,10) max EXCL
    }
}

TEST(EnemyAI06Test, ShootReflectionIsDeterministic) {
    EnemyAI06 a;
    EnemyAI06 b;
    a.SetSeed(7);
    b.SetSeed(7);
    for (int i = 0; i < 64; ++i) {
        int ra = -1;
        int rb = -1;
        EXPECT_TRUE(a.ShootReflection(ra));
        EXPECT_TRUE(b.ShootReflection(rb));
        EXPECT_EQ(ra, rb);
    }
}

TEST(EnemyAI06Test, ShootReflectionGatedWhileDeadTakesNoDraw) {
    // Dead ShootReflection must NOT advance the stream: a parallel live enemy
    // that skips the dead one's gated call stays in lockstep.
    EnemyAI06 live;
    EnemyAI06 gated;
    live.SetSeed(99);
    gated.SetSeed(99);
    gated.SetDead(true);

    int roll = -1;
    EXPECT_FALSE(gated.ShootReflection(roll)); // gated, no draw

    int liveFirst = -1;
    EXPECT_TRUE(live.ShootReflection(liveFirst));
    gated.SetDead(false);
    int gatedFirst = -1;
    EXPECT_TRUE(gated.ShootReflection(gatedFirst));
    EXPECT_EQ(gatedFirst, liveFirst); // first real draw matches after un-gating
}

TEST(EnemyAI06Test, ShootReflectionGatedWhileDizzyTakesNoDraw) {
    EnemyAI06 e;
    e.SetSeed(5);
    e.SetDizzy(true);
    int roll = -1;
    EXPECT_FALSE(e.ShootReflection(roll)); // dizzy -> gated, no draw
}

TEST(EnemyAI06Test, ShootReflectionNoRngFieldTakesNoDraw) {
    // The draw is inside the rg_random != null (0x0C) guard: when rg_random is
    // null the decomp returns without drawing. An enemy with no rg_random must
    // not advance the stream.
    EnemyAI06 noRng;
    EnemyAI06 withRng;
    noRng.SetSeed(321);
    withRng.SetSeed(321);
    noRng.SetHasRng(false);

    int roll = -1;
    EXPECT_FALSE(noRng.ShootReflection(roll)); // no rg_random -> no draw

    // Re-enable: its first real draw equals the with-rng enemy's first draw.
    noRng.SetHasRng(true);
    int a = -1;
    int b = -1;
    EXPECT_TRUE(noRng.ShootReflection(a));
    EXPECT_TRUE(withRng.ShootReflection(b));
    EXPECT_EQ(a, b);
}

// ---- ChildDead ------------------------------------------------------------

TEST(EnemyAI06Test, ChildDeadClearsAwake) {
    EnemyAI06 e;
    e.SetAwake(true);
    e.ChildDead();
    EXPECT_FALSE(e.Awake()); // awake = 0 (0x18)
}

TEST(EnemyAI06Test, ChildDeadAfterAwakeStopsThinking) {
    // After ChildDead, awake is false (the owner's FixedUpdate gate would no-op).
    EnemyAI06 e;
    e.SetAwake(true);
    EXPECT_TRUE(e.Awake());
    e.ChildDead();
    EXPECT_FALSE(e.Awake());
}

// ---- EndCycle -------------------------------------------------------------

TEST(EnemyAI06Test, EndCycleZeroesMoveDirectionAndFiresVirtualWhenAlive) {
    EnemyAI06 e;
    e.SetMoveDirection(glm::vec2(0.7F, -0.7F));
    EXPECT_TRUE(e.EndCycle());                       // not dead -> virtual gate true
    EXPECT_EQ(e.MoveDirection(), glm::vec2(0.0F, 0.0F)); // move_direction = zero
}

TEST(EnemyAI06Test, EndCycleZeroesMoveDirectionButSkipsVirtualWhenDead) {
    EnemyAI06 e;
    e.SetMoveDirection(glm::vec2(1.0F, 0.0F));
    e.SetDead(true);
    EXPECT_FALSE(e.EndCycle());                      // dead -> virtual gate false
    EXPECT_EQ(e.MoveDirection(), glm::vec2(0.0F, 0.0F)); // still zeroed
}

TEST(EnemyAI06Test, EndCycleTakesNoRngDraw) {
    EnemyAI06 cycled;
    EnemyAI06 quiet;
    cycled.SetSeed(2024);
    quiet.SetSeed(2024);
    cycled.EndCycle();
    cycled.EndCycle();
    int a = -1;
    int b = -1;
    EXPECT_TRUE(cycled.ShootReflection(a));
    EXPECT_TRUE(quiet.ShootReflection(b));
    EXPECT_EQ(a, b); // EndCycle consumed no draws
}

// ---- Full interleaved replay determinism ----------------------------------

TEST(EnemyAI06Test, FullStreamReplayIsDeterministic) {
    // Interleave every method; only ShootReflection consumes RNG. Replaying from
    // the same seed must produce the identical roll sequence (lockstep).
    auto run = [](int seed) {
        EnemyAI06 e;
        e.SetSeed(seed);
        std::vector<int> trace;
        for (int i = 0; i < 32; ++i) {
            e.Scout();          // 0 draws
            int roll = -1;
            e.ShootReflection(roll); // 1 int draw
            trace.push_back(roll);
            e.EndCycle();       // 0 draws
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

TEST(EnemyAI06Test, DifferentSeedsDiverge) {
    EnemyAI06 a;
    EnemyAI06 b;
    a.SetSeed(1);
    b.SetSeed(424242);
    bool diverged = false;
    for (int i = 0; i < 64; ++i) {
        int ra = -1;
        int rb = -1;
        a.ShootReflection(ra);
        b.ShootReflection(rb);
        if (ra != rb) {
            diverged = true;
        }
    }
    EXPECT_TRUE(diverged);
}

// NOLINTEND(readability-magic-numbers)
