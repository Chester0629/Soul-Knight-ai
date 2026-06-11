#include <gtest/gtest.h>

#include "combat/Gun013.hpp"
#include "data/RGRandom.hpp"

using Game::Gun013;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- SpreadHalfSpan: baseAngle + baseAngle*recoil (the pre-draw half-span) ----

TEST(Gun013Test, SpreadHalfSpanIsBaseAngleTimesOnePlusRecoil) {
    // FAITHFUL: Attack @ 964334 -- fVar4 = baseAngle + baseAngle * recoil.
    EXPECT_FLOAT_EQ(Gun013::SpreadHalfSpan(10.0F, 0.0F), 10.0F);  // recoil 0 -> baseAngle
    EXPECT_FLOAT_EQ(Gun013::SpreadHalfSpan(10.0F, 1.0F), 20.0F);  // recoil 1 -> doubled
    EXPECT_FLOAT_EQ(Gun013::SpreadHalfSpan(8.0F, 0.5F), 12.0F);   // 8 + 8*0.5
}

TEST(Gun013Test, SpreadHalfSpanAppliesNoClamp) {
    // The decomp applies NO clamp; a negative recoil < -1 yields a negative span,
    // exactly as the bare additive expression computes (not floored at 0).
    EXPECT_FLOAT_EQ(Gun013::SpreadHalfSpan(10.0F, -2.0F), -10.0F); // 10 + 10*-2
    EXPECT_FLOAT_EQ(Gun013::SpreadHalfSpan(0.0F, 5.0F), 0.0F);     // zero base -> zero
}

// ---- Scatter: exactly ONE RGRandom::Range(float) draw, symmetric ------------

TEST(Gun013Test, ScatterEqualsSymmetricRangeDraw) {
    // FAITHFUL: Attack @ 964337 -- RGRandom::Range(-spread, +spread), one float
    // draw (max INCLUSIVE). A parallel same-seeded reference reproduces it.
    const float baseAngle = 12.0F;
    const float recoil = 0.25F;
    const float spread = Gun013::SpreadHalfSpan(baseAngle, recoil); // 12 + 12*0.25 = 15

    Gun013 weapon;
    weapon.SetSeed(424242);
    RGRandom reference;
    reference.SetRandomSeed(424242);

    const float got = weapon.Scatter(baseAngle, recoil);
    const float want = reference.Range(-spread, spread);
    EXPECT_FLOAT_EQ(got, want);
}

TEST(Gun013Test, ScatterDrawsExactlyOnePerCallInOrder) {
    // The draw COUNT and ORDER must match the decomp: one float per Scatter().
    // A reference stream drawing the same symmetric range, the same number of
    // times, must stay byte-for-byte in lockstep across a sequence of shots.
    const float baseAngle = 20.0F;
    const float recoil = 0.5F;
    const float spread = Gun013::SpreadHalfSpan(baseAngle, recoil); // 30

    Gun013 weapon;
    weapon.SetSeed(7);
    RGRandom reference;
    reference.SetRandomSeed(7);

    for (int shot = 0; shot < 8; ++shot) {
        EXPECT_FLOAT_EQ(weapon.Scatter(baseAngle, recoil),
                        reference.Range(-spread, spread));
    }

    // After 8 paired draws both streams are at the same position: the next draw
    // (an int this time) must still agree -> exactly one draw per Scatter, no
    // hidden extra advance.
    EXPECT_EQ(weapon.Rng().Range(0, 1000000), reference.Range(0, 1000000));
}

TEST(Gun013Test, ScatterStaysWithinHalfSpan) {
    // Symmetric scatter: every draw lies in [-spread, +spread] (max inclusive).
    const float baseAngle = 15.0F;
    const float recoil = 1.0F;
    const float spread = Gun013::SpreadHalfSpan(baseAngle, recoil); // 30

    Gun013 weapon;
    weapon.SetSeed(99);
    for (int shot = 0; shot < 64; ++shot) {
        const float s = weapon.Scatter(baseAngle, recoil);
        EXPECT_GE(s, -spread);
        EXPECT_LE(s, spread);
    }
}

TEST(Gun013Test, ZeroSpanScattersToZero) {
    // recoil == -1 makes the half-span 0; Range(0, 0) yields 0 with no spread,
    // and still consumes exactly one float draw (lockstep preserved).
    Gun013 weapon;
    weapon.SetSeed(2024);
    RGRandom reference;
    reference.SetRandomSeed(2024);

    const float got = weapon.Scatter(40.0F, -1.0F); // span = 40 + 40*-1 = 0
    EXPECT_FLOAT_EQ(got, reference.Range(0.0F, 0.0F));
    EXPECT_FLOAT_EQ(got, 0.0F);
}

// NOLINTEND(readability-magic-numbers)
