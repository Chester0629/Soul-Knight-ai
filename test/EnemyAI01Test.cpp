#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/EnemyAI01.hpp"

using Game::EnemyAI01;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
float Len(const glm::vec2 &v) { return std::sqrt(v.x * v.x + v.y * v.y); }
} // namespace

// ---- Scout ----------------------------------------------------------------

TEST(EnemyAI01Test, ScoutRollInRange) {
    EnemyAI01 e;
    e.SetSeed(123);
    for (int i = 0; i < 128; ++i) {
        const int r = e.Scout();
        EXPECT_GE(r, 0);
        EXPECT_LT(r, EnemyAI01::kScoutRerollCeiling); // Range(0,10) max EXCL
    }
}

TEST(EnemyAI01Test, ScoutClearsTarget) {
    EnemyAI01 e;
    e.SetSeed(1);
    e.SetHasTarget(true);
    e.Scout();
    EXPECT_FALSE(e.HasTarget()); // target_obj = null (0x7C)
}

TEST(EnemyAI01Test, ScoutIsDeterministic) {
    EnemyAI01 a;
    EnemyAI01 b;
    a.SetSeed(7);
    b.SetSeed(7);
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(a.Scout(), b.Scout());
    }
}

TEST(EnemyAI01Test, ScoutGatedWhileDeadTakesNoDraw) {
    // Dead Scout must NOT advance the stream: a parallel live enemy that skips
    // the dead one's gated calls stays in lockstep.
    EnemyAI01 live;
    EnemyAI01 gated;
    live.SetSeed(99);
    gated.SetSeed(99);
    gated.SetDead(true);

    EXPECT_EQ(gated.Scout(), -1); // gated sentinel, no draw
    const int liveFirst = live.Scout();
    gated.SetDead(false);
    EXPECT_EQ(gated.Scout(), liveFirst); // resumes exactly where live was
}

TEST(EnemyAI01Test, ScoutGatedWhileDizzyTakesNoDraw) {
    EnemyAI01 e;
    e.SetSeed(5);
    e.SetDizzy(true);
    EXPECT_EQ(e.Scout(), -1); // gated, no draw
    e.SetDizzy(false);
    EXPECT_GE(e.Scout(), 0); // first real draw only after un-gating
}

// ---- RunReflection (wander) ----------------------------------------------

TEST(EnemyAI01Test, WanderIsNormalizedOrZero) {
    EnemyAI01 e;
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

TEST(EnemyAI01Test, WanderIsDeterministic) {
    EnemyAI01 a;
    EnemyAI01 b;
    a.SetSeed(31337);
    b.SetSeed(31337);
    for (int i = 0; i < 64; ++i) {
        const glm::vec2 da = a.RunReflection();
        const glm::vec2 db = b.RunReflection();
        EXPECT_FLOAT_EQ(da.x, db.x);
        EXPECT_FLOAT_EQ(da.y, db.y);
    }
}

TEST(EnemyAI01Test, WanderConsumesTwoDrawsInOrder) {
    // RunReflection draws Range(-1,1) twice; a reference stream drawing the same
    // two floats from the same seed must match component-for-component.
    EnemyAI01 e;
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

TEST(EnemyAI01Test, WanderTakesNoGate) {
    // RunReflection has no dead/dizzy gate: even dead it still draws twice.
    EnemyAI01 alive;
    EnemyAI01 corpse;
    alive.SetSeed(77);
    corpse.SetSeed(77);
    corpse.SetDead(true);
    const glm::vec2 a = alive.RunReflection();
    const glm::vec2 b = corpse.RunReflection();
    EXPECT_FLOAT_EQ(a.x, b.x); // both consumed two draws (no gate)
    EXPECT_FLOAT_EQ(a.y, b.y);
}

// ---- ShootReflection ------------------------------------------------------

TEST(EnemyAI01Test, ShootClearsCanShootAndReportsDelay) {
    EnemyAI01 e;
    e.SetSeed(1);
    EXPECT_TRUE(e.CanShoot()); // default armed
    float cd = -1.0F;
    const bool fired = e.ShootReflection(cd, 1.5F);
    EXPECT_TRUE(fired);
    EXPECT_FALSE(e.CanShoot()); // 0x40 = 0
    EXPECT_FLOAT_EQ(cd, 1.5F);  // shoot_cd (0x3C) -> ShootReflection re-fire
}

TEST(EnemyAI01Test, ShootGatedWhileDead) {
    EnemyAI01 e;
    e.SetSeed(1);
    e.SetDead(true);
    float cd = 0.0F;
    EXPECT_FALSE(e.ShootReflection(cd, 1.0F));
    EXPECT_TRUE(e.CanShoot()); // gate hit -> can_shoot untouched
}

TEST(EnemyAI01Test, ShootGatedWhileDizzy) {
    EnemyAI01 e;
    e.SetSeed(1);
    e.SetDizzy(true);
    float cd = 0.0F;
    EXPECT_FALSE(e.ShootReflection(cd, 1.0F));
    EXPECT_TRUE(e.CanShoot()); // gate hit -> can_shoot untouched
}

TEST(EnemyAI01Test, ShootTakesNoRngDraw) {
    // ShootReflection has no rg_random draws; the stream after a shot must equal
    // the stream of an enemy that never shot.
    EnemyAI01 shot;
    EnemyAI01 quiet;
    shot.SetSeed(456);
    quiet.SetSeed(456);
    float cd = 0.0F;
    shot.ShootReflection(cd, 1.0F);
    EXPECT_EQ(shot.Scout(), quiet.Scout()); // no extra draw consumed
}

// ---- FixedRotation --------------------------------------------------------

TEST(EnemyAI01Test, FixedRotationBranchesOnTarget) {
    EnemyAI01 e;
    e.SetSeed(1);
    EXPECT_EQ(e.FixedRotation(), EnemyAI01::RotationResult::FaceDefault); // null
    e.SetHasTarget(true);
    EXPECT_EQ(e.FixedRotation(), EnemyAI01::RotationResult::AimAtTarget);
}

TEST(EnemyAI01Test, FixedRotationTakesNoRngDraw) {
    EnemyAI01 rotated;
    EnemyAI01 quiet;
    rotated.SetSeed(2);
    quiet.SetSeed(2);
    rotated.SetHasTarget(true);
    rotated.FixedRotation();
    rotated.FixedRotation();
    EXPECT_EQ(rotated.Scout(), quiet.Scout()); // pure branch, no draw
}

// ---- OnGameStateChange (wake gate) ---------------------------------------

TEST(EnemyAI01Test, WakesOnRunningStateWhenRoomReady) {
    EnemyAI01 e;
    EXPECT_FALSE(e.Awake());
    EXPECT_TRUE(e.OnGameStateChange(EnemyAI01::kRunningGameState, true));
    EXPECT_TRUE(e.Awake()); // 0x18 = 1
}

TEST(EnemyAI01Test, IgnoresNonRunningState) {
    EnemyAI01 e;
    EXPECT_FALSE(e.OnGameStateChange(0, true)); // game_state != 1 -> early return
    EXPECT_FALSE(e.Awake());
    EXPECT_FALSE(e.OnGameStateChange(2, true));
    EXPECT_FALSE(e.Awake());
}

TEST(EnemyAI01Test, DoesNotWakeWhenRoomNotReady) {
    EnemyAI01 e;
    EXPECT_FALSE(e.OnGameStateChange(EnemyAI01::kRunningGameState, false));
    EXPECT_FALSE(e.Awake()); // room flag (0x10) != 1 -> no wake
}

TEST(EnemyAI01Test, WakeReturnsTrueOnlyOnTransition) {
    EnemyAI01 e;
    EXPECT_TRUE(e.OnGameStateChange(EnemyAI01::kRunningGameState, true));
    // already awake: still latches awake but reports no transition.
    EXPECT_FALSE(e.OnGameStateChange(EnemyAI01::kRunningGameState, true));
    EXPECT_TRUE(e.Awake());
}

// ---- FixedUpdate physics --------------------------------------------------

using StepResult = EnemyAI01::StepResult;

TEST(EnemyAI01Test, NotAwakeIsCompleteNoOp) {
    // awake (0x18) gate at line 675975: a not-awake step does NOTHING -- no
    // friction decay, no velocity write, awake stays false.
    EnemyAI01 e;
    e.SetInertialVel(10.0F); // knockback would otherwise decay
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Asleep);
    EXPECT_FLOAT_EQ(e.InertialVel(), 10.0F); // unchanged: no decay while asleep
    EXPECT_FALSE(e.Awake());
}

TEST(EnemyAI01Test, AwakeAliveSteersWithoutDecay) {
    EnemyAI01 e;
    e.SetSeed(1);
    e.SetAwake(true);
    e.SetInertialVel(1.0F); // 1.0 is NOT > 1.0 -> not-knockback path
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Steer);
    EXPECT_FLOAT_EQ(e.InertialVel(), 1.0F); // no decay below threshold
    EXPECT_TRUE(e.Awake());
}

TEST(EnemyAI01Test, AwakeDeadSetsAsleepAndNoDecay) {
    // not-knockback path + dead (0x38): awake is set to 0 (line 675978); NO decay.
    EnemyAI01 e;
    e.SetAwake(true);
    e.SetDead(true);
    e.SetInertialVel(0.5F); // <= 1.0 -> not-knockback path
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Dead);
    EXPECT_FALSE(e.Awake());                 // 0x18 = 0 (line 675978)
    EXPECT_FLOAT_EQ(e.InertialVel(), 0.5F);  // no decay on the dead path
}

TEST(EnemyAI01Test, KinematicSkipsKnockbackEvenAboveThreshold) {
    // kinematic (0x58) forces the not-knockback path regardless of inertial_vel.
    EnemyAI01 e;
    e.SetAwake(true);
    e.SetKinematic(true);
    e.SetInertialVel(10.0F); // would be knockback if not kinematic
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Steer);
    EXPECT_FLOAT_EQ(e.InertialVel(), 10.0F); // no decay (not the knockback path)
}

TEST(EnemyAI01Test, KnockbackActiveDecaysAndReportsKnockback) {
    // not kinematic + inertial_vel > 1.0 -> else branch composes force and decays
    // inertial_vel *= friction (line 676049).
    EnemyAI01 e;
    e.SetAwake(true);
    e.SetInertialVel(10.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback); // 10 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 5.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback); // 5 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 2.5F);
}

TEST(EnemyAI01Test, KnockbackThresholdIsStrictlyGreater) {
    EnemyAI01 e;
    e.SetAwake(true);
    e.SetInertialVel(EnemyAI01::kKnockbackThreshold); // exactly 1.0 -> NOT knockback
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Steer);
    EXPECT_FLOAT_EQ(e.InertialVel(), 1.0F); // no decay at the boundary
}

TEST(EnemyAI01Test, FixedUpdateTakesNoRngDraw) {
    EnemyAI01 stepped;
    EnemyAI01 quiet;
    stepped.SetSeed(321);
    quiet.SetSeed(321);
    stepped.SetAwake(true);
    stepped.SetInertialVel(10.0F);
    stepped.FixedUpdateStep(0.5F);
    stepped.FixedUpdateStep(0.5F);
    EXPECT_EQ(stepped.Scout(), quiet.Scout()); // FixedUpdate consumes no draw
}

// ---- Full interleaved replay determinism ----------------------------------

TEST(EnemyAI01Test, FullStreamReplayIsDeterministic) {
    // Interleave Scout + RunReflection (the only RNG-consuming methods) with the
    // zero-draw methods and replay from the same seed -> identical sequence.
    auto run = [](int seed) {
        EnemyAI01 e;
        e.SetSeed(seed);
        e.SetAwake(true);
        std::vector<float> trace;
        float cd = 0.0F;
        for (int i = 0; i < 24; ++i) {
            trace.push_back(static_cast<float>(e.Scout())); // 1 int draw
            const glm::vec2 d = e.RunReflection();          // 2 float draws
            trace.push_back(d.x);
            trace.push_back(d.y);
            e.ShootReflection(cd, 1.0F);                     // 0 draws
            e.FixedRotation();                               // 0 draws
            e.OnGameStateChange(EnemyAI01::kRunningGameState, true); // 0 draws
            e.FixedUpdateStep(0.9F);                         // 0 draws
            e.SetCanShoot(true); // re-arm so next ShootReflection latches again
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

TEST(EnemyAI01Test, DifferentSeedsDiverge) {
    EnemyAI01 a;
    EnemyAI01 b;
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
