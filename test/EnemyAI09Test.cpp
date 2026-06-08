#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/EnemyAI09.hpp"

using Game::EnemyAI09;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
float Len(const glm::vec2 &v) { return std::sqrt(v.x * v.x + v.y * v.y); }
} // namespace

// ---- Scout ----------------------------------------------------------------

TEST(EnemyAI09Test, ScoutClearsTargetWhenActive) {
    EnemyAI09 e;
    e.SetSeed(1);
    e.SetHasTarget(true);
    EXPECT_TRUE(e.Scout());
    EXPECT_FALSE(e.HasTarget()); // target_obj = null (0x7C)
}

TEST(EnemyAI09Test, ScoutGatedWhileDizzyKeepsTarget) {
    EnemyAI09 e;
    e.SetSeed(1);
    e.SetHasTarget(true);
    e.SetDizzy(true);
    EXPECT_FALSE(e.Scout());     // gated
    EXPECT_TRUE(e.HasTarget());  // untouched
}

TEST(EnemyAI09Test, ScoutGatedWhileDeadKeepsTarget) {
    EnemyAI09 e;
    e.SetSeed(1);
    e.SetHasTarget(true);
    e.SetDead(true);
    EXPECT_FALSE(e.Scout());     // gated
    EXPECT_TRUE(e.HasTarget());  // untouched
}

TEST(EnemyAI09Test, ScoutTakesNoRngDraw) {
    // Scout has no rg_random draws; the stream after Scout must equal the stream
    // of an enemy that never scouted (only RunReflection consumes RNG).
    EnemyAI09 scouted;
    EnemyAI09 quiet;
    scouted.SetSeed(456);
    quiet.SetSeed(456);
    scouted.Scout();
    const glm::vec2 a = scouted.RunReflection();
    const glm::vec2 b = quiet.RunReflection();
    EXPECT_FLOAT_EQ(a.x, b.x);
    EXPECT_FLOAT_EQ(a.y, b.y);
}

// ---- RunReflection (wander) ----------------------------------------------

TEST(EnemyAI09Test, WanderIsNormalizedOrZero) {
    EnemyAI09 e;
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

TEST(EnemyAI09Test, WanderIsDeterministic) {
    EnemyAI09 a;
    EnemyAI09 b;
    a.SetSeed(31337);
    b.SetSeed(31337);
    for (int i = 0; i < 64; ++i) {
        const glm::vec2 da = a.RunReflection();
        const glm::vec2 db = b.RunReflection();
        EXPECT_FLOAT_EQ(da.x, db.x);
        EXPECT_FLOAT_EQ(da.y, db.y);
    }
}

TEST(EnemyAI09Test, WanderConsumesTwoDrawsInOrder) {
    // RunReflection draws Range(-1,1) twice; a reference stream drawing the same
    // two floats from the same seed must match component-for-component.
    EnemyAI09 e;
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

TEST(EnemyAI09Test, WanderHasNoGate) {
    // RunReflection in the decomp is ungated -- even dead/dizzy enemies take the
    // two draws (the gate lives in Scout/ShootReflection, not here).
    EnemyAI09 live;
    EnemyAI09 dead;
    live.SetSeed(77);
    dead.SetSeed(77);
    dead.SetDead(true);
    dead.SetDizzy(true);
    const glm::vec2 a = live.RunReflection();
    const glm::vec2 b = dead.RunReflection();
    EXPECT_FLOAT_EQ(a.x, b.x); // dead still drew -> identical stream
    EXPECT_FLOAT_EQ(a.y, b.y);
}

// ---- ShootReflection -------------------------------------------------------

TEST(EnemyAI09Test, ShootLatchesCanShootAndDelay) {
    EnemyAI09 e;
    e.SetSeed(1);
    EXPECT_TRUE(e.CanShoot()); // starts true
    float cd = -1.0F;
    EXPECT_TRUE(e.ShootReflection(cd, 1.5F));
    EXPECT_FALSE(e.CanShoot()); // 0x40 = 0
    EXPECT_FLOAT_EQ(cd, 1.5F);  // shoot_cd -> Invoke delay
}

TEST(EnemyAI09Test, ShootGatedWhileDead) {
    EnemyAI09 e;
    e.SetSeed(1);
    e.SetDead(true);
    float cd = 0.0F;
    EXPECT_FALSE(e.ShootReflection(cd, 1.0F));
    EXPECT_TRUE(e.CanShoot()); // unchanged when gated
}

TEST(EnemyAI09Test, ShootGatedWhileDizzy) {
    EnemyAI09 e;
    e.SetSeed(1);
    e.SetDizzy(true);
    float cd = 0.0F;
    EXPECT_FALSE(e.ShootReflection(cd, 1.0F));
    EXPECT_TRUE(e.CanShoot()); // unchanged when gated
}

TEST(EnemyAI09Test, ShootTakesNoRngDraw) {
    // ShootReflection has no rg_random draws; the stream after a shot must equal
    // the stream of an enemy that never shot.
    EnemyAI09 shot;
    EnemyAI09 quiet;
    shot.SetSeed(999);
    quiet.SetSeed(999);
    float cd = 0.0F;
    shot.ShootReflection(cd, 1.0F);
    const glm::vec2 a = shot.RunReflection();
    const glm::vec2 b = quiet.RunReflection();
    EXPECT_FLOAT_EQ(a.x, b.x);
    EXPECT_FLOAT_EQ(a.y, b.y);
}

// ---- GetForce gate ---------------------------------------------------------

TEST(EnemyAI09Test, ForceAppliesOnlyWhenNotCanHit) {
    EnemyAI09 e;
    EXPECT_FALSE(e.CanHit());        // default false
    EXPECT_TRUE(e.ShouldApplyForce()); // can_hit == 0 -> base GetForce runs
    e.SetCanHit(true);
    EXPECT_FALSE(e.ShouldApplyForce()); // can_hit == 1 -> force ignored
}

// ---- FixedUpdate physics --------------------------------------------------

using StepResult = EnemyAI09::StepResult;

TEST(EnemyAI09Test, NotAwakeIsCompleteNoOp) {
    // awake (0x18) gate at line 678538: a not-awake step does NOTHING -- no
    // friction decay and no velocity write, regardless of any other state.
    EnemyAI09 e;
    e.SetAwake(false);
    e.SetInertialVel(10.0F); // knockback would otherwise decay
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Asleep);
    EXPECT_FLOAT_EQ(e.InertialVel(), 10.0F); // unchanged: no decay while asleep
}

TEST(EnemyAI09Test, DeadStopClearsAwakeNoDecay) {
    // awake && inertial_vel <= 1.0 && dead (line 678540): clears awake (0x18=0),
    // owner zeroes velocity, NO friction decay.
    EnemyAI09 e;
    EXPECT_TRUE(e.Awake());
    e.SetDead(true);
    e.SetInertialVel(0.5F); // <= 1.0
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::DeadStop);
    EXPECT_FALSE(e.Awake());               // awake cleared
    EXPECT_FLOAT_EQ(e.InertialVel(), 0.5F); // no decay
    // a second step is now a no-op (awake was cleared).
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Asleep);
}

TEST(EnemyAI09Test, SteerWhenAliveBelowThreshold) {
    EnemyAI09 e;
    e.SetInertialVel(1.0F); // 1.0 is NOT > 1.0 -> low branch, not dead -> steer
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Steer);
    EXPECT_FLOAT_EQ(e.InertialVel(), 1.0F); // no decay below threshold
    EXPECT_TRUE(e.Awake());                  // steer leaves awake set
}

TEST(EnemyAI09Test, KnockbackCompositesAndDecaysNoEarlyDeadStop) {
    // not dead + inertial_vel > 1.0 -> knockback composited, decays by friction
    // (line 678610). No early return semantics needed for the scalar model.
    EnemyAI09 e;
    e.SetInertialVel(10.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback); // 10 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 5.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback); // 5 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 2.5F);
}

TEST(EnemyAI09Test, DeadWithHighInertialStillKnockback) {
    // The dead branch lives INSIDE the inertial_vel <= 1.0 branch; while
    // inertial_vel > 1.0 a dead enemy still takes the knockback decay path
    // (the dead-stop only triggers once inertia has decayed below threshold).
    EnemyAI09 e;
    e.SetDead(true);
    e.SetInertialVel(4.0F); // > 1.0 -> knockback regardless of dead
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback);
    EXPECT_FLOAT_EQ(e.InertialVel(), 2.0F);
    EXPECT_TRUE(e.Awake()); // not cleared on the knockback path
    // now below threshold and dead -> dead stop clears awake.
    e.SetInertialVel(0.5F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::DeadStop);
    EXPECT_FALSE(e.Awake());
}

// ---- Full interleaved replay determinism ----------------------------------

TEST(EnemyAI09Test, FullStreamReplayIsDeterministic) {
    // Interleave RunReflection (the only RNG-consuming method) with the zero-draw
    // methods and replay from the same seed -> identical sequence (lockstep).
    auto run = [](int seed) {
        EnemyAI09 e;
        e.SetSeed(seed);
        std::vector<float> trace;
        float cd = 0.0F;
        for (int i = 0; i < 24; ++i) {
            e.Scout();                            // 0 draws
            const glm::vec2 d = e.RunReflection(); // 2 float draws
            trace.push_back(d.x);
            trace.push_back(d.y);
            e.ShootReflection(cd, 1.0F);          // 0 draws
            e.SetInertialVel(2.0F);
            e.FixedUpdateStep(0.9F);              // 0 draws
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

TEST(EnemyAI09Test, DifferentSeedsDiverge) {
    EnemyAI09 a;
    EnemyAI09 b;
    a.SetSeed(1);
    b.SetSeed(424242);
    bool diverged = false;
    for (int i = 0; i < 64; ++i) {
        const glm::vec2 da = a.RunReflection();
        const glm::vec2 db = b.RunReflection();
        if (da.x != db.x || da.y != db.y) {
            diverged = true;
        }
    }
    EXPECT_TRUE(diverged);
}

// NOLINTEND(readability-magic-numbers)
