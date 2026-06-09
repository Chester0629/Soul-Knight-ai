#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/EnemyAI01.hpp"
#include "sim/EnemyController.hpp"
#include "sim/FireIntent.hpp"
#include "sim/Scheduler.hpp"

using Game::Sim::EnemyController;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
EnemyController::Params MakeParams() {
    EnemyController::Params p;
    p.speed = 60.0F;
    p.speedRate = 0.0F;
    p.friction = 0.5F;
    p.shootCdSeconds = 1.0F;
    p.scoutRateSeconds = 0.5F;
    p.kinematic = false;
    return p;
}
} // namespace

TEST(EnemyControllerTest, SpawnsAtPositionAlive) {
    EnemyController e(MakeParams(), glm::vec2{100.0F, 50.0F}, 1234);
    EXPECT_FLOAT_EQ(e.State().pos.x, 100.0F);
    EXPECT_FLOAT_EQ(e.State().pos.y, 50.0F);
    EXPECT_FALSE(e.State().dead);
}

TEST(EnemyControllerTest, SteerVelocityIsMoveDirTimesSpeed) {
    EnemyController e(MakeParams(), glm::vec2{0.0F, 0.0F}, 1);
    e.SetMoveDir(glm::vec2{1.0F, 0.0F});
    const glm::vec2 v = e.ComputeVelocity();
    EXPECT_FLOAT_EQ(v.x, 60.0F);
    EXPECT_FLOAT_EQ(v.y, 0.0F);
}

TEST(EnemyControllerTest, KnockbackAddsForceThenDecays) {
    EnemyController e(MakeParams(), glm::vec2{0.0F, 0.0F}, 1);
    e.SetMoveDir(glm::vec2{0.0F, 0.0F});
    e.ApplyForce(glm::vec2{1.0F, 0.0F}, 10.0F);
    EXPECT_FLOAT_EQ(e.InertialVel(), 10.0F);
    const glm::vec2 v = e.ComputeVelocity();
    EXPECT_FLOAT_EQ(v.x, 10.0F);
    EXPECT_FLOAT_EQ(e.InertialVel(), 5.0F);
}

TEST(EnemyControllerTest, ForceIsCappedAt28) {
    EnemyController e(MakeParams(), glm::vec2{0.0F, 0.0F}, 1);
    e.ApplyForce(glm::vec2{0.0F, 1.0F}, 999.0F);
    EXPECT_FLOAT_EQ(e.InertialVel(), 28.0F);
}

TEST(EnemyControllerTest, KinematicIgnoresKnockbackTerm) {
    EnemyController::Params p = MakeParams();
    p.kinematic = true;
    EnemyController e(p, glm::vec2{0.0F, 0.0F}, 1);
    e.SetMoveDir(glm::vec2{0.0F, 0.0F});
    e.ApplyForce(glm::vec2{1.0F, 0.0F}, 20.0F);
    const glm::vec2 v = e.ComputeVelocity();
    EXPECT_FLOAT_EQ(v.x, 0.0F);
    EXPECT_FLOAT_EQ(v.y, 0.0F);
}

TEST(EnemyControllerTest, KnockbackSuppressedAtExactThreshold) {
    // The gate is strictly inertialVel > 1.0; exactly 1.0 must NOT apply knockback
    // and must NOT decay.
    EnemyController e(MakeParams(), glm::vec2{0.0F, 0.0F}, 1);
    e.SetMoveDir(glm::vec2{0.0F, 0.0F});
    e.ApplyForce(glm::vec2{1.0F, 0.0F}, 1.0F); // exactly at threshold
    const glm::vec2 v = e.ComputeVelocity();
    EXPECT_FLOAT_EQ(v.x, 0.0F);
    EXPECT_FLOAT_EQ(e.InertialVel(), 1.0F); // untouched (no decay)
}

TEST(EnemyControllerTest, SteerAndKnockbackCompose) {
    // The common case: a moving enemy that also takes knockback -- both terms add.
    EnemyController e(MakeParams(), glm::vec2{0.0F, 0.0F}, 1); // friction 0.5
    e.SetMoveDir(glm::vec2{1.0F, 0.0F});   // steer 60 px/s along +x
    e.ApplyForce(glm::vec2{0.0F, 1.0F}, 10.0F); // knockback 10 along +y
    const glm::vec2 v = e.ComputeVelocity();
    EXPECT_FLOAT_EQ(v.x, 60.0F);
    EXPECT_FLOAT_EQ(v.y, 10.0F);
}

TEST(EnemyControllerTest, DoubleComputeViolatesCallOnceContract) {
    // MISUSE DEMONSTRATION (not supported behaviour): ComputeVelocity is call-once
    // per tick (see its header contract). Calling it twice decays inertia twice;
    // this test exists to make that hazard visible. friction 0.5: 10 -> 5 -> 2.5.
    EnemyController e(MakeParams(), glm::vec2{0.0F, 0.0F}, 1);
    e.SetMoveDir(glm::vec2{0.0F, 0.0F});
    e.ApplyForce(glm::vec2{1.0F, 0.0F}, 10.0F);
    e.ComputeVelocity();
    e.ComputeVelocity();
    EXPECT_FLOAT_EQ(e.InertialVel(), 2.5F);
}

TEST(EnemyControllerTest, ScoutTickAdvancesStreamAndPicksWanderDir) {
    EnemyController::Params p;
    p.scoutRateSeconds = 0.02F; // 1 tick
    EnemyController e(p, glm::vec2{0.0F, 0.0F}, 777);
    Game::Sim::Scheduler sched;
    std::vector<Game::Sim::FireIntent> fire;
    e.MutableState().awake = true;
    e.Activate(sched, fire);

    Game::EnemyAI01 ref;
    ref.SetSeed(777);

    for (int i = 0; i < 4; ++i) {
        sched.Tick();
        ref.Scout();
        const glm::vec2 refDir = ref.RunReflection();
        EXPECT_FLOAT_EQ(e.MoveDir().x, refDir.x);
        EXPECT_FLOAT_EQ(e.MoveDir().y, refDir.y);
    }
}

TEST(EnemyControllerTest, ScoutTickNoOpWhileNotAwake) {
    // The awake gate: an Activated-but-not-awake enemy must take NO scout draw and
    // leave its move dir at zero, keeping its RNG stream pristine.
    EnemyController::Params p;
    p.scoutRateSeconds = 0.02F; // 1 tick
    EnemyController e(p, glm::vec2{0.0F, 0.0F}, 777);
    Game::Sim::Scheduler sched;
    std::vector<Game::Sim::FireIntent> fire;
    // NOTE: awake left false.
    e.Activate(sched, fire);

    Game::EnemyAI01 ref; // never drawn
    ref.SetSeed(777);
    for (int i = 0; i < 4; ++i) {
        sched.Tick();
    }
    EXPECT_FLOAT_EQ(e.MoveDir().x, 0.0F);
    EXPECT_FLOAT_EQ(e.MoveDir().y, 0.0F);
    EXPECT_EQ(e.Brain().Rng().Range(0, 1000), ref.Rng().Range(0, 1000)); // unadvanced
}

TEST(EnemyControllerTest, ShootTickEmitsAimedFireIntentOnCadence) {
    EnemyController::Params p;
    p.shootCdSeconds = 0.04F;     // 2 ticks
    p.scoutRateSeconds = 100.0F;  // keep scout out of the way
    EnemyController e(p, glm::vec2{0.0F, 0.0F}, 5);
    Game::Sim::Scheduler sched;
    std::vector<Game::Sim::FireIntent> fire;
    e.MutableState().awake = true;
    e.SetTarget(glm::vec2{10.0F, 0.0F});
    e.Activate(sched, fire);

    sched.Tick(); // tick 1: nothing (shoot due at tick 2)
    EXPECT_TRUE(fire.empty());
    sched.Tick(); // tick 2: shoot fires
    ASSERT_EQ(fire.size(), 1U);
    EXPECT_EQ(fire[0].pattern, Game::Sim::FirePattern::Single);
    EXPECT_EQ(fire[0].camp, 1);
    EXPECT_NEAR(fire[0].dir.x, 1.0F, 1e-4F);
    EXPECT_NEAR(fire[0].dir.y, 0.0F, 1e-4F);
    sched.Tick();
    sched.Tick(); // tick 4: re-scheduled shot fires again
    EXPECT_EQ(fire.size(), 2U);
}

TEST(EnemyControllerTest, DeadEnemyEmitsNoFireAndTakesNoDraw) {
    EnemyController::Params p;
    p.shootCdSeconds = 0.02F;
    p.scoutRateSeconds = 0.02F;
    EnemyController e(p, glm::vec2{0.0F, 0.0F}, 9);
    Game::Sim::Scheduler sched;
    std::vector<Game::Sim::FireIntent> fire;
    e.MutableState().awake = true;
    e.Activate(sched, fire);
    e.Kill();

    Game::EnemyAI01 ref;
    ref.SetSeed(9);
    for (int i = 0; i < 8; ++i) {
        sched.Tick();
    }
    EXPECT_TRUE(fire.empty());
    EXPECT_EQ(e.Brain().Rng().Range(0, 1000), ref.Rng().Range(0, 1000));
}

TEST(EnemyControllerTest, FullCadenceReplayIsDeterministic) {
    auto run = [](int seed) {
        EnemyController::Params p;
        p.shootCdSeconds = 0.06F;
        p.scoutRateSeconds = 0.04F;
        EnemyController e(p, glm::vec2{0.0F, 0.0F}, seed);
        Game::Sim::Scheduler sched;
        std::vector<Game::Sim::FireIntent> fire;
        e.MutableState().awake = true;
        e.SetTarget(glm::vec2{5.0F, 5.0F});
        e.Activate(sched, fire);
        std::vector<float> trace;
        for (int i = 0; i < 20; ++i) {
            sched.Tick();
            const glm::vec2 v = e.ComputeVelocity();
            trace.push_back(v.x);
            trace.push_back(v.y);
            trace.push_back(static_cast<float>(fire.size()));
        }
        return trace;
    };
    const auto a = run(2024);
    const auto b = run(2024);
    EXPECT_EQ(a, b);
}

// NOLINTEND(readability-magic-numbers)
