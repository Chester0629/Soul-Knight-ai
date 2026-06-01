#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include "combat/EnemyAI.hpp"

using Game::AIState;
using Game::EnemyAI;
using Game::EnemyDef;

namespace {
EnemyDef MakeEnemy(float shootCdSeconds) {
    EnemyDef d;
    d.shootCd = shootCdSeconds;
    return d;
}
} // namespace

// NOLINTBEGIN(readability-magic-numbers)

TEST(EnemyAITest, IdleWhenPlayerFar) {
    const auto def = MakeEnemy(1.0F);
    EnemyAI ai(def, 100.0F, 30.0F);
    const auto d = ai.Update(16.0F, glm::vec2(0.0F, 0.0F), glm::vec2(500.0F, 0.0F));
    EXPECT_EQ(d.state, AIState::Idle);
    EXPECT_FALSE(d.shouldShoot);
    EXPECT_FLOAT_EQ(d.moveDir.x, 0.0F);
}

TEST(EnemyAITest, ChasesTowardPlayerInDetectRange) {
    const auto def = MakeEnemy(1.0F);
    EnemyAI ai(def, 100.0F, 30.0F);
    const auto d = ai.Update(16.0F, glm::vec2(0.0F, 0.0F), glm::vec2(60.0F, 0.0F));
    EXPECT_EQ(d.state, AIState::Chase);
    EXPECT_NEAR(d.moveDir.x, 1.0F, 1e-4F); // unit vector toward +x
    EXPECT_NEAR(d.moveDir.y, 0.0F, 1e-4F);
    EXPECT_FALSE(d.shouldShoot);
}

TEST(EnemyAITest, AttacksAndShootsWhenClose) {
    const auto def = MakeEnemy(1.0F);
    EnemyAI ai(def, 100.0F, 30.0F);
    const auto d = ai.Update(16.0F, glm::vec2(0.0F, 0.0F), glm::vec2(20.0F, 0.0F));
    EXPECT_EQ(d.state, AIState::Attack);
    EXPECT_TRUE(d.shouldShoot); // first frame in range fires
    EXPECT_FLOAT_EQ(d.moveDir.x, 0.0F); // holds position
}

TEST(EnemyAITest, ShootRespectsCooldown) {
    const auto def = MakeEnemy(1.0F); // 1 second between shots
    EnemyAI ai(def, 100.0F, 30.0F);
    const glm::vec2 self(0.0F, 0.0F);
    const glm::vec2 player(20.0F, 0.0F);

    EXPECT_TRUE(ai.Update(16.0F, self, player).shouldShoot);  // fires
    EXPECT_FALSE(ai.Update(16.0F, self, player).shouldShoot); // still cooling
    EXPECT_TRUE(ai.Update(1000.0F, self, player).shouldShoot); // 1s later, fires
}

// NOLINTEND(readability-magic-numbers)
