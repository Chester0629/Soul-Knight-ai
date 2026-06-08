#include <gtest/gtest.h>

#include "combat/Gun012.hpp"
#include "data/RGRandom.hpp"

using Game::Gun012;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- ctor defaults: count==1, baseAngle==15 (Gun012___ctor immediates) -------

TEST(Gun012Test, CtorDefaultsMatchDecompImmediates) {
    Gun012 gun;
    EXPECT_EQ(gun.Count(), 1);
    EXPECT_FLOAT_EQ(gun.BaseAngle(), 15.0F);
    // Named constants mirror the ctor writes at this+0x70 / this+0x74.
    EXPECT_EQ(Gun012::kDefaultCount, 1);
    EXPECT_FLOAT_EQ(Gun012::kDefaultBaseAngle, 15.0F);
}

// ---- count<1 guard: no fire (Gun012__Attack @ 964283) ------------------------

TEST(Gun012Test, CanFireGuardsSubOneCount) {
    EXPECT_FALSE(Gun012(0).CanFire());
    EXPECT_FALSE(Gun012(-3).CanFire());
    EXPECT_TRUE(Gun012(1).CanFire());
    EXPECT_TRUE(Gun012(8).CanFire());
}

// ---- FAN START: -((count + oddBias) / 2) (Gun012__Attack @ 964296) -----------

TEST(Gun012Test, FanStartSignedFloorTable) {
    // count 1->0, 2->-1, 3->-1, 4->-2, 5->-2, 6->-3, 7->-3, 8->-4.
    EXPECT_EQ(Gun012::FanStartFor(1), 0);
    EXPECT_EQ(Gun012::FanStartFor(2), -1);
    EXPECT_EQ(Gun012::FanStartFor(3), -1);
    EXPECT_EQ(Gun012::FanStartFor(4), -2);
    EXPECT_EQ(Gun012::FanStartFor(5), -2);
    EXPECT_EQ(Gun012::FanStartFor(6), -3);
    EXPECT_EQ(Gun012::FanStartFor(7), -3);
    EXPECT_EQ(Gun012::FanStartFor(8), -4);
}

TEST(Gun012Test, FanStartMatchesIndependentExpansion) {
    // Independently re-expand the decomp arithmetic: odd counts bias -1, even 0,
    // then signed truncating /2 and negate.
    for (int count = 1; count <= 16; ++count) {
        const int oddBias = (count & 1) ? -1 : 0;
        const int expected = -((count + oddBias) / 2);
        EXPECT_EQ(Gun012::FanStartFor(count), expected);
    }
}

TEST(Gun012Test, FanStartUsesInstanceCount) {
    Gun012 gun(5);
    EXPECT_EQ(gun.FanStart(), -2);
    gun.SetCount(7);
    EXPECT_EQ(gun.FanStart(), -3);
}

// ---- SCATTER half-angle: base + base*recoil (Gun012__Attack @ 964310-964320) -

TEST(Gun012Test, ScatterHalfAngleZeroRecoilIsBaseAngle) {
    Gun012 gun; // baseAngle 15
    EXPECT_FLOAT_EQ(gun.ScatterHalfAngle(0.0F), 15.0F);
}

TEST(Gun012Test, ScatterHalfAngleAddsRecoilTerm) {
    Gun012 gun(1, 15.0F);
    // 15 + 15*0.5 = 22.5
    EXPECT_FLOAT_EQ(gun.ScatterHalfAngle(0.5F), 22.5F);
    // 15 + 15*1 = 30
    EXPECT_FLOAT_EQ(gun.ScatterHalfAngle(1.0F), 30.0F);
    // A different base angle scales the same way: 20 + 20*0.25 = 25.
    Gun012 wide(1, 20.0F);
    EXPECT_FLOAT_EQ(wide.ScatterHalfAngle(0.25F), 25.0F);
}

// ---- RANDOM scatter: exactly ONE symmetric draw (Gun012__Attack @ 964326) ----

TEST(Gun012Test, RollScatterIsOneSymmetricInclusiveDraw) {
    // The brain's scatter must equal a parallel same-seeded RGRandom drawing
    // Range(-half, +half) exactly once -- same count AND same order.
    Gun012 gun(1, 15.0F);
    gun.SetSeed(2024);
    RGRandom reference;
    reference.SetRandomSeed(2024);

    const float half = gun.ScatterHalfAngle(0.0F); // 15, no draw
    const float scattered = gun.RollScatter(half);
    EXPECT_FLOAT_EQ(scattered, reference.Range(-half, half));

    // Both streams are now one draw deep; they stay in lockstep on the next draw.
    EXPECT_EQ(gun.Rng().Range(0, 1000000), reference.Range(0, 1000000));
}

TEST(Gun012Test, ScatterAngleHelperDrawsExactlyOnce) {
    // ScatterAngle == ScatterHalfAngle then ONE RollScatter draw, in order.
    Gun012 gun(1, 15.0F);
    gun.SetSeed(777);
    RGRandom reference;
    reference.SetRandomSeed(777);

    const float recoil = 0.5F;             // half-angle 22.5, no draw
    const float half = 15.0F + 15.0F * recoil;
    const float a = gun.ScatterAngle(recoil);
    EXPECT_FLOAT_EQ(a, reference.Range(-half, half)); // one draw, matched

    // Confirm only ONE draw happened: streams remain in lockstep.
    EXPECT_EQ(gun.Rng().Range(0, 1000000), reference.Range(0, 1000000));
}

// ---- determinism: the count<1 guard path makes ZERO draws --------------------

TEST(Gun012Test, NoFirePathConsumesNoRandomDraws) {
    Gun012 gun(0); // CanFire() == false -> end-of-fire effect only, no scatter
    gun.SetSeed(99999);
    RGRandom reference;
    reference.SetRandomSeed(99999);

    EXPECT_FALSE(gun.CanFire());
    // Pure-geometric helpers never draw either.
    (void)gun.FanStart();
    (void)Gun012::FanStartFor(8);
    (void)gun.ScatterHalfAngle(0.5F);

    // The stream is byte-for-byte where it started: next draw matches reference.
    EXPECT_EQ(gun.Rng().Range(0, 1000000), reference.Range(0, 1000000));
}

// NOLINTEND(readability-magic-numbers)
