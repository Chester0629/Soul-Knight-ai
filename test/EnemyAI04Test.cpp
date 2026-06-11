#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/EnemyAI04.hpp"

using Game::EnemyAI04;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
float Len(const glm::vec2 &v) { return std::sqrt(v.x * v.x + v.y * v.y); }
} // namespace

// ---- Scout ----------------------------------------------------------------

TEST(EnemyAI04Test, ScoutRollInRange) {
    EnemyAI04 e;
    e.SetSeed(123);
    for (int i = 0; i < 128; ++i) {
        const int r = e.Scout();
        EXPECT_GE(r, 0);
        EXPECT_LT(r, EnemyAI04::kScoutRerollCeiling); // Range(0,10) max EXCL
    }
}

TEST(EnemyAI04Test, ScoutClearsTarget) {
    EnemyAI04 e;
    e.SetSeed(1);
    e.Scout();
    EXPECT_FALSE(e.HasTarget()); // target_obj = null (0x7C)
}

TEST(EnemyAI04Test, ScoutIsDeterministic) {
    EnemyAI04 a;
    EnemyAI04 b;
    a.SetSeed(7);
    b.SetSeed(7);
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(a.Scout(), b.Scout());
    }
}

TEST(EnemyAI04Test, ScoutGatedWhileDeadTakesNoDraw) {
    // Dead Scout must NOT advance the stream: a parallel live enemy that skips
    // the dead one's gated calls stays in lockstep.
    EnemyAI04 live;
    EnemyAI04 gated;
    live.SetSeed(99);
    gated.SetSeed(99);
    gated.SetDead(true);

    EXPECT_EQ(gated.Scout(), -1); // gated sentinel, no draw
    const int liveFirst = live.Scout();
    gated.SetDead(false);
    EXPECT_EQ(gated.Scout(), liveFirst); // first real draw matches live's first
}

TEST(EnemyAI04Test, ScoutGatedWhileDizzyTakesNoDraw) {
    EnemyAI04 e;
    e.SetSeed(5);
    e.SetDizzy(true);
    EXPECT_EQ(e.Scout(), -1); // gated, no draw
    e.SetDizzy(false);
    EXPECT_GE(e.Scout(), 0);   // un-gated draw resumes
}

// ---- RunReflection (wander) ----------------------------------------------

TEST(EnemyAI04Test, WanderIsNormalizedOrZero) {
    EnemyAI04 e;
    e.SetSeed(2024);
    for (int i = 0; i < 128; ++i) {
        const glm::vec2 d = e.RunReflection();
        const float len = Len(d);
        const bool unit = std::fabs(len - 1.0F) < 1e-4F;
        const bool zero = len < 1e-6F;
        EXPECT_TRUE(unit || zero); // normalized direction (or degenerate zero)
        EXPECT_EQ(d, e.MoveDirection()); // stored as move_direction (0x74)
    }
}

TEST(EnemyAI04Test, WanderIsDeterministic) {
    EnemyAI04 a;
    EnemyAI04 b;
    a.SetSeed(31337);
    b.SetSeed(31337);
    for (int i = 0; i < 64; ++i) {
        const glm::vec2 da = a.RunReflection();
        const glm::vec2 db = b.RunReflection();
        EXPECT_FLOAT_EQ(da.x, db.x);
        EXPECT_FLOAT_EQ(da.y, db.y);
    }
}

TEST(EnemyAI04Test, WanderConsumesTwoDrawsInOrder) {
    // RunReflection draws Range(-1,1) twice; a reference stream drawing the same
    // two floats from the same seed must match component-for-component.
    EnemyAI04 e;
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

// ---- FixedUpdate physics --------------------------------------------------

using StepResult = EnemyAI04::StepResult;

TEST(EnemyAI04Test, NotAwakeIsCompleteNoOp) {
    // awake (0x18) gate at line 677154: a not-awake step does NOTHING -- no
    // friction decay, no awake clear, regardless of any other state.
    EnemyAI04 e;
    e.SetInertialVel(10.0F); // knockback would otherwise decay
    e.SetDead(true);          // dead-stop would otherwise clear awake
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Asleep);
    EXPECT_FLOAT_EQ(e.InertialVel(), 10.0F); // unchanged: no decay while asleep
    EXPECT_FALSE(e.Awake());                 // was already false; not toggled
}

TEST(EnemyAI04Test, DeadStopClearsAwakeAndIsTerminal) {
    // awake && inertial_vel <= 1.0 && dead -> DeadStop: sets awake = 0 (the only
    // state write), no friction decay.
    EnemyAI04 e;
    e.SetAwake(true);
    e.SetDead(true);
    e.SetInertialVel(0.5F); // <= 1.0 -> dead branch reachable
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::DeadStop);
    EXPECT_FALSE(e.Awake());                 // awake cleared (line 677157)
    EXPECT_FLOAT_EQ(e.InertialVel(), 0.5F);  // no decay on the dead branch
}

TEST(EnemyAI04Test, AwakeAliveLowInertialSteers) {
    EnemyAI04 e;
    e.SetAwake(true);
    e.SetInertialVel(1.0F); // 1.0 is NOT > 1.0 -> steer branch (not knockback)
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Steer);
    EXPECT_FLOAT_EQ(e.InertialVel(), 1.0F); // no decay below threshold
    EXPECT_TRUE(e.Awake());                 // steer does not touch awake
}

TEST(EnemyAI04Test, DeadCheckOnlyInsideLowInertialBranch) {
    // A dead enemy with inertial_vel > 1.0 takes the ELSE (knockback) branch, NOT
    // DeadStop -- the decomp's dead check is nested inside the <= 1.0 branch only.
    EnemyAI04 e;
    e.SetAwake(true);
    e.SetDead(true);
    e.SetInertialVel(10.0F); // > 1.0 -> knockback branch, dead check skipped
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback);
    EXPECT_FLOAT_EQ(e.InertialVel(), 5.0F); // decayed by friction
    EXPECT_TRUE(e.Awake());                 // knockback branch never clears awake
}

TEST(EnemyAI04Test, KnockbackActiveDecaysAndReportsKnockback) {
    // awake && inertial_vel > 1.0 -> knockback: inertial_vel *= friction
    // (line 677226). The only decaying path.
    EnemyAI04 e;
    e.SetAwake(true);
    e.SetInertialVel(10.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback); // 10 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 5.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback); // 5 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 2.5F);
}

TEST(EnemyAI04Test, FixedUpdateTakesNoRngDraw) {
    // FixedUpdate has no rg_random draws; the stream after stepping must equal the
    // stream of an enemy that never stepped.
    EnemyAI04 stepped;
    EnemyAI04 quiet;
    stepped.SetSeed(456);
    quiet.SetSeed(456);
    stepped.SetAwake(true);
    stepped.SetInertialVel(10.0F);
    stepped.FixedUpdateStep(0.5F); // knockback path (decays inertial_vel only)
    stepped.SetInertialVel(0.0F);
    stepped.FixedUpdateStep(0.5F); // steer path
    EXPECT_EQ(stepped.Scout(), quiet.Scout()); // no extra draw consumed
}

// ---- Full interleaved replay determinism ----------------------------------

TEST(EnemyAI04Test, FullStreamReplayIsDeterministic) {
    // Interleave Scout + RunReflection (the only RNG-consuming methods) plus the
    // zero-draw FixedUpdate, and replay from the same seed -> identical sequence.
    auto run = [](int seed) {
        EnemyAI04 e;
        e.SetSeed(seed);
        e.SetAwake(true);
        std::vector<float> trace;
        for (int i = 0; i < 24; ++i) {
            trace.push_back(static_cast<float>(e.Scout())); // 1 int draw
            const glm::vec2 d = e.RunReflection();          // 2 float draws
            trace.push_back(d.x);
            trace.push_back(d.y);
            e.SetInertialVel(5.0F);
            e.FixedUpdateStep(0.9F); // 0 draws (knockback)
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

TEST(EnemyAI04Test, DifferentSeedsDiverge) {
    EnemyAI04 a;
    EnemyAI04 b;
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
