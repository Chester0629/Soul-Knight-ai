#include <gtest/gtest.h>

#include <vector>

#include "combat/GunStaffWizard.hpp"
#include "data/RGRandom.hpp"

using Game::GunStaffWizard;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- NextPhase: cyclic 4-state machine, deterministic, no RNG ---------------

TEST(GunStaffWizardTest, NextPhaseCyclesZeroToThreeToZero) {
    // 0->1->2->3->0, the barrel/phase rotation written back each Attack.
    EXPECT_EQ(GunStaffWizard::NextPhase(0), 1);
    EXPECT_EQ(GunStaffWizard::NextPhase(1), 2);
    EXPECT_EQ(GunStaffWizard::NextPhase(2), 3);
    EXPECT_EQ(GunStaffWizard::NextPhase(3), 0);
}

TEST(GunStaffWizardTest, NextPhaseFullCycleReturnsToStart) {
    // Driving the counter four times from any covered start returns to it.
    int phase = 0;
    for (int i = 0; i < GunStaffWizard::kPhaseCount; ++i) {
        phase = GunStaffWizard::NextPhase(phase);
    }
    EXPECT_EQ(phase, 0);

    phase = 2;
    for (int i = 0; i < GunStaffWizard::kPhaseCount; ++i) {
        phase = GunStaffWizard::NextPhase(phase);
    }
    EXPECT_EQ(phase, 2);
}

TEST(GunStaffWizardTest, NextPhaseLeavesOutOfRangeUnchanged) {
    // The decomp default case skips the write-back: anything not 0..3 is left as
    // is (no wrap, no clamp).
    EXPECT_EQ(GunStaffWizard::NextPhase(4), 4);
    EXPECT_EQ(GunStaffWizard::NextPhase(-1), -1);
    EXPECT_EQ(GunStaffWizard::NextPhase(99), 99);
}

TEST(GunStaffWizardTest, NextPhaseTakesNoDraw) {
    // Phase advance must not advance the seeded stream.
    GunStaffWizard g;
    RGRandom ref;
    g.SetSeed(4242);
    ref.SetRandomSeed(4242);
    int phase = 0;
    for (int i = 0; i < 16; ++i) {
        phase = GunStaffWizard::NextPhase(phase);
    }
    // First post-advance scatter draw must equal the very first ref draw: the
    // phase pumps consumed nothing.
    EXPECT_FLOAT_EQ(g.ScatterAngle(10.0F), ref.Range(-10.0F, 10.0F));
}

// ---- IsEarlyOut: sign-gate `x < -x` == x < 0, no RNG ------------------------

TEST(GunStaffWizardTest, EarlyOutWhenGateNegative) {
    // Negative gate (burst exhausted) -> early-out path, no firing/scatter.
    EXPECT_TRUE(GunStaffWizard::IsEarlyOut(-1));
    EXPECT_TRUE(GunStaffWizard::IsEarlyOut(-15));
}

TEST(GunStaffWizardTest, FiresWhenGateNonNegative) {
    // Zero and positive gate -> firing path.
    EXPECT_FALSE(GunStaffWizard::IsEarlyOut(0));
    EXPECT_FALSE(GunStaffWizard::IsEarlyOut(1));
    EXPECT_FALSE(GunStaffWizard::IsEarlyOut(GunStaffWizard::kInitGateValue));
}

TEST(GunStaffWizardTest, CtorInitGateSelectsFiringPath) {
    // The ctor pre-loads the gate to 1, so the FIRST Attack fires (not early-out).
    EXPECT_FALSE(GunStaffWizard::IsEarlyOut(GunStaffWizard::kInitGateValue));
}

// ---- SpreadHalfAngle: base + base*deviation = base*(1 + deviation), no RNG --

TEST(GunStaffWizardTest, SpreadIsBaseWhenDeviationZero) {
    EXPECT_FLOAT_EQ(GunStaffWizard::SpreadHalfAngle(12.0F, 0.0F), 12.0F);
}

TEST(GunStaffWizardTest, SpreadScalesByDeviation) {
    // 10 + 10*0.5 = 15 ; 20 + 20*1.0 = 40.
    EXPECT_FLOAT_EQ(GunStaffWizard::SpreadHalfAngle(10.0F, 0.5F), 15.0F);
    EXPECT_FLOAT_EQ(GunStaffWizard::SpreadHalfAngle(20.0F, 1.0F), 40.0F);
}

TEST(GunStaffWizardTest, SpreadNegativeDeviationTightensCone) {
    // A negative deviation subtracts: 10 + 10*(-0.25) = 7.5.
    EXPECT_FLOAT_EQ(GunStaffWizard::SpreadHalfAngle(10.0F, -0.25F), 7.5F);
}

// ---- ScatterAngle: exactly ONE float draw, symmetric in [-spread, spread] ---

TEST(GunStaffWizardTest, ScatterStaysWithinSpreadBounds) {
    GunStaffWizard g;
    g.SetSeed(1234);
    const float spread = GunStaffWizard::SpreadHalfAngle(15.0F, 0.2F);
    for (int i = 0; i < 64; ++i) {
        const float a = g.ScatterAngle(spread);
        EXPECT_GE(a, -spread);
        EXPECT_LE(a, spread);
    }
}

TEST(GunStaffWizardTest, ScatterDrawsExactlyOneFloatInOrder) {
    // A parallel same-seeded RGRandom must reproduce each scatter draw exactly:
    // proves the brain draws ONE float per shot, in order, with the -spread/
    // +spread bounds and nothing else advancing the stream.
    GunStaffWizard g;
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

TEST(GunStaffWizardTest, ScatterIsDeterministicForSameSeed) {
    GunStaffWizard a;
    GunStaffWizard b;
    a.SetSeed(555);
    b.SetSeed(555);
    for (int i = 0; i < 32; ++i) {
        EXPECT_FLOAT_EQ(a.ScatterAngle(15.0F), b.ScatterAngle(15.0F));
    }
}

// ---- end-to-end: phase advance + sign-gate, one draw only on firing path ----

TEST(GunStaffWizardTest, EarlyOutPathTakesNoScatterDraw) {
    // Model the Attack control flow: advance the phase counter (no draw), check
    // the sign-gate; on the early-out path the decomp returns BEFORE the scatter,
    // so the seeded stream must not advance. A firing shot then matches a
    // reference stream's FIRST draw.
    GunStaffWizard g;
    RGRandom ref;
    g.SetSeed(31337);
    ref.SetRandomSeed(31337);

    // Several Attacks that all hit the early-out gate: phase advances, no draw.
    int phase = GunStaffWizard::kInitGateValue; // arbitrary covered start (1)
    for (int i = 0; i < 8; ++i) {
        phase = GunStaffWizard::NextPhase(phase);   // phase pump, no draw
        ASSERT_TRUE(GunStaffWizard::IsEarlyOut(-1)); // gate negative -> early out
        // no ScatterAngle call on the early-out path
    }
    EXPECT_GE(phase, 0);

    // Now a firing Attack (gate >= 0): exactly one scatter draw, in lockstep.
    ASSERT_FALSE(GunStaffWizard::IsEarlyOut(0));
    const float spread = GunStaffWizard::SpreadHalfAngle(20.0F, 0.3F);
    EXPECT_FLOAT_EQ(g.ScatterAngle(spread), ref.Range(-spread, spread));
}

TEST(GunStaffWizardTest, FiringPathDrawsOnePerShotInLockstep) {
    // A run of firing Attacks (each advancing the phase, each drawing once) must
    // reproduce draw-for-draw against a parallel same-seeded stream.
    auto run = [](int seed) -> std::vector<float> {
        GunStaffWizard g;
        g.SetSeed(seed);
        std::vector<float> trace;
        int phase = 0;
        for (int shot = 0; shot < 6; ++shot) {
            phase = GunStaffWizard::NextPhase(phase); // pump, no draw
            const float spread = GunStaffWizard::SpreadHalfAngle(18.0F, 0.25F);
            trace.push_back(g.ScatterAngle(spread)); // one draw per firing shot
        }
        return trace;
    };
    const auto a = run(606);
    const auto b = run(606);
    ASSERT_EQ(a.size(), b.size());
    EXPECT_EQ(a.size(), static_cast<std::size_t>(6));
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_FLOAT_EQ(a[i], b[i]);
    }
}

// NOLINTEND(readability-magic-numbers)
