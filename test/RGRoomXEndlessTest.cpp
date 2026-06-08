#include <gtest/gtest.h>

#include <vector>

#include "data/RGRandom.hpp"
#include "world/RGRoomXEndless.hpp"

using Game::RGRandom;
using Game::RGRoomXEndless;

// NOLINTBEGIN(readability-magic-numbers)

namespace {

RGRoomXEndless::PlacedRect Rect(float x, float y, float w, float h) {
    RGRoomXEndless::PlacedRect r;
    r.x = x;
    r.y = y;
    r.width = w;
    r.height = h;
    return r;
}

} // namespace

// --- Empty placed list -> never occupied -------------------------------------

TEST(RGRoomXEndlessTest, EmptyListIsNeverOccupied) {
    const std::vector<RGRoomXEndless::PlacedRect> placed;
    EXPECT_FALSE(RGRoomXEndless::IsOccupied(5, 5, 1, 1, placed));
    EXPECT_FALSE(RGRoomXEndless::IsOccupied(0, 0, 4, 4, placed));
}

// --- A coincident rect overlaps ----------------------------------------------

TEST(RGRoomXEndlessTest, CoincidentRectIsOccupied) {
    // Placed rect exactly on the candidate cell footprint (1x1 at (5,5)).
    const std::vector<RGRoomXEndless::PlacedRect> placed = {Rect(5, 5, 1, 1)};
    EXPECT_TRUE(RGRoomXEndless::IsOccupied(5, 5, 1, 1, placed));
}

// --- The 1-cell padding makes adjacent rects overlap -------------------------

TEST(RGRoomXEndlessTest, OneCellPaddingCatchesTouchingRect) {
    // Candidate 1x1 at (5,5): padded query = (4,4,3,3) -> spans x in [4,7],
    // y in [4,7]. A placed rect immediately to the right at x=7 still touches
    // the padded query's right edge, so it must count as occupied.
    const std::vector<RGRoomXEndless::PlacedRect> placed = {Rect(7, 5, 1, 1)};
    EXPECT_TRUE(RGRoomXEndless::IsOccupied(5, 5, 1, 1, placed));
}

TEST(RGRoomXEndlessTest, PaddingDiagonalNeighbourOverlaps) {
    // Diagonal neighbour just inside the padded margin still overlaps because
    // both axes intersect the padded query.
    const std::vector<RGRoomXEndless::PlacedRect> placed = {Rect(7, 7, 1, 1)};
    EXPECT_TRUE(RGRoomXEndless::IsOccupied(5, 5, 1, 1, placed));
}

// --- A rect well outside the padded query does NOT overlap -------------------

TEST(RGRoomXEndlessTest, DistantRectIsFree) {
    // Candidate 1x1 at (5,5): padded query x/y in [4,7]. A rect starting at
    // x=20 is far past the padded right edge on the x axis -> no overlap.
    const std::vector<RGRoomXEndless::PlacedRect> placed = {Rect(20, 20, 1, 1)};
    EXPECT_FALSE(RGRoomXEndless::IsOccupied(5, 5, 1, 1, placed));
}

TEST(RGRoomXEndlessTest, SeparatedOnSingleAxisIsFree) {
    // Overlaps on y but the placed rect starts at x=9, beyond the padded query
    // right edge (xMax=7). Both axes must overlap, so this is free.
    const std::vector<RGRoomXEndless::PlacedRect> placed = {Rect(9, 5, 1, 1)};
    EXPECT_FALSE(RGRoomXEndless::IsOccupied(5, 5, 1, 1, placed));
}

// --- First overlap short-circuits; order/scan covers the whole list ----------

TEST(RGRoomXEndlessTest, OverlapFoundAnywhereInList) {
    // The first two are clearly free; the third overlaps. IsOccupied must scan
    // the whole list and report occupied.
    const std::vector<RGRoomXEndless::PlacedRect> placed = {
        Rect(20, 20, 1, 1), Rect(30, 30, 2, 2), Rect(5, 5, 1, 1)};
    EXPECT_TRUE(RGRoomXEndless::IsOccupied(5, 5, 1, 1, placed));
}

TEST(RGRoomXEndlessTest, AllFreeReportsFree) {
    const std::vector<RGRoomXEndless::PlacedRect> placed = {
        Rect(20, 20, 1, 1), Rect(30, 30, 2, 2), Rect(0, 0, 1, 1)};
    // Candidate at (10,10): padded query x/y in [9,13]; none of the placed
    // rects reach into that band.
    EXPECT_FALSE(RGRoomXEndless::IsOccupied(10, 10, 1, 1, placed));
}

// --- Larger candidate footprint widens the padded query ----------------------

TEST(RGRoomXEndlessTest, LargerFootprintWidensQuery) {
    // Candidate 3x3 at (5,5): padded query = (4,4,5,5) -> x/y in [4,9]. A rect
    // at (9,9) touches the padded far corner and must overlap.
    const std::vector<RGRoomXEndless::PlacedRect> placed = {Rect(9, 9, 1, 1)};
    EXPECT_TRUE(RGRoomXEndless::IsOccupied(5, 5, 3, 3, placed));
    // A rect at (11,11) is clear of the padded query on both axes.
    const std::vector<RGRoomXEndless::PlacedRect> far = {Rect(11, 11, 1, 1)};
    EXPECT_FALSE(RGRoomXEndless::IsOccupied(5, 5, 3, 3, far));
}

// --- A wide placed rect spanning the candidate counts as occupied ------------

TEST(RGRoomXEndlessTest, EnclosingRectIsOccupied) {
    // A big placed rect that fully contains the candidate cell.
    const std::vector<RGRoomXEndless::PlacedRect> placed = {Rect(0, 0, 20, 20)};
    EXPECT_TRUE(RGRoomXEndless::IsOccupied(5, 5, 1, 1, placed));
}

// --- IsOccupied is a pure predicate: it draws NO RNG -------------------------

TEST(RGRoomXEndlessTest, DrawsNoRandomNumbers) {
    // The decomp body takes no RGRandom draws. Verify by running a seeded stream
    // in parallel: calling IsOccupied many times must not advance it.
    RGRandom control;
    control.SetRandomSeed(4242);
    const int before = control.Range(0, 1000000);

    RGRandom probe;
    probe.SetRandomSeed(4242);
    const std::vector<RGRoomXEndless::PlacedRect> placed = {Rect(5, 5, 1, 1),
                                                            Rect(9, 9, 2, 2)};
    for (int i = 0; i < 50; ++i) {
        (void)RGRoomXEndless::IsOccupied(i, i, 1, 1, placed);
        (void)RGRoomXEndless::IsOccupied(i, i, 3, 3, placed);
    }
    // The probe stream has not been touched by IsOccupied, so its first draw
    // matches the control's first draw exactly.
    const int after = probe.Range(0, 1000000);
    EXPECT_EQ(before, after);
}

// NOLINTEND(readability-magic-numbers)
