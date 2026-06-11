#include <gtest/gtest.h>

#include <vector>

#include "combat/BulletColor.hpp"
#include "data/RGRandom.hpp"

using Game::BulletColor;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- PickColorIndex: exactly ONE int draw in [0, 6), in order ----------------

TEST(BulletColorTest, PickColorIndexStaysWithinSixVariants) {
    BulletColor b;
    b.SetSeed(1234);
    for (int i = 0; i < 256; ++i) {
        const int idx = b.PickColorIndex();
        EXPECT_GE(idx, 0);
        EXPECT_LT(idx, BulletColor::kColorCount); // max EXCLUSIVE -> {0..5}
    }
}

TEST(BulletColorTest, PickColorIndexDrawsExactlyOneIntInOrder) {
    // A parallel same-seeded RGRandom must reproduce each colour roll exactly:
    // proves Start draws ONE int per spawn, in order, with the [0, 6) bounds and
    // nothing else advancing the stream.
    BulletColor b;
    RGRandom ref;
    b.SetSeed(98765);
    ref.SetRandomSeed(98765);

    for (int i = 0; i < 32; ++i) {
        const int got = b.PickColorIndex();
        const int expected = ref.Range(0, BulletColor::kColorCount); // one draw
        EXPECT_EQ(got, expected);
    }
}

TEST(BulletColorTest, PickColorIndexIsDeterministicForSameSeed) {
    BulletColor a;
    BulletColor b;
    a.SetSeed(555);
    b.SetSeed(555);
    for (int i = 0; i < 32; ++i) {
        EXPECT_EQ(a.PickColorIndex(), b.PickColorIndex());
    }
}

TEST(BulletColorTest, PickColorIndexUpdatesStoredIndex) {
    BulletColor b;
    b.SetSeed(42);
    EXPECT_EQ(b.ColorIndex(), -1); // not rolled yet
    const int idx = b.PickColorIndex();
    EXPECT_EQ(b.ColorIndex(), idx);
}

// ---- ShouldRotate: gate predicate `awake && rotate_angle != 0`, ZERO draws ---

TEST(BulletColorTest, ShouldRotateOnlyWhenAwakeAndNonZeroAngle) {
    // The single recoverable scalar: rotate iff alive and the spin amount != 0.
    EXPECT_TRUE(BulletColor::ShouldRotate(true, 5));
    EXPECT_TRUE(BulletColor::ShouldRotate(true, -3)); // any non-zero angle
}

TEST(BulletColorTest, ShouldNotRotateWhenNotAwake) {
    // alive == false -> the early return fires regardless of rotate_angle.
    EXPECT_FALSE(BulletColor::ShouldRotate(false, 5));
    EXPECT_FALSE(BulletColor::ShouldRotate(false, 0));
}

TEST(BulletColorTest, ShouldNotRotateWhenAngleIsZero) {
    // rotate_angle == 0 -> the `rot == 0` half of the gate fires.
    EXPECT_FALSE(BulletColor::ShouldRotate(true, 0));
}

TEST(BulletColorTest, ShouldRotateMemberOverloadMatchesFields) {
    BulletColor b;
    // default: not awake, angle 0 -> no rotate.
    EXPECT_FALSE(b.ShouldRotate());

    b.SetAwake(true);
    EXPECT_FALSE(b.ShouldRotate()); // awake but angle still 0

    b.SetRotateAngle(7);
    EXPECT_TRUE(b.ShouldRotate()); // awake && angle != 0

    b.SetAwake(false);
    EXPECT_FALSE(b.ShouldRotate()); // angle != 0 but not awake
}

// ---- The rotate gate must never touch the RNG stream -------------------------

TEST(BulletColorTest, GatePredicateConsumesNoDraw) {
    // ShouldRotate is pure: a parallel same-seeded stream proves the gate makes
    // ZERO draws, so the next colour roll still lands on the seed's first value.
    BulletColor b;
    RGRandom ref;
    b.SetSeed(31337);
    ref.SetRandomSeed(31337);

    // Hammer the gate across the full truth table; none of these may draw.
    for (int awake = 0; awake <= 1; ++awake) {
        for (int angle = -2; angle <= 2; ++angle) {
            (void)BulletColor::ShouldRotate(awake != 0, angle);
        }
    }

    // First colour roll must equal the stream's untouched first int draw.
    const int got = b.PickColorIndex();
    const int expected = ref.Range(0, BulletColor::kColorCount);
    EXPECT_EQ(got, expected);
}

// NOLINTEND(readability-magic-numbers)
