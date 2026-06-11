#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/EnemyAI10.hpp"

using Game::EnemyAI10;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
float Len(const glm::vec2 &v) { return std::sqrt(v.x * v.x + v.y * v.y); }
} // namespace

// ---- Scout ----------------------------------------------------------------

TEST(EnemyAI10Test, ScoutClearsTargetWhenActive) {
    EnemyAI10 e;
    e.SetSeed(1);
    e.SetHasTarget(true);
    EXPECT_TRUE(e.Scout());
    EXPECT_FALSE(e.HasTarget()); // target_obj = null (0x7C)
}

TEST(EnemyAI10Test, ScoutGatedWhileDizzy) {
    EnemyAI10 e;
    e.SetSeed(1);
    e.SetHasTarget(true);
    e.SetDizzy(true);
    EXPECT_FALSE(e.Scout());     // gated
    EXPECT_TRUE(e.HasTarget());  // unchanged: no state write while gated
}

TEST(EnemyAI10Test, ScoutGatedWhileDead) {
    EnemyAI10 e;
    e.SetSeed(1);
    e.SetHasTarget(true);
    e.SetDead(true);
    EXPECT_FALSE(e.Scout());     // gated
    EXPECT_TRUE(e.HasTarget());  // unchanged
}

TEST(EnemyAI10Test, ScoutTakesNoRngDraw) {
    // Scout has no rg_random draws: the stream after Scout must match an enemy
    // that only ran RunReflection from the same seed.
    EnemyAI10 scouted;
    EnemyAI10 quiet;
    scouted.SetSeed(321);
    quiet.SetSeed(321);
    scouted.Scout();
    const glm::vec2 a = scouted.RunReflection();
    const glm::vec2 b = quiet.RunReflection();
    EXPECT_FLOAT_EQ(a.x, b.x);
    EXPECT_FLOAT_EQ(a.y, b.y);
}

TEST(EnemyAI10Test, ScoutGatedTakesNoRngDraw) {
    // A gated (dizzy) Scout must not perturb the stream either.
    EnemyAI10 gated;
    EnemyAI10 quiet;
    gated.SetSeed(77);
    quiet.SetSeed(77);
    gated.SetDizzy(true);
    EXPECT_FALSE(gated.Scout());
    gated.SetDizzy(false);
    const glm::vec2 a = gated.RunReflection();
    const glm::vec2 b = quiet.RunReflection();
    EXPECT_FLOAT_EQ(a.x, b.x);
    EXPECT_FLOAT_EQ(a.y, b.y);
}

// ---- RunReflection (wander) ----------------------------------------------

TEST(EnemyAI10Test, WanderIsNormalizedOrZero) {
    EnemyAI10 e;
    e.SetSeed(2024);
    for (int i = 0; i < 128; ++i) {
        const glm::vec2 d = e.RunReflection();
        const float len = Len(d);
        const bool unit = std::fabs(len - 1.0F) < 1e-4F;
        const bool zero = len < 1e-6F;
        EXPECT_TRUE(unit || zero);        // normalized direction (or degenerate zero)
        EXPECT_EQ(d, e.MoveDirection());  // stored as move_direction (0x74)
    }
}

TEST(EnemyAI10Test, WanderIsDeterministic) {
    EnemyAI10 a;
    EnemyAI10 b;
    a.SetSeed(31337);
    b.SetSeed(31337);
    for (int i = 0; i < 64; ++i) {
        const glm::vec2 da = a.RunReflection();
        const glm::vec2 db = b.RunReflection();
        EXPECT_FLOAT_EQ(da.x, db.x);
        EXPECT_FLOAT_EQ(da.y, db.y);
    }
}

TEST(EnemyAI10Test, WanderConsumesTwoDrawsInOrder) {
    // RunReflection draws Range(-1,1) twice; a reference stream drawing the same
    // two floats from the same seed must match component-for-component.
    EnemyAI10 e;
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

// ---- ShootReflection / OnAtk ---------------------------------------------

TEST(EnemyAI10Test, ShootLatchesCanShootFalseAndReturnsDelay) {
    EnemyAI10 e;
    e.SetSeed(1);
    EXPECT_TRUE(e.CanShoot()); // starts armed
    float delay = -1.0F;
    EXPECT_TRUE(e.ShootReflection(delay, 1.25F));
    EXPECT_FALSE(e.CanShoot());      // 0x40 = 0
    EXPECT_FLOAT_EQ(delay, 1.25F);   // shoot_cd -> Invoke("OnAtk") delay
}

TEST(EnemyAI10Test, ShootGatedWhileDead) {
    EnemyAI10 e;
    e.SetSeed(1);
    e.SetDead(true);
    float delay = -1.0F;
    EXPECT_FALSE(e.ShootReflection(delay, 1.0F));
    EXPECT_TRUE(e.CanShoot());       // unchanged: gated
    EXPECT_FLOAT_EQ(delay, -1.0F);   // out-param untouched while gated
}

TEST(EnemyAI10Test, ShootGatedWhileDizzy) {
    EnemyAI10 e;
    e.SetSeed(1);
    e.SetDizzy(true);
    float delay = -1.0F;
    EXPECT_FALSE(e.ShootReflection(delay, 1.0F));
    EXPECT_TRUE(e.CanShoot());       // unchanged: gated
}

TEST(EnemyAI10Test, ShootTakesNoRngDraw) {
    // ShootReflection has no rg_random draws; the stream after a shot must equal
    // the stream of an enemy that never shot.
    EnemyAI10 shot;
    EnemyAI10 quiet;
    shot.SetSeed(456);
    quiet.SetSeed(456);
    float delay = 0.0F;
    shot.ShootReflection(delay, 1.0F);
    const glm::vec2 a = shot.RunReflection();
    const glm::vec2 b = quiet.RunReflection();
    EXPECT_FLOAT_EQ(a.x, b.x);
    EXPECT_FLOAT_EQ(a.y, b.y);
}

TEST(EnemyAI10Test, OnAtkFreezesMovement) {
    EnemyAI10 e;
    e.SetSeed(1);
    e.RunReflection(); // give move_direction a nonzero value
    EXPECT_GT(Len(e.MoveDirection()), 0.0F);
    e.OnAtk();
    EXPECT_FLOAT_EQ(e.MoveDirection().x, 0.0F); // move_direction = Vector2.zero
    EXPECT_FLOAT_EQ(e.MoveDirection().y, 0.0F);
}

TEST(EnemyAI10Test, OnAtkTakesNoRngDraw) {
    EnemyAI10 acted;
    EnemyAI10 quiet;
    acted.SetSeed(99);
    quiet.SetSeed(99);
    acted.OnAtk();
    const glm::vec2 a = acted.RunReflection();
    const glm::vec2 b = quiet.RunReflection();
    EXPECT_FLOAT_EQ(a.x, b.x);
    EXPECT_FLOAT_EQ(a.y, b.y);
}

// ---- FixedRotation --------------------------------------------------------

TEST(EnemyAI10Test, FixedRotationReportsTargetPresence) {
    EnemyAI10 e;
    EXPECT_FALSE(e.FixedRotation()); // no target -> owner uses own transform
    e.SetHasTarget(true);
    EXPECT_TRUE(e.FixedRotation());  // target -> owner aims at the target
}

// ---- GetHurt (temp_enemy dispatch) ---------------------------------------

TEST(EnemyAI10Test, GetHurtNonTempDelegatesToBase) {
    EnemyAI10 e;
    EXPECT_FALSE(e.TempEnemy());
    EXPECT_EQ(e.GetHurt(), EnemyAI10::HurtResult::Base);
}

TEST(EnemyAI10Test, GetHurtTempAliveShowsUi) {
    EnemyAI10 e;
    e.SetTempEnemy(true);
    EXPECT_EQ(e.GetHurt(), EnemyAI10::HurtResult::TempUi);
}

TEST(EnemyAI10Test, GetHurtTempDeadDoesNothing) {
    EnemyAI10 e;
    e.SetTempEnemy(true);
    e.SetDead(true);
    EXPECT_EQ(e.GetHurt(), EnemyAI10::HurtResult::TempDead);
}

// ---- FixedUpdate physics --------------------------------------------------

using StepResult = EnemyAI10::StepResult;

TEST(EnemyAI10Test, NotAwakeIsCompleteNoOp) {
    // awake (0x18) gate at line 678961: a not-awake step does NOTHING -- no
    // friction decay and no state write, regardless of any other state.
    EnemyAI10 e;
    e.SetInertialVel(10.0F); // knockback would otherwise decay
    e.SetDead(true);          // dead would otherwise clear awake (already false)
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Asleep);
    EXPECT_FLOAT_EQ(e.InertialVel(), 10.0F); // unchanged: no decay while asleep
    EXPECT_FALSE(e.Awake());
}

TEST(EnemyAI10Test, AwakeDeadLowInertiaStopsAndClearsAwake) {
    // inertial_vel <= 1.0 AND dead -> awake cleared (0x18 = 0), DeadStop.
    EnemyAI10 e;
    e.SetAwake(true);
    e.SetDead(true);
    e.SetInertialVel(0.5F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::DeadStop);
    EXPECT_FALSE(e.Awake());                  // 0x18 = 0
    EXPECT_FLOAT_EQ(e.InertialVel(), 0.5F);   // no decay on the dead-stop path
}

TEST(EnemyAI10Test, AwakeAliveLowInertiaSteersNoDecay) {
    // inertial_vel <= 1.0 AND not dead -> Steer, no decay, awake stays set.
    EnemyAI10 e;
    e.SetAwake(true);
    e.SetInertialVel(1.0F); // 1.0 is NOT > 1.0 -> steer path
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Steer);
    EXPECT_FLOAT_EQ(e.InertialVel(), 1.0F); // no decay below/at threshold
    EXPECT_TRUE(e.Awake());
}

TEST(EnemyAI10Test, KnockbackDecaysByFrictionAndReportsKnockback) {
    // inertial_vel > 1.0 -> knockback composition + inertial_vel *= friction.
    EnemyAI10 e;
    e.SetAwake(true);
    e.SetInertialVel(10.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback); // 10 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 5.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback); // 5 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 2.5F);
}

TEST(EnemyAI10Test, DeadWithHighInertiaTakesKnockbackNotDeadStop) {
    // Faithful subtlety: the dead handler is nested under the <= 1.0 path only.
    // A dead enemy with inertial_vel > 1.0 takes the knockback branch and decays;
    // awake stays set (it is not cleared on this path).
    EnemyAI10 e;
    e.SetAwake(true);
    e.SetDead(true);
    e.SetInertialVel(4.0F); // > 1.0
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback);
    EXPECT_FLOAT_EQ(e.InertialVel(), 2.0F); // decayed
    EXPECT_TRUE(e.Awake());                  // NOT cleared on the knockback path
}

TEST(EnemyAI10Test, FixedUpdateTakesNoRngDraw) {
    // FixedUpdate has zero rg_random draws; the stream after stepping must match
    // an enemy that never stepped.
    EnemyAI10 stepped;
    EnemyAI10 quiet;
    stepped.SetSeed(2024);
    quiet.SetSeed(2024);
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

TEST(EnemyAI10Test, FullStreamReplayIsDeterministic) {
    // RunReflection is the only RNG-consuming method (2 float draws). Interleave
    // the full method set and replay from the same seed -> identical sequence.
    auto run = [](int seed) {
        EnemyAI10 e;
        e.SetSeed(seed);
        e.SetAwake(true);
        std::vector<float> trace;
        float delay = 0.0F;
        for (int i = 0; i < 24; ++i) {
            trace.push_back(e.Scout() ? 1.0F : 0.0F); // 0 draws
            const glm::vec2 d = e.RunReflection();    // 2 float draws
            trace.push_back(d.x);
            trace.push_back(d.y);
            e.ShootReflection(delay, 1.0F);            // 0 draws
            e.OnAtk();                                  // 0 draws
            e.SetInertialVel(8.0F);
            e.FixedUpdateStep(0.5F);                    // 0 draws
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

TEST(EnemyAI10Test, DifferentSeedsDiverge) {
    EnemyAI10 a;
    EnemyAI10 b;
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
