#include <gtest/gtest.h>

#include "combat/GunHeroBow.hpp"
#include "data/RGRandom.hpp"

using Game::GunHeroBow;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- ChargeRatio: charge / max_charge, deterministic, no RNG ----------------

TEST(GunHeroBowTest, ChargeRatioZeroAtNoCharge) {
    EXPECT_FLOAT_EQ(GunHeroBow::ChargeRatio(0.0F, 2.0F), 0.0F);
}

TEST(GunHeroBowTest, ChargeRatioHalfAndFull) {
    EXPECT_FLOAT_EQ(GunHeroBow::ChargeRatio(1.0F, 2.0F), 0.5F);
    EXPECT_FLOAT_EQ(GunHeroBow::ChargeRatio(2.0F, 2.0F), 1.0F);
}

TEST(GunHeroBowTest, ChargeRatioOverChargeExceedsOneNoClamp) {
    // The decomp applies NO clamp: over-charge yields ratio > 1.
    EXPECT_FLOAT_EQ(GunHeroBow::ChargeRatio(3.0F, 2.0F), 1.5F);
}

TEST(GunHeroBowTest, ChargeRatioGuardsNonPositiveCap) {
    // Degenerate divisor guard (impossible in the original): returns 0, not NaN.
    EXPECT_FLOAT_EQ(GunHeroBow::ChargeRatio(1.0F, 0.0F), 0.0F);
    EXPECT_FLOAT_EQ(GunHeroBow::ChargeRatio(1.0F, -2.0F), 0.0F);
}

// ---- ScaledSpeed: chargeRatio * baseSpeed -----------------------------------

TEST(GunHeroBowTest, ScaledSpeedScalesBaseByRatio) {
    // half-drawn bow -> half speed; fully drawn -> full speed.
    EXPECT_FLOAT_EQ(GunHeroBow::ScaledSpeed(200.0F, 0.5F), 100.0F);
    EXPECT_FLOAT_EQ(GunHeroBow::ScaledSpeed(200.0F, 1.0F), 200.0F);
    EXPECT_FLOAT_EQ(GunHeroBow::ScaledSpeed(200.0F, 0.0F), 0.0F);
}

TEST(GunHeroBowTest, ScaledSpeedComposesWithChargeRatio) {
    // base 300, charge 1.5 / max 2.0 -> ratio 0.75 -> 225.
    const float ratio = GunHeroBow::ChargeRatio(1.5F, 2.0F);
    EXPECT_FLOAT_EQ(GunHeroBow::ScaledSpeed(300.0F, ratio), 225.0F);
}

// ---- HasArrows: arrow_count >= 1 gate (else dry-release SFX) -----------------

TEST(GunHeroBowTest, HasArrowsFiresOnlyWithAtLeastOne) {
    EXPECT_FALSE(GunHeroBow::HasArrows(0));   // empty quiver -> dry SFX, no fire
    EXPECT_FALSE(GunHeroBow::HasArrows(-3));  // negative guarded the same way
    EXPECT_TRUE(GunHeroBow::HasArrows(1));
    EXPECT_TRUE(GunHeroBow::HasArrows(5));
    EXPECT_EQ(GunHeroBow::kMinArrowsToFire, 1);
}

// ---- ShouldFireNext: fires exactly arrow_count arrows (indices 0..n-1) -------

TEST(GunHeroBowTest, ShouldFireNextRespectsArrowCount) {
    // single arrow: after firing index 0, 0+1 < 1 is false -> stop.
    EXPECT_FALSE(GunHeroBow::ShouldFireNext(0, 1));
    // three arrows: fire indices 0,1,2 -> continue after 0 and 1, stop after 2.
    EXPECT_TRUE(GunHeroBow::ShouldFireNext(0, 3));
    EXPECT_TRUE(GunHeroBow::ShouldFireNext(1, 3));
    EXPECT_FALSE(GunHeroBow::ShouldFireNext(2, 3));
}

TEST(GunHeroBowTest, ShouldFireNextFiresExactlyArrowCountArrows) {
    // Walk the loop: count how many arrows fire for a given arrow_count.
    for (int n = 1; n <= 6; ++n) {
        int fired = 1; // Attack always fires index 0 once HasArrows passed
        int index = 0;
        while (GunHeroBow::ShouldFireNext(index, n)) {
            ++index;
            ++fired;
        }
        EXPECT_EQ(fired, n) << "arrow_count " << n;
    }
}

// ---- Determinism: ZERO RNG draws on every path ------------------------------

TEST(GunHeroBowTest, MakesNoRngDrawsAcrossEveryPath) {
    GunHeroBow bow;
    RGRandom ref;
    bow.SetSeed(4242);
    ref.SetRandomSeed(4242);

    // Exercise every recoverable path; none may advance the stream.
    for (int i = 0; i < 16; ++i) {
        const float ratio = GunHeroBow::ChargeRatio(static_cast<float>(i), 8.0F);
        (void)GunHeroBow::ScaledSpeed(150.0F, ratio);
        (void)GunHeroBow::HasArrows(i - 4);
        (void)GunHeroBow::ShouldFireNext(i % 3, 3);
    }

    // A parallel same-seeded RGRandom must still produce the identical next draw:
    // proof the bow drew nothing (draw count = 0, stream non-advancing).
    EXPECT_EQ(bow.Rng().Range(0, 1000000), ref.Range(0, 1000000));
    EXPECT_TRUE(bow.Seeded());
}

// NOLINTEND(readability-magic-numbers)
