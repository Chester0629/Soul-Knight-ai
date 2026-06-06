#include <gtest/gtest.h>

#include <vector>

#include "combat/BossAI04.hpp"

using Game::BossAI04;

// NOLINTBEGIN(readability-magic-numbers)

TEST(BossAI04Test, EntersAngryBelowHalfHp) {
    BossAI04 b;
    b.OnHurt(301, 600); // > 50% -> still calm
    EXPECT_FALSE(b.Angry());

    b.OnHurt(299, 600); // < 50% -> angry
    EXPECT_TRUE(b.Angry());
}

TEST(BossAI04Test, ExactlyHalfIsNotAngry) {
    BossAI04 b;
    b.OnHurt(300, 600); // 0.5 is not < 0.5
    EXPECT_FALSE(b.Angry());
}

TEST(BossAI04Test, AngryTransitionHappensOnce) {
    BossAI04 b;
    b.OnHurt(100, 600); // angry
    EXPECT_TRUE(b.Angry());
    b.OnHurt(10, 600);  // still below 50%, no double-fire side effects
    EXPECT_TRUE(b.Angry());
}

TEST(BossAI04Test, ZeroMaxHpDoesNotEnterAngry) {
    BossAI04 b;
    b.OnHurt(0, 0); // guard against divide-by-zero
    EXPECT_FALSE(b.Angry());
}

TEST(BossAI04Test, AtkIndexStartsIdle) {
    BossAI04 b;
    EXPECT_EQ(b.AtkIndex(), BossAI04::kNoAttack);
}

TEST(BossAI04Test, ChooseAttackSetsIndexInRange) {
    BossAI04 b;
    b.SetSeed(4242);
    for (int i = 0; i < 64; ++i) {
        const int idx = b.ChooseAttack();
        EXPECT_GE(idx, 1);
        EXPECT_LE(idx, BossAI04::kAttackCount);
        EXPECT_EQ(b.AtkIndex(), idx); // ChooseAttack drives the state machine
    }
}

TEST(BossAI04Test, ChooseAttackIsDeterministic) {
    BossAI04 a;
    BossAI04 b;
    a.SetSeed(4242);
    b.SetSeed(4242);
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(a.ChooseAttack(), b.ChooseAttack()); // same seed -> same out
    }
}

TEST(BossAI04Test, DifferentSeedsDiverge) {
    BossAI04 a;
    BossAI04 b;
    a.SetSeed(1);
    b.SetSeed(987654);
    bool diverged = false;
    for (int i = 0; i < 64; ++i) {
        if (a.ChooseAttack() != b.ChooseAttack()) {
            diverged = true;
        }
    }
    EXPECT_TRUE(diverged);
}

TEST(BossAI04Test, ChooseAttackCoversAllFive) {
    BossAI04 b;
    b.SetSeed(7);
    std::vector<int> seen(BossAI04::kAttackCount + 1, 0);
    for (int i = 0; i < 600; ++i) {
        seen[static_cast<std::size_t>(b.ChooseAttack())]++;
    }
    for (int idx = 1; idx <= BossAI04::kAttackCount; ++idx) {
        EXPECT_GT(seen[static_cast<std::size_t>(idx)], 0); // every atk reachable
    }
}

TEST(BossAI04Test, StopAttackResetsToIdle) {
    BossAI04 b;
    b.SetSeed(11);
    b.ChooseAttack();
    EXPECT_NE(b.AtkIndex(), BossAI04::kNoAttack);
    b.StopAttack();
    EXPECT_EQ(b.AtkIndex(), BossAI04::kNoAttack);
}

TEST(BossAI04Test, Atk02DelayInRangeWhenCalm) {
    BossAI04 b;
    b.SetSeed(2024);
    for (int i = 0; i < 64; ++i) {
        const float d = b.Atk02RefireDelay();
        EXPECT_GE(d, BossAI04::kAtk02DelayMin);
        EXPECT_LE(d, BossAI04::kAtk02DelayMax);
    }
}

TEST(BossAI04Test, Atk02DelayGetsAngryBonus) {
    // Same seed: angry path must equal calm path + 1.0s exactly (cadence gate).
    BossAI04 calm;
    BossAI04 mad;
    calm.SetSeed(555);
    mad.SetSeed(555);
    mad.OnHurt(1, 600); // force angry without disturbing the RNG stream
    EXPECT_TRUE(mad.Angry());
    for (int i = 0; i < 32; ++i) {
        const float c = calm.Atk02RefireDelay();
        const float m = mad.Atk02RefireDelay();
        EXPECT_FLOAT_EQ(m, c + BossAI04::kAngryAtk02DelayBonus);
    }
}

TEST(BossAI04Test, Atk02DelayIsDeterministic) {
    BossAI04 a;
    BossAI04 b;
    a.SetSeed(31337);
    b.SetSeed(31337);
    for (int i = 0; i < 32; ++i) {
        EXPECT_FLOAT_EQ(a.Atk02RefireDelay(), b.Atk02RefireDelay());
    }
}

TEST(BossAI04Test, FullStreamReplayIsDeterministic) {
    // Interleave the three draws and replay -> identical sequence (lockstep).
    auto run = [](int seed) {
        BossAI04 b;
        b.SetSeed(seed);
        std::vector<float> trace;
        for (int i = 0; i < 24; ++i) {
            trace.push_back(static_cast<float>(b.ChooseAttack()));
            trace.push_back(b.Atk02RefireDelay());
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

// NOLINTEND(readability-magic-numbers)
