#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/EnemyAIShark.hpp"

using Game::EnemyAIShark;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
float Len(const glm::vec2 &v) { return std::sqrt(v.x * v.x + v.y * v.y); }
} // namespace

// ---- Scout ----------------------------------------------------------------

TEST(EnemyAISharkTest, ScoutRollInRange) {
    EnemyAIShark e;
    e.SetSeed(123);
    for (int i = 0; i < 128; ++i) {
        const int r = e.Scout();
        EXPECT_GE(r, 0);
        EXPECT_LT(r, EnemyAIShark::kScoutRerollCeiling); // Range(0,10) max EXCL
    }
}

TEST(EnemyAISharkTest, ScoutClearsTarget) {
    EnemyAIShark e;
    e.SetSeed(1);
    e.Scout();
    EXPECT_FALSE(e.HasTarget()); // target_obj = null (0x7C)
}

TEST(EnemyAISharkTest, ScoutIsDeterministic) {
    EnemyAIShark a;
    EnemyAIShark b;
    a.SetSeed(7);
    b.SetSeed(7);
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(a.Scout(), b.Scout());
    }
}

TEST(EnemyAISharkTest, ScoutGatedWhileDeadTakesNoDraw) {
    // Dead Scout must NOT advance the stream: a live enemy that skips the dead
    // one's gated call stays in lockstep.
    EnemyAIShark live;
    EnemyAIShark gated;
    live.SetSeed(99);
    gated.SetSeed(99);
    gated.SetDead(true);

    EXPECT_EQ(gated.Scout(), -1); // gated sentinel, no draw
    const int liveFirst = live.Scout();
    gated.SetDead(false);
    EXPECT_EQ(gated.Scout(), liveFirst); // first real draw matches live's first
}

TEST(EnemyAISharkTest, ScoutGatedWhileDizzyTakesNoDraw) {
    EnemyAIShark e;
    e.SetSeed(5);
    e.SetDizzy(true);
    EXPECT_EQ(e.Scout(), -1); // gated, no draw
}

TEST(EnemyAISharkTest, ScoutMatchesReferenceStreamRange0To10) {
    // The Scout draw is rg_random.Range(0, 10); a parallel reference stream from
    // the same seed must produce the identical sequence.
    EnemyAIShark e;
    Game::RGRandom ref;
    e.SetSeed(2020);
    ref.SetRandomSeed(2020);
    for (int i = 0; i < 32; ++i) {
        EXPECT_EQ(e.Scout(), ref.Range(0, 10));
    }
}

// ---- RunReflection (dash direction) --------------------------------------

TEST(EnemyAISharkTest, DirIsNormalizedOrZero) {
    EnemyAIShark e;
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

TEST(EnemyAISharkTest, DirIsDeterministic) {
    EnemyAIShark a;
    EnemyAIShark b;
    a.SetSeed(31337);
    b.SetSeed(31337);
    for (int i = 0; i < 64; ++i) {
        const glm::vec2 da = a.RunReflection();
        const glm::vec2 db = b.RunReflection();
        EXPECT_FLOAT_EQ(da.x, db.x);
        EXPECT_FLOAT_EQ(da.y, db.y);
    }
}

TEST(EnemyAISharkTest, RunReflectionConsumesTwoDrawsInOrder) {
    // RunReflection draws Range(-1,1) twice; a reference stream drawing the same
    // two floats from the same seed must match component-for-component.
    EnemyAIShark e;
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

TEST(EnemyAISharkTest, RunReflectionIsUngatedByDeadAndDizzy) {
    // RunReflection has NO dead/dizzy gate in the decomp: it still draws twice.
    EnemyAIShark gated;
    EnemyAIShark plain;
    gated.SetSeed(77);
    plain.SetSeed(77);
    gated.SetDead(true);
    gated.SetDizzy(true);
    const glm::vec2 dg = gated.RunReflection();
    const glm::vec2 dp = plain.RunReflection();
    EXPECT_FLOAT_EQ(dg.x, dp.x); // same draws despite dead+dizzy
    EXPECT_FLOAT_EQ(dg.y, dp.y);
}

// ---- ShootReflection ------------------------------------------------------

TEST(EnemyAISharkTest, ShootRollInRange) {
    EnemyAIShark e;
    e.SetSeed(11);
    for (int i = 0; i < 128; ++i) {
        const int r = e.ShootReflection();
        EXPECT_GE(r, 0);
        EXPECT_LT(r, EnemyAIShark::kShootRollCeiling); // Range(0,100) max EXCL
    }
}

TEST(EnemyAISharkTest, ShootGatedWhileDeadTakesNoDraw) {
    EnemyAIShark e;
    e.SetSeed(3);
    e.SetDead(true);
    EXPECT_EQ(e.ShootReflection(), -1); // gated, no draw
}

TEST(EnemyAISharkTest, ShootGatedWhileDizzyTakesNoDraw) {
    EnemyAIShark e;
    e.SetSeed(3);
    e.SetDizzy(true);
    EXPECT_EQ(e.ShootReflection(), -1); // gated, no draw
}

TEST(EnemyAISharkTest, ShootMatchesReferenceStreamRange0To100) {
    EnemyAIShark e;
    Game::RGRandom ref;
    e.SetSeed(555);
    ref.SetRandomSeed(555);
    for (int i = 0; i < 32; ++i) {
        EXPECT_EQ(e.ShootReflection(), ref.Range(0, 100));
    }
}

// ---- Dash -----------------------------------------------------------------

TEST(EnemyAISharkTest, DashClearsCanShootAndReturnsDelay) {
    EnemyAIShark e;
    e.SetSeed(1);
    EXPECT_TRUE(e.CanShoot()); // default
    float delay = -1.0F;
    e.Dash(delay, 0.8F);
    EXPECT_FALSE(e.CanShoot());  // 0x40 = 0
    EXPECT_FLOAT_EQ(delay, 0.8F); // shoot_cd Invoke delay
}

TEST(EnemyAISharkTest, DashTakesNoRngDraw) {
    // Dash has no rg_random draws; the stream after a Dash must equal an enemy
    // that never dashed.
    EnemyAIShark dashed;
    EnemyAIShark quiet;
    dashed.SetSeed(456);
    quiet.SetSeed(456);
    float delay = 0.0F;
    dashed.Dash(delay, 1.0F);
    EXPECT_EQ(dashed.Scout(), quiet.Scout()); // no extra draw consumed
}

// ---- Dashing coroutine (wave cadence) ------------------------------------

TEST(EnemyAISharkTest, DashingFirstStepInitsTimerAndInterval) {
    EnemyAIShark e;
    EXPECT_TRUE(e.DashingStep()); // first tick emits a Wave
    EXPECT_FLOAT_EQ(e.WaveTimer(), 0.0F);                       // timer = 0
    EXPECT_FLOAT_EQ(e.WaveInterval(), EnemyAIShark::kWaveInterval); // 0.2
}

TEST(EnemyAISharkTest, DashingAdvancesTimerByInterval) {
    EnemyAIShark e;
    e.DashingStep(); // init: timer = 0
    e.DashingStep(); // advance: timer = 0.2
    EXPECT_FLOAT_EQ(e.WaveTimer(), 0.2F);
    e.DashingStep(); // advance: timer = 0.4
    EXPECT_FLOAT_EQ(e.WaveTimer(), 0.4F);
}

TEST(EnemyAISharkTest, DashingEmitsWavesUntilTimerLimit) {
    // timer starts at 0, increments by 0.2, emits while timer < 1.5.
    // Ticks: 0.0,0.2,0.4,0.6,0.8,1.0,1.2,1.4 -> 8 waves; 1.6 -> stop.
    EnemyAIShark e;
    int waves = 0;
    for (int i = 0; i < 32; ++i) {
        if (e.DashingStep()) {
            ++waves;
        } else {
            break;
        }
    }
    EXPECT_EQ(waves, 8); // 0.0 .. 1.4 inclusive (8 ticks below 1.5)
}

TEST(EnemyAISharkTest, DashingStopsWhenDead) {
    // The MoveNext existence check (op_Implicit self) stops the coroutine; we
    // model it as not-dead. A dead shark emits no Wave even on the first tick.
    EnemyAIShark e;
    e.SetDead(true);
    EXPECT_FALSE(e.DashingStep()); // no Wave: object considered gone
}

TEST(EnemyAISharkTest, DashingTakesNoRngDraw) {
    EnemyAIShark coro;
    EnemyAIShark quiet;
    coro.SetSeed(909);
    quiet.SetSeed(909);
    for (int i = 0; i < 8; ++i) {
        coro.DashingStep();
    }
    EXPECT_EQ(coro.Scout(), quiet.Scout()); // coroutine consumed no draws
}

// ---- GetForce -------------------------------------------------------------

TEST(EnemyAISharkTest, GetForceAppliesWhenNotDashing) {
    EnemyAIShark e;
    const float applied = e.GetForce(12.0F);
    EXPECT_FLOAT_EQ(applied, 12.0F);
    EXPECT_FLOAT_EQ(e.InertialVel(), 12.0F);
}

TEST(EnemyAISharkTest, GetForceClampsToCap) {
    EnemyAIShark e;
    const float applied = e.GetForce(40.0F); // > 28 cap
    EXPECT_FLOAT_EQ(applied, EnemyAIShark::kForceCap); // 28
    EXPECT_FLOAT_EQ(e.InertialVel(), EnemyAIShark::kForceCap);
}

TEST(EnemyAISharkTest, GetForceIgnoredWhileDashing) {
    EnemyAIShark e;
    e.SetInertialVel(3.0F);
    e.SetDashing(true);
    const float applied = e.GetForce(20.0F);
    EXPECT_FLOAT_EQ(applied, 3.0F);          // impulse ignored
    EXPECT_FLOAT_EQ(e.InertialVel(), 3.0F);  // unchanged
}

// ---- FixedUpdate physics --------------------------------------------------

using StepResult = EnemyAIShark::StepResult;

TEST(EnemyAISharkTest, NotAwakeIsCompleteNoOp) {
    // awake (0x18) gate at line 681368: a not-awake step does NOTHING -- no
    // decay, no awake clear, regardless of any other state.
    EnemyAIShark e;
    e.SetAwake(false);
    e.SetInertialVel(10.0F); // knockback would otherwise decay
    e.SetDead(true);          // dead-clear would otherwise fire
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Asleep);
    EXPECT_FLOAT_EQ(e.InertialVel(), 10.0F); // unchanged: no decay while asleep
    EXPECT_FALSE(e.Awake());                  // still asleep (never re-cleared)
}

TEST(EnemyAISharkTest, DeadAndLowVelClearsAwake) {
    // inertial_vel <= 1.0 && dead -> clears awake (0x18 = 0); no decay.
    EnemyAIShark e;
    e.SetAwake(true);
    e.SetDead(true);
    e.SetInertialVel(0.5F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::DeadClear);
    EXPECT_FALSE(e.Awake());                 // awake cleared (line 681371)
    EXPECT_FLOAT_EQ(e.InertialVel(), 0.5F);  // no decay on this path
}

TEST(EnemyAISharkTest, AliveAndLowVelSteersWithoutDecay) {
    EnemyAIShark e;
    e.SetAwake(true);
    e.SetInertialVel(1.0F); // 1.0 is NOT > 1.0 -> steer path
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Steer);
    EXPECT_TRUE(e.Awake());                  // not cleared (alive)
    EXPECT_FLOAT_EQ(e.InertialVel(), 1.0F);  // no decay below threshold
}

TEST(EnemyAISharkTest, KnockbackActiveDecaysAndReportsKnockback) {
    // inertial_vel > 1.0 -> knockback added, inertial_vel *= friction (681440),
    // and the dead/awake-clear branches do not run.
    EnemyAIShark e;
    e.SetAwake(true);
    e.SetInertialVel(10.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback); // 10 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 5.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback); // 5 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 2.5F);
    EXPECT_TRUE(e.Awake()); // knockback path never clears awake
}

TEST(EnemyAISharkTest, KnockbackPathIgnoresDead) {
    // The dead branch is ONLY on the inertial_vel <= 1.0 path; a dead shark with
    // a strong impulse still takes the Knockback branch (and keeps awake).
    EnemyAIShark e;
    e.SetAwake(true);
    e.SetDead(true);
    e.SetInertialVel(10.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback);
    EXPECT_TRUE(e.Awake()); // awake NOT cleared on the > 1.0 path
}

TEST(EnemyAISharkTest, FixedUpdateTakesNoRngDraw) {
    EnemyAIShark stepped;
    EnemyAIShark quiet;
    stepped.SetSeed(321);
    quiet.SetSeed(321);
    stepped.SetInertialVel(10.0F);
    stepped.FixedUpdateStep(0.5F);
    stepped.FixedUpdateStep(0.5F);
    EXPECT_EQ(stepped.Scout(), quiet.Scout()); // FixedUpdate consumed no draws
}

// ---- Full interleaved replay determinism ----------------------------------

TEST(EnemyAISharkTest, FullStreamReplayIsDeterministic) {
    // Interleave Scout + RunReflection + ShootReflection (the RNG-consuming
    // methods) with the zero-draw methods and replay from the same seed ->
    // identical sequence (the lockstep guarantee).
    auto run = [](int seed) {
        EnemyAIShark e;
        e.SetSeed(seed);
        std::vector<float> trace;
        float delay = 0.0F;
        for (int i = 0; i < 24; ++i) {
            trace.push_back(static_cast<float>(e.Scout()));            // 1 int draw
            const glm::vec2 d = e.RunReflection();                     // 2 float draws
            trace.push_back(d.x);
            trace.push_back(d.y);
            trace.push_back(static_cast<float>(e.ShootReflection()));  // 1 int draw
            e.Dash(delay, 1.0F);                                       // 0 draws
            e.DashingStep();                                           // 0 draws
            e.FixedUpdateStep(0.9F);                                   // 0 draws
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

TEST(EnemyAISharkTest, DifferentSeedsDiverge) {
    EnemyAIShark a;
    EnemyAIShark b;
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
