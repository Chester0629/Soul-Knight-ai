#include <gtest/gtest.h>

#include <cmath>

#include "sim/BossController.hpp"

using Game::Sim::BossController;

// NOLINTBEGIN(readability-magic-numbers)

TEST(BossControllerTest, SpawnsWithBaseShootCd) {
    BossController b(/*baseShootCd=*/2.0F, glm::vec2{0.0F, 0.0F}, /*maxHp=*/600, 1);
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCdSeconds(), 2.0F);
    EXPECT_FLOAT_EQ(b.State().pos.x, 0.0F);
}

TEST(BossControllerTest, AngryHalvesShootCdOnceBelowHalfHp) {
    BossController b(2.0F, glm::vec2{0.0F, 0.0F}, 600, 1);
    b.OnHurt(400, 600);
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCdSeconds(), 2.0F);
    b.OnHurt(200, 600);
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCdSeconds(), 1.0F);
    b.OnHurt(50, 600);
    EXPECT_FLOAT_EQ(b.ShootCdSeconds(), 1.0F);
}

TEST(BossControllerTest, ChaseDirIsTowardPlayer) {
    BossController b(2.0F, glm::vec2{0.0F, 0.0F}, 600, 1);
    b.SetTarget(glm::vec2{10.0F, 0.0F});
    const glm::vec2 d = b.ChaseDir();
    EXPECT_NEAR(d.x, 1.0F, 1e-4F);
    EXPECT_NEAR(d.y, 0.0F, 1e-4F);
}

// NOLINTEND(readability-magic-numbers)
