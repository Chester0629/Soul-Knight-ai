#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/EnemyAI08.hpp"

using Game::EnemyAI08;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
float Len(const glm::vec2 &v) { return std::sqrt(v.x * v.x + v.y * v.y); }
} // namespace

// ---- Scout ----------------------------------------------------------------

TEST(EnemyAI08Test, ScoutRollInRange) {
    EnemyAI08 e;
    e.SetSeed(123);
    for (int i = 0; i < 128; ++i) {
        const int r = e.Scout();
        EXPECT_GE(r, 0);
        EXPECT_LT(r, EnemyAI08::kScoutRerollCeiling); // Range(0,10) max EXCL
    }
}

TEST(EnemyAI08Test, ScoutClearsTarget) {
    EnemyAI08 e;
    e.SetSeed(1);
    e.Scout();
    EXPECT_FALSE(e.HasTarget()); // target_obj = null (0x7C)
}

TEST(EnemyAI08Test, ScoutIsDeterministic) {
    EnemyAI08 a;
    EnemyAI08 b;
    a.SetSeed(7);
    b.SetSeed(7);
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(a.Scout(), b.Scout());
    }
}

TEST(EnemyAI08Test, ScoutGatedWhileDeadTakesNoDraw) {
    // Dead Scout must NOT advance the stream: a parallel live enemy that skips
    // the dead one's gated calls stays in lockstep.
    EnemyAI08 live;
    EnemyAI08 gated;
    live.SetSeed(99);
    gated.SetSeed(99);
    gated.SetDead(true);

    EXPECT_EQ(gated.Scout(), -1); // gated sentinel, no draw
    // live's first draw must equal gated's first *real* draw after un-gating.
    const int liveFirst = live.Scout();
    gated.SetDead(false);
    EXPECT_EQ(gated.Scout(), liveFirst);
}

TEST(EnemyAI08Test, ScoutGatedWhileDizzyTakesNoDraw) {
    // dizzy (0xA1) is the FIRST gate the decomp evaluates; it must skip the draw.
    EnemyAI08 live;
    EnemyAI08 gated;
    live.SetSeed(55);
    gated.SetSeed(55);
    gated.SetDizzy(true);

    EXPECT_EQ(gated.Scout(), -1); // gated, no draw
    const int liveFirst = live.Scout();
    gated.SetDizzy(false);
    EXPECT_EQ(gated.Scout(), liveFirst); // resumes in lockstep
}

// ---- RunReflection (wander) ----------------------------------------------

TEST(EnemyAI08Test, WanderIsNormalizedOrZero) {
    EnemyAI08 e;
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

TEST(EnemyAI08Test, WanderIsDeterministic) {
    EnemyAI08 a;
    EnemyAI08 b;
    a.SetSeed(31337);
    b.SetSeed(31337);
    for (int i = 0; i < 64; ++i) {
        const glm::vec2 da = a.RunReflection();
        const glm::vec2 db = b.RunReflection();
        EXPECT_FLOAT_EQ(da.x, db.x);
        EXPECT_FLOAT_EQ(da.y, db.y);
    }
}

TEST(EnemyAI08Test, WanderConsumesTwoDrawsInOrder) {
    // RunReflection draws Range(-1,1) twice; a reference stream drawing the same
    // two floats from the same seed must match component-for-component.
    EnemyAI08 e;
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

TEST(EnemyAI08Test, WanderIsUngated) {
    // RunReflection has no dead/dizzy gate in the decomp: it draws even when dead.
    EnemyAI08 gated;
    EnemyAI08 plain;
    gated.SetSeed(404);
    plain.SetSeed(404);
    gated.SetDead(true);
    gated.SetDizzy(true);
    const glm::vec2 dg = gated.RunReflection();
    const glm::vec2 dp = plain.RunReflection();
    EXPECT_FLOAT_EQ(dg.x, dp.x); // same two draws regardless of dead/dizzy
    EXPECT_FLOAT_EQ(dg.y, dp.y);
}

// ---- GetWeapon ------------------------------------------------------------

TEST(EnemyAI08Test, GetWeaponRollInRange) {
    EnemyAI08 e;
    e.SetSeed(321);
    const int total = 6;
    for (int i = 0; i < 128; ++i) {
        const int idx = e.GetWeapon(total);
        EXPECT_GE(idx, 0);
        EXPECT_LT(idx, total); // Range(0, total) max EXCL
    }
}

TEST(EnemyAI08Test, GetWeaponIsDeterministic) {
    EnemyAI08 a;
    EnemyAI08 b;
    a.SetSeed(17);
    b.SetSeed(17);
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(a.GetWeapon(8), b.GetWeapon(8));
    }
}

TEST(EnemyAI08Test, GetWeaponMatchesReferenceStream) {
    // GetWeapon is a single Range(0, total) draw -- a reference stream must match.
    EnemyAI08 e;
    Game::RGRandom ref;
    e.SetSeed(909);
    ref.SetRandomSeed(909);
    for (int i = 0; i < 32; ++i) {
        EXPECT_EQ(e.GetWeapon(5), ref.Range(0, 5));
    }
}

TEST(EnemyAI08Test, GetWeaponEmptyRangeReturnsZeroButStillDraws) {
    // total <= 0 is a degenerate Range (returns min == 0) but STILL consumes the
    // call, so the stream advances identically to a reference that drew Range(0,0).
    EnemyAI08 e;
    Game::RGRandom ref;
    e.SetSeed(11);
    ref.SetRandomSeed(11);
    EXPECT_EQ(e.GetWeapon(0), 0);
    EXPECT_EQ(ref.Range(0, 0), 0);
    // both advanced one int draw -> next draws stay in lockstep.
    EXPECT_EQ(e.GetWeapon(7), ref.Range(0, 7));
}

// ---- DisAppear ------------------------------------------------------------

TEST(EnemyAI08Test, DisAppearLatchesDeadAndClearsAwake) {
    EnemyAI08 e;
    e.SetAwake(true);
    EXPECT_FALSE(e.Dead());
    e.DisAppear();
    EXPECT_FALSE(e.Awake()); // 0x18 = 0
    EXPECT_TRUE(e.Dead());   // 0x38 = 1
}

TEST(EnemyAI08Test, DisAppearTakesNoRngDraw) {
    EnemyAI08 vanished;
    EnemyAI08 quiet;
    vanished.SetSeed(456);
    quiet.SetSeed(456);
    vanished.DisAppear();
    // DisAppear consumes no draw, but it latches dead -> Scout is now gated.
    // Compare the wander stream instead (RunReflection is ungated, no extra draw).
    const glm::vec2 dv = vanished.RunReflection();
    const glm::vec2 dq = quiet.RunReflection();
    EXPECT_FLOAT_EQ(dv.x, dq.x);
    EXPECT_FLOAT_EQ(dv.y, dq.y);
}

// ---- FixedUpdate physics --------------------------------------------------

using StepResult = EnemyAI08::StepResult;

TEST(EnemyAI08Test, NotAwakeIsCompleteNoOp) {
    // awake (0x18) gate at line 678150: a not-awake step does NOTHING -- no
    // friction decay and no velocity write, regardless of any other state.
    EnemyAI08 e;
    e.SetInertialVel(10.0F); // knockback would otherwise decay
    e.SetDead(true);         // dead-stop would otherwise apply
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Asleep);
    EXPECT_FLOAT_EQ(e.InertialVel(), 10.0F); // unchanged: no decay while asleep
    EXPECT_FALSE(e.Awake());                 // stays not-awake (was never set)
}

TEST(EnemyAI08Test, KnockbackActiveDecaysAndReportsKnockback) {
    // awake + inertial_vel > 1.0 -> knockback replaces steering, decays by
    // friction (line 678222). This is the ONLY decaying branch.
    EnemyAI08 e;
    e.SetAwake(true);
    e.SetInertialVel(10.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback); // 10 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 5.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback); // 5 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 2.5F);
}

TEST(EnemyAI08Test, KnockbackTakesPriorityOverDead) {
    // The decomp's outer split is inertial_vel <= 1.0 (steer/dead) vs > 1.0
    // (knockback). An active knockback runs the decay branch even while dead,
    // and does NOT zero awake.
    EnemyAI08 e;
    e.SetAwake(true);
    e.SetDead(true);
    e.SetInertialVel(8.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback);
    EXPECT_FLOAT_EQ(e.InertialVel(), 4.0F);
    EXPECT_TRUE(e.Awake()); // knockback branch never clears awake
}

TEST(EnemyAI08Test, DeadBelowThresholdStopsAndClearsAwake) {
    // inertial_vel <= 1.0 AND dead (0x38) -> awake (0x18) = 0, velocity zeroed,
    // dead branch tail-returns (line 678164).
    EnemyAI08 e;
    e.SetAwake(true);
    e.SetDead(true);
    e.SetInertialVel(1.0F); // 1.0 is NOT > 1.0 -> enters the <= branch
    EXPECT_EQ(e.FixedUpdateStep(0.9F), StepResult::DeadStop);
    EXPECT_FALSE(e.Awake());                  // 0x18 = 0
    EXPECT_FLOAT_EQ(e.InertialVel(), 1.0F);   // dead branch does not decay
}

TEST(EnemyAI08Test, AliveBelowThresholdSteersWithoutDecay) {
    EnemyAI08 e;
    e.SetAwake(true);
    e.SetInertialVel(1.0F); // 1.0 is NOT > 1.0 -> <= branch, not dead -> steer
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Steer);
    EXPECT_FLOAT_EQ(e.InertialVel(), 1.0F); // no decay below threshold
    EXPECT_TRUE(e.Awake());                 // steer leaves awake set
}

TEST(EnemyAI08Test, FixedUpdateTakesNoRngDraw) {
    // FixedUpdate has zero rg_random draws; the stream after stepping must equal
    // the stream of an enemy that never stepped.
    EnemyAI08 stepped;
    EnemyAI08 quiet;
    stepped.SetSeed(2024);
    quiet.SetSeed(2024);
    stepped.SetAwake(true);
    stepped.SetInertialVel(10.0F);
    stepped.FixedUpdateStep(0.5F); // knockback decay, no draw
    stepped.FixedUpdateStep(0.5F);
    EXPECT_EQ(stepped.Scout(), quiet.Scout()); // no extra draw consumed
}

// ---- Full interleaved replay determinism ----------------------------------

TEST(EnemyAI08Test, FullStreamReplayIsDeterministic) {
    // Interleave Scout + RunReflection + GetWeapon (the RNG-consuming methods)
    // with the zero-draw methods and replay from the same seed -> identical
    // sequence (lockstep guarantee).
    auto run = [](int seed) {
        EnemyAI08 e;
        e.SetSeed(seed);
        e.SetAwake(true);
        std::vector<float> trace;
        for (int i = 0; i < 24; ++i) {
            trace.push_back(static_cast<float>(e.Scout()));      // 1 int draw
            const glm::vec2 d = e.RunReflection();               // 2 float draws
            trace.push_back(d.x);
            trace.push_back(d.y);
            trace.push_back(static_cast<float>(e.GetWeapon(5)));  // 1 int draw
            e.FixedUpdateStep(0.9F);                             // 0 draws
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

TEST(EnemyAI08Test, DifferentSeedsDiverge) {
    EnemyAI08 a;
    EnemyAI08 b;
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
