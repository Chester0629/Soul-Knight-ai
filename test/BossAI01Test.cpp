#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/BossAI01.hpp"

using Game::BossAI01;

// NOLINTBEGIN(readability-magic-numbers)

TEST(BossAI01Test, EntersAngryBelowHalfHp) {
    BossAI01 b(2.0F); // boss01 base shoot_cd = 2
    b.OnHurt(301, 600); // > 50% -> still calm
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 2.0F);
    EXPECT_FLOAT_EQ(b.AnimSpeed(), 1.0F);

    b.OnHurt(299, 600); // < 50% -> angry
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 1.0F);    // halved
    EXPECT_FLOAT_EQ(b.AnimSpeed(), 1.2F);
}

TEST(BossAI01Test, AngryTransitionHappensOnce) {
    BossAI01 b(2.0F);
    b.OnHurt(100, 600); // angry, shoot_cd 1.0
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 1.0F);
    b.OnHurt(10, 600);  // still below 50%, must NOT halve again
    EXPECT_FLOAT_EQ(b.ShootCd(), 1.0F);
}

TEST(BossAI01Test, ExactlyHalfIsNotAngry) {
    BossAI01 b(2.0F);
    b.OnHurt(300, 600); // 0.5 is not < 0.5
    EXPECT_FALSE(b.Angry());
}

TEST(BossAI01Test, ChooseAttackInRangeAndDeterministic) {
    BossAI01 a(2.0F);
    BossAI01 b(2.0F);
    a.SetSeed(4242);
    b.SetSeed(4242);
    for (int i = 0; i < 64; ++i) {
        const int ia = a.ChooseAttack();
        const int ib = b.ChooseAttack();
        EXPECT_EQ(ia, ib); // deterministic
        EXPECT_GE(ia, 0);
        EXPECT_LT(ia, BossAI01::kAttackCount);
    }
}

TEST(BossAI01Test, ChooseAttackCoversAllFour) {
    BossAI01 b(2.0F);
    b.SetSeed(7);
    std::vector<int> seen(BossAI01::kAttackCount, 0);
    for (int i = 0; i < 400; ++i) {
        seen[static_cast<std::size_t>(b.ChooseAttack())]++;
    }
    for (int c : seen) {
        EXPECT_GT(c, 0); // every attack reachable
    }
}

TEST(BossAI01Test, WanderDirectionIsUnitAndDeterministic) {
    BossAI01 a(2.0F);
    BossAI01 b(2.0F);
    a.SetSeed(99);
    b.SetSeed(99);
    for (int i = 0; i < 32; ++i) {
        const glm::vec2 da = a.WanderDirection();
        const glm::vec2 db = b.WanderDirection();
        EXPECT_FLOAT_EQ(da.x, db.x);
        EXPECT_FLOAT_EQ(da.y, db.y);
        const float len = std::sqrt(da.x * da.x + da.y * da.y);
        EXPECT_NEAR(len, 1.0F, 1e-4F); // normalized (degenerate zero is improbable)
    }
}

// NOLINTEND(readability-magic-numbers)
