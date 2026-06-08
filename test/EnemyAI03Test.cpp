#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/EnemyAI03.hpp"

using Game::EnemyAI03;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
float Len(const glm::vec2 &v) { return std::sqrt(v.x * v.x + v.y * v.y); }
} // namespace

// ---- Scout ----------------------------------------------------------------

TEST(EnemyAI03Test, ScoutRollInRange) {
    EnemyAI03 e;
    e.SetSeed(123);
    for (int i = 0; i < 128; ++i) {
        const int r = e.Scout();
        EXPECT_GE(r, 0);
        EXPECT_LT(r, EnemyAI03::kScoutRerollCeiling); // Range(0,10) max EXCL
    }
}

TEST(EnemyAI03Test, ScoutClearsTarget) {
    EnemyAI03 e;
    e.SetSeed(1);
    e.Scout();
    EXPECT_FALSE(e.HasTarget()); // target_obj = null
}

TEST(EnemyAI03Test, ScoutIsDeterministic) {
    EnemyAI03 a;
    EnemyAI03 b;
    a.SetSeed(7);
    b.SetSeed(7);
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(a.Scout(), b.Scout());
    }
}

TEST(EnemyAI03Test, ScoutGatedWhileDeadTakesNoDraw) {
    // Dead Scout must NOT advance the stream: a parallel live enemy that skips
    // the dead one's gated calls stays in lockstep.
    EnemyAI03 live;
    EnemyAI03 gated;
    live.SetSeed(99);
    gated.SetSeed(99);
    gated.SetDead(true);

    EXPECT_EQ(gated.Scout(), -1); // gated sentinel, no draw
    // live's first draw must equal gated's first *real* draw after un-gating.
    const int liveFirst = live.Scout();
    gated.SetDead(false);
    EXPECT_EQ(gated.Scout(), liveFirst);
}

TEST(EnemyAI03Test, ScoutGatedWhileDizzyTakesNoDraw) {
    EnemyAI03 e;
    e.SetSeed(5);
    EXPECT_TRUE(e.ApplyDizzy()); // latch dizzy
    EXPECT_EQ(e.Scout(), -1);     // gated, no draw
}

// ---- RunReflection (wander) ----------------------------------------------

TEST(EnemyAI03Test, WanderIsNormalizedOrZero) {
    EnemyAI03 e;
    e.SetSeed(2024);
    for (int i = 0; i < 128; ++i) {
        const glm::vec2 d = e.RunReflection();
        const float len = Len(d);
        const bool unit = std::fabs(len - 1.0F) < 1e-4F;
        const bool zero = len < 1e-6F;
        EXPECT_TRUE(unit || zero); // normalized direction (or degenerate zero)
        EXPECT_EQ(d, e.MoveDirection()); // stored as move_direction
    }
}

TEST(EnemyAI03Test, WanderIsDeterministic) {
    EnemyAI03 a;
    EnemyAI03 b;
    a.SetSeed(31337);
    b.SetSeed(31337);
    for (int i = 0; i < 64; ++i) {
        const glm::vec2 da = a.RunReflection();
        const glm::vec2 db = b.RunReflection();
        EXPECT_FLOAT_EQ(da.x, db.x);
        EXPECT_FLOAT_EQ(da.y, db.y);
    }
}

TEST(EnemyAI03Test, WanderConsumesTwoDrawsInOrder) {
    // RunReflection draws Range(-1,1) twice; a reference stream drawing the same
    // two floats from the same seed must match component-for-component.
    EnemyAI03 e;
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

// ---- ShootReflection / StopShooting --------------------------------------

TEST(EnemyAI03Test, ShootLatchesStateAndDelays) {
    EnemyAI03 e;
    e.SetSeed(1);
    float st = -1.0F;
    float cd = -1.0F;
    const bool fired = e.ShootReflection(st, cd, 0.4F, 1.5F);
    EXPECT_TRUE(fired);
    EXPECT_TRUE(e.Shooting());        // 0x80 = 1
    EXPECT_FALSE(e.CanShoot());       // 0x40 = 0
    EXPECT_FLOAT_EQ(st, 0.4F);        // shoot_time -> StopShooting delay
    EXPECT_FLOAT_EQ(cd, 1.5F);        // shoot_cd -> ShootReflection delay
}

TEST(EnemyAI03Test, ShootClearsLockOnlyWhenNotLockInShooting) {
    float st = 0.0F;
    float cd = 0.0F;

    EnemyAI03 free;
    free.SetSeed(1);
    EXPECT_TRUE(free.ShootReflection(st, cd, 0.3F, 1.0F));
    EXPECT_FALSE(free.WeaponLockTarget()); // not locked -> cleared

    EnemyAI03 locked;
    locked.SetSeed(1);
    locked.SetLockInShooting(true);
    // weapon_lock_target starts false; with lock_in_shooting we must NOT clear,
    // so set it true first and confirm it survives the shot.
    locked.StopShooting();                  // sets weapon_lock_target = true
    EXPECT_TRUE(locked.WeaponLockTarget());
    EXPECT_TRUE(locked.ShootReflection(st, cd, 0.3F, 1.0F));
    EXPECT_TRUE(locked.WeaponLockTarget()); // lock_in_shooting -> kept
}

TEST(EnemyAI03Test, ShootGatedWhileDead) {
    EnemyAI03 e;
    e.SetSeed(1);
    e.SetDead(true);
    float st = 0.0F;
    float cd = 0.0F;
    EXPECT_FALSE(e.ShootReflection(st, cd, 0.3F, 1.0F));
    EXPECT_FALSE(e.Shooting());
}

TEST(EnemyAI03Test, ShootGatedWhileDizzy) {
    EnemyAI03 e;
    e.SetSeed(1);
    EXPECT_TRUE(e.ApplyDizzy());
    float st = 0.0F;
    float cd = 0.0F;
    EXPECT_FALSE(e.ShootReflection(st, cd, 0.3F, 1.0F));
    EXPECT_FALSE(e.Shooting());
}

TEST(EnemyAI03Test, StopShootingIsInverseLatch) {
    EnemyAI03 e;
    e.SetSeed(1);
    float st = 0.0F;
    float cd = 0.0F;
    e.ShootReflection(st, cd, 0.3F, 1.0F);
    EXPECT_TRUE(e.Shooting());
    e.StopShooting();
    EXPECT_FALSE(e.Shooting());          // 0x80 = 0
    EXPECT_TRUE(e.WeaponLockTarget());   // 0x1C = 1
}

TEST(EnemyAI03Test, ShootTakesNoRngDraw) {
    // ShootReflection has no rg_random draws; the stream after a shot must equal
    // the stream of an enemy that never shot.
    EnemyAI03 shot;
    EnemyAI03 quiet;
    shot.SetSeed(456);
    quiet.SetSeed(456);
    float st = 0.0F;
    float cd = 0.0F;
    shot.ShootReflection(st, cd, 0.3F, 1.0F);
    shot.StopShooting();
    EXPECT_EQ(shot.Scout(), quiet.Scout()); // no extra draw consumed
}

// ---- Dizzy ----------------------------------------------------------------

TEST(EnemyAI03Test, DizzyLatchesWhenAlive) {
    EnemyAI03 e;
    EXPECT_FALSE(e.Dizzy());
    EXPECT_TRUE(e.ApplyDizzy());
    EXPECT_TRUE(e.Dizzy());
}

TEST(EnemyAI03Test, DizzyGatedWhileDead) {
    EnemyAI03 e;
    e.SetDead(true);
    EXPECT_FALSE(e.ApplyDizzy());
    EXPECT_FALSE(e.Dizzy());
}

TEST(EnemyAI03Test, ClearDizzyRecovers) {
    EnemyAI03 e;
    e.ApplyDizzy();
    e.ClearDizzy();
    EXPECT_FALSE(e.Dizzy());
}

// ---- FixedUpdate physics --------------------------------------------------

using StepResult = EnemyAI03::StepResult;

TEST(EnemyAI03Test, NotAwakeIsCompleteNoOp) {
    // awake (0x18) gate at line 676785: a not-awake step does NOTHING -- no
    // friction decay and no velocity write, regardless of any other state.
    EnemyAI03 e;
    e.SetInertialVel(10.0F);    // knockback would otherwise decay
    e.SetStandInShooting(true); // and freeze would otherwise apply
    EXPECT_EQ(e.FixedUpdateStep(false, 0.5F), StepResult::Asleep);
    EXPECT_FLOAT_EQ(e.InertialVel(), 10.0F); // unchanged: no decay while asleep
}

TEST(EnemyAI03Test, MovementFrozenOnlyWhenStandAndShooting) {
    EnemyAI03 e;
    e.SetSeed(1);
    // not shooting -> steer
    EXPECT_EQ(e.FixedUpdateStep(true, 0.9F), StepResult::Steer);
    // stand_in_shooting alone -> still steer
    e.SetStandInShooting(true);
    EXPECT_EQ(e.FixedUpdateStep(true, 0.9F), StepResult::Steer);
    // stand_in_shooting && shooting -> frozen
    float st = 0.0F;
    float cd = 0.0F;
    e.ShootReflection(st, cd, 0.3F, 1.0F); // shooting = true
    EXPECT_EQ(e.FixedUpdateStep(true, 0.9F), StepResult::Frozen);
}

TEST(EnemyAI03Test, FrozenStepDoesNotDecayKnockback) {
    // Frozen path (stand_in_shooting && shooting) skips the entire not-frozen
    // block, so even an active knockback does NOT decay this step.
    EnemyAI03 e;
    e.SetSeed(1);
    e.SetInertialVel(10.0F);
    e.SetStandInShooting(true);
    float st = 0.0F;
    float cd = 0.0F;
    e.ShootReflection(st, cd, 0.3F, 1.0F); // shooting = true -> frozen
    EXPECT_EQ(e.FixedUpdateStep(true, 0.5F), StepResult::Frozen);
    EXPECT_FLOAT_EQ(e.InertialVel(), 10.0F); // no decay on the frozen path
}

TEST(EnemyAI03Test, KnockbackActiveDecaysAndReportsKnockback) {
    // not frozen + inertial_vel > 1.0 -> knockback replaces steering, decays by
    // friction (line 676825), and early-returns (the steer block does not run).
    EnemyAI03 e;
    e.SetInertialVel(10.0F);
    EXPECT_EQ(e.FixedUpdateStep(true, 0.5F), StepResult::Knockback); // 10 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 5.0F);
    EXPECT_EQ(e.FixedUpdateStep(true, 0.5F), StepResult::Knockback); // 5 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 2.5F);
}

TEST(EnemyAI03Test, KnockbackHeldBelowThresholdSteersWithoutDecay) {
    EnemyAI03 e;
    e.SetInertialVel(1.0F); // 1.0 is NOT > 1.0 -> branch skipped, normal steer
    EXPECT_EQ(e.FixedUpdateStep(true, 0.5F), StepResult::Steer);
    EXPECT_FLOAT_EQ(e.InertialVel(), 1.0F); // no decay below threshold
}

// ---- Full interleaved replay determinism ----------------------------------

TEST(EnemyAI03Test, FullStreamReplayIsDeterministic) {
    // Interleave Scout + RunReflection (the only RNG-consuming methods) and
    // replay from the same seed -> identical sequence (lockstep guarantee).
    auto run = [](int seed) {
        EnemyAI03 e;
        e.SetSeed(seed);
        std::vector<float> trace;
        float st = 0.0F;
        float cd = 0.0F;
        for (int i = 0; i < 24; ++i) {
            trace.push_back(static_cast<float>(e.Scout())); // 1 int draw
            const glm::vec2 d = e.RunReflection();          // 2 float draws
            trace.push_back(d.x);
            trace.push_back(d.y);
            e.ShootReflection(st, cd, 0.3F, 1.0F);          // 0 draws
            e.StopShooting();                                // 0 draws
            e.FixedUpdateStep(true, 0.9F);                   // 0 draws
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

TEST(EnemyAI03Test, DifferentSeedsDiverge) {
    EnemyAI03 a;
    EnemyAI03 b;
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
