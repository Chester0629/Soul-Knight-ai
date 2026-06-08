#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/EnemyAI11.hpp"

using Game::EnemyAI11;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
float Len(const glm::vec2 &v) { return std::sqrt(v.x * v.x + v.y * v.y); }
} // namespace

using StepResult = EnemyAI11::StepResult;

// ---- FixedUpdate physics --------------------------------------------------

TEST(EnemyAI11Test, NotAwakeIsCompleteNoOp) {
    // awake (0x18) gate at line 679549: a not-awake step does NOTHING -- no
    // decay and no velocity write, regardless of any other state.
    EnemyAI11 e;
    e.SetInertialVel(10.0F); // knockback would otherwise decay
    e.SetDead(true);         // dead branch would otherwise apply
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Asleep);
    EXPECT_FLOAT_EQ(e.InertialVel(), 10.0F); // unchanged: no decay while asleep
    EXPECT_TRUE(e.Dead());                    // dead untouched while asleep
}

TEST(EnemyAI11Test, AwakeDeadLatchesAwakeOffWithoutDecay) {
    // inertial_vel <= 1.0 AND dead -> latch awake = 0 (line 679552), exit, no
    // decay (the steer block does not run).
    EnemyAI11 e;
    e.SetAwake(true);
    e.SetDead(true);
    e.SetInertialVel(0.5F); // <= 1.0 -> dead sub-branch is reachable
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Dead);
    EXPECT_FALSE(e.Awake());                 // awake latched off
    EXPECT_FLOAT_EQ(e.InertialVel(), 0.5F);  // no decay on the dead path
}

TEST(EnemyAI11Test, AwakeAliveLowInertiaSteersWithoutDecay) {
    EnemyAI11 e;
    e.SetAwake(true);
    e.SetInertialVel(1.0F); // 1.0 is NOT > 1.0 -> steer branch, not knockback
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Steer);
    EXPECT_FLOAT_EQ(e.InertialVel(), 1.0F); // no decay below threshold
    EXPECT_TRUE(e.Awake());                  // steer does not touch awake
}

TEST(EnemyAI11Test, KnockbackActiveDecaysAndReportsKnockback) {
    // inertial_vel > 1.0 -> knockback impulse added to steering, inertial_vel *=
    // friction (line 679621). This is the ONLY decay site.
    EnemyAI11 e;
    e.SetAwake(true);
    e.SetInertialVel(10.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback); // 10 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 5.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback); // 5 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 2.5F);
}

TEST(EnemyAI11Test, KnockbackBranchIgnoresDead) {
    // The dead check lives ONLY inside the inertial_vel <= 1.0 branch. With
    // inertial_vel > 1.0, a dead enemy still takes the knockback (decay) branch.
    EnemyAI11 e;
    e.SetAwake(true);
    e.SetDead(true);
    e.SetInertialVel(4.0F); // > 1.0 -> knockback else-branch (no dead check)
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback);
    EXPECT_FLOAT_EQ(e.InertialVel(), 2.0F); // decayed
    EXPECT_TRUE(e.Awake());                  // knockback branch never clears awake
}

TEST(EnemyAI11Test, FixedUpdateTakesNoRngDraw) {
    // FixedUpdateStep has no rg_random draws; the stream after stepping must equal
    // the stream of an enemy that never stepped.
    EnemyAI11 stepped;
    EnemyAI11 quiet;
    stepped.SetSeed(456);
    quiet.SetSeed(456);
    stepped.SetAwake(true);
    stepped.SetInertialVel(8.0F);
    stepped.FixedUpdateStep(0.5F);
    const glm::vec2 a = stepped.RunReflection();
    const glm::vec2 b = quiet.RunReflection();
    EXPECT_FLOAT_EQ(a.x, b.x); // no extra draw consumed by FixedUpdateStep
    EXPECT_FLOAT_EQ(a.y, b.y);
}

// ---- Scout ----------------------------------------------------------------

TEST(EnemyAI11Test, ScoutClearsTargetWhenActive) {
    EnemyAI11 e;
    e.SetHasTarget(true);
    EXPECT_TRUE(e.Scout());
    EXPECT_FALSE(e.HasTarget()); // target_obj = null (0x7C)
}

TEST(EnemyAI11Test, ScoutGatedWhileDizzy) {
    EnemyAI11 e;
    e.SetHasTarget(true);
    e.SetDizzy(true);
    EXPECT_FALSE(e.Scout());     // gated
    EXPECT_TRUE(e.HasTarget());  // unchanged: gate ran before the clear
}

TEST(EnemyAI11Test, ScoutGatedWhileDead) {
    EnemyAI11 e;
    e.SetHasTarget(true);
    e.SetDead(true);
    EXPECT_FALSE(e.Scout());     // gated
    EXPECT_TRUE(e.HasTarget());  // unchanged
}

TEST(EnemyAI11Test, ScoutTakesNoRngDraw) {
    // This override does NOT reproduce the base Scout's Range(0,10): a Scout call
    // must not advance the stream relative to an enemy that never scouted.
    EnemyAI11 scouted;
    EnemyAI11 quiet;
    scouted.SetSeed(99);
    quiet.SetSeed(99);
    EXPECT_TRUE(scouted.Scout());
    const glm::vec2 a = scouted.RunReflection();
    const glm::vec2 b = quiet.RunReflection();
    EXPECT_FLOAT_EQ(a.x, b.x); // no draw consumed by Scout
    EXPECT_FLOAT_EQ(a.y, b.y);
}

// ---- RunReflection (wander) ----------------------------------------------

TEST(EnemyAI11Test, WanderIsNormalizedOrZero) {
    EnemyAI11 e;
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

TEST(EnemyAI11Test, WanderIsDeterministic) {
    EnemyAI11 a;
    EnemyAI11 b;
    a.SetSeed(31337);
    b.SetSeed(31337);
    for (int i = 0; i < 64; ++i) {
        const glm::vec2 da = a.RunReflection();
        const glm::vec2 db = b.RunReflection();
        EXPECT_FLOAT_EQ(da.x, db.x);
        EXPECT_FLOAT_EQ(da.y, db.y);
    }
}

TEST(EnemyAI11Test, WanderConsumesTwoDrawsInOrder) {
    // RunReflection draws Range(-1,1) twice (lines 679674/679679); a reference
    // stream drawing the same two floats from the same seed must match.
    EnemyAI11 e;
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

TEST(EnemyAI11Test, ShootLatchesStateArmsBurstAndDelay) {
    EnemyAI11 e;
    e.SetSeed(1);
    float cd = -1.0F;
    const bool fired = e.ShootReflection(cd, 1.5F);
    EXPECT_TRUE(fired);
    EXPECT_FALSE(e.CanShoot());                // 0x40 = 0
    EXPECT_EQ(e.AtkCount(), EnemyAI11::kBurstCount); // 0xC4 = 5
    EXPECT_FALSE(e.WeaponLockTarget());        // 0x1C = 0
    EXPECT_FLOAT_EQ(e.MoveDirection().x, 0.0F); // move_direction = zero
    EXPECT_FLOAT_EQ(e.MoveDirection().y, 0.0F);
    EXPECT_FLOAT_EQ(cd, 1.5F);                  // shoot_cd -> Invoke delay
}

TEST(EnemyAI11Test, ShootZeroesAnExistingMoveDirection) {
    EnemyAI11 e;
    e.SetSeed(1);
    e.RunReflection(); // set some non-zero (likely) move_direction first
    float cd = 0.0F;
    EXPECT_TRUE(e.ShootReflection(cd, 1.0F));
    EXPECT_FLOAT_EQ(e.MoveDirection().x, 0.0F); // overwritten to zero (line 679719)
    EXPECT_FLOAT_EQ(e.MoveDirection().y, 0.0F);
}

TEST(EnemyAI11Test, ShootGatedWhileDead) {
    EnemyAI11 e;
    e.SetSeed(1);
    e.SetDead(true);
    float cd = 0.0F;
    EXPECT_FALSE(e.ShootReflection(cd, 1.0F));
    EXPECT_TRUE(e.CanShoot());   // unchanged
    EXPECT_EQ(e.AtkCount(), 0);  // burst not armed
}

TEST(EnemyAI11Test, ShootGatedWhileDizzy) {
    EnemyAI11 e;
    e.SetSeed(1);
    e.SetDizzy(true);
    float cd = 0.0F;
    EXPECT_FALSE(e.ShootReflection(cd, 1.0F));
    EXPECT_TRUE(e.CanShoot());
    EXPECT_EQ(e.AtkCount(), 0);
}

TEST(EnemyAI11Test, ShootTakesNoRngDraw) {
    EnemyAI11 shot;
    EnemyAI11 quiet;
    shot.SetSeed(7);
    quiet.SetSeed(7);
    float cd = 0.0F;
    shot.ShootReflection(cd, 1.0F);
    const glm::vec2 a = shot.RunReflection();
    const glm::vec2 b = quiet.RunReflection();
    EXPECT_FLOAT_EQ(a.x, b.x); // no extra draw consumed
    EXPECT_FLOAT_EQ(a.y, b.y);
}

// ---- OnAtk burst decision + DrainBurst ------------------------------------

TEST(EnemyAI11Test, OnAtkSpawnsExtraBulletOnlyWhileBurstRemains) {
    EnemyAI11 e;
    e.SetSeed(1);
    EXPECT_FALSE(e.OnAtkSpawnsExtraBullet()); // atk_count == 0 -> no extra bullet
    float cd = 0.0F;
    e.ShootReflection(cd, 1.0F);              // arms atk_count = 5
    EXPECT_TRUE(e.OnAtkSpawnsExtraBullet());  // atk_count != 0 -> extra bullet
}

TEST(EnemyAI11Test, DrainBurstDecrementsToZero) {
    EnemyAI11 e;
    e.SetSeed(1);
    float cd = 0.0F;
    e.ShootReflection(cd, 1.0F); // atk_count = 5
    EXPECT_EQ(e.DrainBurst(), 4);
    EXPECT_EQ(e.DrainBurst(), 3);
    EXPECT_EQ(e.DrainBurst(), 2);
    EXPECT_EQ(e.DrainBurst(), 1);
    EXPECT_EQ(e.DrainBurst(), 0);
    EXPECT_FALSE(e.OnAtkSpawnsExtraBullet()); // drained: no more extra bullets
}

TEST(EnemyAI11Test, DrainBurstTakesNoRngDraw) {
    EnemyAI11 drained;
    EnemyAI11 quiet;
    drained.SetSeed(3);
    quiet.SetSeed(3);
    float cd = 0.0F;
    drained.ShootReflection(cd, 1.0F);
    drained.DrainBurst();
    const glm::vec2 a = drained.RunReflection();
    const glm::vec2 b = quiet.RunReflection();
    EXPECT_FLOAT_EQ(a.x, b.x);
    EXPECT_FLOAT_EQ(a.y, b.y);
}

// ---- ChildDead decision ---------------------------------------------------

TEST(EnemyAI11Test, ChildDeadInvokesOnlyWhenDeadObjPresent) {
    EnemyAI11 e;
    EXPECT_TRUE(e.ChildDead(true));   // dead_obj != null -> schedule respawn Invoke
    EXPECT_FALSE(e.ChildDead(false)); // dead_obj == null -> return, no Invoke
}

// ---- Full interleaved replay determinism ----------------------------------

TEST(EnemyAI11Test, FullStreamReplayIsDeterministic) {
    // Interleave every method; only RunReflection consumes RNG (2 float draws).
    // Replaying from the same seed -> identical sequence (lockstep guarantee).
    auto run = [](int seed) {
        EnemyAI11 e;
        e.SetSeed(seed);
        e.SetAwake(true);
        std::vector<float> trace;
        float cd = 0.0F;
        for (int i = 0; i < 24; ++i) {
            e.Scout();                              // 0 draws
            const glm::vec2 d = e.RunReflection();  // 2 float draws
            trace.push_back(d.x);
            trace.push_back(d.y);
            e.ShootReflection(cd, 1.0F);            // 0 draws
            trace.push_back(static_cast<float>(e.DrainBurst())); // 0 draws
            e.SetInertialVel(5.0F);
            e.FixedUpdateStep(0.5F);                // 0 draws
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

TEST(EnemyAI11Test, DifferentSeedsDiverge) {
    EnemyAI11 a;
    EnemyAI11 b;
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
