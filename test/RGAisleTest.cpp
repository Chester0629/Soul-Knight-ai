#include <gtest/gtest.h>

#include <vector>

#include "data/RGRandom.hpp"
#include "world/RGAisle.hpp"

using Game::RGAisle;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

namespace {

// Reproduce the decomp's pass-2 edge classification on a known noise grid so the
// test asserts the exact branch math, not RGAisle's own output.
int ClassifyCell(const std::vector<int> &noise, int w, int h, int x, int y) {
    auto at = [&](int cx, int cy) {
        return noise[static_cast<std::size_t>(cx) * static_cast<std::size_t>(h) +
                     static_cast<std::size_t>(cy)];
    };
    const int self = at(x, y);
    int count = (self == 0) ? 1 : 0;
    if (x > 0) {
        const int wgt = (self == 0) ? 2 : 1;
        if (at(x - 1, y) == 0) {
            count = wgt;
        }
    }
    if (x < w - 1 && at(x + 1, y) == 0) {
        ++count;
    }
    if (y > 0 && at(x, y - 1) == 0) {
        ++count;
    }
    if (y < h - 1 && at(x, y + 1) == 0) {
        ++count;
    }
    return count;
}

} // namespace

// --- GridDims: direction selector faithful to (direction | 2) == 2 -----------

TEST(RGAisleTest, GridDimsNorthAndWestUseXOffset) {
    // NORTH(0) and WEST(2): width = roomXOffset-1, height = 5.
    const RGAisle::Dims north = RGAisle::GridDims(RGAisle::DIR_NORTH, 13, 8);
    EXPECT_EQ(north.width, 12);
    EXPECT_EQ(north.height, RGAisle::kAisleShortExtent);

    const RGAisle::Dims west = RGAisle::GridDims(RGAisle::DIR_WEST, 13, 8);
    EXPECT_EQ(west.width, 12);
    EXPECT_EQ(west.height, RGAisle::kAisleShortExtent);
}

TEST(RGAisleTest, GridDimsEastAndSouthUseYOffset) {
    // EAST(1) and SOUTH(3): width = 5, height = roomYOffset-1.
    const RGAisle::Dims east = RGAisle::GridDims(RGAisle::DIR_EAST, 13, 8);
    EXPECT_EQ(east.width, RGAisle::kAisleShortExtent);
    EXPECT_EQ(east.height, 7);

    const RGAisle::Dims south = RGAisle::GridDims(RGAisle::DIR_SOUTH, 13, 8);
    EXPECT_EQ(south.width, RGAisle::kAisleShortExtent);
    EXPECT_EQ(south.height, 7);
}

TEST(RGAisleTest, GridDimsShortAxisIsAlwaysFive) {
    // Exactly one axis is the fixed short extent (5) for every direction.
    for (int dir = 0; dir <= 3; ++dir) {
        const RGAisle::Dims d = RGAisle::GridDims(dir, 20, 20);
        const bool oneAxisIsFive = (d.width == RGAisle::kAisleShortExtent) ^
                                    (d.height == RGAisle::kAisleShortExtent);
        EXPECT_TRUE(oneAxisIsFive) << "dir " << dir;
    }
}

// --- BuildFloorEdges: pass-1 RNG cadence is draw-for-draw faithful -----------

TEST(RGAisleTest, FloorPass1DrawsRangeZeroTwoPerCellInOrder) {
    // Pass 1 draws exactly width*height x Range(0,2); pass 2 takes no draw. The
    // edge grid must equal pass-2 classification of the parallel stream's noise.
    const RGAisle::Dims dims = RGAisle::GridDims(RGAisle::DIR_EAST, 13, 8);
    const int cells = dims.width * dims.height;

    RGAisle b;
    b.SetSeed(4242);
    const auto edges = b.BuildFloorEdges(dims);
    EXPECT_EQ(static_cast<int>(edges.size()), cells);

    RGRandom ref2;
    ref2.SetRandomSeed(4242);
    std::vector<int> noise;
    noise.reserve(static_cast<std::size_t>(cells));
    for (int i = 0; i < cells; ++i) {
        noise.push_back(ref2.Range(0, RGAisle::kNoiseCeiling));
    }

    // The edge grid must equal pass-2 classification of that exact noise grid.
    for (int x = 0; x < dims.width; ++x) {
        for (int y = 0; y < dims.height; ++y) {
            const std::size_t idx =
                static_cast<std::size_t>(x) *
                    static_cast<std::size_t>(dims.height) +
                static_cast<std::size_t>(y);
            EXPECT_EQ(edges[idx],
                      ClassifyCell(noise, dims.width, dims.height, x, y))
                << "cell (" << x << "," << y << ")";
        }
    }
}

TEST(RGAisleTest, FloorPass2TakesNoExtraDraw) {
    // After BuildFloorEdges, a parallel stream advanced by exactly width*height
    // Range(0,2) draws stays lockstep (pass 2 is RNG-free, pass 3 not run here).
    const RGAisle::Dims dims = RGAisle::GridDims(RGAisle::DIR_NORTH, 11, 11);
    const int cells = dims.width * dims.height;

    RGAisle aisle;
    aisle.SetSeed(7);
    aisle.BuildFloorEdges(dims);
    // Draw one more from the aisle stream by reusing it on another grid call:
    const auto secondGrid = aisle.BuildFloorEdges(dims);

    RGRandom ref;
    ref.SetRandomSeed(7);
    for (int i = 0; i < cells; ++i) {
        (void)ref.Range(0, RGAisle::kNoiseCeiling);
    }
    // Reconstruct the second call's noise from the reference and classify it.
    std::vector<int> noise2;
    noise2.reserve(static_cast<std::size_t>(cells));
    for (int i = 0; i < cells; ++i) {
        noise2.push_back(ref.Range(0, RGAisle::kNoiseCeiling));
    }
    for (int x = 0; x < dims.width; ++x) {
        for (int y = 0; y < dims.height; ++y) {
            const std::size_t idx =
                static_cast<std::size_t>(x) *
                    static_cast<std::size_t>(dims.height) +
                static_cast<std::size_t>(y);
            EXPECT_EQ(secondGrid[idx],
                      ClassifyCell(noise2, dims.width, dims.height, x, y));
        }
    }
}

TEST(RGAisleTest, FloorDeterministicForSameSeed) {
    const RGAisle::Dims dims = RGAisle::GridDims(RGAisle::DIR_SOUTH, 9, 14);
    RGAisle a;
    RGAisle b;
    a.SetSeed(12345);
    b.SetSeed(12345);
    EXPECT_EQ(a.BuildFloorEdges(dims), b.BuildFloorEdges(dims));
}

TEST(RGAisleTest, FloorDegenerateGridTakesNoDraw) {
    // offset 1 -> width/height = 0: the decomp's `0 < extent` guards skip every
    // loop. No grid, no draws -> a parallel stream stays untouched.
    const RGAisle::Dims dims = RGAisle::GridDims(RGAisle::DIR_WEST, 1, 1);
    EXPECT_EQ(dims.width, 0);

    RGAisle aisle;
    aisle.SetSeed(55);
    const auto edges = aisle.BuildFloorEdges(dims);
    EXPECT_TRUE(edges.empty());

    // The stream must not have advanced: a fresh same-seed aisle on a real grid
    // produces the same result as this aisle on a real grid afterwards.
    const RGAisle::Dims real = RGAisle::GridDims(RGAisle::DIR_EAST, 10, 10);
    const auto afterDegenerate = aisle.BuildFloorEdges(real);

    RGAisle fresh;
    fresh.SetSeed(55);
    const auto freshGrid = fresh.BuildFloorEdges(real);
    EXPECT_EQ(afterDegenerate, freshGrid);
}

// --- RollAisleWall: gated 2x Range(0,100) cadence ----------------------------

TEST(RGAisleTest, AisleWallGateFailsTakesNoDraw) {
    // offset 1 -> aisleLength 0: gate fails, NO draw, placed=false.
    RGAisle aisle;
    aisle.SetSeed(321);
    const RGAisle::WallRoll roll =
        aisle.RollAisleWall(RGAisle::DIR_NORTH, 1, 1);
    EXPECT_FALSE(roll.placed);
    EXPECT_EQ(roll.firstRoll, -1);
    EXPECT_EQ(roll.secondRoll, -1);

    // Stream untouched: the next real call equals a fresh same-seed stream.
    const RGAisle::WallRoll real =
        aisle.RollAisleWall(RGAisle::DIR_NORTH, 10, 10);
    RGAisle fresh;
    fresh.SetSeed(321);
    const RGAisle::WallRoll freshRoll =
        fresh.RollAisleWall(RGAisle::DIR_NORTH, 10, 10);
    EXPECT_EQ(real.firstRoll, freshRoll.firstRoll);
    EXPECT_EQ(real.secondRoll, freshRoll.secondRoll);
}

TEST(RGAisleTest, AisleWallDrawsTwoRangeHundredInOrder) {
    // Gate passes: two Range(0,100) draws, first then second, in stream order.
    RGAisle aisle;
    aisle.SetSeed(909);
    const RGAisle::WallRoll roll =
        aisle.RollAisleWall(RGAisle::DIR_EAST, 10, 9);
    EXPECT_TRUE(roll.placed);

    RGRandom ref;
    ref.SetRandomSeed(909);
    const int expectedFirst = ref.Range(0, RGAisle::kWallRollCeiling);
    const int expectedSecond = ref.Range(0, RGAisle::kWallRollCeiling);
    EXPECT_EQ(roll.firstRoll, expectedFirst);
    EXPECT_EQ(roll.secondRoll, expectedSecond);
}

TEST(RGAisleTest, AisleWallPrefabIndexFromFirstRoll) {
    // firstRoll < 6 -> prefab index 1, else 0 (decomp `iVar1 < 6`).
    for (int seed = 0; seed < 40; ++seed) {
        RGAisle aisle;
        aisle.SetSeed(seed);
        const RGAisle::WallRoll roll =
            aisle.RollAisleWall(RGAisle::DIR_SOUTH, 12, 12);
        ASSERT_TRUE(roll.placed);
        const int expected =
            (roll.firstRoll < RGAisle::kWallVariantThreshold) ? 1 : 0;
        EXPECT_EQ(roll.prefabIndex, expected) << "seed " << seed;
    }
}

TEST(RGAisleTest, AisleWallUsesCorrectAxisOffset) {
    // NORTH/WEST gate on roomXOffset-1; EAST/SOUTH gate on roomYOffset-1.
    // Here xOffset gives a passing gate but yOffset gives a failing one.
    RGAisle northAisle;
    northAisle.SetSeed(5);
    const RGAisle::WallRoll north =
        northAisle.RollAisleWall(RGAisle::DIR_NORTH, 10, 1);
    EXPECT_TRUE(north.placed); // uses xOffset(10)-1 = 9 > 0

    RGAisle eastAisle;
    eastAisle.SetSeed(5);
    const RGAisle::WallRoll east =
        eastAisle.RollAisleWall(RGAisle::DIR_EAST, 10, 1);
    EXPECT_FALSE(east.placed); // uses yOffset(1)-1 = 0 -> gate fails
}

// --- Owner-side marker --------------------------------------------------------

TEST(RGAisleTest, FloorPass3IsOwnerSide) {
    // Documentation hook: pass 3 (variant pick + Instantiate) is owner-side and
    // not modelled here (corrupted loop bound + Instantiate-only effect).
    EXPECT_FALSE(RGAisle::FloorDrawsAreOwnerSide());
}

// NOLINTEND(readability-magic-numbers)
