#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/EnemyAI15.hpp"

using Game::EnemyAI15;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
float Len(const glm::vec2 &v) { return std::sqrt(v.x * v.x + v.y * v.y); }
} // namespace

// ---- Scout ----------------------------------------------------------------

TEST(EnemyAI15Test, ScoutRollInRange) {
    EnemyAI15 e;
    e.SetSeed(123);
    for (int i = 0; i < 128; ++i) {
        const int r = e.Scout();
        EXPECT_GE(r, 0);
        EXPECT_LT(r, EnemyAI15::kScoutRerollCeiling); // Range(0,10) max EXCL
    }
}

TEST(EnemyAI15Test, ScoutClearsTarget) {
    EnemyAI15 e;
    e.SetSeed(1);
    e.Scout();
    EXPECT_FALSE(e.HasTarget()); // target_obj = null (0x7C)
}

TEST(EnemyAI15Test, ScoutIsDeterministic) {
    EnemyAI15 a;
    EnemyAI15 b;
    a.SetSeed(7);
    b.SetSeed(7);
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(a.Scout(), b.Scout());
    }
}

TEST(EnemyAI15Test, ScoutMatchesReferenceStream) {
    // Scout's only draw is Range(0,10); a reference stream from the same seed
    // must produce the identical roll, confirming draw count == 1.
    EnemyAI15 e;
    Game::RGRandom ref;
    e.SetSeed(2024);
    ref.SetRandomSeed(2024);
    for (int i = 0; i < 32; ++i) {
        EXPECT_EQ(e.Scout(), ref.Range(0, 10));
    }
}

TEST(EnemyAI15Test, ScoutGatedWhileDeadTakesNoDraw) {
    // Dead Scout must NOT advance the stream: a parallel live enemy that skips
    // the dead one's gated calls stays in lockstep.
    EnemyAI15 live;
    EnemyAI15 gated;
    live.SetSeed(99);
    gated.SetSeed(99);
    gated.SetDead(true);

    EXPECT_EQ(gated.Scout(), -1); // gated sentinel, no draw
    const int liveFirst = live.Scout();
    gated.SetDead(false);
    EXPECT_EQ(gated.Scout(), liveFirst); // un-gated draw matches live's first.
}

TEST(EnemyAI15Test, ScoutGatedWhileDizzyTakesNoDraw) {
    EnemyAI15 e;
    e.SetSeed(5);
    e.SetDizzy(true);
    EXPECT_EQ(e.Scout(), -1); // gated, no draw
}

// ---- RunReflection (wander) ----------------------------------------------

TEST(EnemyAI15Test, WanderIsNormalizedOrZero) {
    EnemyAI15 e;
    e.SetSeed(2024);
    for (int i = 0; i < 128; ++i) {
        const glm::vec2 d = e.RunReflection();
        const float len = Len(d);
        const bool unit = std::fabs(len - 1.0F) < 1e-4F;
        const bool zero = len < 1e-6F;
        EXPECT_TRUE(unit || zero);        // normalized direction (or degenerate)
        EXPECT_EQ(d, e.MoveDirection());  // stored as move_direction (0x74)
    }
}

TEST(EnemyAI15Test, WanderIsDeterministic) {
    EnemyAI15 a;
    EnemyAI15 b;
    a.SetSeed(31337);
    b.SetSeed(31337);
    for (int i = 0; i < 64; ++i) {
        const glm::vec2 da = a.RunReflection();
        const glm::vec2 db = b.RunReflection();
        EXPECT_FLOAT_EQ(da.x, db.x);
        EXPECT_FLOAT_EQ(da.y, db.y);
    }
}

TEST(EnemyAI15Test, WanderConsumesTwoDrawsInOrder) {
    // RunReflection draws Range(-1,1) twice; a reference stream drawing the same
    // two floats from the same seed must match component-for-component.
    EnemyAI15 e;
    Game::RGRandom ref;
    e.SetSeed(808);
    ref.SetRandomSeed(808);

    const float rx = ref.Range(-1.0F, 1.0F);
    const float ry = ref.Range(-1.0F, 1.0F);
    const float len = std::sqrt(rx * rx + ry * ry);
    const glm::vec2 expected =
        len > 0.0F ? glm::vec2(rx / len, ry / len) : glm::vec2(0.0F, 0.0F);

    const glm::vec2 d = e.RunReflection();
    EXPECT_FLOAT_EQ(d.x, expected.x);
    EXPECT_FLOAT_EQ(d.y, expected.y);
}

TEST(EnemyAI15Test, WanderHasNoDeadDizzyGate) {
    // Unlike Scout/ShootReflection, RunReflection has NO gate in the decomp: it
    // draws even while dead or dizzy. Confirm it still consumes its two draws.
    EnemyAI15 gatedish;
    Game::RGRandom ref;
    gatedish.SetSeed(555);
    ref.SetRandomSeed(555);
    gatedish.SetDead(true);
    gatedish.SetDizzy(true);

    const float rx = ref.Range(-1.0F, 1.0F);
    const float ry = ref.Range(-1.0F, 1.0F);
    const float len = std::sqrt(rx * rx + ry * ry);
    const glm::vec2 expected =
        len > 0.0F ? glm::vec2(rx / len, ry / len) : glm::vec2(0.0F, 0.0F);

    const glm::vec2 d = gatedish.RunReflection(); // not gated -> draws anyway
    EXPECT_FLOAT_EQ(d.x, expected.x);
    EXPECT_FLOAT_EQ(d.y, expected.y);
}

// ---- ShootReflection ------------------------------------------------------

TEST(EnemyAI15Test, ShootRollInRange) {
    EnemyAI15 e;
    e.SetSeed(321);
    for (int i = 0; i < 128; ++i) {
        const int r = e.ShootReflection();
        EXPECT_GE(r, 0);
        EXPECT_LT(r, EnemyAI15::kShootRerollCeiling); // Range(0,100) max EXCL
    }
}

TEST(EnemyAI15Test, ShootMatchesReferenceStream) {
    // ShootReflection's only draw is Range(0,100); a reference stream from the
    // same seed must produce the identical roll, confirming draw count == 1.
    EnemyAI15 e;
    Game::RGRandom ref;
    e.SetSeed(4242);
    ref.SetRandomSeed(4242);
    for (int i = 0; i < 32; ++i) {
        EXPECT_EQ(e.ShootReflection(), ref.Range(0, 100));
    }
}

TEST(EnemyAI15Test, ShootIsDeterministic) {
    EnemyAI15 a;
    EnemyAI15 b;
    a.SetSeed(13);
    b.SetSeed(13);
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(a.ShootReflection(), b.ShootReflection());
    }
}

TEST(EnemyAI15Test, ShootGatedWhileDeadTakesNoDraw) {
    EnemyAI15 live;
    EnemyAI15 gated;
    live.SetSeed(77);
    gated.SetSeed(77);
    gated.SetDead(true);

    EXPECT_EQ(gated.ShootReflection(), -1); // gated, no draw
    const int liveFirst = live.ShootReflection();
    gated.SetDead(false);
    EXPECT_EQ(gated.ShootReflection(), liveFirst);
}

TEST(EnemyAI15Test, ShootGatedWhileDizzyTakesNoDraw) {
    EnemyAI15 e;
    e.SetSeed(9);
    e.SetDizzy(true);
    EXPECT_EQ(e.ShootReflection(), -1); // gated, no draw
}

// ---- FixedUpdate physics --------------------------------------------------

using StepResult = EnemyAI15::StepResult;

TEST(EnemyAI15Test, NotAwakeIsCompleteNoOp) {
    // awake (0x18) gate at line 680954: a not-awake step does NOTHING -- no
    // friction decay, no velocity write, no awake clear, regardless of state.
    EnemyAI15 e;
    e.SetAwake(false);
    e.SetInertialVel(10.0F); // knockback would otherwise decay
    e.SetDead(true);          // dead would otherwise latch awake = 0 (already 0)
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Asleep);
    EXPECT_FLOAT_EQ(e.InertialVel(), 10.0F); // unchanged: no decay while asleep
    EXPECT_FALSE(e.Awake());                  // still asleep (unchanged)
}

TEST(EnemyAI15Test, SteerWhenAwakeAliveAndLowInertia) {
    EnemyAI15 e;
    e.SetInertialVel(0.5F); // <= 1.0 and not dead -> steer
    EXPECT_EQ(e.FixedUpdateStep(0.9F), StepResult::Steer);
    EXPECT_FLOAT_EQ(e.InertialVel(), 0.5F); // no decay on the steer path
    EXPECT_TRUE(e.Awake());                  // steer leaves awake set
}

TEST(EnemyAI15Test, DeadLowInertiaLatchesAwakeOff) {
    // inertial_vel <= 1.0 AND dead (0x38): latch awake = 0 (line 680957), owner
    // zeroes velocity; no inertia decay; steering block not reached.
    EnemyAI15 e;
    e.SetDead(true);
    e.SetInertialVel(0.5F); // <= 1.0
    EXPECT_TRUE(e.Awake());
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Dead);
    EXPECT_FALSE(e.Awake());                  // awake cleared (0x18 = 0)
    EXPECT_FLOAT_EQ(e.InertialVel(), 0.5F);   // no decay on the dead path
}

TEST(EnemyAI15Test, DeadButKnockbackActiveTakesKnockbackBranch) {
    // The dead handling lives INSIDE the inertial_vel <= 1.0 path. While
    // inertial_vel > 1.0 the else branch (knockback) runs even when dead, and
    // awake is NOT cleared.
    EnemyAI15 e;
    e.SetDead(true);
    e.SetInertialVel(10.0F); // > 1.0 -> knockback branch
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback);
    EXPECT_FLOAT_EQ(e.InertialVel(), 5.0F);   // decayed by friction
    EXPECT_TRUE(e.Awake());                    // knockback branch leaves awake set
}

TEST(EnemyAI15Test, KnockbackActiveDecaysAndReportsKnockback) {
    // inertial_vel > 1.0 -> knockback velocity composed, inertial_vel *= friction
    // (line 681026).
    EnemyAI15 e;
    e.SetInertialVel(10.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback); // 10 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 5.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback); // 5 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 2.5F);
}

TEST(EnemyAI15Test, KnockbackAtThresholdSteersWithoutDecay) {
    EnemyAI15 e;
    e.SetInertialVel(1.0F); // 1.0 is NOT > 1.0 -> non-knockback path, steer
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Steer);
    EXPECT_FLOAT_EQ(e.InertialVel(), 1.0F); // no decay at/below threshold
}

TEST(EnemyAI15Test, FixedUpdateTakesNoRngDraw) {
    // FixedUpdate has no rg_random draws; the stream after a step must equal an
    // enemy that never stepped.
    EnemyAI15 stepped;
    EnemyAI15 quiet;
    stepped.SetSeed(456);
    quiet.SetSeed(456);
    stepped.SetInertialVel(10.0F);
    stepped.FixedUpdateStep(0.5F);   // knockback
    stepped.SetInertialVel(0.0F);
    stepped.FixedUpdateStep(0.9F);   // steer
    EXPECT_EQ(stepped.Scout(), quiet.Scout()); // no extra draw consumed
}

// ---- Full interleaved replay determinism ----------------------------------

TEST(EnemyAI15Test, FullStreamReplayIsDeterministic) {
    // Interleave Scout + RunReflection + ShootReflection (all RNG-consuming
    // methods) and replay from the same seed -> identical sequence (lockstep).
    auto run = [](int seed) {
        EnemyAI15 e;
        e.SetSeed(seed);
        std::vector<float> trace;
        for (int i = 0; i < 24; ++i) {
            trace.push_back(static_cast<float>(e.Scout()));          // 1 int draw
            const glm::vec2 d = e.RunReflection();                   // 2 float draws
            trace.push_back(d.x);
            trace.push_back(d.y);
            trace.push_back(static_cast<float>(e.ShootReflection())); // 1 int draw
            e.FixedUpdateStep(0.9F);                                  // 0 draws
        }
        return trace;
    };
    const auto a = run(2718);
    const auto b = run(2718);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_FLOAT_EQ(a[i], b[i]);
    }
}

TEST(EnemyAI15Test, DifferentSeedsDiverge) {
    EnemyAI15 a;
    EnemyAI15 b;
    a.SetSeed(1);
    b.SetSeed(424242);
    bool diverged = false;
    for (int i = 0; i < 64; ++i) {
        if (a.Scout() != b.Scout()) {
            diverged = true;
        }
    }
    EXPECT_TRUE(diverged);
}

// NOLINTEND(readability-magic-numbers)
