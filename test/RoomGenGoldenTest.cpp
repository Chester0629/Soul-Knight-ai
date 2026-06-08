#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "world/RoomGen.hpp"

using Game::RoomGen;

// NOLINTBEGIN(readability-magic-numbers)

// === RoomGen golden-grid regression locks (Manual Intervention #4) ============
//
// RoomGen is fully deterministic from (seed, entrance[], options) and its integer
// RNG path is validated bit-exact to Unity (report #1). These goldens LOCK the
// current grid output for fixed seeds so any accidental change to the generation
// pipeline (stage order, an extra/missing RNG draw, a band/threshold edit) is
// caught immediately.
//
// IMPORTANT FIDELITY CAVEAT: these goldens encode the *reconstructed* RoomGen
// constants, NOT a captured real-game grid. Per the #4 recovery dossier, two
// inputs remain STILL-EXTERNAL-BLOCKED and could not be byte-recovered from any
// Ghidra export (the RGRoomX__SetRGRandomSeed @ 0x50f350 body is truncated before
// that code, and the DAT_0050F930 float-weight block is absent):
//   (1) the random-room size-roll / difficulty-roll ORDER, and
//   (2) ComputeBaseLevel's float weights {1,1.5,2}/{+0.5,+1} + thresholds 16/22.
// The CONFIRMED-faithful parts (obstacle count bands, IsWallIntersect margin,
// CreateFloor/CreateWall passes, render pass) ARE decomp-matched. So: a failure of
// an Explicit golden (g1/g2/g3/g5) means a real regression in the generator. The
// random-room golden (g4) additionally pins the RECONSTRUCTED roll order; if a
// captured real-game grid ever lands, g4 is the one to re-baseline.

namespace {

// FNV-1a 64-bit over a sequence of ints (order-sensitive).
std::uint64_t Fnv1a(const std::vector<int> &data) {
    std::uint64_t h = 1469598103934665603ULL; // offset basis
    for (const int v : data) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int b = 0; b < 4; ++b) {
            h ^= static_cast<std::uint64_t>((u >> (b * 8)) & 0xFFU);
            h *= 1099511628211ULL; // FNV prime
        }
    }
    return h;
}

std::uint64_t GridHash(const RoomGen &r) { return Fnv1a(r.Grid()); }

std::uint64_t FloorHash(const RoomGen &r) {
    std::vector<int> flat;
    flat.reserve(r.FloorList().size() * 2);
    for (const auto &c : r.FloorList()) {
        flat.push_back(c.first);
        flat.push_back(c.second);
    }
    return Fnv1a(flat);
}

const std::array<int, 4> kNoDoors = {0, 0, 0, 0};
const std::array<int, 4> kAllDoors = {1, 1, 1, 1};
const std::array<int, 4> kEastOnly = {1, 0, 0, 0};

RoomGen::Options ExplicitRoom(int w, int h, int wallLvl, int obstLvl) {
    RoomGen::Options o;
    o.randomRoom = false;
    o.roomWidth = w;
    o.roomHeight = h;
    o.wallLevel = wallLvl;
    o.obstacleLevel = obstLvl;
    return o;
}

// One golden record: the full deterministic fingerprint of a generated room.
struct Golden {
    std::uint64_t gridHash;
    std::uint64_t floorHash;
    int floors;
    int w;
    int h;
    int wallLevel;
    int obstacleLevel;
    int offsetX;
    int offsetY;
    int specialBoxRate;
};

void CheckGolden(const Golden &g, const RoomGen &r) {
    EXPECT_EQ(GridHash(r), g.gridHash) << "grid hash drift";
    EXPECT_EQ(FloorHash(r), g.floorHash) << "floor hash drift";
    EXPECT_EQ(static_cast<int>(r.FloorList().size()), g.floors) << "floor count drift";
    EXPECT_EQ(r.Width(), g.w);
    EXPECT_EQ(r.Height(), g.h);
    EXPECT_EQ(r.WallLevel(), g.wallLevel);
    EXPECT_EQ(r.ObstacleLevel(), g.obstacleLevel);
    EXPECT_EQ(r.OffsetX(), g.offsetX);
    EXPECT_EQ(r.OffsetY(), g.offsetY);
    EXPECT_EQ(r.SpecialBoxRate(), g.specialBoxRate);
}

} // namespace

// g1: max size, max difficulty, all four doors -> exercises every stage + heavy
//     obstacle placement. CONFIRMED-faithful stages.
TEST(RoomGenGoldenTest, G1_Max25x25_L3_AllDoors) {
    const Golden g{7129463644097436581ULL, 16380198186965041929ULL, 324,
                   25, 25, 3, 3, 8, 8, 0};
    RoomGen r(12345, kAllDoors, ExplicitRoom(25, 25, 3, 3));
    CheckGolden(g, r);
}

// g2: min size, min difficulty, no doors -> clean perimeter + sparse obstacles.
TEST(RoomGenGoldenTest, G2_Min15x15_L1_NoDoors) {
    const Golden g{6165510508371288133ULL, 8667849711382307197ULL, 105,
                   15, 15, 1, 1, 13, 13, 0};
    RoomGen r(999, kNoDoors, ExplicitRoom(15, 15, 1, 1));
    CheckGolden(g, r);
}

// g3: asymmetric w!=h + single east door -> catches x*height+y index bugs + door
//     and wall interplay.
TEST(RoomGenGoldenTest, G3_Asym21x15_L2_EastDoor) {
    const Golden g{7326138852820820341ULL, 4329835637181939792ULL, 167,
                   21, 15, 2, 2, 10, 13, 0};
    RoomGen r(7, kEastOnly, ExplicitRoom(21, 15, 2, 2));
    CheckGolden(g, r);
}

// g4: random-room path (size-roll + difficulty-roll). Pins the RECONSTRUCTED roll
//     order; re-baseline this one if a captured real-game grid ever lands.
TEST(RoomGenGoldenTest, G4_RandomRoom_Floor0) {
    const Golden g{11808428556677858851ULL, 14488161160482090163ULL, 192,
                   15, 25, 3, 2, 13, 8, 0};
    RoomGen::Options o;
    o.randomRoom = true;
    o.floorIndex = 0;
    RoomGen r(12345, kAllDoors, o);
    CheckGolden(g, r);
}

// g5: a second max-size seed -> distinct grid; guards against seed-independent bugs.
TEST(RoomGenGoldenTest, G5_Max25x25_L3_Seed2024) {
    const Golden g{3994404615466042921ULL, 1370606586774510252ULL, 396,
                   25, 25, 3, 3, 8, 8, 0};
    RoomGen r(2024, kAllDoors, ExplicitRoom(25, 25, 3, 3));
    CheckGolden(g, r);
}

// NOLINTEND(readability-magic-numbers)
