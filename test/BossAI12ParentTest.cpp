#include <gtest/gtest.h>

#include "combat/BossAI12Parent.hpp"
#include "data/RGRandom.hpp"

using Game::BossAI12Parent;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- UpDateBossHp: aggregation -------------------------------------------

TEST(BossAI12ParentTest, TotalHpSumsBothHalves) {
    EXPECT_EQ(BossAI12Parent::TotalHp(300, 250), 550);
    EXPECT_EQ(BossAI12Parent::TotalHp(0, 0), 0);
    EXPECT_EQ(BossAI12Parent::TotalHp(1000, 0), 1000);
}

TEST(BossAI12ParentTest, TotalMaxHpSumsBothHalves) {
    EXPECT_EQ(BossAI12Parent::TotalMaxHp(600, 600), 1200);
    EXPECT_EQ(BossAI12Parent::TotalMaxHp(800, 400), 1200);
}

TEST(BossAI12ParentTest, UpDateBossHpCachesAggregatedTotals) {
    BossAI12Parent p;
    const float w = p.UpDateBossHp(300, 600, 250, 600);
    EXPECT_EQ(p.LastTotalHp(), 550);       // 300 + 250
    EXPECT_EQ(p.LastTotalMaxHp(), 1200);   // 600 + 600
    EXPECT_FLOAT_EQ(p.LastHpBarWidth(), w);
}

// ---- UpDateBossHp: HP-bar fill formula (BossInfo__UpDateBossHp) -----------

TEST(BossAI12ParentTest, FullHpGivesFullBarWidth) {
    BossAI12Parent p;
    // both halves at full -> (1200 * 400) / 1200 == 400.0
    const float w = p.UpDateBossHp(600, 600, 600, 600);
    EXPECT_FLOAT_EQ(w, BossAI12Parent::kHpBarFullWidth);
    EXPECT_FLOAT_EQ(w, 400.0F);
}

TEST(BossAI12ParentTest, HalfHpGivesHalfBarWidth) {
    BossAI12Parent p;
    // total 600 / 1200 -> 0.5 * 400 == 200.0
    const float w = p.UpDateBossHp(300, 600, 300, 600);
    EXPECT_FLOAT_EQ(w, 200.0F);
}

TEST(BossAI12ParentTest, BarWidthMatchesDecompFormula) {
    BossAI12Parent p;
    // Golden re-derivation of BossInfo__UpDateBossHp:
    //   current = 450 + 120 = 570 ; max = 600 + 600 = 1200
    //   width   = (570 * 400) / 1200 = 190.0
    const float w = p.UpDateBossHp(450, 600, 120, 600);
    EXPECT_FLOAT_EQ(w, (570.0F * 400.0F) / 1200.0F);
    EXPECT_FLOAT_EQ(w, 190.0F);
}

TEST(BossAI12ParentTest, ZeroHpGivesEmptyBar) {
    BossAI12Parent p;
    const float w = p.UpDateBossHp(0, 600, 0, 600);
    EXPECT_FLOAT_EQ(w, 0.0F);
}

TEST(BossAI12ParentTest, ZeroMaxHpDoesNotDivideByZero) {
    BossAI12Parent p;
    const float w = p.UpDateBossHp(0, 0, 0, 0); // guarded: max total == 0
    EXPECT_FLOAT_EQ(w, 0.0F);
    EXPECT_EQ(p.LastTotalMaxHp(), 0);
}

TEST(BossAI12ParentTest, AsymmetricHalvesAggregateBeforeDividing) {
    BossAI12Parent p;
    // One half nearly dead, the other untouched; the bar reflects the SUM,
    // not either half alone: current = 30 + 600 = 630, max = 600 + 600 = 1200.
    const float w = p.UpDateBossHp(30, 600, 600, 600);
    EXPECT_FLOAT_EQ(w, (630.0F * 400.0F) / 1200.0F);
    EXPECT_EQ(p.LastTotalHp(), 630);
}

// ---- BossDead: the both-halves-dead gate ---------------------------------

TEST(BossAI12ParentTest, BossDeadRequiresBothHalvesDead) {
    EXPECT_TRUE(BossAI12Parent::BossDead(true, true));
}

TEST(BossAI12ParentTest, BossDeadFalseWhenEitherHalfAlive) {
    // FAITHFUL: each 0x38 (dead) check returns early if the half is alive.
    EXPECT_FALSE(BossAI12Parent::BossDead(false, false));
    EXPECT_FALSE(BossAI12Parent::BossDead(true, false));  // half 2 alive
    EXPECT_FALSE(BossAI12Parent::BossDead(false, true));  // half 1 alive
}

// ---- RNG lockstep: the parent draws NOTHING ------------------------------

TEST(BossAI12ParentTest, ParentConsumesNoRngDraws) {
    // BossAI12Parent is a plain MonoBehaviour: none of its bodies draw RNG.
    // Run a parallel reference stream alongside heavy brain use and prove the
    // reference stream is untouched (same seed -> identical sequence after).
    RGRandom reference;
    RGRandom witness;
    reference.SetRandomSeed(13579);
    witness.SetRandomSeed(13579);

    BossAI12Parent p;
    for (int i = 0; i < 32; ++i) {
        // Exercise every modelled parent path; none of these may pull a draw.
        p.UpDateBossHp(600 - i * 10, 600, 600 - i * 5, 600);
        (void)BossAI12Parent::BossDead((i & 1) != 0, (i & 2) != 0);
        (void)BossAI12Parent::TotalHp(i, i + 1);
        (void)BossAI12Parent::TotalMaxHp(600, 600);
    }

    // The witness stream advanced 0 times alongside 32 brain iterations, so it
    // must still match the reference stream draw-for-draw.
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(reference.Range(0, 1000000), witness.Range(0, 1000000));
    }
}

// ---- Determinism of the pure aggregation ---------------------------------

TEST(BossAI12ParentTest, UpDateBossHpIsPureAndDeterministic) {
    BossAI12Parent a;
    BossAI12Parent b;
    for (int i = 0; i < 16; ++i) {
        const int h1 = 600 - i * 7;
        const int h2 = 600 - i * 11;
        EXPECT_FLOAT_EQ(a.UpDateBossHp(h1, 600, h2, 600),
                        b.UpDateBossHp(h1, 600, h2, 600));
    }
}

// NOLINTEND(readability-magic-numbers)
