#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/BulletRoundabout.hpp"

using Game::BulletRoundabout;
using State = Game::BulletRoundabout::State;

// NOLINTBEGIN(readability-magic-numbers)

// --- AdjustmentAngle: DeltaAngle wrap ----------------------------------------

TEST(BulletRoundaboutTest, WrapLeavesSmallDeltaUnchanged) {
    // |delta| < 180 -> untouched (the `< 180.0` compare).
    EXPECT_FLOAT_EQ(BulletRoundabout::WrapDeltaAngle(0.0F), 0.0F);
    EXPECT_FLOAT_EQ(BulletRoundabout::WrapDeltaAngle(90.0F), 90.0F);
    EXPECT_FLOAT_EQ(BulletRoundabout::WrapDeltaAngle(-90.0F), -90.0F);
    // Exactly 180 is NOT > 180, so it stays (matches the strict `> 180`).
    EXPECT_FLOAT_EQ(BulletRoundabout::WrapDeltaAngle(180.0F), 180.0F);
    EXPECT_FLOAT_EQ(BulletRoundabout::WrapDeltaAngle(-180.0F), -180.0F);
}

TEST(BulletRoundaboutTest, WrapPullsLargeDeltaAcrossFullTurn) {
    // delta > 180 -> delta - 360 ; delta < -180 -> delta + 360.
    EXPECT_FLOAT_EQ(BulletRoundabout::WrapDeltaAngle(270.0F), -90.0F);
    EXPECT_FLOAT_EQ(BulletRoundabout::WrapDeltaAngle(-270.0F), 90.0F);
    EXPECT_FLOAT_EQ(BulletRoundabout::WrapDeltaAngle(190.0F), -170.0F);
    EXPECT_FLOAT_EQ(BulletRoundabout::WrapDeltaAngle(-350.0F), 10.0F);
}

// --- AdjustmentAngle: step vs snap -------------------------------------------

TEST(BulletRoundaboutTest, StepsTowardTargetWhenGapExceedsSpeed) {
    // delta = wrap(100 - 0) = 100 > angle_speed 30 -> step by angle_speed.
    // delta > 0 -> move_angle -= angle_speed (the recovered Z||N branch).
    EXPECT_FLOAT_EQ(BulletRoundabout::StepMoveAngle(0.0F, 100.0F, 30.0F), -30.0F);
    // delta = wrap(-100 - 0) = -100, |delta| 100 > 30 -> delta <= 0 -> +speed.
    EXPECT_FLOAT_EQ(BulletRoundabout::StepMoveAngle(0.0F, -100.0F, 30.0F), 30.0F);
}

TEST(BulletRoundaboutTest, SnapsWhenSpeedExceedsGap) {
    // angle_speed 50 > |delta| 20 -> snap: move_angle -= delta = 0 - 20 = -20.
    EXPECT_FLOAT_EQ(BulletRoundabout::StepMoveAngle(0.0F, 20.0F, 50.0F), -20.0F);
    // From a non-zero current: delta = wrap(10 - 30) = -20, |-20| 20 < 50 -> snap.
    // move_angle -= delta -> 30 - (-20) = 50.
    EXPECT_FLOAT_EQ(BulletRoundabout::StepMoveAngle(30.0F, 10.0F, 50.0F), 50.0F);
}

TEST(BulletRoundaboutTest, StepBoundaryUsesStepBranchWhenEqual) {
    // angle_speed == |delta| (30 == 30) -> the `step <= |delta|` step branch.
    // delta = +30 > 0 -> move_angle -= speed -> 0 - 30 = -30.
    EXPECT_FLOAT_EQ(BulletRoundabout::StepMoveAngle(0.0F, 30.0F, 30.0F), -30.0F);
}

TEST(BulletRoundaboutTest, StepUsesWrappedDeltaPastHalfTurn) {
    // target 350, current 0 -> raw delta 350 wraps to -10; |-10| 10 < 50 -> snap:
    // move_angle -= (-10) = 10. (Confirms the wrap feeds the step/snap decision.)
    EXPECT_FLOAT_EQ(BulletRoundabout::StepMoveAngle(0.0F, 350.0F, 50.0F), 10.0F);
}

// --- MoveNext: dispatch ------------------------------------------------------

TEST(BulletRoundaboutTest, DispatchMapsStatesToBlocks) {
    // s < 3 -> s + 3: 0->3 (Entry), 1->4 (Spin), 2->5 (Tail).
    EXPECT_EQ(BulletRoundabout::Dispatch(State::Entry), State::Entry);
    EXPECT_EQ(BulletRoundabout::Dispatch(State::Spin), State::Spin);
    EXPECT_EQ(BulletRoundabout::Dispatch(State::Tail), State::Tail);
    // Done (-1) is not < 3 in the unsigned compare path -> disp 0 -> no-op (Done).
    EXPECT_EQ(BulletRoundabout::Dispatch(State::Done), State::Done);
}

// --- MoveNext: spin counter state machine ------------------------------------

TEST(BulletRoundaboutTest, BeginSpinSeedsCounterFromRepeatCount) {
    BulletRoundabout b;
    EXPECT_EQ(b.CurrentState(), State::Entry);
    b.BeginSpin(3.0F);
    EXPECT_FLOAT_EQ(b.Counter(), 3.0F);
    EXPECT_EQ(b.CurrentState(), State::Spin);
    EXPECT_FALSE(b.SpinComplete());
}

TEST(BulletRoundaboutTest, SpinStepCountsDownByOneUntilExhausted) {
    BulletRoundabout b;
    b.BeginSpin(3.0F);

    // The loop runs exactly `count` iterations (counter > 0), each -= 1.0.
    auto run = [&]() -> std::vector<float> {
        std::vector<float> seen;
        while (b.SpinStep()) {
            seen.push_back(b.Counter());
            EXPECT_EQ(b.CurrentState(), State::Spin);
        }
        return seen;
    };

    const std::vector<float> seen = run();
    const std::vector<float> expected = {2.0F, 1.0F, 0.0F};
    EXPECT_EQ(seen, expected);

    // Exhausted: counter <= 0 -> loop ends, machine advances to Done.
    EXPECT_FLOAT_EQ(b.Counter(), 0.0F);
    EXPECT_TRUE(b.SpinComplete());
    EXPECT_EQ(b.CurrentState(), State::Done);
    // Further calls stay false / idempotent.
    EXPECT_FALSE(b.SpinStep());
}

TEST(BulletRoundaboutTest, ZeroSpinCountRunsNoIterations) {
    BulletRoundabout b;
    b.BeginSpin(0.0F);
    // counter <= 0 from the start -> the `while (counter > 0)` body never runs.
    EXPECT_FALSE(b.SpinStep());
    EXPECT_TRUE(b.SpinComplete());
    EXPECT_FLOAT_EQ(b.Counter(), 0.0F);
}

// --- Determinism: this brain makes ZERO RNG draws ----------------------------

TEST(BulletRoundaboutTest, MakesNoRngDraws) {
    // Neither AdjustmentAngle nor MoveNext calls rg_random, so exercising the full
    // brain must leave the seeded stream completely un-advanced: an identically
    // seeded reference instance that did nothing must produce the same next draw.
    BulletRoundabout a;
    BulletRoundabout reference;
    a.SetSeed(20240607);
    reference.SetSeed(20240607);
    EXPECT_TRUE(a.Seeded());

    // Drive every recoverable code path on 'a'.
    (void)BulletRoundabout::WrapDeltaAngle(270.0F);
    (void)BulletRoundabout::StepMoveAngle(0.0F, 100.0F, 30.0F);
    (void)BulletRoundabout::Dispatch(State::Spin);
    a.BeginSpin(5.0F);
    while (a.SpinStep()) {
        // velocity-multiply side effect is the owner's; no draw happens here.
    }

    // If any draw had occurred on 'a', this next draw would diverge.
    EXPECT_EQ(a.Rng().Range(0, 1000000), reference.Rng().Range(0, 1000000));
}

// NOLINTEND(readability-magic-numbers)
