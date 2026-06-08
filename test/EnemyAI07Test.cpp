#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/EnemyAI07.hpp"

using Game::EnemyAI07;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
float Len(const glm::vec2 &v) { return std::sqrt(v.x * v.x + v.y * v.y); }
} // namespace

// ---- ctor -----------------------------------------------------------------

TEST(EnemyAI07Test, CtorSetsNeedTap) {
    EnemyAI07 e; // EnemyAI07___ctor: need_tap (0xAC) = 1
    EXPECT_TRUE(e.NeedTap());
    EXPECT_FALSE(e.InAtk1()); // 0xAD default 0
}

// ---- Scout ----------------------------------------------------------------

TEST(EnemyAI07Test, ScoutRollInRange) {
    EnemyAI07 e;
    e.SetSeed(123);
    for (int i = 0; i < 128; ++i) {
        const int r = e.Scout();
        EXPECT_GE(r, 0);
        EXPECT_LT(r, EnemyAI07::kScoutRerollCeiling); // Range(0,10) max EXCL
    }
}

TEST(EnemyAI07Test, ScoutClearsTarget) {
    EnemyAI07 e;
    e.SetSeed(1);
    e.Scout();
    EXPECT_FALSE(e.HasTarget()); // target_obj = null
}

TEST(EnemyAI07Test, ScoutIsDeterministic) {
    EnemyAI07 a;
    EnemyAI07 b;
    a.SetSeed(7);
    b.SetSeed(7);
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(a.Scout(), b.Scout());
    }
}

TEST(EnemyAI07Test, ScoutGatedWhileDeadTakesNoDraw) {
    // Dead Scout must NOT advance the stream: a parallel live enemy that skips
    // the dead one's gated calls stays in lockstep.
    EnemyAI07 live;
    EnemyAI07 gated;
    live.SetSeed(99);
    gated.SetSeed(99);
    gated.SetDead(true);

    EXPECT_EQ(gated.Scout(), -1); // gated sentinel, no draw
    const int liveFirst = live.Scout();
    gated.SetDead(false);
    EXPECT_EQ(gated.Scout(), liveFirst);
}

TEST(EnemyAI07Test, ScoutGatedWhileDizzyTakesNoDraw) {
    EnemyAI07 e;
    e.SetSeed(5);
    e.SetDizzy(true);
    EXPECT_EQ(e.Scout(), -1); // gated, no draw
}

// ---- ShootReflection (attack-selection roll) ------------------------------

TEST(EnemyAI07Test, ShootRollInRange) {
    EnemyAI07 e;
    e.SetSeed(321);
    for (int i = 0; i < 128; ++i) {
        const int r = e.ShootReflection();
        EXPECT_GE(r, 0);
        EXPECT_LT(r, EnemyAI07::kAttackRollCeiling); // Range(0,100) max EXCL
    }
}

TEST(EnemyAI07Test, ShootRollIsDeterministic) {
    EnemyAI07 a;
    EnemyAI07 b;
    a.SetSeed(2024);
    b.SetSeed(2024);
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(a.ShootReflection(), b.ShootReflection());
    }
}

TEST(EnemyAI07Test, ShootGatedWhileDeadTakesNoDraw) {
    EnemyAI07 live;
    EnemyAI07 gated;
    live.SetSeed(55);
    gated.SetSeed(55);
    gated.SetDead(true);

    EXPECT_EQ(gated.ShootReflection(), -1); // gated, no draw
    const int liveFirst = live.ShootReflection();
    gated.SetDead(false);
    EXPECT_EQ(gated.ShootReflection(), liveFirst); // stayed in lockstep
}

TEST(EnemyAI07Test, ShootGatedWhileDizzyTakesNoDraw) {
    EnemyAI07 e;
    e.SetSeed(5);
    e.SetDizzy(true);
    EXPECT_EQ(e.ShootReflection(), -1); // gated, no draw
}

TEST(EnemyAI07Test, ShootRollMatchesReferenceStream) {
    // The attack-selection roll is exactly rg_random.Range(0, 100); a reference
    // stream from the same seed must produce the identical roll.
    EnemyAI07 e;
    Game::RGRandom ref;
    e.SetSeed(909);
    ref.SetRandomSeed(909);
    EXPECT_EQ(e.ShootReflection(), ref.Range(0, 100));
}

// ---- RunReflection (wander) ----------------------------------------------

TEST(EnemyAI07Test, WanderIsNormalizedOrZero) {
    EnemyAI07 e;
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

TEST(EnemyAI07Test, WanderIsDeterministic) {
    EnemyAI07 a;
    EnemyAI07 b;
    a.SetSeed(31337);
    b.SetSeed(31337);
    for (int i = 0; i < 64; ++i) {
        const glm::vec2 da = a.RunReflection();
        const glm::vec2 db = b.RunReflection();
        EXPECT_FLOAT_EQ(da.x, db.x);
        EXPECT_FLOAT_EQ(da.y, db.y);
    }
}

TEST(EnemyAI07Test, WanderConsumesTwoDrawsInOrder) {
    // RunReflection draws Range(-1,1) twice; a reference stream drawing the same
    // two floats from the same seed must match component-for-component.
    EnemyAI07 e;
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

TEST(EnemyAI07Test, WanderIsNotGatedByDeadOrDizzy) {
    // Unlike Scout/ShootReflection, RunReflection has NO dead/dizzy gate: it
    // draws unconditionally. A dead-but-still-ticked enemy must consume the same
    // two draws as a live one (lockstep).
    EnemyAI07 alive;
    EnemyAI07 deadDizzy;
    alive.SetSeed(4242);
    deadDizzy.SetSeed(4242);
    deadDizzy.SetDead(true);
    deadDizzy.SetDizzy(true);

    const glm::vec2 a = alive.RunReflection();
    const glm::vec2 d = deadDizzy.RunReflection();
    EXPECT_FLOAT_EQ(a.x, d.x);
    EXPECT_FLOAT_EQ(a.y, d.y);
}

// ---- Atk1 -----------------------------------------------------------------

TEST(EnemyAI07Test, Atk1LatchesInAtk1AndStops) {
    EnemyAI07 e;
    e.SetSeed(1);
    e.RunReflection(); // give move_direction a non-zero value first
    EXPECT_FALSE(e.InAtk1());
    e.Atk1();
    EXPECT_TRUE(e.InAtk1());                 // 0xAD = 1
    EXPECT_FLOAT_EQ(e.MoveDirection().x, 0.0F); // Vector2.zero
    EXPECT_FLOAT_EQ(e.MoveDirection().y, 0.0F);
}

TEST(EnemyAI07Test, Atk1TakesNoRngDraw) {
    // Atk1 has no rg_random draws; the stream after Atk1 must equal that of an
    // enemy that never called Atk1.
    EnemyAI07 attacked;
    EnemyAI07 quiet;
    attacked.SetSeed(456);
    quiet.SetSeed(456);
    attacked.Atk1();
    EXPECT_EQ(attacked.Scout(), quiet.Scout()); // no extra draw consumed
}

// ---- GetForce gate --------------------------------------------------------

TEST(EnemyAI07Test, GetForceGateBlockedDuringAtk1) {
    EnemyAI07 e;
    e.SetSeed(1);
    EXPECT_TRUE(e.GetForceGate()); // !in_atk1 -> base GetForce runs
    e.Atk1();
    EXPECT_FALSE(e.GetForceGate()); // in_atk1 -> base GetForce suppressed
}

// ---- Atk2 gate ------------------------------------------------------------

TEST(EnemyAI07Test, Atk2GateRequiresAliveAndNotDizzy) {
    EnemyAI07 e;
    EXPECT_TRUE(e.Atk2Active()); // !dead && !dizzy
    e.SetDizzy(true);
    EXPECT_FALSE(e.Atk2Active());
    e.SetDizzy(false);
    e.SetDead(true);
    EXPECT_FALSE(e.Atk2Active());
}

// ---- FixedUpdate physics --------------------------------------------------

using StepResult = EnemyAI07::StepResult;

TEST(EnemyAI07Test, NotAwakeIsCompleteNoOp) {
    // awake (0x18) gate at line 677751: a not-awake step does NOTHING -- no
    // friction decay and no awake/velocity change, regardless of other state.
    EnemyAI07 e;
    e.SetInertialVel(10.0F); // knockback would otherwise decay
    e.SetDead(true);          // dead branch would otherwise fire
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Asleep);
    EXPECT_FLOAT_EQ(e.InertialVel(), 10.0F); // unchanged: no decay while asleep
}

TEST(EnemyAI07Test, DeadBranchClearsAwakeWhenInertialLow) {
    // awake && inertial_vel <= 1.0 && dead -> awake = 0 (line 677754), velocity
    // zeroed, return. No friction decay.
    EnemyAI07 e;
    e.SetAwake(true);
    e.SetDead(true);
    e.SetInertialVel(0.5F); // <= 1.0 -> non-knockback path, dead sub-branch
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Dead);
    EXPECT_FALSE(e.Awake());                  // awake cleared
    EXPECT_FLOAT_EQ(e.InertialVel(), 0.5F);   // no decay on the dead path
}

TEST(EnemyAI07Test, SteerWhenAwakeAliveAndInertialLow) {
    EnemyAI07 e;
    e.SetAwake(true);
    e.SetInertialVel(1.0F); // 1.0 is <= 1.0 -> non-knockback, not dead -> steer
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Steer);
    EXPECT_TRUE(e.Awake());                  // awake untouched on steer path
    EXPECT_FLOAT_EQ(e.InertialVel(), 1.0F);  // no decay on steer path
}

TEST(EnemyAI07Test, KnockbackActiveDecaysAndReportsKnockback) {
    // awake && inertial_vel > 1.0 -> knockback replaces steering and decays by
    // friction (line 677823). Independent of dead (the > 1.0 branch has no dead
    // check).
    EnemyAI07 e;
    e.SetAwake(true);
    e.SetInertialVel(10.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback); // 10 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 5.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback); // 5 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 2.5F);
}

TEST(EnemyAI07Test, KnockbackIgnoresDeadFlag) {
    // The > 1.0 branch has no dead sub-check: a dead enemy with high inertial_vel
    // still takes the Knockback path (and decays), NOT the Dead path.
    EnemyAI07 e;
    e.SetAwake(true);
    e.SetDead(true);
    e.SetInertialVel(4.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback);
    EXPECT_TRUE(e.Awake()); // awake NOT cleared on the knockback path
    EXPECT_FLOAT_EQ(e.InertialVel(), 2.0F);
}

// ---- Full interleaved replay determinism ----------------------------------

TEST(EnemyAI07Test, FullStreamReplayIsDeterministic) {
    // Interleave Scout + RunReflection + ShootReflection (the RNG-consuming
    // methods) and replay from the same seed -> identical sequence.
    auto run = [](int seed) {
        EnemyAI07 e;
        e.SetSeed(seed);
        std::vector<float> trace;
        for (int i = 0; i < 24; ++i) {
            trace.push_back(static_cast<float>(e.Scout()));          // 1 int draw
            const glm::vec2 d = e.RunReflection();                   // 2 float draws
            trace.push_back(d.x);
            trace.push_back(d.y);
            trace.push_back(static_cast<float>(e.ShootReflection())); // 1 int draw
            e.Atk1();                                                 // 0 draws
            e.FixedUpdateStep(0.9F);                                  // 0 draws
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

TEST(EnemyAI07Test, DifferentSeedsDiverge) {
    EnemyAI07 a;
    EnemyAI07 b;
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
