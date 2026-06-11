#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/EnemyAI14.hpp"

using Game::EnemyAI14;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
float Len(const glm::vec2 &v) { return std::sqrt(v.x * v.x + v.y * v.y); }
} // namespace

// ---- Scout ----------------------------------------------------------------

TEST(EnemyAI14Test, ScoutClearsTargetWhenActive) {
    EnemyAI14 e;
    e.SetSeed(1);
    e.SetHasTarget(true);
    EXPECT_TRUE(e.Scout());
    EXPECT_FALSE(e.HasTarget()); // target_obj = null (0x7C)
}

TEST(EnemyAI14Test, ScoutGatedWhileDeadDoesNothing) {
    EnemyAI14 e;
    e.SetSeed(1);
    e.SetHasTarget(true);
    e.SetDead(true);
    EXPECT_FALSE(e.Scout());     // gated by dead (0x38)
    EXPECT_TRUE(e.HasTarget());  // no state write while gated
}

TEST(EnemyAI14Test, ScoutGatedWhileDizzyDoesNothing) {
    EnemyAI14 e;
    e.SetSeed(1);
    e.SetHasTarget(true);
    e.SetDizzy(true);
    EXPECT_FALSE(e.Scout());     // gated by dizzy (0xA1)
    EXPECT_TRUE(e.HasTarget());  // no state write while gated
}

TEST(EnemyAI14Test, ScoutTakesNoRngDraw) {
    // Scout has no rg_random draws; the stream after a Scout must equal an
    // enemy that never scouted (next RunReflection draws must match).
    EnemyAI14 scouted;
    EnemyAI14 quiet;
    scouted.SetSeed(777);
    quiet.SetSeed(777);
    scouted.Scout();
    const glm::vec2 a = scouted.RunReflection();
    const glm::vec2 b = quiet.RunReflection();
    EXPECT_FLOAT_EQ(a.x, b.x);
    EXPECT_FLOAT_EQ(a.y, b.y);
}

// ---- RunReflection (wander) ----------------------------------------------

TEST(EnemyAI14Test, WanderIsNormalizedOrZero) {
    EnemyAI14 e;
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

TEST(EnemyAI14Test, WanderIsDeterministic) {
    EnemyAI14 a;
    EnemyAI14 b;
    a.SetSeed(31337);
    b.SetSeed(31337);
    for (int i = 0; i < 64; ++i) {
        const glm::vec2 da = a.RunReflection();
        const glm::vec2 db = b.RunReflection();
        EXPECT_FLOAT_EQ(da.x, db.x);
        EXPECT_FLOAT_EQ(da.y, db.y);
    }
}

TEST(EnemyAI14Test, WanderConsumesTwoDrawsInOrder) {
    // RunReflection draws Range(-1,1) twice; a reference stream drawing the same
    // two floats from the same seed must match component-for-component.
    EnemyAI14 e;
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

TEST(EnemyAI14Test, WanderDrawsEvenWhenDeadOrDizzy) {
    // RunReflection has NO gate in the decomp: it draws unconditionally. A dead
    // enemy and an alive enemy from the same seed must produce identical draws.
    EnemyAI14 dead;
    EnemyAI14 alive;
    dead.SetSeed(42);
    alive.SetSeed(42);
    dead.SetDead(true);
    dead.SetDizzy(true);
    for (int i = 0; i < 16; ++i) {
        const glm::vec2 dd = dead.RunReflection();
        const glm::vec2 aa = alive.RunReflection();
        EXPECT_FLOAT_EQ(dd.x, aa.x);
        EXPECT_FLOAT_EQ(dd.y, aa.y);
    }
}

// ---- ShootReflection ------------------------------------------------------

TEST(EnemyAI14Test, ShootLatchesStateAndDelays) {
    EnemyAI14 e;
    e.SetSeed(1);
    float atk = -1.0F;
    float end = -1.0F;
    const bool fired = e.ShootReflection(atk, end, 1.5F);
    EXPECT_TRUE(fired);
    EXPECT_FALSE(e.CanShoot());        // 0x40 = 0
    EXPECT_FALSE(e.WeaponLockTarget()); // 0x1C = 0
    EXPECT_FLOAT_EQ(atk, 1.5F);        // shoot_cd -> OnAtk delay
    EXPECT_FLOAT_EQ(end, 0.5F);        // literal 0.5 -> EndAtk delay
}

TEST(EnemyAI14Test, ShootGatedWhileDead) {
    EnemyAI14 e;
    e.SetSeed(1);
    e.SetDead(true);
    float atk = 0.0F;
    float end = 0.0F;
    EXPECT_FALSE(e.ShootReflection(atk, end, 1.5F));
    EXPECT_TRUE(e.CanShoot()); // unchanged: gated took no write
}

TEST(EnemyAI14Test, ShootGatedWhileDizzy) {
    EnemyAI14 e;
    e.SetSeed(1);
    e.SetDizzy(true);
    float atk = 0.0F;
    float end = 0.0F;
    EXPECT_FALSE(e.ShootReflection(atk, end, 1.5F));
    EXPECT_TRUE(e.CanShoot()); // unchanged: gated took no write
}

TEST(EnemyAI14Test, ShootTakesNoRngDraw) {
    // ShootReflection has no rg_random draws; the stream after a shot must equal
    // the stream of an enemy that never shot.
    EnemyAI14 shot;
    EnemyAI14 quiet;
    shot.SetSeed(456);
    quiet.SetSeed(456);
    float atk = 0.0F;
    float end = 0.0F;
    shot.ShootReflection(atk, end, 1.0F);
    const glm::vec2 a = shot.RunReflection();
    const glm::vec2 b = quiet.RunReflection();
    EXPECT_FLOAT_EQ(a.x, b.x);
    EXPECT_FLOAT_EQ(a.y, b.y);
}

// ---- OnAtk ----------------------------------------------------------------

TEST(EnemyAI14Test, OnAtkZeroesMoveWhenActive) {
    EnemyAI14 e;
    e.SetSeed(1);
    e.RunReflection(); // set a non-zero move direction first (usually)
    EXPECT_TRUE(e.OnAtk());
    EXPECT_FLOAT_EQ(e.MoveDirection().x, 0.0F); // Vector2.zero (0x74)
    EXPECT_FLOAT_EQ(e.MoveDirection().y, 0.0F);
}

TEST(EnemyAI14Test, OnAtkGatedWhileDead) {
    EnemyAI14 e;
    e.SetSeed(1);
    e.SetDead(true);
    EXPECT_FALSE(e.OnAtk());
}

TEST(EnemyAI14Test, OnAtkNotGatedByDizzy) {
    // OnAtk only gates on dead (0x38), NOT dizzy -- a dizzy enemy still zeroes.
    EnemyAI14 e;
    e.SetSeed(1);
    e.SetDizzy(true);
    EXPECT_TRUE(e.OnAtk());
}

TEST(EnemyAI14Test, OnAtkTakesNoRngDraw) {
    EnemyAI14 atked;
    EnemyAI14 quiet;
    atked.SetSeed(321);
    quiet.SetSeed(321);
    atked.OnAtk();
    const glm::vec2 a = atked.RunReflection();
    const glm::vec2 b = quiet.RunReflection();
    EXPECT_FLOAT_EQ(a.x, b.x);
    EXPECT_FLOAT_EQ(a.y, b.y);
}

// ---- EndAtk ---------------------------------------------------------------

TEST(EnemyAI14Test, EndAtkSetsWeaponLockTarget) {
    EnemyAI14 e;
    e.SetSeed(1);
    EXPECT_FALSE(e.WeaponLockTarget());
    e.EndAtk();
    EXPECT_TRUE(e.WeaponLockTarget()); // 0x1C = 1 (no gate)
}

TEST(EnemyAI14Test, EndAtkRunsEvenWhenDead) {
    // EndAtk has no gate in the decomp -- it sets the latch regardless of state.
    EnemyAI14 e;
    e.SetSeed(1);
    e.SetDead(true);
    e.EndAtk();
    EXPECT_TRUE(e.WeaponLockTarget());
}

// ---- FixedUpdate physics --------------------------------------------------

using StepResult = EnemyAI14::StepResult;

TEST(EnemyAI14Test, NotAwakeIsCompleteNoOp) {
    // awake (0x18) gate at line 680550: a not-awake step does NOTHING -- no
    // friction decay and no awake change, regardless of any other state.
    EnemyAI14 e;
    e.SetAwake(false);
    e.SetInertialVel(10.0F); // knockback would otherwise decay
    e.SetDead(true);          // dead would otherwise clear awake
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Asleep);
    EXPECT_FLOAT_EQ(e.InertialVel(), 10.0F); // unchanged: no decay while asleep
    EXPECT_FALSE(e.Awake());                  // still false (unchanged)
}

TEST(EnemyAI14Test, DeadBranchClearsAwakeAndEarlyReturns) {
    // inertial_vel <= 1.0 && dead (line 680552): awake = 0 (line 680553), no
    // decay (steering block skipped via the early-return tail at line 680564).
    EnemyAI14 e;
    e.SetAwake(true);
    e.SetDead(true);
    e.SetInertialVel(1.0F); // 1.0 is NOT > 1.0 -> enters the <= 1.0 branch
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Dead);
    EXPECT_FALSE(e.Awake());                 // awake cleared (0x18 = 0)
    EXPECT_FLOAT_EQ(e.InertialVel(), 1.0F);  // no decay on the dead branch
}

TEST(EnemyAI14Test, SteerBranchWhenAliveAndLowInertia) {
    // inertial_vel <= 1.0 && not dead -> normal steering, no decay.
    EnemyAI14 e;
    e.SetAwake(true);
    e.SetInertialVel(0.5F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Steer);
    EXPECT_FLOAT_EQ(e.InertialVel(), 0.5F);  // no decay below threshold
    EXPECT_TRUE(e.Awake());                   // awake untouched on steer
}

TEST(EnemyAI14Test, ThresholdExactlyOneIsNotKnockback) {
    // 1.0 is NOT > 1.0 -> takes the <= 1.0 branch (steer when alive), no decay.
    EnemyAI14 e;
    e.SetAwake(true);
    e.SetInertialVel(1.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Steer);
    EXPECT_FLOAT_EQ(e.InertialVel(), 1.0F);
}

TEST(EnemyAI14Test, KnockbackActiveDecaysAndReportsKnockback) {
    // inertial_vel > 1.0 -> knockback steering, decays by friction (line 680622).
    EnemyAI14 e;
    e.SetAwake(true);
    e.SetInertialVel(10.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback); // 10 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 5.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback); // 5 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 2.5F);
}

TEST(EnemyAI14Test, KnockbackIgnoresDeadFlag) {
    // The dead sub-branch only lives inside the <= 1.0 branch; an active
    // knockback (> 1.0) decays even when dead and does NOT clear awake.
    EnemyAI14 e;
    e.SetAwake(true);
    e.SetDead(true);
    e.SetInertialVel(4.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback);
    EXPECT_FLOAT_EQ(e.InertialVel(), 2.0F);
    EXPECT_TRUE(e.Awake()); // awake NOT cleared on the knockback branch
}

TEST(EnemyAI14Test, FixedUpdateTakesNoRngDraw) {
    EnemyAI14 stepped;
    EnemyAI14 quiet;
    stepped.SetSeed(2024);
    quiet.SetSeed(2024);
    stepped.SetAwake(true);
    stepped.SetInertialVel(10.0F);
    stepped.FixedUpdateStep(0.5F);
    const glm::vec2 a = stepped.RunReflection();
    const glm::vec2 b = quiet.RunReflection();
    EXPECT_FLOAT_EQ(a.x, b.x);
    EXPECT_FLOAT_EQ(a.y, b.y);
}

// ---- Full interleaved replay determinism ----------------------------------

TEST(EnemyAI14Test, FullStreamReplayIsDeterministic) {
    // Interleave every method; only RunReflection consumes RNG (2 draws each).
    // Replay from the same seed -> identical sequence (lockstep guarantee).
    auto run = [](int seed) {
        EnemyAI14 e;
        e.SetSeed(seed);
        e.SetAwake(true);
        std::vector<float> trace;
        float atk = 0.0F;
        float end = 0.0F;
        for (int i = 0; i < 24; ++i) {
            trace.push_back(e.Scout() ? 1.0F : 0.0F);   // 0 draws
            const glm::vec2 d = e.RunReflection();       // 2 float draws
            trace.push_back(d.x);
            trace.push_back(d.y);
            e.ShootReflection(atk, end, 1.0F);           // 0 draws
            e.OnAtk();                                    // 0 draws
            e.EndAtk();                                   // 0 draws
            e.SetInertialVel(8.0F);
            e.FixedUpdateStep(0.9F);                      // 0 draws
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

TEST(EnemyAI14Test, DifferentSeedsDiverge) {
    EnemyAI14 a;
    EnemyAI14 b;
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
