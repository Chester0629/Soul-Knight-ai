#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/EnemyAI12.hpp"

using Game::EnemyAI12;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
float Len(const glm::vec2 &v) { return std::sqrt(v.x * v.x + v.y * v.y); }
} // namespace

// ---- Scout ----------------------------------------------------------------

TEST(EnemyAI12Test, ScoutActiveClearsTarget) {
    EnemyAI12 e;
    e.SetSeed(1);
    EXPECT_TRUE(e.Scout());      // not gated
    EXPECT_FALSE(e.HasTarget()); // target_obj = null (0x7C)
}

TEST(EnemyAI12Test, ScoutGatedWhileDead) {
    EnemyAI12 e;
    e.SetSeed(1);
    e.SetDead(true);
    EXPECT_FALSE(e.Scout()); // gated by dead
}

TEST(EnemyAI12Test, ScoutGatedWhileDizzy) {
    EnemyAI12 e;
    e.SetSeed(1);
    e.SetDizzy(true);
    EXPECT_FALSE(e.Scout()); // gated by dizzy
}

TEST(EnemyAI12Test, ScoutTakesNoRngDraw) {
    // Scout has NO rg_random draws: the stream after Scouting must equal the
    // stream of an enemy that never Scouted. Drive RunReflection (which draws)
    // and compare component-for-component.
    EnemyAI12 scouted;
    EnemyAI12 quiet;
    scouted.SetSeed(777);
    quiet.SetSeed(777);
    scouted.Scout();
    scouted.Scout();
    const glm::vec2 a = scouted.RunReflection();
    const glm::vec2 b = quiet.RunReflection();
    EXPECT_FLOAT_EQ(a.x, b.x);
    EXPECT_FLOAT_EQ(a.y, b.y);
}

// ---- RunReflection (wander) ----------------------------------------------

TEST(EnemyAI12Test, WanderIsNormalizedOrZero) {
    EnemyAI12 e;
    e.SetSeed(2024);
    for (int i = 0; i < 128; ++i) {
        const glm::vec2 d = e.RunReflection();
        const float len = Len(d);
        const bool unit = std::fabs(len - 1.0F) < 1e-4F;
        const bool zero = len < 1e-6F;
        EXPECT_TRUE(unit || zero);        // normalized (or degenerate zero)
        EXPECT_EQ(d, e.MoveDirection());  // stored as move_direction (0x74)
    }
}

TEST(EnemyAI12Test, WanderIsDeterministic) {
    EnemyAI12 a;
    EnemyAI12 b;
    a.SetSeed(31337);
    b.SetSeed(31337);
    for (int i = 0; i < 64; ++i) {
        const glm::vec2 da = a.RunReflection();
        const glm::vec2 db = b.RunReflection();
        EXPECT_FLOAT_EQ(da.x, db.x);
        EXPECT_FLOAT_EQ(da.y, db.y);
    }
}

TEST(EnemyAI12Test, WanderConsumesTwoDrawsInOrder) {
    // RunReflection draws Range(-1,1) twice (lines 680067, 680072); a reference
    // stream drawing the same two floats from the same seed must match.
    EnemyAI12 e;
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

// ---- ShootReflection ------------------------------------------------------

TEST(EnemyAI12Test, ShootLatchesStateAndDelay) {
    EnemyAI12 e;
    e.SetSeed(1);
    float delay = -1.0F;
    const bool fired = e.ShootReflection(delay, 1.25F);
    EXPECT_TRUE(fired);
    EXPECT_FALSE(e.CanShoot());          // 0x40 = 0
    EXPECT_FALSE(e.WeaponLockTarget());  // 0x1C = 0
    EXPECT_FLOAT_EQ(delay, 1.25F);       // shoot_cd -> CreateFireBall delay
}

TEST(EnemyAI12Test, ShootZeroesMoveDirection) {
    EnemyAI12 e;
    e.SetSeed(1);
    e.RunReflection(); // set a non-zero move_direction first (usually)
    float delay = 0.0F;
    EXPECT_TRUE(e.ShootReflection(delay, 1.0F));
    EXPECT_FLOAT_EQ(e.MoveDirection().x, 0.0F); // set_move_direction(zero)
    EXPECT_FLOAT_EQ(e.MoveDirection().y, 0.0F);
}

TEST(EnemyAI12Test, ShootGatedWhileDead) {
    EnemyAI12 e;
    e.SetSeed(1);
    e.SetDead(true);
    float delay = 0.0F;
    EXPECT_FALSE(e.ShootReflection(delay, 1.0F));
    EXPECT_TRUE(e.CanShoot()); // unchanged: gated
}

TEST(EnemyAI12Test, ShootGatedWhileDizzy) {
    EnemyAI12 e;
    e.SetSeed(1);
    e.SetDizzy(true);
    float delay = 0.0F;
    EXPECT_FALSE(e.ShootReflection(delay, 1.0F));
    EXPECT_TRUE(e.CanShoot()); // unchanged: gated
}

TEST(EnemyAI12Test, ShootTakesNoRngDraw) {
    // ShootReflection has no rg_random draws; the stream after a shot must equal
    // the stream of an enemy that never shot.
    EnemyAI12 shot;
    EnemyAI12 quiet;
    shot.SetSeed(456);
    quiet.SetSeed(456);
    float delay = 0.0F;
    shot.ShootReflection(delay, 1.0F);
    const glm::vec2 a = shot.RunReflection();
    const glm::vec2 b = quiet.RunReflection();
    EXPECT_FLOAT_EQ(a.x, b.x);
    EXPECT_FLOAT_EQ(a.y, b.y);
}

// ---- FixedUpdate physics --------------------------------------------------

using StepResult = EnemyAI12::StepResult;

TEST(EnemyAI12Test, NotAwakeIsCompleteNoOp) {
    // awake (0x18) gate at line 679942: a not-awake step does NOTHING -- no
    // friction decay and no velocity write, regardless of any other state.
    EnemyAI12 e;
    e.SetAwake(false);
    e.SetInertialVel(10.0F);    // knockback would otherwise decay
    e.SetDead(true);            // dead branch would otherwise fire
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Asleep);
    EXPECT_FLOAT_EQ(e.InertialVel(), 10.0F); // unchanged: no decay while asleep
}

TEST(EnemyAI12Test, AwakeAliveLowInertiaSteers) {
    EnemyAI12 e;
    e.SetAwake(true);
    e.SetInertialVel(0.5F); // <= 1.0
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Steer);
    EXPECT_FLOAT_EQ(e.InertialVel(), 0.5F); // no decay below/at threshold
}

TEST(EnemyAI12Test, ThresholdIsExclusiveForKnockback) {
    // inertial_vel == 1.0 is NOT > 1.0 -> steering side, no decay (line 679943).
    EnemyAI12 e;
    e.SetAwake(true);
    e.SetInertialVel(1.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Steer);
    EXPECT_FLOAT_EQ(e.InertialVel(), 1.0F);
}

TEST(EnemyAI12Test, AwakeDeadLowInertiaFlipsAwakeOffAndReturnsDead) {
    // inertial_vel <= 1.0 AND dead -> awake set false (0x18 = 0, line 679945),
    // early-return; the steering block does not run.
    EnemyAI12 e;
    e.SetAwake(true);
    e.SetDead(true);
    e.SetInertialVel(0.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Dead);
    EXPECT_FALSE(e.Awake());              // awake = false (0x18 = 0)
    EXPECT_FLOAT_EQ(e.InertialVel(), 0.0F); // no decay on the dead path
}

TEST(EnemyAI12Test, DeadBranchUnreachableWhileKnockbackActive) {
    // The dead branch lives inside the inertial_vel <= 1.0 side, so an active
    // knockback (> 1.0) takes the Knockback path even when dead.
    EnemyAI12 e;
    e.SetAwake(true);
    e.SetDead(true);
    e.SetInertialVel(10.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback);
    EXPECT_TRUE(e.Awake());               // not flipped (dead branch skipped)
    EXPECT_FLOAT_EQ(e.InertialVel(), 5.0F); // decayed by friction
}

TEST(EnemyAI12Test, KnockbackActiveDecaysAndReportsKnockback) {
    // not asleep + inertial_vel > 1.0 -> knockback velocity composed, decays by
    // friction (line 680014), velocity written.
    EnemyAI12 e;
    e.SetAwake(true);
    e.SetInertialVel(10.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback); // 10 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 5.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback); // 5 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 2.5F);
}

TEST(EnemyAI12Test, FixedUpdateTakesNoRngDraw) {
    // FixedUpdate has no rg_random draws; stepping it must not advance the stream.
    EnemyAI12 stepped;
    EnemyAI12 quiet;
    stepped.SetSeed(99);
    quiet.SetSeed(99);
    stepped.SetAwake(true);
    stepped.SetInertialVel(10.0F);
    stepped.FixedUpdateStep(0.5F);
    stepped.FixedUpdateStep(0.5F);
    const glm::vec2 a = stepped.RunReflection();
    const glm::vec2 b = quiet.RunReflection();
    EXPECT_FLOAT_EQ(a.x, b.x);
    EXPECT_FLOAT_EQ(a.y, b.y);
}

// ---- Full interleaved replay determinism ----------------------------------

TEST(EnemyAI12Test, FullStreamReplayIsDeterministic) {
    // Interleave RunReflection (the only RNG-consuming method) with the zero-draw
    // methods (Scout / ShootReflection / FixedUpdateStep) and replay from the
    // same seed -> identical sequence (lockstep guarantee).
    auto run = [](int seed) {
        EnemyAI12 e;
        e.SetSeed(seed);
        e.SetAwake(true);
        std::vector<float> trace;
        float delay = 0.0F;
        for (int i = 0; i < 24; ++i) {
            e.Scout();                       // 0 draws
            const glm::vec2 d = e.RunReflection(); // 2 float draws
            trace.push_back(d.x);
            trace.push_back(d.y);
            e.ShootReflection(delay, 1.0F);  // 0 draws
            e.FixedUpdateStep(0.9F);         // 0 draws
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

TEST(EnemyAI12Test, DifferentSeedsDiverge) {
    EnemyAI12 a;
    EnemyAI12 b;
    a.SetSeed(1);
    b.SetSeed(424242);
    bool diverged = false;
    for (int i = 0; i < 64; ++i) {
        const glm::vec2 da = a.RunReflection();
        const glm::vec2 db = b.RunReflection();
        if (da != db) {
            diverged = true;
        }
    }
    EXPECT_TRUE(diverged);
}

// NOLINTEND(readability-magic-numbers)
