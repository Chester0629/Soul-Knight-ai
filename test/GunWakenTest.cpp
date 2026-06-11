#include <gtest/gtest.h>

#include <vector>

#include "combat/GunWaken.hpp"
#include "data/RGRandom.hpp"

using Game::GunWaken;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- ctor scalars: exact recovered values (no logic, no RNG) ----------------

TEST(GunWakenTest, CtorScalarsMatchDecompImmediates) {
    // GunWaken___ctor @ game_full.c:970412-970414: 0xc, 0x32, 0x42340000 -- the recovered
    // awakened-mode (mode-2) override fields atk_mode2/critical_mode2/speed_mode2
    // (Il2CppDumper dump.cs:270938-270940).
    EXPECT_EQ(GunWaken::kAtkMode2, 12);
    EXPECT_EQ(GunWaken::kCriticalMode2, 50);
    EXPECT_FLOAT_EQ(GunWaken::kSpeedMode2, 45.0F);
}

// ---- IsAwakened: the wakenFlag gate (owner+0x84), no RNG --------------------

TEST(GunWakenTest, FlagZeroIsNormalMode) {
    // 970452: `if (owner+0x84 == 0)` -> normal mode (scatter runs).
    EXPECT_FALSE(GunWaken::IsAwakened(GunWaken::kWakenFlagNormal));
    EXPECT_FALSE(GunWaken::IsAwakened(0));
}

TEST(GunWakenTest, NonZeroFlagIsAwakened) {
    // Any non-zero flag -> awakened (the gate is NOT taken; draw skipped).
    EXPECT_TRUE(GunWaken::IsAwakened(1));
    EXPECT_TRUE(GunWaken::IsAwakened(-1));
    EXPECT_TRUE(GunWaken::IsAwakened(255));
}

// ---- SpreadHalfAngle: angle + angle*recoil, deterministic, no RNG ----------

TEST(GunWakenTest, SpreadIsAngleTimesOnePlusRecoil) {
    // 970453: spread = angle + angle*recoil.
    EXPECT_FLOAT_EQ(GunWaken::SpreadHalfAngle(10.0F, 0.5F), 15.0F); // 10 + 5
    EXPECT_FLOAT_EQ(GunWaken::SpreadHalfAngle(12.0F, 0.0F), 12.0F); // recoil 0
    EXPECT_FLOAT_EQ(GunWaken::SpreadHalfAngle(8.0F, 1.0F), 16.0F);  // 8 + 8
}

TEST(GunWakenTest, SpreadHandlesNegativeRecoilFactor) {
    // No clamp in the decomp; a negative recoil simply subtracts.
    EXPECT_FLOAT_EQ(GunWaken::SpreadHalfAngle(20.0F, -0.25F), 15.0F); // 20 - 5
}

// ---- ScatterAngle: exactly ONE float draw, symmetric in [-spread, spread] --

TEST(GunWakenTest, ScatterStaysWithinSpreadBounds) {
    GunWaken g;
    g.SetSeed(1234);
    const float spread = GunWaken::SpreadHalfAngle(15.0F, 0.2F);
    for (int i = 0; i < 64; ++i) {
        const float a = g.ScatterAngle(spread);
        EXPECT_GE(a, -spread);
        EXPECT_LE(a, spread);
    }
}

TEST(GunWakenTest, ScatterDrawsExactlyOneFloatInOrder) {
    // A parallel same-seeded RGRandom reproduces each scatter draw exactly:
    // proves ScatterAngle draws ONE float per shot, in order, over the
    // -spread/+spread bounds with nothing else advancing the stream.
    GunWaken g;
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

TEST(GunWakenTest, ScatterIsDeterministicForSameSeed) {
    GunWaken a;
    GunWaken b;
    a.SetSeed(555);
    b.SetSeed(555);
    for (int i = 0; i < 32; ++i) {
        EXPECT_FLOAT_EQ(a.ScatterAngle(15.0F), b.ScatterAngle(15.0F));
    }
}

// ---- ResolveScatter: the conditional-RNG decision (G5) ----------------------

TEST(GunWakenTest, NormalModeResolveDrawsOneFloatMatchingDirectScatter) {
    // wakenFlag == 0: ResolveScatter computes spread then takes the same single
    // draw as ScatterAngle(spread). A parallel stream proves count + order.
    GunWaken g;
    RGRandom ref;
    g.SetSeed(4242);
    ref.SetRandomSeed(4242);

    const float angle = 14.0F;
    const float recoil = 0.5F;
    const float spread = GunWaken::SpreadHalfAngle(angle, recoil); // 21.0
    const float got = g.ResolveScatter(0, angle, recoil);
    const float expected = ref.Range(-spread, spread);
    EXPECT_FLOAT_EQ(got, expected);
}

TEST(GunWakenTest, AwakenedModeReturnsZeroAndTakesNoDraw) {
    // wakenFlag != 0: the gate skips the whole spread+draw block. ResolveScatter
    // must return 0 AND leave the stream un-advanced. We prove the zero-draw by
    // running a reference stream that takes NO draw across the awakened calls and
    // a single draw afterward: the post-awakened draw must equal the FIRST draw
    // of the reference (i.e. the awakened calls advanced nothing).
    GunWaken g;
    RGRandom ref;
    g.SetSeed(31337);
    ref.SetRandomSeed(31337);

    // Several awakened "shots": each returns 0, each advances the stream ZERO.
    for (int i = 0; i < 8; ++i) {
        EXPECT_FLOAT_EQ(g.ResolveScatter(1, 14.0F, 0.5F), 0.0F);
    }

    // First real (normal-mode) shot draws once; the reference -- which took NO
    // draws above -- matches it on ITS first draw. Equal => awakened path took
    // zero draws (stream in lockstep).
    const float spread = GunWaken::SpreadHalfAngle(14.0F, 0.5F);
    const float got = g.ResolveScatter(0, 14.0F, 0.5F);
    const float expected = ref.Range(-spread, spread); // ref's FIRST draw
    EXPECT_FLOAT_EQ(got, expected);
}

TEST(GunWakenTest, MixedSequenceAdvancesOnlyOnNormalShots) {
    // Interleave awakened and normal shots; a parallel reference that draws ONLY
    // on the normal shots must stay in lockstep. This pins both the draw COUNT
    // (one per normal shot, zero per awakened shot) and the ORDER.
    GunWaken g;
    RGRandom ref;
    g.SetSeed(2024);
    ref.SetRandomSeed(2024);

    const float angle = 16.0F;
    const float recoil = 0.25F;
    const float spread = GunWaken::SpreadHalfAngle(angle, recoil);

    // flag pattern: 0 (draw), 5 (skip), 0 (draw), 0 (draw), 2 (skip), 0 (draw).
    const std::vector<int> flags = {0, 5, 0, 0, 2, 0};
    for (const int flag : flags) {
        const float got = g.ResolveScatter(flag, angle, recoil);
        if (GunWaken::IsAwakened(flag)) {
            EXPECT_FLOAT_EQ(got, 0.0F); // awakened: zero, no draw
        } else {
            const float expected = ref.Range(-spread, spread); // parallel draw
            EXPECT_FLOAT_EQ(got, expected);
        }
    }
}

TEST(GunWakenTest, AwakenedShotsLeaveStreamUntouchedAcrossSeeds) {
    // Determinism across seeds: an all-awakened run never advances the stream, so
    // a fresh same-seeded gun that fires its first NORMAL shot reproduces the
    // exact draw of a gun whose first action is that same normal shot.
    auto firstNormalDraw = [](int seed, int leadingAwakenedShots) -> float {
        GunWaken g;
        g.SetSeed(seed);
        for (int i = 0; i < leadingAwakenedShots; ++i) {
            g.ResolveScatter(7, 10.0F, 0.0F); // awakened: no draw
        }
        return g.ResolveScatter(0, 10.0F, 0.0F); // first real draw
    };
    EXPECT_FLOAT_EQ(firstNormalDraw(99, 0), firstNormalDraw(99, 16));
    EXPECT_FLOAT_EQ(firstNormalDraw(777, 0), firstNormalDraw(777, 3));
}

// NOLINTEND(readability-magic-numbers)
