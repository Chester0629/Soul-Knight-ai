#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/BossAI01.hpp"
#include "data/RGRandom.hpp"

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

// F2: the WITH-TARGET move decision (RunReflection switch). Reproduce it with a parallel
// reference RGRandom drawing in the SAME order -- threshold Range(5,10) FIRST, then selector
// Range(0,10). dist=7 sits inside [5,10) so the retreat branch depends on the threshold draw,
// which makes the draw ORDER observable. Covers determinism + every branch.
TEST(BossAI01Test, ChaseMoveDecisionMatchesRunReflectionSwitch) {
    BossAI01 b(2.0F);
    b.SetSeed(1234);
    Game::RGRandom ref;
    ref.SetRandomSeed(1234);
    const glm::vec2 chase(0.6F, 0.8F); // unit
    const float dist = 7.0F;
    int sawChase = 0;
    int sawRetreat = 0;
    int sawMirrorX = 0;
    int sawMirrorY = 0;
    for (int i = 0; i < 256; ++i) {
        const int thr = ref.Range(5, 10);  // FIRST (must match BossAI01::ChaseMoveDecision)
        const int roll = ref.Range(0, 10); // SECOND
        glm::vec2 expected;
        if (roll < 6) {
            if (dist < static_cast<float>(thr)) {
                expected = glm::vec2(-chase.x, -chase.y); // retreat
                ++sawRetreat;
            } else {
                expected = chase;
                ++sawChase;
            }
        } else if (roll < 8) {
            expected = glm::vec2(-chase.x, chase.y); // mirror X
            ++sawMirrorX;
        } else {
            expected = glm::vec2(chase.x, -chase.y); // mirror Y
            ++sawMirrorY;
        }
        const glm::vec2 got = b.ChaseMoveDecision(chase, dist);
        EXPECT_FLOAT_EQ(got.x, expected.x);
        EXPECT_FLOAT_EQ(got.y, expected.y);
    }
    EXPECT_GT(sawChase, 0);   // every branch reachable over 256 cycles
    EXPECT_GT(sawRetreat, 0);
    EXPECT_GT(sawMirrorX, 0);
    EXPECT_GT(sawMirrorY, 0);
}

// NOLINTEND(readability-magic-numbers)
