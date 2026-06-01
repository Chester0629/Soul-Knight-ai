#include <gtest/gtest.h>

#include "Util/FixedTimestep.hpp"

using Util::FixedTimestep;

// NOLINTBEGIN(readability-magic-numbers)

TEST(FixedTimestepTest, NoStepWhenBelowStep) {
    FixedTimestep ts(10.0F);
    EXPECT_EQ(ts.Advance(4.0F), 0);
    EXPECT_FLOAT_EQ(ts.Accumulator(), 4.0F);
}

TEST(FixedTimestepTest, SingleStepConsumesExactly) {
    FixedTimestep ts(10.0F);
    EXPECT_EQ(ts.Advance(10.0F), 1);
    EXPECT_FLOAT_EQ(ts.Accumulator(), 0.0F);
}

TEST(FixedTimestepTest, MultipleStepsKeepRemainder) {
    FixedTimestep ts(10.0F);
    EXPECT_EQ(ts.Advance(25.0F), 2);
    EXPECT_FLOAT_EQ(ts.Accumulator(), 5.0F);
}

TEST(FixedTimestepTest, AccumulatesAcrossCalls) {
    FixedTimestep ts(10.0F);
    EXPECT_EQ(ts.Advance(6.0F), 0);
    EXPECT_EQ(ts.Advance(6.0F), 1); // 12 total -> 1 step, 2 left over
    EXPECT_FLOAT_EQ(ts.Accumulator(), 2.0F);
}

TEST(FixedTimestepTest, AlphaIsFractionOfStep) {
    FixedTimestep ts(10.0F);
    ts.Advance(5.0F);
    EXPECT_FLOAT_EQ(ts.Alpha(), 0.5F);
}

TEST(FixedTimestepTest, MaxStepsCapsAndDropsBacklog) {
    FixedTimestep ts(10.0F, 3);
    // 100ms would be 10 steps; capped at 3, and the backlog is dropped.
    EXPECT_EQ(ts.Advance(100.0F), 3);
    EXPECT_FLOAT_EQ(ts.Accumulator(), 0.0F);
}

TEST(FixedTimestepTest, ResetClearsAccumulator) {
    FixedTimestep ts(10.0F);
    ts.Advance(7.0F);
    ts.Reset();
    EXPECT_FLOAT_EQ(ts.Accumulator(), 0.0F);
}

TEST(FixedTimestepTest, IgnoresNonPositiveDt) {
    FixedTimestep ts(10.0F);
    EXPECT_EQ(ts.Advance(-5.0F), 0);
    EXPECT_FLOAT_EQ(ts.Accumulator(), 0.0F);
}

TEST(FixedTimestepTest, NonPositiveStepFallsBackTo60Hz) {
    FixedTimestep ts(0.0F);
    EXPECT_NEAR(ts.StepMs(), 1000.0F / 60.0F, 1e-4F);
}

// NOLINTEND(readability-magic-numbers)
