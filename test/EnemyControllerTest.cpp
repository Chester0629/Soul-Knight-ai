#include <gtest/gtest.h>

#include <cmath>

#include "sim/EnemyController.hpp"

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

TEST(EnemyControllerTest, DoubleComputeDecaysTwice) {
    // Documents the call-once contract (see ComputeVelocity doc): a second call in
    // the same tick decays again. friction 0.5: 10 -> 5 -> 2.5.
    EnemyController e(MakeParams(), glm::vec2{0.0F, 0.0F}, 1);
    e.SetMoveDir(glm::vec2{0.0F, 0.0F});
    e.ApplyForce(glm::vec2{1.0F, 0.0F}, 10.0F);
    e.ComputeVelocity();
    e.ComputeVelocity();
    EXPECT_FLOAT_EQ(e.InertialVel(), 2.5F);
}

// NOLINTEND(readability-magic-numbers)
