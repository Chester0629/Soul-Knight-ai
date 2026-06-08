#include <gtest/gtest.h>

#include "combat/Gun001.hpp"
#include "data/RGRandom.hpp"

using Game::Gun001;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- SpreadHalfAngle: base + base*recoil (pure, no RNG) ----------------------

TEST(Gun001Test, SpreadIsBasePlusBaseTimesRecoil) {
    // FAITHFUL: Attack @ 315778 -- fVar4 = base + base*recoil.
    EXPECT_FLOAT_EQ(Gun001::SpreadHalfAngle(10.0F, 0.0F), 10.0F); // no recoil
    EXPECT_FLOAT_EQ(Gun001::SpreadHalfAngle(10.0F, 0.5F), 15.0F); // +50%
    EXPECT_FLOAT_EQ(Gun001::SpreadHalfAngle(8.0F, 1.0F), 16.0F);  // doubled
}

TEST(Gun001Test, SpreadHasNoClamp) {
    // The decomp applies no clamp: recoil < -1 flips the half-angle sign exactly
    // as the original multiply-add does (base + base*recoil).
    EXPECT_FLOAT_EQ(Gun001::SpreadHalfAngle(10.0F, -2.0F), -10.0F);
    EXPECT_FLOAT_EQ(Gun001::SpreadHalfAngle(0.0F, 5.0F), 0.0F); // zero base -> zero
}

// ---- ScatterAngle: one Range(-spread, +spread) draw, in bounds ---------------

TEST(Gun001Test, ScatterStaysWithinSymmetricSpread) {
    Gun001 g;
    g.SetSeed(4242);
    const float spread = Gun001::SpreadHalfAngle(12.0F, 0.25F); // 15.0
    for (int i = 0; i < 64; ++i) {
        const float a = g.ScatterAngle(12.0F, 0.25F);
        EXPECT_GE(a, -spread);
        EXPECT_LE(a, spread);
    }
}

// ---- RNG determinism: ScatterAngle draws EXACTLY ONE float, in order ----------

TEST(Gun001Test, ScatterAngleMatchesParallelStreamDraw) {
    // The modelled scatter is a single RGRandom::Range(float, float) draw with
    // the recoil-widened half-angle as the symmetric bound. A parallel
    // same-seeded stream drawing the identical Range must produce the identical
    // value -- locking both the formula and the single-draw order.
    Gun001 weapon;
    weapon.SetSeed(20240608);
    RGRandom reference;
    reference.SetRandomSeed(20240608);

    const float baseAngle = 9.0F;
    const float recoil = 0.5F;
    const float spread = baseAngle + baseAngle * recoil; // 13.5

    for (int shot = 0; shot < 32; ++shot) {
        const float got = weapon.ScatterAngle(baseAngle, recoil);
        const float want = reference.Range(-spread, spread);
        EXPECT_FLOAT_EQ(got, want);
    }
}

TEST(Gun001Test, ScatterAngleAdvancesStreamExactlyOncePerShot) {
    // After N ScatterAngle calls the weapon stream must equal a reference stream
    // advanced by EXACTLY N float draws of the same Range -- no hidden extra
    // draws, no missed draws.
    Gun001 weapon;
    weapon.SetSeed(777);
    RGRandom reference;
    reference.SetRandomSeed(777);

    const float spread = Gun001::SpreadHalfAngle(20.0F, 0.0F); // 20.0
    for (int i = 0; i < 10; ++i) {
        (void)weapon.ScatterAngle(20.0F, 0.0F);
        (void)reference.Range(-spread, spread);
    }
    // Both streams have advanced 10 draws; their next int draw must agree.
    EXPECT_EQ(weapon.Rng().Range(0, 1000000), reference.Range(0, 1000000));
}

// ---- SpreadHalfAngle is pure: it never advances the stream -------------------

TEST(Gun001Test, SpreadHalfAngleDrawsNothing) {
    Gun001 weapon;
    weapon.SetSeed(135790);
    RGRandom reference;
    reference.SetRandomSeed(135790);

    for (int i = 0; i < 16; ++i) {
        (void)Gun001::SpreadHalfAngle(10.0F, 0.3F);
    }
    // The static spread math touches no instance state -> stream un-advanced.
    EXPECT_EQ(weapon.Rng().Range(0, 1000000), reference.Range(0, 1000000));
}

// NOLINTEND(readability-magic-numbers)
