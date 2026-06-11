#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/EnemyAI02.hpp"

using Game::EnemyAI02;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
float Len(const glm::vec2 &v) { return std::sqrt(v.x * v.x + v.y * v.y); }
} // namespace

// ---- Scout ----------------------------------------------------------------

TEST(EnemyAI02Test, ScoutRollInRange) {
    EnemyAI02 e;
    e.SetSeed(123);
    for (int i = 0; i < 128; ++i) {
        const int r = e.Scout();
        EXPECT_GE(r, 0);
        EXPECT_LT(r, EnemyAI02::kScoutRerollCeiling); // Range(0,10) max EXCL
    }
}

TEST(EnemyAI02Test, ScoutClearsTarget) {
    EnemyAI02 e;
    e.SetSeed(1);
    e.Scout();
    EXPECT_FALSE(e.HasTarget()); // target_obj = null (0x7C)
}

TEST(EnemyAI02Test, ScoutIsDeterministic) {
    EnemyAI02 a;
    EnemyAI02 b;
    a.SetSeed(7);
    b.SetSeed(7);
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(a.Scout(), b.Scout());
    }
}

TEST(EnemyAI02Test, ScoutGatedWhileDeadTakesNoDraw) {
    // Dead Scout must NOT advance the stream: a parallel live enemy that skips
    // the dead one's gated calls stays in lockstep.
    EnemyAI02 live;
    EnemyAI02 gated;
    live.SetSeed(99);
    gated.SetSeed(99);
    gated.SetDead(true);

    EXPECT_EQ(gated.Scout(), -1); // gated sentinel, no draw
    const int liveFirst = live.Scout();
    gated.SetDead(false);
    EXPECT_EQ(gated.Scout(), liveFirst); // first real draw matches the live one
}

TEST(EnemyAI02Test, ScoutGatedWhileDizzyTakesNoDraw) {
    EnemyAI02 e;
    e.SetSeed(5);
    e.SetDizzy(true);
    EXPECT_EQ(e.Scout(), -1); // gated, no draw
}

TEST(EnemyAI02Test, ScoutWithoutRngTakesNoDraw) {
    // The decomp guards the draw on rg_random (0x0C) != 0 (line 676548): a null
    // stream still clears the target but takes NO draw.
    EnemyAI02 noRng;
    EnemyAI02 withRng;
    noRng.SetSeed(77);
    withRng.SetSeed(77);
    noRng.SetHasRng(false);

    EXPECT_EQ(noRng.Scout(), -1);   // target cleared, no draw
    EXPECT_FALSE(noRng.HasTarget()); // but target_obj was still nulled (line 676547)
    // re-enable: its first real draw matches the never-gated stream.
    noRng.SetHasRng(true);
    EXPECT_EQ(noRng.Scout(), withRng.Scout());
}

// ---- RunReflection (wander) ----------------------------------------------

TEST(EnemyAI02Test, WanderIsNormalizedOrZero) {
    EnemyAI02 e;
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

TEST(EnemyAI02Test, WanderIsDeterministic) {
    EnemyAI02 a;
    EnemyAI02 b;
    a.SetSeed(31337);
    b.SetSeed(31337);
    for (int i = 0; i < 64; ++i) {
        const glm::vec2 da = a.RunReflection();
        const glm::vec2 db = b.RunReflection();
        EXPECT_FLOAT_EQ(da.x, db.x);
        EXPECT_FLOAT_EQ(da.y, db.y);
    }
}

TEST(EnemyAI02Test, WanderConsumesTwoDrawsInOrder) {
    // RunReflection draws Range(-1,1) twice; a reference stream drawing the same
    // two floats from the same seed must match component-for-component.
    EnemyAI02 e;
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

TEST(EnemyAI02Test, WanderHasNoGate) {
    // RunReflection has NO dead/dizzy gate in the decomp: it draws regardless.
    EnemyAI02 dead;
    EnemyAI02 live;
    dead.SetSeed(404);
    live.SetSeed(404);
    dead.SetDead(true);
    dead.SetDizzy(true);
    const glm::vec2 dd = dead.RunReflection();
    const glm::vec2 ld = live.RunReflection();
    EXPECT_FLOAT_EQ(dd.x, ld.x); // same two draws consumed despite dead+dizzy
    EXPECT_FLOAT_EQ(dd.y, ld.y);
}

// ---- ShootReflection ------------------------------------------------------

TEST(EnemyAI02Test, ShootShavesSpeedRate) {
    EnemyAI02 e;
    e.SetSeed(1);
    float speedRate = 1.0F;
    const bool fired = e.ShootReflection(speedRate);
    EXPECT_TRUE(fired);
    EXPECT_FLOAT_EQ(speedRate, 1.0F - EnemyAI02::kShootSpeedRatePenalty); // 0.5
}

TEST(EnemyAI02Test, ShootGatedWhileDeadLeavesSpeedRate) {
    EnemyAI02 e;
    e.SetSeed(1);
    e.SetDead(true);
    float speedRate = 1.0F;
    EXPECT_FALSE(e.ShootReflection(speedRate));
    EXPECT_FLOAT_EQ(speedRate, 1.0F); // untouched while dead
}

TEST(EnemyAI02Test, ShootGatedWhileDizzyLeavesSpeedRate) {
    EnemyAI02 e;
    e.SetSeed(1);
    e.SetDizzy(true);
    float speedRate = 1.0F;
    EXPECT_FALSE(e.ShootReflection(speedRate));
    EXPECT_FLOAT_EQ(speedRate, 1.0F); // untouched while dizzy
}

TEST(EnemyAI02Test, ShootTakesNoRngDraw) {
    // ShootReflection has no rg_random draws; the stream after a shot must equal
    // the stream of an enemy that never shot.
    EnemyAI02 shot;
    EnemyAI02 quiet;
    shot.SetSeed(456);
    quiet.SetSeed(456);
    float speedRate = 1.0F;
    shot.ShootReflection(speedRate);
    EXPECT_EQ(shot.Scout(), quiet.Scout()); // no extra draw consumed
}

// ---- FixedUpdate physics --------------------------------------------------

using StepResult = EnemyAI02::StepResult;

TEST(EnemyAI02Test, NotAwakeIsCompleteNoOp) {
    // awake (0x18) gate at line 676438: a not-awake step does NOTHING -- no
    // friction decay, no velocity write, no awake clear, regardless of state.
    EnemyAI02 e;
    e.SetInertialVel(10.0F); // knockback would otherwise decay
    e.SetDead(true);          // dead-stop would otherwise clear awake
    e.SetAwake(false);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Asleep);
    EXPECT_FLOAT_EQ(e.InertialVel(), 10.0F); // unchanged: no decay while asleep
    EXPECT_FALSE(e.Awake());                 // stays not awake (never set)
}

TEST(EnemyAI02Test, SteerWhenAliveAndLowInertia) {
    EnemyAI02 e;
    e.SetAwake(true);
    e.SetInertialVel(1.0F); // 1.0 is NOT > 1.0 -> steer branch
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Steer);
    EXPECT_FLOAT_EQ(e.InertialVel(), 1.0F); // NO decay on the steer path
    EXPECT_TRUE(e.Awake());                 // alive steer keeps awake
}

TEST(EnemyAI02Test, DeadStopClearsAwakeOnSteerPath) {
    // inertial_vel <= 1.0 AND dead -> awake cleared (line 676441), no decay.
    EnemyAI02 e;
    e.SetAwake(true);
    e.SetDead(true);
    e.SetInertialVel(0.5F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::DeadStop);
    EXPECT_FALSE(e.Awake());                 // awake = 0 (line 676441)
    EXPECT_FLOAT_EQ(e.InertialVel(), 0.5F);  // no friction decay on this path
}

TEST(EnemyAI02Test, DeadButHighInertiaTakesKnockbackNotDeadStop) {
    // The dead handling is nested INSIDE the inertial_vel <= 1.0 branch. With
    // inertial_vel > 1.0 the else (knockback) branch runs even while dead, and it
    // decays inertial_vel without clearing awake.
    EnemyAI02 e;
    e.SetAwake(true);
    e.SetDead(true);
    e.SetInertialVel(10.0F); // > 1.0 -> knockback else branch
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback);
    EXPECT_FLOAT_EQ(e.InertialVel(), 5.0F); // decayed by friction
    EXPECT_TRUE(e.Awake());                 // awake NOT cleared on knockback path
}

TEST(EnemyAI02Test, KnockbackDecaysByFrictionEachStep) {
    // inertial_vel > 1.0 -> knockback branch: inertial_vel *= friction (line
    // 676510). NO early return, but the pure outcome is decay each call.
    EnemyAI02 e;
    e.SetAwake(true);
    e.SetInertialVel(10.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback); // 10 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 5.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Knockback); // 5 > 1.0
    EXPECT_FLOAT_EQ(e.InertialVel(), 2.5F);
    // once it drops to <= 1.0 the steer branch takes over (no further decay).
    e.SetInertialVel(1.0F);
    EXPECT_EQ(e.FixedUpdateStep(0.5F), StepResult::Steer);
    EXPECT_FLOAT_EQ(e.InertialVel(), 1.0F);
}

// ---- OnGameStateChange (wake path) ----------------------------------------

TEST(EnemyAI02Test, WakeIgnoresNonOneGameState) {
    // Gate (lines 676702-676705): only gameState == 1 triggers the wake check.
    EnemyAI02 e;
    e.SetRoomReady(true); // room is ready, but the state is wrong
    EXPECT_FALSE(e.OnGameStateChange(0)); // gameState != 1
    EXPECT_FALSE(e.Awake());
    EXPECT_FALSE(e.OnGameStateChange(2));
    EXPECT_FALSE(e.Awake());
    EXPECT_FALSE(e.OnGameStateChange(-1));
    EXPECT_FALSE(e.Awake()); // never woke on a non-1 state
}

TEST(EnemyAI02Test, WakeGatedWhileTempEnemy) {
    // temp_enemy (0x19) != 0 gates the wake (line 676703: piVar1 != 0 -> return).
    EnemyAI02 e;
    e.SetTempEnemy(true);
    e.SetRoomReady(true);
    EXPECT_FALSE(e.OnGameStateChange(1)); // gameState==1 but temp_enemy true
    EXPECT_FALSE(e.Awake());              // awake stays false
}

TEST(EnemyAI02Test, WakeGatedWhileRoomNotReady) {
    // the_maker/the_room null or the_room.state(0x10) != 1 -> no wake (the owner
    // mirrors that whole chain into RoomReady; here it is false).
    EnemyAI02 e;
    e.SetRoomReady(false);
    EXPECT_FALSE(e.OnGameStateChange(1)); // gameState==1 but room not ready
    EXPECT_FALSE(e.Awake());              // awake stays false
}

TEST(EnemyAI02Test, WakeSetsAwakeWhenReady) {
    // gameState==1 && !temp_enemy && room-ready -> awake (0x18) = 1 (line 676713).
    EnemyAI02 e;
    e.SetTempEnemy(false);
    e.SetRoomReady(true);
    EXPECT_FALSE(e.Awake()); // starts not awake (gates FixedUpdate)
    EXPECT_TRUE(e.OnGameStateChange(1));
    EXPECT_TRUE(e.Awake()); // woke on the full gate chain (676702-676713)
}

TEST(EnemyAI02Test, WakeTakesNoRngDraw) {
    // OnGameStateChange consumes no rg_random draws on any path; the stream after
    // a wake must equal the stream of an enemy that never woke.
    EnemyAI02 woke;
    EnemyAI02 quiet;
    woke.SetSeed(909);
    quiet.SetSeed(909);
    woke.SetRoomReady(true);
    woke.OnGameStateChange(1);     // wakes, but takes no draw
    EXPECT_TRUE(woke.Awake());
    EXPECT_EQ(woke.Scout(), quiet.Scout()); // streams stayed in lockstep
}

// ---- Full interleaved replay determinism ----------------------------------

TEST(EnemyAI02Test, FullStreamReplayIsDeterministic) {
    // Interleave Scout + RunReflection (the only RNG-consuming methods) and
    // replay from the same seed -> identical sequence (lockstep guarantee).
    auto run = [](int seed) {
        EnemyAI02 e;
        e.SetSeed(seed);
        e.SetAwake(true);
        std::vector<float> trace;
        float speedRate = 1.0F;
        for (int i = 0; i < 24; ++i) {
            trace.push_back(static_cast<float>(e.Scout())); // 1 int draw
            const glm::vec2 d = e.RunReflection();          // 2 float draws
            trace.push_back(d.x);
            trace.push_back(d.y);
            e.ShootReflection(speedRate);                    // 0 draws
            e.FixedUpdateStep(0.9F);                          // 0 draws
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

TEST(EnemyAI02Test, DifferentSeedsDiverge) {
    EnemyAI02 a;
    EnemyAI02 b;
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
