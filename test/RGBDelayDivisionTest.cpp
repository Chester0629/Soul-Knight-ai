#include <gtest/gtest.h>

#include <vector>

#include "combat/RGBDelayDivision.hpp"
#include "data/RGRandom.hpp"

using Game::RGBDelayDivision;
using State = Game::RGBDelayDivision::State;

// NOLINTBEGIN(readability-magic-numbers)

// --- AdjustAngle / OnTaken: the spread-angle clamp ---------------------------

TEST(RGBDelayDivisionTest, ShouldClampWhenProductReaches361) {
    // Decomp gate: AdjustAngle returns when c_angle*c_count < 361; OnTaken acts when
    // 361 <= product. So clamp iff product >= 361.
    EXPECT_FALSE(RGBDelayDivision::ShouldClampAngle(30, 12)); // 360 < 361
    EXPECT_TRUE(RGBDelayDivision::ShouldClampAngle(31, 12));  // 372 >= 361
    EXPECT_TRUE(RGBDelayDivision::ShouldClampAngle(90, 5));   // 450 >= 361
    // Exactly 360 is the threshold boundary: 360 < 361 -> NOT clamped (pass-through).
    EXPECT_FALSE(RGBDelayDivision::ShouldClampAngle(360, 1)); // 360 < 361
    EXPECT_TRUE(RGBDelayDivision::ShouldClampAngle(361, 1));  // 361 >= 361
}

TEST(RGBDelayDivisionTest, ClampLeavesAngleUntouchedBelowThreshold) {
    // Under the gate the decomp does an early return WITHOUT writing c_angle.
    EXPECT_EQ(RGBDelayDivision::ClampAngle(30, 12), 30); // product 360 -> unchanged
    EXPECT_EQ(RGBDelayDivision::ClampAngle(15, 8), 15);  // product 120 -> unchanged
    EXPECT_EQ(RGBDelayDivision::ClampAngle(0, 5), 0);    // product 0 -> unchanged
}

TEST(RGBDelayDivisionTest, ClampReducesAngleToFullCircleOverCount) {
    // Over the gate: c_angle = 360 / c_count (integer divide).
    EXPECT_EQ(RGBDelayDivision::ClampAngle(90, 5), 72);  // 360 / 5 = 72
    EXPECT_EQ(RGBDelayDivision::ClampAngle(60, 8), 45);  // 360 / 8 = 45
    EXPECT_EQ(RGBDelayDivision::ClampAngle(50, 12), 30); // 360 / 12 = 30
}

TEST(RGBDelayDivisionTest, ClampUsesTruncatingIntegerDivide) {
    // 360 / 7 = 51.43 -> truncates to 51 (the original __aeabi_idiv is integer).
    // Need product >= 361 to reach the divide: 60 * 7 = 420 >= 361.
    EXPECT_EQ(RGBDelayDivision::ClampAngle(60, 7), 51); // 360 / 7 = 51 (not 52)
    // 360 / 11 = 32.7 -> 32 ; product 40 * 11 = 440 >= 361.
    EXPECT_EQ(RGBDelayDivision::ClampAngle(40, 11), 32);
}

// --- FixedUpdate: the spin gate ----------------------------------------------

TEST(RGBDelayDivisionTest, ShouldRotateOnlyWhenAwakeAndRotating) {
    // Decomp: proceed iff awake && rotate_angle != 0.
    EXPECT_TRUE(RGBDelayDivision::ShouldRotate(true, 5));
    EXPECT_TRUE(RGBDelayDivision::ShouldRotate(true, -3)); // any non-zero angle
    EXPECT_FALSE(RGBDelayDivision::ShouldRotate(true, 0)); // rotate_angle 0 -> bail
    EXPECT_FALSE(RGBDelayDivision::ShouldRotate(false, 5)); // not awake -> bail
    EXPECT_FALSE(RGBDelayDivision::ShouldRotate(false, 0));
}

// --- Division MoveNext: dispatch ---------------------------------------------

TEST(RGBDelayDivisionTest, DispatchMapsStatesToBlocks) {
    // s < 3 -> s + 3: 0->3 (Wait), 1->4 (Spawn), 2->5 (Finalise).
    EXPECT_EQ(RGBDelayDivision::Dispatch(State::Wait), State::Wait);
    EXPECT_EQ(RGBDelayDivision::Dispatch(State::Spawn), State::Spawn);
    EXPECT_EQ(RGBDelayDivision::Dispatch(State::Finalise), State::Finalise);
    // Done (-1) is not < 3 in the unsigned compare path -> disp 0 -> no-op (Done).
    EXPECT_EQ(RGBDelayDivision::Dispatch(State::Done), State::Done);
}

// --- Division MoveNext: symmetric fan layout ---------------------------------

TEST(RGBDelayDivisionTest, FanStartIndexEvenCountIsSymmetricAboutGap) {
    // Even count: start = -(count / 2).
    EXPECT_EQ(RGBDelayDivision::FanStartIndex(2), -1);  // -(2/2)
    EXPECT_EQ(RGBDelayDivision::FanStartIndex(4), -2);  // -(4/2)
    EXPECT_EQ(RGBDelayDivision::FanStartIndex(6), -3);  // -(6/2)
}

TEST(RGBDelayDivisionTest, FanStartIndexOddCountIsSymmetricAboutCentre) {
    // Odd count: start = -((count - 1) / 2).
    EXPECT_EQ(RGBDelayDivision::FanStartIndex(1), 0);   // -((1-1)/2)
    EXPECT_EQ(RGBDelayDivision::FanStartIndex(3), -1);  // -((3-1)/2)
    EXPECT_EQ(RGBDelayDivision::FanStartIndex(5), -2);  // -((5-1)/2)
}

// --- Division coroutine: the fan spawn walk ----------------------------------

TEST(RGBDelayDivisionTest, BeginSpawnSeedsWalkAtFanStart) {
    RGBDelayDivision b;
    EXPECT_EQ(b.CurrentState(), State::Wait);
    b.BeginSpawn(5);
    EXPECT_EQ(b.CurrentState(), State::Spawn);
    EXPECT_EQ(b.Remaining(), 5);
    EXPECT_EQ(b.NextIndex(), -2); // FanStartIndex(5)
    EXPECT_FALSE(b.SpawnComplete());
}

TEST(RGBDelayDivisionTest, SpawnStepWalksSymmetricOddFan) {
    RGBDelayDivision b;
    b.BeginSpawn(5);

    std::vector<int> seen;
    int idx = 0;
    while (b.SpawnStep(idx)) {
        seen.push_back(idx);
        EXPECT_EQ(b.CurrentState(), b.SpawnComplete() ? State::Done : State::Spawn);
    }
    // 5 children centred on 0: -2,-1,0,1,2.
    const std::vector<int> expected = {-2, -1, 0, 1, 2};
    EXPECT_EQ(seen, expected);
    EXPECT_TRUE(b.SpawnComplete());
    EXPECT_EQ(b.CurrentState(), State::Done);
    EXPECT_EQ(b.Remaining(), 0);
}

TEST(RGBDelayDivisionTest, SpawnStepWalksSymmetricEvenFan) {
    RGBDelayDivision b;
    b.BeginSpawn(4);

    std::vector<int> seen;
    int idx = 0;
    while (b.SpawnStep(idx)) {
        seen.push_back(idx);
    }
    // 4 children, start -(4/2) = -2: -2,-1,0,1.
    const std::vector<int> expected = {-2, -1, 0, 1};
    EXPECT_EQ(seen, expected);
    EXPECT_TRUE(b.SpawnComplete());
}

TEST(RGBDelayDivisionTest, SpawnWithNonPositiveCountRunsNoChildren) {
    RGBDelayDivision b;
    b.BeginSpawn(0);
    // c_count <= 0 -> the decomp's `if (0 < c_count)` fan loop never runs.
    EXPECT_EQ(b.CurrentState(), State::Done);
    int idx = -999;
    EXPECT_FALSE(b.SpawnStep(idx));
    EXPECT_EQ(idx, -999); // outIndex untouched when no child remains
    EXPECT_TRUE(b.SpawnComplete());
}

// --- Determinism: this brain makes ZERO RNG draws ----------------------------

TEST(RGBDelayDivisionTest, MakesNoRngDraws) {
    // None of AdjustAngle/OnTaken/FixedUpdate/Division calls rg_random, so exercising
    // the whole brain must leave a seeded stream completely un-advanced: an
    // identically seeded reference that did nothing must produce the same next draw.
    RGBDelayDivision a;
    RGBDelayDivision reference;
    a.SetSeed(20240608);
    reference.SetSeed(20240608);
    EXPECT_TRUE(a.Seeded());

    // Drive every recoverable code path on 'a'.
    (void)RGBDelayDivision::ShouldClampAngle(90, 5);
    (void)RGBDelayDivision::ClampAngle(90, 5);
    (void)RGBDelayDivision::ShouldRotate(true, 7);
    (void)RGBDelayDivision::Dispatch(State::Spawn);
    (void)RGBDelayDivision::FanStartIndex(6);
    a.BeginSpawn(7);
    int idx = 0;
    while (a.SpawnStep(idx)) {
        // child Instantiate / PlayEffect / rotation is the owner's; no draw here.
    }

    // If any draw had occurred on 'a', this next draw would diverge.
    EXPECT_EQ(a.Rng().Range(0, 1000000), reference.Rng().Range(0, 1000000));
}

// NOLINTEND(readability-magic-numbers)
