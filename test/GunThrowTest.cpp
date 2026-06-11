#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/GunThrow.hpp"
#include "data/RGRandom.hpp"

using Game::GunThrow;
using Game::GunThrowThrowingIterator;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
// Reference re-implementation of the recoverable GetShootAngle fan-out, used to
// independently confirm the brain's output (not a copy of the production sign
// helpers -- this expands the decomp arithmetic inline).
float ExpectedShootAngle(int count, int index, float span, float offset) {
    if (count < 2) {
        return 0.0F;
    }
    int signedStep = 0;
    int halfSteps = 0;
    int stepSign = 0;
    if (count % 2 == 1) {
        signedStep = static_cast<int>(std::ceil(index * 0.5F)) *
                     ((index & 1) * 2 - 1);
        halfSteps = static_cast<int>(std::ceil((count - 1) * 0.5F));
        stepSign = (((count - 1) * 2) & 2) - 1;
    } else {
        signedStep = static_cast<int>(std::ceil((index + 1) * 0.5F)) *
                     ((((index + 1) * 2) & 2) - 1);
        halfSteps = static_cast<int>(std::ceil(count * 0.5F));
        stepSign = -1;
    }
    const float denom = std::fabs(static_cast<float>(halfSteps * stepSign));
    return std::fabs(span + offset) * (static_cast<float>(signedStep) / denom);
}
} // namespace

// ---- SpreadSpanForCount: the recoverable 15/30 branches + 0 early-out -------

TEST(GunThrowTest, SpreadSpanSingleShotIsZero) {
    EXPECT_FLOAT_EQ(GunThrow::SpreadSpanForCount(0), 0.0F);
    EXPECT_FLOAT_EQ(GunThrow::SpreadSpanForCount(1), 0.0F);
}

TEST(GunThrowTest, SpreadSpan15For2And3) {
    EXPECT_FLOAT_EQ(GunThrow::SpreadSpanForCount(2), GunThrow::kSpread2to3);
    EXPECT_FLOAT_EQ(GunThrow::SpreadSpanForCount(3), GunThrow::kSpread2to3);
    EXPECT_FLOAT_EQ(GunThrow::kSpread2to3, 15.0F);
}

TEST(GunThrowTest, SpreadSpan30For4And5) {
    EXPECT_FLOAT_EQ(GunThrow::SpreadSpanForCount(4), GunThrow::kSpread4to5);
    EXPECT_FLOAT_EQ(GunThrow::SpreadSpanForCount(5), GunThrow::kSpread4to5);
    EXPECT_FLOAT_EQ(GunThrow::kSpread4to5, 30.0F);
}

TEST(GunThrowTest, SpreadSpanHighCountsHitUnrecoverableBranch) {
    // We may NOT assert the magnitude (DAT_00b24804/08 are UNRECOVERABLE), only
    // that 6..7 and 8+ resolve to their respective named constants (same object
    // identity), proving the branch selection is correct.
    EXPECT_FLOAT_EQ(GunThrow::SpreadSpanForCount(6), GunThrow::kSpread6to7);
    EXPECT_FLOAT_EQ(GunThrow::SpreadSpanForCount(7), GunThrow::kSpread6to7);
    EXPECT_FLOAT_EQ(GunThrow::SpreadSpanForCount(8), GunThrow::kSpread8plus);
    EXPECT_FLOAT_EQ(GunThrow::SpreadSpanForCount(12), GunThrow::kSpread8plus);
}

// ---- GetShootAngle: per-bullet fan-out (recoverable for counts 2..5) --------

TEST(GunThrowTest, ShootAngleBelowTwoIsStraight) {
    EXPECT_FLOAT_EQ(GunThrow::GetShootAngle(1, 0, 0.0F), 0.0F);
    EXPECT_FLOAT_EQ(GunThrow::GetShootAngle(0, 0, 0.0F), 0.0F);
}

TEST(GunThrowTest, ShootAngleThreeFansSymmetric) {
    // count=3 (odd), span=15: index 0 is the centre, 1 and 2 fan +-15.
    EXPECT_FLOAT_EQ(GunThrow::GetShootAngle(3, 0, 0.0F), 0.0F);
    EXPECT_FLOAT_EQ(GunThrow::GetShootAngle(3, 1, 0.0F), 15.0F);
    EXPECT_FLOAT_EQ(GunThrow::GetShootAngle(3, 2, 0.0F), -15.0F);
}

TEST(GunThrowTest, ShootAngleTwoFansBothWays) {
    // count=2 (even), span=15: the two bullets split +-15 (no centre shot).
    EXPECT_FLOAT_EQ(GunThrow::GetShootAngle(2, 0, 0.0F), 15.0F);
    EXPECT_FLOAT_EQ(GunThrow::GetShootAngle(2, 1, 0.0F), -15.0F);
}

TEST(GunThrowTest, ShootAngleFiveUsesThirtySpan) {
    // count=5 (odd), span=30. halfSteps=CeilToInt(4*0.5)=2 -> denom 2.
    // index1: CeilToInt(0.5)=1, sign +1 -> 30*(1/2)=15.
    EXPECT_FLOAT_EQ(GunThrow::GetShootAngle(5, 0, 0.0F), 0.0F);
    EXPECT_FLOAT_EQ(GunThrow::GetShootAngle(5, 1, 0.0F), 15.0F);
    EXPECT_FLOAT_EQ(GunThrow::GetShootAngle(5, 2, 0.0F), -15.0F);
    EXPECT_FLOAT_EQ(GunThrow::GetShootAngle(5, 3, 0.0F), 30.0F);
    EXPECT_FLOAT_EQ(GunThrow::GetShootAngle(5, 4, 0.0F), -30.0F);
}

TEST(GunThrowTest, ShootAngleMatchesIndependentExpansion) {
    // Cross-check every recoverable-span case against an inline re-expansion of
    // the decomp arithmetic (counts 2..5 have known spans 15/30).
    for (int count = 2; count <= 5; ++count) {
        const float span = (count < 4) ? 15.0F : 30.0F;
        for (int idx = 0; idx < count; ++idx) {
            EXPECT_FLOAT_EQ(GunThrow::GetShootAngle(count, idx, 0.0F),
                            ExpectedShootAngle(count, idx, span, 0.0F));
        }
    }
}

TEST(GunThrowTest, ShootAngleBaseOffsetAddsUnderAbs) {
    // baseAngleOffset (param_2+0x30) is summed with the span under ABS().
    // count=3,index=1: |15 + 5| * (1/1) = 20.
    EXPECT_FLOAT_EQ(GunThrow::GetShootAngle(3, 1, 5.0F), 20.0F);
    // A large negative offset flips the sum's sign but ABS keeps magnitude.
    // |15 + (-40)| = 25, times (1/1) -> 25.
    EXPECT_FLOAT_EQ(GunThrow::GetShootAngle(3, 1, -40.0F), 25.0F);
}

// ---- ResetConsume: CeilToInt(damage*count / bulletCount) --------------------

TEST(GunThrowTest, ResetConsumeCeilsTheRatio) {
    // 7*1 / 2 = 3.5 -> CeilToInt -> 4.
    EXPECT_EQ(GunThrow::ResetConsume(7, 1, 2), 4);
    // 6*1 / 3 = 2.0 -> 2 (exact, no rounding up).
    EXPECT_EQ(GunThrow::ResetConsume(6, 1, 3), 2);
    // 10*1 / 4 = 2.5 -> 3.
    EXPECT_EQ(GunThrow::ResetConsume(10, 1, 4), 3);
}

TEST(GunThrowTest, ResetConsumeGuardsZeroBulletCount) {
    EXPECT_EQ(GunThrow::ResetConsume(10, 1, 0), 0);
}

// ---- ResetDir gate ----------------------------------------------------------

TEST(GunThrowTest, ResetDirNeedsAChild) {
    EXPECT_FALSE(GunThrow::ResetDirHasChild(0));
    EXPECT_FALSE(GunThrow::ResetDirHasChild(-1));
    EXPECT_TRUE(GunThrow::ResetDirHasChild(1));
    EXPECT_TRUE(GunThrow::ResetDirHasChild(3));
}

// ---- GetWeaponAngle: thrown-child fan-out -----------------------------------

TEST(GunThrowTest, WeaponAngleBelowTwoIsStraight) {
    EXPECT_FLOAT_EQ(GunThrow::GetWeaponAngle(1, 0, 0), 0.0F);
}

TEST(GunThrowTest, WeaponAngleDefaultsSpanTo60WhenSiblingZero) {
    // count=3 (odd) -> half=FloorToInt(1.5)=1; span default 60.
    // index0: (0-1)/1 *60*0.5 = -30. index2: (2-1)/1*60*0.5 = 30.
    EXPECT_FLOAT_EQ(GunThrow::GetWeaponAngle(3, 0, 0), -30.0F);
    EXPECT_FLOAT_EQ(GunThrow::GetWeaponAngle(3, 0, 1), 0.0F);
    EXPECT_FLOAT_EQ(GunThrow::GetWeaponAngle(3, 0, 2), 30.0F);
}

TEST(GunThrowTest, WeaponAngleUsesSiblingSpanWhenNonZero) {
    // siblingCount=40 -> span=40. count=3,index2: (2-1)/1*40*0.5 = 20.
    EXPECT_FLOAT_EQ(GunThrow::GetWeaponAngle(3, 40, 2), 20.0F);
}

// ---- Throwing coroutine iterator state machine ------------------------------

TEST(GunThrowTest, IteratorFiresOnceThenTerminates) {
    GunThrowThrowingIterator it;
    EXPECT_EQ(it.State(), GunThrowThrowingIterator::kRunning);

    EXPECT_FALSE(it.MoveNext(2)); // childCount>0 -> throw step
    EXPECT_TRUE(it.Fired());
    EXPECT_EQ(it.State(), GunThrowThrowingIterator::kDone);

    // Second pump: state is already -1, so (state|1)==1 is false -> no fire.
    EXPECT_FALSE(it.MoveNext(2));
    EXPECT_FALSE(it.Fired());
    EXPECT_EQ(it.State(), GunThrowThrowingIterator::kDone);
}

TEST(GunThrowTest, IteratorNoChildDoesNotFire) {
    GunThrowThrowingIterator it;
    EXPECT_FALSE(it.MoveNext(0)); // childCount==0 -> predicate false
    EXPECT_FALSE(it.Fired());
    EXPECT_EQ(it.State(), GunThrowThrowingIterator::kDone);
}

// ---- RNG determinism: GunThrow's modelled math draws ZERO from the stream ---

TEST(GunThrowTest, SpreadMathConsumesNoRandomDraws) {
    // The fan-out is purely geometric: a parallel same-seeded stream must stay
    // in lockstep (un-advanced) after exercising every modelled method.
    GunThrow weapon;
    weapon.SetSeed(123456);
    RGRandom reference;
    reference.SetRandomSeed(123456);

    // Exercise the recoverable math heavily.
    for (int count = 0; count <= 12; ++count) {
        for (int idx = 0; idx < (count > 0 ? count : 1); ++idx) {
            (void)GunThrow::GetShootAngle(count, idx, 2.5F);
            (void)GunThrow::GetWeaponAngle(count, 0, idx);
        }
        (void)GunThrow::ResetConsume(8, 1, count > 0 ? count : 1);
        (void)GunThrow::SpreadSpanForCount(count);
    }
    GunThrowThrowingIterator it;
    (void)it.MoveNext(3);

    // The weapon stream must be byte-for-byte where it started: the next draw
    // equals the reference's first draw (no hidden advance happened).
    EXPECT_EQ(weapon.Rng().Range(0, 1000000), reference.Range(0, 1000000));
}

// NOLINTEND(readability-magic-numbers)
