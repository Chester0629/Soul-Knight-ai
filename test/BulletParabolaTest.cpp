#include <gtest/gtest.h>

#include "combat/BulletParabola.hpp"
#include "data/RGRandom.hpp"

using Game::BulletParabola;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- ShouldAccumulate: the FixedUpdate gate `!active || counter == 0` --------

TEST(BulletParabolaTest, GateOpenWhenInactive) {
    // !active -> gate true regardless of the counter value (counter is only read
    // under `active` in the decomp; the `!active` term short-circuits).
    EXPECT_TRUE(BulletParabola::ShouldAccumulate(false, 0));
    EXPECT_TRUE(BulletParabola::ShouldAccumulate(false, 7));
    EXPECT_TRUE(BulletParabola::ShouldAccumulate(false, -3));
}

TEST(BulletParabolaTest, GateOpenWhenActiveAndCounterZero) {
    // active && counter == 0 -> gate true.
    EXPECT_TRUE(BulletParabola::ShouldAccumulate(true, 0));
}

TEST(BulletParabolaTest, GateClosedWhenActiveAndCounterNonZero) {
    // active && counter != 0 -> gate false (accumulator suppressed this step).
    EXPECT_FALSE(BulletParabola::ShouldAccumulate(true, 1));
    EXPECT_FALSE(BulletParabola::ShouldAccumulate(true, 42));
    EXPECT_FALSE(BulletParabola::ShouldAccumulate(true, -5));
}

// ---- Tick: progress(0x54) += rate(0x4c) * fixedDeltaTime, behind the gate ----

TEST(BulletParabolaTest, ProgressStartsAtZero) {
    BulletParabola b;
    EXPECT_FLOAT_EQ(b.Progress(), 0.0F);
}

TEST(BulletParabolaTest, SingleStepAddsRateTimesDt) {
    // Default state (inactive) -> gate open. progress += rate * dt.
    BulletParabola b;
    b.SetRate(3.0F);
    // 0 + 3 * 0.02 = 0.06.
    EXPECT_FLOAT_EQ(b.Tick(0.02F), 0.06F);
    EXPECT_FLOAT_EQ(b.Progress(), 0.06F);
}

TEST(BulletParabolaTest, AccumulatesAcrossFixedDtSequence) {
    // Drive a fixed-dt sequence and assert the exact running sum from the decomp
    // formula: progress = sum(rate * dt_i).
    BulletParabola b;
    b.SetRate(2.5F);
    const float dt = 0.02F; // a typical fixed step
    float expected = 0.0F;
    for (int i = 0; i < 10; ++i) {
        expected += 2.5F * dt;
        EXPECT_FLOAT_EQ(b.Tick(dt), expected);
    }
    // 10 steps of 2.5 * 0.02 = 0.05 each -> 0.5 total.
    EXPECT_FLOAT_EQ(b.Progress(), 0.5F);
}

TEST(BulletParabolaTest, VariableDtSequenceMatchesFormula) {
    // Mixed dt values; the accumulator must track rate * dt exactly per step.
    BulletParabola b;
    b.SetRate(4.0F);
    const float dts[] = {0.01F, 0.02F, 0.005F, 0.03F};
    float expected = 0.0F;
    for (const float dt : dts) {
        expected += 4.0F * dt;
        EXPECT_FLOAT_EQ(b.Tick(dt), expected);
    }
}

TEST(BulletParabolaTest, GateSuppressesAccumulationWhenActiveAndCounterNonZero) {
    // active && counter != 0 -> the accumulator does NOT run; progress is frozen.
    BulletParabola b;
    b.SetRate(5.0F);
    b.SetActive(true);
    b.SetCounter(3);
    EXPECT_FLOAT_EQ(b.Tick(0.02F), 0.0F);
    EXPECT_FLOAT_EQ(b.Tick(0.02F), 0.0F);
    EXPECT_FLOAT_EQ(b.Progress(), 0.0F);

    // Drop the counter to 0 (still active) -> gate opens, accumulation resumes.
    b.SetCounter(0);
    EXPECT_FLOAT_EQ(b.Tick(0.02F), 0.1F); // 5 * 0.02
}

TEST(BulletParabolaTest, InactiveAccumulatesRegardlessOfCounter) {
    // !active -> gate open even with a non-zero counter set.
    BulletParabola b;
    b.SetRate(1.0F);
    b.SetActive(false);
    b.SetCounter(99);
    EXPECT_FLOAT_EQ(b.Tick(0.5F), 0.5F);
}

TEST(BulletParabolaTest, ResetProgressZeroesAccumulator) {
    BulletParabola b;
    b.SetRate(2.0F);
    b.Tick(0.5F); // -> 1.0
    EXPECT_FLOAT_EQ(b.Progress(), 1.0F);
    b.ResetProgress();
    EXPECT_FLOAT_EQ(b.Progress(), 0.0F);
}

TEST(BulletParabolaTest, ZeroRateNeverAdvances) {
    // rate 0 -> the accumulator is a no-op even with the gate open.
    BulletParabola b;
    b.SetRate(0.0F);
    for (int i = 0; i < 5; ++i) {
        EXPECT_FLOAT_EQ(b.Tick(0.02F), 0.0F);
    }
}

// ---- Determinism: NO RGRandom draw is ever made by this brain ----------------

TEST(BulletParabolaTest, MakesNoRngDrawsAcrossTicks) {
    // None of the three recovered bodies calls rg_random. A parallel same-seeded
    // RGRandom must therefore reproduce the SAME first draw after the brain has
    // been driven, proving the brain never advanced its own stream.
    BulletParabola b;
    RGRandom ref;
    b.SetSeed(424242);
    ref.SetRandomSeed(424242);

    b.SetRate(3.0F);
    for (int i = 0; i < 50; ++i) {
        b.Tick(0.016F);
    }

    // The brain's stream and the untouched parallel stream agree on the next
    // draw -> the brain made zero draws.
    EXPECT_EQ(b.Rng().Range(0, 1000000), ref.Range(0, 1000000));
}

TEST(BulletParabolaTest, SeedRoundTrips) {
    BulletParabola b;
    EXPECT_FALSE(b.Seeded());
    b.SetSeed(7);
    EXPECT_TRUE(b.Seeded());
}

// NOLINTEND(readability-magic-numbers)
