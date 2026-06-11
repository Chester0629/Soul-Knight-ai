#include <gtest/gtest.h>

#include <vector>

#include "combat/BossAINian.hpp"

using Game::BossAINian;

// NOLINTBEGIN(readability-magic-numbers)

// --- Invisibility / damage immunity (signature mechanic) -------------------

TEST(BossAINianTest, HitsWhileInvisibleAreAbsorbed) {
    BossAINian b(2.0F);
    EXPECT_FALSE(b.IsInvisible());

    EXPECT_TRUE(b.TurnInvisible());
    EXPECT_TRUE(b.IsInvisible());
    EXPECT_FLOAT_EQ(b.Alpha(), BossAINian::kInvisibleAlpha);

    // A would-be lethal hit must be ignored while phased out: returns absorbed,
    // and crucially does NOT trigger the angry phase.
    EXPECT_TRUE(b.OnHurt(1, 600)); // absorbed
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 2.0F);
    EXPECT_FLOAT_EQ(b.AnimSpeed(), 1.0F);
}

TEST(BossAINianTest, TurnInvisibleIsGuardedWhileAlreadyInvisible) {
    BossAINian b(2.0F);
    EXPECT_TRUE(b.TurnInvisible());  // first phase-out succeeds
    EXPECT_FALSE(b.TurnInvisible()); // guarded: cannot re-enter mid-window
    EXPECT_FLOAT_EQ(b.InvisibleTimeLeft(), BossAINian::kInvisibleDuration);
}

TEST(BossAINianTest, InvisibilityTimesOutAfterTwoSeconds) {
    BossAINian b(2.0F);
    b.TurnInvisible();
    EXPECT_FLOAT_EQ(b.InvisibleTimeLeft(), 2.0F);

    EXPECT_FALSE(b.Tick(0.5F)); // 1.5 left
    EXPECT_TRUE(b.IsInvisible());
    EXPECT_FLOAT_EQ(b.InvisibleTimeLeft(), 1.5F);

    EXPECT_FALSE(b.Tick(1.0F)); // 0.5 left
    EXPECT_TRUE(b.IsInvisible());

    EXPECT_TRUE(b.Tick(0.5F)); // window elapsed -> becomes visible
    EXPECT_FALSE(b.IsInvisible());
    EXPECT_FLOAT_EQ(b.Alpha(), BossAINian::kVisibleAlpha);
    EXPECT_FLOAT_EQ(b.InvisibleTimeLeft(), 0.0F);
}

TEST(BossAINianTest, TickIsNoOpWhenVisible) {
    BossAINian b(2.0F);
    EXPECT_FALSE(b.Tick(5.0F));
    EXPECT_FALSE(b.IsInvisible());
}

TEST(BossAINianTest, HurtableAgainAfterPhaseBackIn) {
    BossAINian b(2.0F);
    b.TurnInvisible();
    EXPECT_TRUE(b.OnHurt(1, 600)); // absorbed
    EXPECT_TRUE(b.Tick(2.0F));     // phase back in
    EXPECT_FALSE(b.IsInvisible());

    // Now a sub-50% hit lands and triggers angry.
    EXPECT_FALSE(b.OnHurt(299, 600)); // landed
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 1.0F);
}

// --- Angry phase ------------------------------------------------------------

TEST(BossAINianTest, EntersAngryBelowHalfHp) {
    BossAINian b(2.0F);
    EXPECT_FALSE(b.OnHurt(301, 600)); // > 50% -> calm
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 2.0F);
    EXPECT_FLOAT_EQ(b.AnimSpeed(), 1.0F);

    EXPECT_FALSE(b.OnHurt(299, 600)); // < 50% -> angry
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 1.0F);   // halved
    EXPECT_FLOAT_EQ(b.AnimSpeed(), 1.2F);
}

TEST(BossAINianTest, AngryTransitionHappensOnce) {
    BossAINian b(2.0F);
    b.OnHurt(100, 600); // angry, shoot_cd 1.0
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 1.0F);
    b.OnHurt(10, 600); // still below 50% but must NOT halve again
    EXPECT_FLOAT_EQ(b.ShootCd(), 1.0F);
}

TEST(BossAINianTest, ExactlyHalfIsNotAngry) {
    BossAINian b(2.0F);
    b.OnHurt(300, 600); // 0.5 is not < 0.5
    EXPECT_FALSE(b.Angry());
}

// --- Determinism ------------------------------------------------------------

TEST(BossAINianTest, RollInvisibilityIsDeterministic) {
    BossAINian a(2.0F);
    BossAINian b(2.0F);
    a.SetSeed(1234);
    b.SetSeed(1234);
    for (int i = 0; i < 128; ++i) {
        EXPECT_EQ(a.RollInvisibility(50), b.RollInvisibility(50));
    }
}

TEST(BossAINianTest, RollInvisibilityRespectsChanceBounds) {
    BossAINian always(2.0F);
    BossAINian never(2.0F);
    always.SetSeed(7);
    never.SetSeed(7);
    for (int i = 0; i < 64; ++i) {
        EXPECT_TRUE(always.RollInvisibility(100)); // roll in [0,100) always < 100
        EXPECT_FALSE(never.RollInvisibility(0));   // nothing < 0
    }
}

TEST(BossAINianTest, RollInvisibilityCanGoBothWays) {
    BossAINian b(2.0F);
    b.SetSeed(99);
    std::vector<int> outcomes(2, 0);
    for (int i = 0; i < 400; ++i) {
        outcomes[b.RollInvisibility(50) ? 1 : 0]++;
    }
    EXPECT_GT(outcomes[0], 0); // sometimes stays visible
    EXPECT_GT(outcomes[1], 0); // sometimes phases out
}

TEST(BossAINianTest, SameSeedSameInvisibilitySequence) {
    BossAINian a(2.0F);
    BossAINian b(2.0F);
    a.SetSeed(2025);
    b.SetSeed(2025);
    std::vector<bool> seqA;
    std::vector<bool> seqB;
    for (int i = 0; i < 50; ++i) {
        seqA.push_back(a.RollInvisibility(30));
        seqB.push_back(b.RollInvisibility(30));
    }
    EXPECT_EQ(seqA, seqB);
}

// NOLINTEND(readability-magic-numbers)
