#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "combat/Gun016.hpp"
#include "data/RGRandom.hpp"

using Game::Gun016;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- HeatRatio: shoot_time / shoot_max_time, deterministic, no RNG ----------

TEST(Gun016Test, HeatRatioZeroAtColdStart) {
    // No heat accumulated -> ratio 0.
    EXPECT_FLOAT_EQ(Gun016::HeatRatio(0.0F, Gun016::kShootMaxTime), 0.0F);
}

TEST(Gun016Test, HeatRatioHalfAndFull) {
    // 1.0 / 2.0 = 0.5 ; 2.0 / 2.0 = 1.0.
    EXPECT_FLOAT_EQ(Gun016::HeatRatio(1.0F, Gun016::kShootMaxTime), 0.5F);
    EXPECT_FLOAT_EQ(Gun016::HeatRatio(2.0F, Gun016::kShootMaxTime), 1.0F);
}

TEST(Gun016Test, HeatRatioOverHeatExceedsOneNoClamp) {
    // The decomp applies NO clamp: over-heat yields ratio > 1.
    EXPECT_FLOAT_EQ(Gun016::HeatRatio(3.0F, Gun016::kShootMaxTime), 1.5F);
}

TEST(Gun016Test, HeatRatioGuardsNonPositiveCap) {
    // Degenerate divisor guard (impossible in the original): returns 0, not NaN.
    EXPECT_FLOAT_EQ(Gun016::HeatRatio(1.0F, 0.0F), 0.0F);
}

// ---- ShouldTickHeat: firing && shoot_time < shoot_max_time, no RNG ----------

TEST(Gun016Test, TicksHeatOnlyWhileFiringAndBelowCap) {
    // firing && below cap -> tick.
    EXPECT_TRUE(Gun016::ShouldTickHeat(true, 0.0F, Gun016::kShootMaxTime));
    EXPECT_TRUE(Gun016::ShouldTickHeat(true, 1.9F, Gun016::kShootMaxTime));
}

TEST(Gun016Test, DoesNotTickWhenNotFiring) {
    EXPECT_FALSE(Gun016::ShouldTickHeat(false, 0.0F, Gun016::kShootMaxTime));
}

TEST(Gun016Test, DoesNotTickAtOrAboveCap) {
    // shoot_time >= shoot_max_time -> the strict < gate fails.
    EXPECT_FALSE(Gun016::ShouldTickHeat(true, Gun016::kShootMaxTime,
                                        Gun016::kShootMaxTime));
    EXPECT_FALSE(Gun016::ShouldTickHeat(true, 2.5F, Gun016::kShootMaxTime));
}

// ---- Spread: Max(0, base + base*recoilMul + heatRatio*maxDeviation) ---------

TEST(Gun016Test, SpreadColdIsRecoilFanOnly) {
    // heatRatio 0 -> base + base*recoilMul (the heat term vanishes).
    // 10 + 10*0.5 = 15.
    EXPECT_FLOAT_EQ(Gun016::Spread(10.0F, 0.5F, 0.0F), 15.0F);
    // recoilMul 0 -> just the base angle.
    EXPECT_FLOAT_EQ(Gun016::Spread(12.0F, 0.0F, 0.0F), 12.0F);
}

TEST(Gun016Test, SpreadTightensWithHeatBecauseMaxDeviationIsNegative) {
    // max_deviation is -15, so the heat term SUBTRACTS: building heat tightens
    // the cone. base=20, recoilMul=0 -> raw = 20 + heat*(-15).
    // heat 0   -> 20 ; heat 0.5 -> 20 - 7.5 = 12.5 ; heat 1 -> 20 - 15 = 5.
    EXPECT_FLOAT_EQ(Gun016::Spread(20.0F, 0.0F, 0.0F), 20.0F);
    EXPECT_FLOAT_EQ(Gun016::Spread(20.0F, 0.0F, 0.5F), 12.5F);
    EXPECT_FLOAT_EQ(Gun016::Spread(20.0F, 0.0F, 1.0F), 5.0F);
    // Monotonic decrease as heat rises (the spin-up "settle" behaviour).
    EXPECT_GT(Gun016::Spread(20.0F, 0.0F, 0.25F),
              Gun016::Spread(20.0F, 0.0F, 0.75F));
}

TEST(Gun016Test, SpreadFlooredAtZeroByMaxClamp) {
    // raw goes negative when the heat term dominates; Mathf.Max(0,.) floors it.
    // base=10, recoilMul=0, heat 1 -> 10 + 1*(-15) = -5 -> clamped to 0.
    EXPECT_FLOAT_EQ(Gun016::Spread(10.0F, 0.0F, 1.0F), 0.0F);
    // deep over-heat stays floored, never negative.
    EXPECT_FLOAT_EQ(Gun016::Spread(10.0F, 0.0F, 5.0F), 0.0F);
}

TEST(Gun016Test, SpreadCombinesRecoilFanAndHeatTerm) {
    // Full formula: base + base*recoilMul + heat*(-15).
    // base=18, recoilMul=0.5, heat=0.4 -> 18 + 9 + 0.4*(-15) = 27 - 6 = 21.
    EXPECT_FLOAT_EQ(Gun016::Spread(18.0F, 0.5F, 0.4F), 21.0F);
}

// ---- ScatterAngle: exactly ONE float draw, symmetric in [-spread, spread] ---

TEST(Gun016Test, ScatterStaysWithinSpreadBounds) {
    Gun016 g;
    g.SetSeed(1234);
    const float spread = Gun016::Spread(15.0F, 0.2F, 0.3F);
    for (int i = 0; i < 64; ++i) {
        const float a = g.ScatterAngle(spread);
        EXPECT_GE(a, -spread);
        EXPECT_LE(a, spread);
    }
}

TEST(Gun016Test, ScatterDrawsExactlyOneFloatInOrder) {
    // A parallel same-seeded RGRandom must reproduce each scatter draw exactly:
    // proves the brain draws ONE float per shot, in order, with the -spread/
    // +spread bounds and nothing else advancing the stream.
    Gun016 g;
    RGRandom ref;
    g.SetSeed(98765);
    ref.SetRandomSeed(98765);

    const std::vector<float> spreads = {5.0F, 12.5F, 0.0F, 7.0F, 18.0F};
    for (const float spread : spreads) {
        const float got = g.ScatterAngle(spread);
        const float expected = ref.Range(-spread, spread); // one parallel draw
        EXPECT_FLOAT_EQ(got, expected);
    }
}

TEST(Gun016Test, ScatterIsDeterministicForSameSeed) {
    Gun016 a;
    Gun016 b;
    a.SetSeed(555);
    b.SetSeed(555);
    for (int i = 0; i < 32; ++i) {
        EXPECT_FLOAT_EQ(a.ScatterAngle(15.0F), b.ScatterAngle(15.0F));
    }
}

// ---- end-to-end: heat path feeds one scatter draw per shot, in lockstep -----

TEST(Gun016Test, OneScatterDrawPerShotAcrossRisingHeat) {
    // Simulate a burst whose heat climbs each shot; the spread shrinks while the
    // scatter draw count stays exactly one per shot. A parallel same-seeded
    // stream must reproduce the whole sequence (count + order lockstep).
    auto run = [](int seed) -> std::vector<float> {
        Gun016 g;
        g.SetSeed(seed);
        std::vector<float> trace;
        // five shots, heat ratio rising 0.0, 0.25, 0.5, 0.75, 1.0.
        for (int shot = 0; shot < 5; ++shot) {
            const float heat = Gun016::HeatRatio(
                0.5F * static_cast<float>(shot), Gun016::kShootMaxTime);
            const float spread = Gun016::Spread(20.0F, 0.3F, heat);
            trace.push_back(g.ScatterAngle(spread));
        }
        return trace;
    };
    const auto a = run(31337);
    const auto b = run(31337);
    ASSERT_EQ(a.size(), b.size());
    EXPECT_EQ(a.size(), static_cast<std::size_t>(5));
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_FLOAT_EQ(a[i], b[i]);
    }
}

// NOLINTEND(readability-magic-numbers)
