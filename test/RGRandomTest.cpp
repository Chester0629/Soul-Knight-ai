#include <gtest/gtest.h>

#include <array>
#include <set>
#include <vector>

#include "data/RGRandom.hpp"

using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

namespace {

// Golden sequences for the determinism root. The integer path is VALIDATED
// bit-exact against the reverse-engineered Unity 2017.4 reference (see
// UnityParityInt12340 below): InitState constant 1812433253, the 11/8/19 xorshift
// triple, and Range(int) = min + r % (max-min) all reproduce the reference word
// 3463400838 / Range(0,INT_MAX) 1315917191. These vectors lock the contract the
// rest of the game replays against; regenerate only if the algorithm changes.
const std::array<int, 10> kSeed12345Range0to100 = {16, 56, 99, 70, 89,
                                                   41, 69, 90, 90, 66};
const std::array<int, 12> kSeed21345Range0to100 = {65, 21, 45, 94, 25, 1,
                                                   93, 56, 99, 91, 15, 74};
const std::array<int, 12> kSeed1Range0to6 = {4, 4, 2, 0, 2, 2, 0, 2, 2, 1, 1, 3};
const std::array<int, 8> kSeed42RangeNeg10to10 = {-7, -8, 5, -3, 4, 2, 5, 6};
// Unity float draw = (uint32)(word<<9)/0xFFFFFFFF; Range(0,1) = 1 - value.
const std::array<float, 5> kSeed7Range0to1 = {0.50979018F, 0.32472038F,
                                              0.30478084F, 0.66823816F,
                                              0.46461058F};

} // namespace

// --- Recorded golden sequences (regression lock) -----------------------------

TEST(RGRandomTest, GoldenSeed12345IntSequence) {
    RGRandom r;
    r.SetRandomSeed(12345);
    for (int expected : kSeed12345Range0to100) {
        EXPECT_EQ(r.Range(0, 100), expected);
    }
}

TEST(RGRandomTest, GoldenSeed21345IntSequence) {
    RGRandom r;
    r.SetRandomSeed(21345);
    for (int expected : kSeed21345Range0to100) {
        EXPECT_EQ(r.Range(0, 100), expected);
    }
}

// Bit-exact parity with the reverse-engineered Unity 2017.4 reference
// (macklinb gist): InitState(1234), first Range(0, INT_MAX) == 1315917191.
// This is the authoritative cross-check that our InitState + Xorshift128 advance
// + Range(int) reduction match Unity, independent of our own recorded goldens.
TEST(RGRandomTest, UnityParityInt1234) {
    RGRandom r;
    r.SetRandomSeed(1234);
    EXPECT_EQ(r.Range(0, 2147483647), 1315917191);
}

TEST(RGRandomTest, GoldenSeed1SmallSpan) {
    RGRandom r;
    r.SetRandomSeed(1);
    for (int expected : kSeed1Range0to6) {
        EXPECT_EQ(r.Range(0, 6), expected);
    }
}

TEST(RGRandomTest, GoldenSeed42NegativeSpan) {
    RGRandom r;
    r.SetRandomSeed(42);
    for (int expected : kSeed42RangeNeg10to10) {
        EXPECT_EQ(r.Range(-10, 10), expected);
    }
}

TEST(RGRandomTest, GoldenSeed7FloatSequence) {
    RGRandom r;
    r.SetRandomSeed(7);
    for (float expected : kSeed7Range0to1) {
        EXPECT_FLOAT_EQ(r.Range(0.0F, 1.0F), expected);
    }
}

// --- Determinism: same seed -> identical sequence ----------------------------

TEST(RGRandomTest, SameSeedSameIntSequence) {
    RGRandom a;
    RGRandom b;
    a.SetRandomSeed(2024);
    b.SetRandomSeed(2024);
    for (int i = 0; i < 256; ++i) {
        EXPECT_EQ(a.Range(0, 1000), b.Range(0, 1000)) << "draw " << i;
    }
}

TEST(RGRandomTest, SameSeedSameFloatSequence) {
    RGRandom a;
    RGRandom b;
    a.SetRandomSeed(-9991);
    b.SetRandomSeed(-9991);
    for (int i = 0; i < 256; ++i) {
        EXPECT_FLOAT_EQ(a.Range(-5.0F, 5.0F), b.Range(-5.0F, 5.0F)) << "draw " << i;
    }
}

TEST(RGRandomTest, ReseedReproducesFromStart) {
    RGRandom r;
    r.SetRandomSeed(555);
    std::vector<int> first;
    for (int i = 0; i < 32; ++i) {
        first.push_back(r.Range(0, 10000));
    }
    // Re-seeding the same stream restarts the identical sequence.
    r.SetRandomSeed(555);
    for (int i = 0; i < 32; ++i) {
        EXPECT_EQ(r.Range(0, 10000), first[static_cast<std::size_t>(i)]);
    }
}

// --- Independence: streams don't interfere -----------------------------------

TEST(RGRandomTest, StreamsAreIndependent) {
    // Two streams with the same seed: advancing one must not affect the other.
    RGRandom a;
    RGRandom b;
    a.SetRandomSeed(31337);
    b.SetRandomSeed(31337);

    // Drain a few from a only.
    a.Range(0, 1000);
    a.Range(0, 1000);
    a.Range(0, 1000);

    // b is still at the start; the rest of b's stream must equal a's full stream
    // (offset by the 3 we already pulled from a). Easiest check: a fresh c.
    RGRandom c;
    c.SetRandomSeed(31337);
    EXPECT_EQ(b.Range(0, 1000), c.Range(0, 1000));
    EXPECT_EQ(b.Range(0, 1000), c.Range(0, 1000));
    EXPECT_EQ(b.Range(0, 1000), c.Range(0, 1000));
}

TEST(RGRandomTest, DifferentSeedsDiverge) {
    RGRandom a;
    RGRandom b;
    a.SetRandomSeed(1);
    b.SetRandomSeed(2);
    bool anyDifferent = false;
    for (int i = 0; i < 64; ++i) {
        if (a.Range(0, 1000000) != b.Range(0, 1000000)) {
            anyDifferent = true;
        }
    }
    EXPECT_TRUE(anyDifferent);
}

// --- Boundary semantics ------------------------------------------------------

TEST(RGRandomTest, IntRangeMaxExclusive) {
    RGRandom r;
    r.SetRandomSeed(77);
    std::set<int> seen;
    for (int i = 0; i < 50000; ++i) {
        int v = r.Range(0, 3);
        EXPECT_GE(v, 0);
        EXPECT_LT(v, 3); // max is EXCLUSIVE
        seen.insert(v);
    }
    // Over 50k draws we should observe the full inclusive-lower/exclusive-upper
    // span {0,1,2} and never 3.
    EXPECT_EQ(seen.count(0), 1u);
    EXPECT_EQ(seen.count(1), 1u);
    EXPECT_EQ(seen.count(2), 1u);
    EXPECT_EQ(seen.count(3), 0u);
}

TEST(RGRandomTest, IntRangeEmptyReturnsMin) {
    RGRandom r;
    r.SetRandomSeed(5);
    EXPECT_EQ(r.Range(5, 5), 5);   // empty span
    EXPECT_EQ(r.Range(10, 2), 10); // inverted span -> lower bound
}

TEST(RGRandomTest, FloatRangeWithinInclusiveBounds) {
    RGRandom r;
    r.SetRandomSeed(123);
    for (int i = 0; i < 100000; ++i) {
        float v = r.Range(0.0F, 1.0F);
        EXPECT_GE(v, 0.0F);
        EXPECT_LE(v, 1.0F); // max is INCLUSIVE
    }
}

TEST(RGRandomTest, FloatRangeDegenerateReturnsValue) {
    RGRandom r;
    r.SetRandomSeed(55);
    for (int i = 0; i < 8; ++i) {
        EXPECT_FLOAT_EQ(r.Range(5.0F, 5.0F), 5.0F);
    }
}

// --- Lazy-seed semantics -----------------------------------------------------

TEST(RGRandomTest, SeededReflectsState) {
    RGRandom r;
    EXPECT_FALSE(r.Seeded());
    r.SetRandomSeed(1);
    EXPECT_TRUE(r.Seeded());
}

TEST(RGRandomTest, LazySeedFromFallbackSeed) {
    // An unseeded stream lazily seeds from FallbackSeed on first draw, matching
    // the original EnsureSeeded() behaviour.
    RGRandom lazy;
    lazy.FallbackSeed = 4242;
    EXPECT_FALSE(lazy.Seeded());
    int first = lazy.Range(0, 100000);
    EXPECT_TRUE(lazy.Seeded());

    // Should equal an explicitly-seeded stream with the same seed.
    RGRandom explicitSeed;
    explicitSeed.SetRandomSeed(4242);
    EXPECT_EQ(first, explicitSeed.Range(0, 100000));
}

// NOLINTEND(readability-magic-numbers)
