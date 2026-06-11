#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/EnemyAI13.hpp"

using Game::EnemyAI13;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
float Len(const glm::vec2 &v) { return std::sqrt(v.x * v.x + v.y * v.y); }
} // namespace

// ---- Scout ----------------------------------------------------------------

TEST(EnemyAI13Test, ScoutClearsTarget) {
    EnemyAI13 e;
    e.SetSeed(1);
    e.Scout();
    EXPECT_FALSE(e.HasTarget()); // target_obj = null (0x7C)
}

TEST(EnemyAI13Test, ScoutTakesNoRngDraw) {
    // Scout has no rg_random draw; the stream after a Scout must equal the stream
    // of an enemy that never scouted (lockstep).
    EnemyAI13 scouted;
    EnemyAI13 quiet;
    scouted.SetSeed(321);
    quiet.SetSeed(321);
    scouted.SetCanHit(true);
    quiet.SetCanHit(true);
    scouted.Scout();
    glm::vec2 a{0.0F, 0.0F};
    glm::vec2 b{0.0F, 0.0F};
    EXPECT_TRUE(scouted.RunReflection(a));
    EXPECT_TRUE(quiet.RunReflection(b));
    EXPECT_FLOAT_EQ(a.x, b.x);
    EXPECT_FLOAT_EQ(a.y, b.y);
}

// ---- RunReflection (wander) ----------------------------------------------

TEST(EnemyAI13Test, RunReflectionGatedWhenNotCanHit) {
    // can_hit (0xAC) == 0 -> Invoke + return, NO draw taken.
    EnemyAI13 e;
    e.SetSeed(5);
    glm::vec2 out{9.0F, 9.0F};
    EXPECT_FALSE(e.RunReflection(out)); // gated path
    // out untouched, move_direction untouched (still origin).
    EXPECT_FLOAT_EQ(out.x, 9.0F);
    EXPECT_FLOAT_EQ(out.y, 9.0F);
    EXPECT_FLOAT_EQ(e.MoveDirection().x, 0.0F);
    EXPECT_FLOAT_EQ(e.MoveDirection().y, 0.0F);
}

TEST(EnemyAI13Test, RunReflectionGatedTakesNoDraw) {
    // A gated (can_hit == 0) RunReflection must NOT advance the stream: a parallel
    // active enemy stays in lockstep across the gated one's skipped draws.
    EnemyAI13 gated;
    EnemyAI13 live;
    gated.SetSeed(99);
    live.SetSeed(99);

    glm::vec2 dummy{0.0F, 0.0F};
    EXPECT_FALSE(gated.RunReflection(dummy)); // gated, no draw

    // live (can_hit) draws; then un-gate gated -> its first real draw must match.
    live.SetCanHit(true);
    glm::vec2 liveDir{0.0F, 0.0F};
    EXPECT_TRUE(live.RunReflection(liveDir));

    gated.SetCanHit(true);
    glm::vec2 gatedDir{0.0F, 0.0F};
    EXPECT_TRUE(gated.RunReflection(gatedDir));
    EXPECT_FLOAT_EQ(gatedDir.x, liveDir.x);
    EXPECT_FLOAT_EQ(gatedDir.y, liveDir.y);
}

TEST(EnemyAI13Test, WanderIsNormalizedOrZero) {
    EnemyAI13 e;
    e.SetSeed(2024);
    e.SetCanHit(true);
    for (int i = 0; i < 128; ++i) {
        glm::vec2 d{0.0F, 0.0F};
        EXPECT_TRUE(e.RunReflection(d));
        const float len = Len(d);
        const bool unit = std::fabs(len - 1.0F) < 1e-4F;
        const bool zero = len < 1e-6F;
        EXPECT_TRUE(unit || zero);        // normalized direction (or degenerate zero)
        EXPECT_EQ(d, e.MoveDirection());  // stored as move_direction (0x74)
    }
}

TEST(EnemyAI13Test, WanderIsDeterministic) {
    EnemyAI13 a;
    EnemyAI13 b;
    a.SetSeed(31337);
    b.SetSeed(31337);
    a.SetCanHit(true);
    b.SetCanHit(true);
    for (int i = 0; i < 64; ++i) {
        glm::vec2 da{0.0F, 0.0F};
        glm::vec2 db{0.0F, 0.0F};
        EXPECT_TRUE(a.RunReflection(da));
        EXPECT_TRUE(b.RunReflection(db));
        EXPECT_FLOAT_EQ(da.x, db.x);
        EXPECT_FLOAT_EQ(da.y, db.y);
    }
}

TEST(EnemyAI13Test, WanderConsumesTwoDrawsInOrder) {
    // RunReflection draws Range(-1,1) twice; a reference stream drawing the same
    // two floats from the same seed must match component-for-component.
    EnemyAI13 e;
    Game::RGRandom ref;
    e.SetSeed(808);
    e.SetCanHit(true);
    ref.SetRandomSeed(808);

    const float rx = ref.Range(-1.0F, 1.0F);
    const float ry = ref.Range(-1.0F, 1.0F);
    const float len = std::sqrt(rx * rx + ry * ry);
    const glm::vec2 expected =
        len > 0.0F ? glm::vec2(rx / len, ry / len) : glm::vec2(0.0F, 0.0F);

    glm::vec2 d{0.0F, 0.0F};
    EXPECT_TRUE(e.RunReflection(d));
    EXPECT_FLOAT_EQ(d.x, expected.x);
    EXPECT_FLOAT_EQ(d.y, expected.y);
}

// ---- ChildDead (detonate-on-death latch) ----------------------------------

TEST(EnemyAI13Test, ChildDeadFirstCallLatchesAndDetonates) {
    EnemyAI13 e;
    EXPECT_FALSE(e.BoomLight());
    const EnemyAI13::ChildDeadResult r = e.ChildDead(true);
    EXPECT_TRUE(r.latched);
    EXPECT_TRUE(r.detonateBoom);        // boom (0xB0) always
    EXPECT_TRUE(r.detonateSecondBoom);  // secondBoom (0xB4) present
    EXPECT_TRUE(e.BoomLight());         // latched (0xAD = 1)
}

TEST(EnemyAI13Test, ChildDeadWithoutSecondBoomDetonatesOnlyFirst) {
    EnemyAI13 e;
    const EnemyAI13::ChildDeadResult r = e.ChildDead(false);
    EXPECT_TRUE(r.latched);
    EXPECT_TRUE(r.detonateBoom);
    EXPECT_FALSE(r.detonateSecondBoom); // secondBoom absent -> not detonated
}

TEST(EnemyAI13Test, ChildDeadIsOnceOnly) {
    EnemyAI13 e;
    const EnemyAI13::ChildDeadResult first = e.ChildDead(true);
    EXPECT_TRUE(first.latched);

    // Re-entry: boom_light already set -> nothing detonates, nothing latches.
    const EnemyAI13::ChildDeadResult second = e.ChildDead(true);
    EXPECT_FALSE(second.latched);
    EXPECT_FALSE(second.detonateBoom);
    EXPECT_FALSE(second.detonateSecondBoom);
    EXPECT_TRUE(e.BoomLight()); // still latched
}

TEST(EnemyAI13Test, ChildDeadTakesNoRngDraw) {
    EnemyAI13 detonated;
    EnemyAI13 quiet;
    detonated.SetSeed(77);
    quiet.SetSeed(77);
    detonated.SetCanHit(true);
    quiet.SetCanHit(true);
    detonated.ChildDead(true);
    glm::vec2 a{0.0F, 0.0F};
    glm::vec2 b{0.0F, 0.0F};
    EXPECT_TRUE(detonated.RunReflection(a));
    EXPECT_TRUE(quiet.RunReflection(b));
    EXPECT_FLOAT_EQ(a.x, b.x); // no extra draw consumed by ChildDead
    EXPECT_FLOAT_EQ(a.y, b.y);
}

// ---- FixedUpdate physics --------------------------------------------------

using StepResult = EnemyAI13::StepResult;

TEST(EnemyAI13Test, NotAwakeIsCompleteNoOp) {
    // awake (0x18) gate at line 680267: a not-awake step does NOTHING -- no
    // friction decay, regardless of any other state.
    EnemyAI13 e;
    e.SetInertialVel(10.0F); // knockback would otherwise decay
    e.SetDead(true);          // dead-stop would otherwise apply
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Asleep);
    EXPECT_FLOAT_EQ(e.InertialVel(), 10.0F); // unchanged: no decay while asleep
    EXPECT_FALSE(e.Awake());                  // stayed asleep
}

TEST(EnemyAI13Test, AwakeAliveLowInertiaSteers) {
    EnemyAI13 e;
    e.SetAwake(true);
    e.SetInertialVel(1.0F); // 1.0 is NOT > 1.0 -> not knockback; alive -> steer
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Steer);
    EXPECT_FLOAT_EQ(e.InertialVel(), 1.0F); // no decay on the steer path
    EXPECT_TRUE(e.Awake());                  // stays awake
}

TEST(EnemyAI13Test, AwakeDeadLowInertiaSleepsAndStops) {
    // inertial_vel <= 1.0 AND dead -> awake = 0 (line 680270), return, NO decay.
    EnemyAI13 e;
    e.SetAwake(true);
    e.SetDead(true);
    e.SetInertialVel(0.5F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::DeadStop);
    EXPECT_FALSE(e.Awake());                 // awake set to 0
    EXPECT_FLOAT_EQ(e.InertialVel(), 0.5F); // no decay on the dead-stop path
}

TEST(EnemyAI13Test, KnockbackTakesPriorityOverDead) {
    // The split is on inertial_vel FIRST (line 680268). inertial_vel > 1.0 enters
    // the knockback branch even for a dead enemy: it decays, it does NOT dead-stop.
    EnemyAI13 e;
    e.SetAwake(true);
    e.SetDead(true);
    e.SetInertialVel(10.0F); // > 1.0 -> knockback branch regardless of dead
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback);
    EXPECT_FLOAT_EQ(e.InertialVel(), 5.0F); // decayed by friction
    EXPECT_TRUE(e.Awake());                  // knockback branch does not sleep
}

TEST(EnemyAI13Test, KnockbackActiveDecaysByFriction) {
    // not knockback boundary: inertial_vel > 1.0 -> decay by friction (line 680339).
    EnemyAI13 e;
    e.SetAwake(true);
    e.SetInertialVel(10.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback); // 10 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 5.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback); // 5 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 2.5F);
}

TEST(EnemyAI13Test, KnockbackBoundaryIsExclusive) {
    // inertial_vel == 1.0 is NOT > 1.0 -> not the knockback branch (steer if alive).
    EnemyAI13 e;
    e.SetAwake(true);
    e.SetInertialVel(1.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Steer);
    EXPECT_FLOAT_EQ(e.InertialVel(), 1.0F); // no decay at/below threshold
}

TEST(EnemyAI13Test, FixedUpdateTakesNoRngDraw) {
    // FixedUpdate has zero rg_random draws; the stream is unaffected.
    EnemyAI13 stepped;
    EnemyAI13 quiet;
    stepped.SetSeed(2718);
    quiet.SetSeed(2718);
    stepped.SetCanHit(true);
    quiet.SetCanHit(true);
    stepped.SetAwake(true);
    stepped.SetInertialVel(10.0F);
    stepped.FixedUpdateStep(0.5F);
    stepped.FixedUpdateStep(0.5F);
    glm::vec2 a{0.0F, 0.0F};
    glm::vec2 b{0.0F, 0.0F};
    EXPECT_TRUE(stepped.RunReflection(a));
    EXPECT_TRUE(quiet.RunReflection(b));
    EXPECT_FLOAT_EQ(a.x, b.x);
    EXPECT_FLOAT_EQ(a.y, b.y);
}

// ---- Full interleaved replay determinism ----------------------------------

TEST(EnemyAI13Test, FullStreamReplayIsDeterministic) {
    // Only RunReflection consumes RNG (and only on the can_hit path). Interleave
    // Scout / ChildDead / FixedUpdate (all 0-draw) with RunReflection and replay
    // from the same seed -> identical sequence (lockstep guarantee).
    auto run = [](int seed) {
        EnemyAI13 e;
        e.SetSeed(seed);
        e.SetCanHit(true);
        e.SetAwake(true);
        std::vector<float> trace;
        for (int i = 0; i < 24; ++i) {
            e.Scout();                       // 0 draws
            glm::vec2 d{0.0F, 0.0F};
            e.RunReflection(d);              // 2 float draws
            trace.push_back(d.x);
            trace.push_back(d.y);
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

TEST(EnemyAI13Test, DifferentSeedsDiverge) {
    EnemyAI13 a;
    EnemyAI13 b;
    a.SetSeed(1);
    b.SetSeed(424242);
    a.SetCanHit(true);
    b.SetCanHit(true);
    bool diverged = false;
    for (int i = 0; i < 64; ++i) {
        glm::vec2 da{0.0F, 0.0F};
        glm::vec2 db{0.0F, 0.0F};
        a.RunReflection(da);
        b.RunReflection(db);
        if (da.x != db.x || da.y != db.y) {
            diverged = true;
        }
    }
    EXPECT_TRUE(diverged);
}

// NOLINTEND(readability-magic-numbers)
