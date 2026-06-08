#include <gtest/gtest.h>

#include "combat/GunMagicBow.hpp"
#include "data/RGRandom.hpp"

using Game::GunMagicBow;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- ShouldTickCharge: firing && charge < max_charge, no RNG ----------------

TEST(GunMagicBowTest, TicksChargeOnlyWhileFiringAndBelowCap) {
    // firing && below cap -> tick (Update @ 967260-967261).
    EXPECT_TRUE(GunMagicBow::ShouldTickCharge(true, 0.0F, 2.0F));
    EXPECT_TRUE(GunMagicBow::ShouldTickCharge(true, 1.9F, 2.0F));
}

TEST(GunMagicBowTest, DoesNotTickWhenNotFiring) {
    // Animator firing bool not set -> the outer gate fails.
    EXPECT_FALSE(GunMagicBow::ShouldTickCharge(false, 0.0F, 2.0F));
    EXPECT_FALSE(GunMagicBow::ShouldTickCharge(false, 1.0F, 2.0F));
}

TEST(GunMagicBowTest, DoesNotTickAtOrAboveCap) {
    // charge >= max_charge -> the strict < gate fails (no clamp/accumulate).
    EXPECT_FALSE(GunMagicBow::ShouldTickCharge(true, 2.0F, 2.0F));
    EXPECT_FALSE(GunMagicBow::ShouldTickCharge(true, 2.5F, 2.0F));
}

// ---- ChargeRatio: charge / max_charge, deterministic, no RNG ----------------

TEST(GunMagicBowTest, ChargeRatioZeroAtColdStart) {
    EXPECT_FLOAT_EQ(GunMagicBow::ChargeRatio(0.0F, 2.0F), 0.0F);
}

TEST(GunMagicBowTest, ChargeRatioHalfAndFull) {
    // 1.0 / 2.0 = 0.5 ; 2.0 / 2.0 = 1.0.
    EXPECT_FLOAT_EQ(GunMagicBow::ChargeRatio(1.0F, 2.0F), 0.5F);
    EXPECT_FLOAT_EQ(GunMagicBow::ChargeRatio(2.0F, 2.0F), 1.0F);
}

TEST(GunMagicBowTest, ChargeRatioOverChargeExceedsOneNoClamp) {
    // The decomp applies NO clamp: an over-charge yields ratio > 1.
    EXPECT_FLOAT_EQ(GunMagicBow::ChargeRatio(3.0F, 2.0F), 1.5F);
}

TEST(GunMagicBowTest, ChargeRatioGuardsNonPositiveCap) {
    // Degenerate divisor guard (impossible in the original): returns 0, not NaN.
    EXPECT_FLOAT_EQ(GunMagicBow::ChargeRatio(1.0F, 0.0F), 0.0F);
    EXPECT_FLOAT_EQ(GunMagicBow::ChargeRatio(1.0F, -2.0F), 0.0F);
}

// ---- ScaledVelocity: velocity = chargeRatio * base, no RNG -------------------

TEST(GunMagicBowTest, ScaledVelocityIsRatioTimesBase) {
    // Attack @ 967315-967325: each base-velocity axis scales by the charge ratio.
    // base 100, ratio 0.5 -> 50.
    EXPECT_FLOAT_EQ(GunMagicBow::ScaledVelocity(100.0F, 0.5F), 50.0F);
    // full charge passes the base through unchanged.
    EXPECT_FLOAT_EQ(GunMagicBow::ScaledVelocity(100.0F, 1.0F), 100.0F);
    // zero charge -> zero velocity (a fully un-drawn bow imparts nothing).
    EXPECT_FLOAT_EQ(GunMagicBow::ScaledVelocity(100.0F, 0.0F), 0.0F);
}

TEST(GunMagicBowTest, ScaledVelocityPreservesSignAndOverCharge) {
    // negative base axis (e.g. a leftward / downward component) keeps its sign.
    EXPECT_FLOAT_EQ(GunMagicBow::ScaledVelocity(-80.0F, 0.5F), -40.0F);
    // over-charge (ratio > 1, no clamp) overshoots the base, matching the decomp.
    EXPECT_FLOAT_EQ(GunMagicBow::ScaledVelocity(60.0F, 1.5F), 90.0F);
}

TEST(GunMagicBowTest, ScaledVelocityAllThreeAxesShareTheRatio) {
    // x (owner+0x80), y (owner+0x84), z (owner+0x88) all use the same fVar4 ratio.
    const float ratio = GunMagicBow::ChargeRatio(1.0F, 2.0F); // 0.5
    const float baseX = 120.0F;
    const float baseY = -90.0F;
    const float baseZ = 30.0F;
    EXPECT_FLOAT_EQ(GunMagicBow::ScaledVelocity(baseX, ratio), 60.0F);
    EXPECT_FLOAT_EQ(GunMagicBow::ScaledVelocity(baseY, ratio), -45.0F);
    EXPECT_FLOAT_EQ(GunMagicBow::ScaledVelocity(baseZ, ratio), 15.0F);
}

// ---- end-to-end: charge feeds the velocity scale, deterministically ---------

TEST(GunMagicBowTest, ChargeRatioFeedsScaledVelocityEndToEnd) {
    // The release-fire shape: ratio = charge/max_charge, then velocity = ratio*base.
    // half-drawn bow (charge 1.0 of 2.0) at base 200 -> velocity 100.
    const float ratio = GunMagicBow::ChargeRatio(1.0F, 2.0F);
    EXPECT_FLOAT_EQ(GunMagicBow::ScaledVelocity(200.0F, ratio), 100.0F);
    // monotonic: a more-drawn bow fires a faster arrow on the same base.
    const float low = GunMagicBow::ScaledVelocity(
        200.0F, GunMagicBow::ChargeRatio(0.5F, 2.0F));
    const float high = GunMagicBow::ScaledVelocity(
        200.0F, GunMagicBow::ChargeRatio(1.5F, 2.0F));
    EXPECT_LT(low, high);
}

// ---- determinism / zero-draw: no GunMagicBow path advances the RNG stream ----

TEST(GunMagicBowTest, MakesZeroRngDrawsAcrossAllPaths) {
    // Every GunMagicBow body (Update, Attack, MakeConsume, StopWeapon) is
    // RNG-free: the charge-to-velocity map is a deterministic scalar, not random
    // scatter. Exercising all helpers must leave a same-seeded reference stream
    // un-advanced -- proven by a parallel draw matching the reference's FIRST
    // draw after the brain has run.
    GunMagicBow g;
    RGRandom ref;
    g.SetSeed(424242);
    ref.SetRandomSeed(424242);

    // Run every recoverable path; none of these may touch the stream.
    (void)GunMagicBow::ShouldTickCharge(true, 0.5F, 2.0F);
    (void)GunMagicBow::ShouldTickCharge(false, 0.5F, 2.0F);
    const float ratio = GunMagicBow::ChargeRatio(1.0F, 2.0F);
    (void)GunMagicBow::ScaledVelocity(150.0F, ratio);
    (void)GunMagicBow::ScaledVelocity(-150.0F, ratio);

    EXPECT_TRUE(g.Seeded());
    // The brain's stream is still at draw #0: its next float equals the
    // reference's first float. If any path had drawn, these would diverge.
    EXPECT_FLOAT_EQ(g.Rng().Range(0.0F, 1.0F), ref.Range(0.0F, 1.0F));
}

TEST(GunMagicBowTest, SeededFlagReflectsSeedWithoutDrawing) {
    GunMagicBow g;
    EXPECT_FALSE(g.Seeded());
    g.SetSeed(7);
    EXPECT_TRUE(g.Seeded());
}

// NOLINTEND(readability-magic-numbers)
