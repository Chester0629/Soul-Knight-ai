#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <set>
#include <utility>
#include <vector>

#include "world/RoomGen.hpp"

using Game::RoomGen;

// NOLINTBEGIN(readability-magic-numbers)

namespace {

// Cell-code legend (mirrors RoomGen.hpp / RGRoomX decompilation).
constexpr int kAisle = -2;
constexpr int kBorder = -1;
constexpr int kFloor = 0;
constexpr int kBigObstacle = 1;
constexpr int kSmallObstacle = 2;
constexpr int kCenter1x1 = 8;
constexpr int kCenter2x2 = 9;
constexpr int kDoor = 11;

const std::array<int, 4> kNoDoors = {0, 0, 0, 0};
const std::array<int, 4> kAllDoors = {1, 1, 1, 1};

RoomGen::Options ExplicitRoom(int w, int h, int wallLvl, int obstLvl) {
    RoomGen::Options o;
    o.randomRoom = false;
    o.roomWidth = w;
    o.roomHeight = h;
    o.wallLevel = wallLvl;
    o.obstacleLevel = obstLvl;
    return o;
}

} // namespace

// --- Determinism: same seed (+ inputs) -> identical grid ---------------------

TEST(RoomGenTest, SameSeedSameGridRandom) {
    RoomGen::Options o;
    o.randomRoom = true;
    o.floorIndex = 0;
    RoomGen a(12345, kAllDoors, o);
    RoomGen b(12345, kAllDoors, o);

    EXPECT_EQ(a.Width(), b.Width());
    EXPECT_EQ(a.Height(), b.Height());
    EXPECT_EQ(a.WallLevel(), b.WallLevel());
    EXPECT_EQ(a.ObstacleLevel(), b.ObstacleLevel());
    EXPECT_EQ(a.Grid(), b.Grid());
    EXPECT_EQ(a.FloorList(), b.FloorList());
}

TEST(RoomGenTest, SameSeedSameGridExplicit) {
    const auto o = ExplicitRoom(25, 25, 3, 3);
    RoomGen a(999, kAllDoors, o);
    RoomGen b(999, kAllDoors, o);
    EXPECT_EQ(a.Grid(), b.Grid());
    EXPECT_EQ(a.FloorList(), b.FloorList());
}

TEST(RoomGenTest, DifferentSeedDifferentGrid) {
    const auto o = ExplicitRoom(25, 25, 3, 3);
    RoomGen a(1, kAllDoors, o);
    RoomGen b(2, kAllDoors, o);
    // Overwhelmingly likely to differ; a stable inequality is a good signal.
    EXPECT_NE(a.Grid(), b.Grid());
}

// --- Dimensions / offsets faithful to the 41x41 centring ---------------------

TEST(RoomGenTest, OffsetsAreCentred) {
    const auto o = ExplicitRoom(21, 15, 1, 1);
    RoomGen r(7, kNoDoors, o);
    EXPECT_EQ(r.Width(), 21);
    EXPECT_EQ(r.Height(), 15);
    EXPECT_EQ(r.OffsetX(), (RoomGen::MAP_SIZE - 21) / 2);
    EXPECT_EQ(r.OffsetY(), (RoomGen::MAP_SIZE - 15) / 2);
    EXPECT_EQ(static_cast<int>(r.Grid().size()), 21 * 15);
}

TEST(RoomGenTest, RandomRoomSizesAreBanded) {
    // Random rooms must pick width/height from {15, 21, 25}.
    const std::set<int> banded = {15, 21, 25};
    for (int seed = 0; seed < 60; ++seed) {
        RoomGen::Options o;
        o.randomRoom = true;
        o.floorIndex = seed % 5; // exercise the floorIndex%5==1 narrowing too
        RoomGen r(seed, kNoDoors, o);
        EXPECT_TRUE(banded.count(r.Width()) == 1) << "seed " << seed;
        EXPECT_TRUE(banded.count(r.Height()) == 1) << "seed " << seed;
        EXPECT_GE(r.WallLevel(), 1);
        EXPECT_LE(r.WallLevel(), 4);
        EXPECT_GE(r.ObstacleLevel(), 1);
        EXPECT_LE(r.ObstacleLevel(), 4);
    }
}

// --- Border stamped on the perimeter -----------------------------------------

TEST(RoomGenTest, PerimeterCornersAreBorder) {
    const auto o = ExplicitRoom(25, 21, 1, 1);
    RoomGen r(3, kNoDoors, o); // no doors -> no aisle carving over the border
    const int w = r.Width();
    const int h = r.Height();
    // Corners are always border (-1) regardless of obstacle placement.
    EXPECT_EQ(r.At(0, 0), kBorder);
    EXPECT_EQ(r.At(w - 1, 0), kBorder);
    EXPECT_EQ(r.At(0, h - 1), kBorder);
    EXPECT_EQ(r.At(w - 1, h - 1), kBorder);
}

TEST(RoomGenTest, NoDoorsLeavePerimeterIntact) {
    // With no entrances, no perimeter cell should become an aisle or a door.
    const auto o = ExplicitRoom(21, 21, 1, 1);
    RoomGen r(11, kNoDoors, o);
    const int w = r.Width();
    const int h = r.Height();
    for (int x = 0; x < w; ++x) {
        EXPECT_NE(r.At(x, 0), kAisle);
        EXPECT_NE(r.At(x, 0), kDoor);
        EXPECT_NE(r.At(x, h - 1), kAisle);
        EXPECT_NE(r.At(x, h - 1), kDoor);
    }
    for (int y = 0; y < h; ++y) {
        EXPECT_NE(r.At(0, y), kAisle);
        EXPECT_NE(r.At(0, y), kDoor);
        EXPECT_NE(r.At(w - 1, y), kAisle);
        EXPECT_NE(r.At(w - 1, y), kDoor);
    }
}

// --- Door gaps present where the entrance flag is set ------------------------

TEST(RoomGenTest, EastDoorCarvesCorridorAndDoors) {
    const auto o = ExplicitRoom(25, 25, 1, 1);
    const std::array<int, 4> eastOnly = {1, 0, 0, 0};
    RoomGen r(5, eastOnly, o);
    const int w = r.Width();
    const int h = r.Height();
    const int cy = (h - 5) / 2;

    // Corridor cells (-2) on the two east columns, rows cy..cy+4.
    for (int i = 0; i <= 4; ++i) {
        EXPECT_EQ(r.At(w - 1, cy + i), kAisle);
        EXPECT_EQ(r.At(w - 2, cy + i), kAisle);
    }
    // Door cells at (w-1, cy-1) and (w-1, cy+5).
    EXPECT_EQ(r.At(w - 1, cy - 1), kDoor);
    EXPECT_EQ(r.At(w - 1, cy + 5), kDoor);
}

TEST(RoomGenTest, NorthDoorCarvesCorridorAndDoors) {
    const auto o = ExplicitRoom(25, 25, 1, 1);
    const std::array<int, 4> northOnly = {0, 1, 0, 0};
    RoomGen r(5, northOnly, o);
    const int w = r.Width();
    const int cx = (w - 5) / 2;

    for (int i = 0; i <= 4; ++i) {
        EXPECT_EQ(r.At(cx + i, 0), kAisle);
        EXPECT_EQ(r.At(cx + i, 1), kAisle);
    }
    EXPECT_EQ(r.At(cx - 1, 0), kDoor);
    EXPECT_EQ(r.At(cx + 5, 0), kDoor);
}

TEST(RoomGenTest, WestDoorCarvesCorridorAndDoors) {
    const auto o = ExplicitRoom(25, 25, 1, 1);
    const std::array<int, 4> westOnly = {0, 0, 1, 0};
    RoomGen r(5, westOnly, o);
    const int h = r.Height();
    const int cy = (h - 5) / 2;

    for (int i = 0; i <= 4; ++i) {
        EXPECT_EQ(r.At(0, cy + i), kAisle);
        EXPECT_EQ(r.At(1, cy + i), kAisle);
    }
    EXPECT_EQ(r.At(0, cy - 1), kDoor);
    EXPECT_EQ(r.At(0, cy + 5), kDoor);
}

TEST(RoomGenTest, SouthDoorCarvesCorridorAndDoors) {
    const auto o = ExplicitRoom(25, 25, 1, 1);
    const std::array<int, 4> southOnly = {0, 0, 0, 1};
    RoomGen r(5, southOnly, o);
    const int w = r.Width();
    const int h = r.Height();
    const int cx = (w - 5) / 2;

    for (int i = 0; i <= 4; ++i) {
        EXPECT_EQ(r.At(cx + i, h - 1), kAisle);
        EXPECT_EQ(r.At(cx + i, h - 2), kAisle);
    }
    EXPECT_EQ(r.At(cx - 1, h - 1), kDoor);
    EXPECT_EQ(r.At(cx + 5, h - 1), kDoor);
}

TEST(RoomGenTest, NoDoorsMeansNoDoorCells) {
    const auto o = ExplicitRoom(21, 21, 2, 2);
    RoomGen r(13, kNoDoors, o);
    const auto &grid = r.Grid();
    EXPECT_EQ(std::count(grid.begin(), grid.end(), kDoor), 0);
    EXPECT_EQ(std::count(grid.begin(), grid.end(), kAisle), 0);
}

// --- floor_list matches the code==0 cells ------------------------------------

TEST(RoomGenTest, FloorListMatchesOpenCells) {
    const auto o = ExplicitRoom(25, 25, 3, 3);
    RoomGen r(2024, kAllDoors, o);

    // Every entry in floor_list must be an open (==0) cell in the grid.
    for (const auto &cell : r.FloorList()) {
        EXPECT_EQ(r.At(cell.first, cell.second), kFloor)
            << "floor cell (" << cell.first << "," << cell.second << ")";
    }

    // The count must equal the number of zero cells in the grid.
    const auto &grid = r.Grid();
    const auto zeros = std::count(grid.begin(), grid.end(), kFloor);
    EXPECT_EQ(static_cast<long>(r.FloorList().size()), zeros);

    // floor_list is collected x-major, y-minor (the original scan order).
    const auto &fl = r.FloorList();
    for (std::size_t i = 1; i < fl.size(); ++i) {
        const bool ordered =
            (fl[i - 1].first < fl[i].first) ||
            (fl[i - 1].first == fl[i].first && fl[i - 1].second < fl[i].second);
        EXPECT_TRUE(ordered) << "floor_list not in x-major order at " << i;
    }
}

// --- Obstacle counts land within the level bands -----------------------------

TEST(RoomGenTest, BigObstacleCellsExistForHighWallLevel) {
    // wall_level 3 -> Range(10,15) big placements; some footprints should make
    // it onto the grid as code 1 / 8 / 9 (centre markers) given a 25x25 room.
    const auto o = ExplicitRoom(25, 25, 3, 1);
    bool sawBig = false;
    for (int seed = 0; seed < 20 && !sawBig; ++seed) {
        RoomGen r(seed, kNoDoors, o);
        const auto &g = r.Grid();
        const bool has =
            std::count(g.begin(), g.end(), kBigObstacle) > 0 ||
            std::count(g.begin(), g.end(), kCenter1x1) > 0 ||
            std::count(g.begin(), g.end(), kCenter2x2) > 0;
        sawBig = sawBig || has;
    }
    EXPECT_TRUE(sawBig);
}

TEST(RoomGenTest, SmallObstacleCellsExistForHighObstacleLevel) {
    const auto o = ExplicitRoom(25, 25, 1, 3);
    bool sawSmall = false;
    for (int seed = 0; seed < 20 && !sawSmall; ++seed) {
        RoomGen r(seed, kNoDoors, o);
        const auto &g = r.Grid();
        sawSmall = sawSmall ||
                   std::count(g.begin(), g.end(), kSmallObstacle) > 0;
    }
    EXPECT_TRUE(sawSmall);
}

TEST(RoomGenTest, ZeroLevelsProduceNoObstacles) {
    // Level 0 (out of band) yields zero placements: no big/small obstacle codes.
    const auto o = ExplicitRoom(21, 21, 0, 0);
    RoomGen r(77, kNoDoors, o);
    const auto &g = r.Grid();
    EXPECT_EQ(std::count(g.begin(), g.end(), kBigObstacle), 0);
    EXPECT_EQ(std::count(g.begin(), g.end(), kSmallObstacle), 0);
    EXPECT_EQ(std::count(g.begin(), g.end(), kCenter1x1), 0);
    EXPECT_EQ(std::count(g.begin(), g.end(), kCenter2x2), 0);
}

// --- Special box rate from badass mode ---------------------------------------

TEST(RoomGenTest, BadassModeSetsSpecialBoxRate) {
    RoomGen::Options o = ExplicitRoom(21, 21, 1, 1);
    o.badassMode = true;
    RoomGen r(1, kNoDoors, o);
    EXPECT_EQ(r.SpecialBoxRate(), 20);

    o.badassMode = false;
    RoomGen r2(1, kNoDoors, o);
    EXPECT_EQ(r2.SpecialBoxRate(), 0);
}

// --- At() bounds safety ------------------------------------------------------

TEST(RoomGenTest, OutOfBoundsAtReturnsZero) {
    const auto o = ExplicitRoom(15, 15, 1, 1);
    RoomGen r(1, kNoDoors, o);
    EXPECT_EQ(r.At(-1, 0), 0);
    EXPECT_EQ(r.At(0, -1), 0);
    EXPECT_EQ(r.At(r.Width(), 0), 0);
    EXPECT_EQ(r.At(0, r.Height()), 0);
}

// NOLINTEND(readability-magic-numbers)
