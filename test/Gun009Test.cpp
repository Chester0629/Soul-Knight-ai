#include <gtest/gtest.h>

#include "combat/Gun009.hpp"
#include "data/RGRandom.hpp"

using Game::Gun009;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- ComputeSpread: baseAngle + baseAngle*recoil == baseAngle*(1+recoil) -----
// FAITHFUL: Gun009__Attack @ 963998. Pure scalar, ZERO RNG draws.

TEST(Gun009Test, ComputeSpreadIsAngleTimesOnePlusRecoil) {
    EXPECT_FLOAT_EQ(Gun009::ComputeSpread(10.0F, 0.0F), 10.0F);  // no recoil
    EXPECT_FLOAT_EQ(Gun009::ComputeSpread(10.0F, 0.5F), 15.0F);  // 10 + 10*0.5
    EXPECT_FLOAT_EQ(Gun009::ComputeSpread(10.0F, 1.0F), 20.0F);  // 10 + 10*1
    EXPECT_FLOAT_EQ(Gun009::ComputeSpread(8.0F, 2.0F), 24.0F);   // 8 + 8*2
}

TEST(Gun009Test, ComputeSpreadAppliesNoClamp) {
    // The decomp has no clamp: a zero baseAngle yields a zero half-angle, and a
    // recoil < -1 flips the sign exactly as the original arithmetic does.
    EXPECT_FLOAT_EQ(Gun009::ComputeSpread(0.0F, 5.0F), 0.0F);
    EXPECT_FLOAT_EQ(Gun009::ComputeSpread(10.0F, -2.0F), -10.0F); // 10 + 10*-2
}

// ---- RollScatter: ONE symmetric RGRandom::Range(-spread, +spread) draw --------
// FAITHFUL: Gun009__Attack @ 964021.

TEST(Gun009Test, RollScatterMatchesSymmetricDrawOnParallelStream) {
    // The weapon's single draw must equal a same-seeded reference stream drawing
    // Range(-spread, +spread) with the SAME bounds, in the SAME order.
    Gun009 weapon;
    weapon.SetSeed(424242);
    RGRandom reference;
    reference.SetRandomSeed(424242);

    const float baseAngle = 12.0F;
    const float recoil = 0.25F;
    const float spread = Gun009::ComputeSpread(baseAngle, recoil); // 15.0

    const float got = weapon.RollScatter(baseAngle, recoil);
    const float want = reference.Range(-spread, spread);
    EXPECT_FLOAT_EQ(got, want);
}

TEST(Gun009Test, RollScatterDrawsExactlyOncePerShot) {
    // Each shot advances the stream by exactly ONE float draw, in lockstep with a
    // parallel reference that draws the matching symmetric range each shot.
    Gun009 weapon;
    weapon.SetSeed(7);
    RGRandom reference;
    reference.SetRandomSeed(7);

    const float baseAngle = 6.0F;
    const float recoil = 1.0F;
    const float spread = Gun009::ComputeSpread(baseAngle, recoil); // 12.0

    for (int shot = 0; shot < 32; ++shot) {
        const float got = weapon.RollScatter(baseAngle, recoil);
        const float want = reference.Range(-spread, spread);
        EXPECT_FLOAT_EQ(got, want);
    }

    // After equal draw counts the streams remain in lockstep: the next raw int
    // draw matches, proving no hidden extra advance happened in RollScatter.
    EXPECT_EQ(weapon.Rng().Range(0, 1000000), reference.Range(0, 1000000));
}

TEST(Gun009Test, RollScatterStaysWithinSpreadBounds) {
    // Range(float) is max-inclusive: every scatter offset lies in [-spread, +spread].
    Gun009 weapon;
    weapon.SetSeed(99);

    const float baseAngle = 20.0F;
    const float recoil = 0.5F;
    const float spread = Gun009::ComputeSpread(baseAngle, recoil); // 30.0

    for (int shot = 0; shot < 64; ++shot) {
        const float offset = weapon.RollScatter(baseAngle, recoil);
        EXPECT_GE(offset, -spread);
        EXPECT_LE(offset, spread);
    }
}

TEST(Gun009Test, ZeroSpreadDrawIsZeroButStillConsumesADraw) {
    // A zero half-angle still issues the draw Range(0, 0) -> 0 (the decomp does not
    // skip the draw), so the stream must still advance exactly one step.
    Gun009 weapon;
    weapon.SetSeed(555);
    RGRandom reference;
    reference.SetRandomSeed(555);

    const float offset = weapon.RollScatter(0.0F, 3.0F); // spread == 0
    EXPECT_FLOAT_EQ(offset, 0.0F);

    // The reference must advance by the same single Range(0,0) draw to stay aligned.
    (void)reference.Range(0.0F, 0.0F);
    EXPECT_EQ(weapon.Rng().Range(0, 1000000), reference.Range(0, 1000000));
}

// NOLINTEND(readability-magic-numbers)
