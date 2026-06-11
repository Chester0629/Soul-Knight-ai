#include <gtest/gtest.h>

#include <vector>

#include "combat/Gun002.hpp"
#include "data/RGRandom.hpp"

using Game::Gun002;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- WillFire: count < 1 -> no-shoot SFX branch ----------------------------

TEST(Gun002Test, WillFireRequiresAtLeastOnePellet) {
    EXPECT_FALSE(Gun002::WillFire(0));
    EXPECT_FALSE(Gun002::WillFire(-3));
    EXPECT_TRUE(Gun002::WillFire(1));
    EXPECT_TRUE(Gun002::WillFire(8));
}

// ---- FanStartIndex: even -(count/2) ; odd -((count-1)/2) -------------------

TEST(Gun002Test, FanStartIndexOddCountsCentred) {
    // odd: -((count-1)/2). 1 -> 0, 3 -> -1, 5 -> -2, 7 -> -3.
    EXPECT_EQ(Gun002::FanStartIndex(1), 0);
    EXPECT_EQ(Gun002::FanStartIndex(3), -1);
    EXPECT_EQ(Gun002::FanStartIndex(5), -2);
    EXPECT_EQ(Gun002::FanStartIndex(7), -3);
}

TEST(Gun002Test, FanStartIndexEvenCountsSymmetric) {
    // even: -(count/2). 2 -> -1, 4 -> -2, 6 -> -3, 8 -> -4.
    EXPECT_EQ(Gun002::FanStartIndex(2), -1);
    EXPECT_EQ(Gun002::FanStartIndex(4), -2);
    EXPECT_EQ(Gun002::FanStartIndex(6), -3);
    EXPECT_EQ(Gun002::FanStartIndex(8), -4);
}

// ---- PelletFanIndex: fan slots walk start..start+count-1 -------------------

TEST(Gun002Test, OddFanIsSymmetricAroundZero) {
    // count 5 -> start -2 -> slots -2,-1,0,1,2 (a centre pellet straight ahead).
    const std::vector<int> expected = {-2, -1, 0, 1, 2};
    for (int p = 0; p < 5; ++p) {
        EXPECT_EQ(Gun002::PelletFanIndex(5, p), expected[static_cast<std::size_t>(p)]);
    }
}

TEST(Gun002Test, EvenFanStraddlesZeroNoCentrePellet) {
    // count 4 -> start -2 -> slots -2,-1,0,1 (the decomp's -(count/2) is NOT
    // symmetric for even counts: there is no symmetric +2 slot; faithfully kept).
    const std::vector<int> expected = {-2, -1, 0, 1};
    for (int p = 0; p < 4; ++p) {
        EXPECT_EQ(Gun002::PelletFanIndex(4, p), expected[static_cast<std::size_t>(p)]);
    }
}

TEST(Gun002Test, SinglePelletFiresStraight) {
    // count 1 -> only slot 0 -> base angle 0 (a straight shot).
    EXPECT_EQ(Gun002::PelletFanIndex(1, 0), 0);
    EXPECT_FLOAT_EQ(Gun002::PelletBaseAngle(1, 0), 0.0F);
}

// ---- PelletBaseAngle: fan slot * step angle (default 15) -------------------

TEST(Gun002Test, BaseAngleUsesDefaultStepOfFifteen) {
    // ctor default step = kDefaultStepAngle == 15. count 3 -> slots -1,0,1.
    EXPECT_FLOAT_EQ(Gun002::kDefaultStepAngle, 15.0F);
    EXPECT_FLOAT_EQ(Gun002::PelletBaseAngle(3, 0), -15.0F);
    EXPECT_FLOAT_EQ(Gun002::PelletBaseAngle(3, 1), 0.0F);
    EXPECT_FLOAT_EQ(Gun002::PelletBaseAngle(3, 2), 15.0F);
}

TEST(Gun002Test, BaseAngleHonoursCustomStep) {
    // count 5, step 10 -> slots -2..2 -> -20,-10,0,10,20.
    const std::vector<float> expected = {-20.0F, -10.0F, 0.0F, 10.0F, 20.0F};
    for (int p = 0; p < 5; ++p) {
        EXPECT_FLOAT_EQ(Gun002::PelletBaseAngle(5, p, 10.0F),
                        expected[static_cast<std::size_t>(p)]);
    }
}

// ---- SpreadHalfSpan: baseAngle * (1 + recoil) ------------------------------

TEST(Gun002Test, SpreadHalfSpanZeroRecoilIsBaseAngle) {
    EXPECT_FLOAT_EQ(Gun002::SpreadHalfSpan(7.0F, 0.0F), 7.0F);
}

TEST(Gun002Test, SpreadHalfSpanScalesByRecoil) {
    // base*(1+recoil): 10 * (1 + 0.5) = 15 ; 8 * (1 + 2) = 24.
    EXPECT_FLOAT_EQ(Gun002::SpreadHalfSpan(10.0F, 0.5F), 15.0F);
    EXPECT_FLOAT_EQ(Gun002::SpreadHalfSpan(8.0F, 2.0F), 24.0F);
}

// ---- ScatterPellet: exactly ONE float draw per pellet, in [-half, half] ----

TEST(Gun002Test, ScatterStaysWithinHalfBounds) {
    Gun002 g;
    g.SetSeed(1234);
    const float half = Gun002::SpreadHalfSpan(20.0F, 0.25F); // 25 deg
    for (int i = 0; i < 64; ++i) {
        const float a = g.ScatterPellet(half);
        EXPECT_GE(a, -half);
        EXPECT_LE(a, half);
    }
}

TEST(Gun002Test, ScatterDrawsExactlyOneFloatInOrder) {
    // Parallel same-seeded RGRandom must reproduce each scatter draw exactly:
    // proves the brain draws ONE float per pellet, in order, with the -half/half
    // bounds and nothing else advancing the stream.
    Gun002 g;
    RGRandom ref;
    g.SetSeed(98765);
    ref.SetRandomSeed(98765);

    const std::vector<float> halves = {5.0F, 12.5F, 30.0F, 7.0F, 18.0F};
    for (const float half : halves) {
        const float got = g.ScatterPellet(half);
        const float expected = ref.Range(-half, half); // one parallel draw
        EXPECT_FLOAT_EQ(got, expected);
    }
}

TEST(Gun002Test, GeometricHelpersDoNotAdvanceStream) {
    // FanStartIndex / PelletFanIndex / PelletBaseAngle / SpreadHalfSpan are all
    // static pure math with ZERO draws: an instance whose helpers are exercised
    // must produce the exact same scatter stream as one that only scatters.
    Gun002 used;
    Gun002 fresh;
    used.SetSeed(2024);
    fresh.SetSeed(2024);

    // Exercise every geometric helper on `used` -- none may touch the RNG.
    for (int count = 1; count <= 8; ++count) {
        for (int p = 0; p < count; ++p) {
            (void)Gun002::PelletBaseAngle(count, p);
        }
        (void)Gun002::FanStartIndex(count);
        (void)Gun002::SpreadHalfSpan(static_cast<float>(count), 0.3F);
    }

    // The next scatter draw on `used` must match `fresh`'s very first draw.
    EXPECT_FLOAT_EQ(used.ScatterPellet(15.0F), fresh.ScatterPellet(15.0F));
}

TEST(Gun002Test, OneScatterDrawPerPelletInLockstep) {
    // Fire a full fan (one ScatterPellet per pellet, indices 0..count-1) and a
    // parallel same-seeded stream must reproduce the whole sequence -> proves the
    // per-pellet draw count is exactly 1 and ordering matches the spawn loop.
    //
    // FAITHFUL: Gun002__Attack @ game_full.c:315863-315879.
    // iVar3 = param_1[0xc] (this+0x30, the weapon's fixed deviation field) is read
    // ONCE before the fan loop. fVar5 = (float)iVar3 + (float)iVar3 * recoil is the
    // same constant half-span fed to every RGRandom__Range call in the loop.
    // PelletBaseAngle varies per pellet (fan slot * step); deviation does NOT.
    // Using PelletBaseAngle as the spread base was a false golden that always passed
    // only because both sides of the assertion shared the same wrong formula.
    static constexpr float kWeaponDeviation = 20.0F; // this+0x30, fixed per weapon

    auto fireFan = [](int seed, int count, float recoil) -> std::vector<float> {
        Gun002 g;
        g.SetSeed(seed);
        std::vector<float> trace;
        if (!Gun002::WillFire(count)) {
            return trace; // no-shoot branch makes zero draws
        }
        // half-span is the same for every pellet (weapon deviation field, not fan angle)
        const float half = Gun002::SpreadHalfSpan(kWeaponDeviation, recoil);
        for (int p = 0; p < count; ++p) {
            trace.push_back(g.ScatterPellet(half));
        }
        return trace;
    };

    const int count = 6;
    const float recoil = 0.2F;
    const auto a = fireFan(31337, count, recoil);
    EXPECT_EQ(a.size(), static_cast<std::size_t>(count)); // one draw per pellet

    // Reproduce with a hand-rolled parallel stream, same draw order.
    RGRandom ref;
    ref.SetRandomSeed(31337);
    // half-span is constant across all pellets -- same fixed weapon deviation field
    const float half = Gun002::SpreadHalfSpan(kWeaponDeviation, recoil);
    for (int p = 0; p < count; ++p) {
        const float expected = ref.Range(-half, half);
        EXPECT_FLOAT_EQ(a[static_cast<std::size_t>(p)], expected);
    }
}

TEST(Gun002Test, NoFireBranchMakesZeroDraws) {
    // count < 1 -> WillFire is false; the owner takes the SFX branch and never
    // scatters. A stream that takes the no-fire branch stays un-advanced, so its
    // first real draw equals a fresh same-seeded stream's first draw.
    Gun002 g;
    RGRandom ref;
    g.SetSeed(777);
    ref.SetRandomSeed(777);

    ASSERT_FALSE(Gun002::WillFire(0)); // owner would not scatter
    // (no ScatterPellet call on the no-fire path)
    EXPECT_FLOAT_EQ(g.ScatterPellet(10.0F), ref.Range(-10.0F, 10.0F));
}

// NOLINTEND(readability-magic-numbers)
