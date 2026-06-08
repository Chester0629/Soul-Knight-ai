#include <gtest/gtest.h>

#include "sim/FixedClock.hpp"
#include "sim/SimConfig.hpp"

using Game::Sim::FixedClock;

// NOLINTBEGIN(readability-magic-numbers)

TEST(FixedClockTest, OneFullStep) {
    FixedClock clk;
    EXPECT_EQ(clk.Advance(20.0F), 1); // exactly one 20ms step
    EXPECT_FLOAT_EQ(clk.RemainderMs(), 0.0F);
}

TEST(FixedClockTest, AccumulatesAcrossFramesWithRemainderCarry) {
    FixedClock clk;
    EXPECT_EQ(clk.Advance(12.0F), 0); // 12 < 20 -> no step yet
    EXPECT_FLOAT_EQ(clk.RemainderMs(), 12.0F);
    EXPECT_EQ(clk.Advance(12.0F), 1); // 24 total -> one step, 4 remainder
    EXPECT_FLOAT_EQ(clk.RemainderMs(), 4.0F);
}

TEST(FixedClockTest, MultipleStepsInOneFrame) {
    FixedClock clk;
    EXPECT_EQ(clk.Advance(50.0F), 2); // 50ms -> 2 steps (40), 10 remainder
    EXPECT_FLOAT_EQ(clk.RemainderMs(), 10.0F);
}

TEST(FixedClockTest, CapsRunawayFrame) {
    FixedClock clk;
    // 1000ms would be 50 steps; capped to kMaxStepsPerAdvance, remainder cleared
    // so we do not bank 49 steps of debt.
    EXPECT_EQ(clk.Advance(1000.0F), Game::Sim::kMaxStepsPerAdvance);
    EXPECT_FLOAT_EQ(clk.RemainderMs(), 0.0F);
}

TEST(FixedClockTest, IgnoresNonPositiveDt) {
    FixedClock clk;
    EXPECT_EQ(clk.Advance(0.0F), 0);
    EXPECT_EQ(clk.Advance(-5.0F), 0);
    EXPECT_FLOAT_EQ(clk.RemainderMs(), 0.0F);
}

TEST(FixedClockTest, SevenStepsDoNotCap) {
    // 140ms == 7 * 20ms: the largest input that does NOT trip the cap, so the
    // remainder is the exact natural value (0), proving the cap boundary.
    FixedClock clk;
    EXPECT_EQ(clk.Advance(140.0F), 7);
    EXPECT_FLOAT_EQ(clk.RemainderMs(), 0.0F);
}

TEST(FixedClockTest, ClockUsableAfterCap) {
    // After a cap event the clock must not be left in a broken state: the
    // remainder was cleared to 0, and subsequent Advance calls behave normally.
    FixedClock clk;
    clk.Advance(1000.0F); // trips the cap, remainder cleared to 0
    EXPECT_EQ(clk.Advance(12.0F), 0);
    EXPECT_FLOAT_EQ(clk.RemainderMs(), 12.0F);
    EXPECT_EQ(clk.Advance(12.0F), 1); // 24ms total -> one step, 4ms carry
    EXPECT_FLOAT_EQ(clk.RemainderMs(), 4.0F);
}

// NOLINTEND(readability-magic-numbers)
