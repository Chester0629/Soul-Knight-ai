#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

#include "world/FloorBlock.hpp"
#include "world/Room.hpp"
#include "world/RoomGen.hpp"

using Game::FloorBlock;
using Game::Room;
using Game::RoomGen;

// NOLINTBEGIN(readability-magic-numbers)

namespace {

constexpr int kCells = 15; // GameScene's fixed room footprint (kRoomCells).

// Build the fixed-size 15x15 room GameScene uses, with the given door flags.
RoomGen MakeRoom(const std::array<int, 4> &entrance, int seed = 4242) {
    RoomGen::Options o;
    o.randomRoom = false;
    o.roomWidth = kCells;
    o.roomHeight = kCells;
    o.wallLevel = 1;
    o.obstacleLevel = 1;
    return RoomGen(seed, entrance, o);
}

bool Walkable(int code) { return !Room::IsSolidCell(code); } // 0 / -2 / 11

// Flood a 41x41 block grid from `start`; return the reached cell set (row-major).
std::vector<char> FloodBlock(const std::vector<int> &block,
                             std::pair<int, int> start) {
    const int B = FloorBlock::kBlock;
    std::vector<char> seen(static_cast<std::size_t>(B) * B, 0);
    if (!Walkable(FloorBlock::At(block, start.first, start.second))) {
        return seen;
    }
    std::vector<std::pair<int, int>> st{start};
    seen[static_cast<std::size_t>(start.first) * B + start.second] = 1;
    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};
    while (!st.empty()) {
        const auto [cx, cy] = st.back();
        st.pop_back();
        for (int d = 0; d < 4; ++d) {
            const int nx = cx + dx[d];
            const int ny = cy + dy[d];
            if (nx < 0 || ny < 0 || nx >= B || ny >= B) {
                continue;
            }
            const std::size_t i = static_cast<std::size_t>(nx) * B + ny;
            if (seen[i] != 0 || !Walkable(FloorBlock::At(block, nx, ny))) {
                continue;
            }
            seen[i] = 1;
            st.push_back({nx, ny});
        }
    }
    return seen;
}

const std::array<int, 4> kNoDoors = {0, 0, 0, 0};
const std::array<int, 4> kEastOnly = {1, 0, 0, 0};
const std::array<int, 4> kWestOnly = {0, 0, 1, 0};
const std::array<int, 4> kAllDoors = {1, 1, 1, 1};

} // namespace

// --- Block dimensions + room stamp -------------------------------------------

TEST(FloorBlockTest, BlockIs41x41) {
    const RoomGen rg = MakeRoom(kNoDoors);
    const auto block = FloorBlock::Build(rg, kNoDoors);
    EXPECT_EQ(FloorBlock::kBlock, 41);
    EXPECT_EQ(static_cast<int>(block.size()),
              FloorBlock::kBlock * FloorBlock::kBlock);
}

TEST(FloorBlockTest, RoomIsCentredAtOffset13) {
    const RoomGen rg = MakeRoom(kNoDoors);
    EXPECT_EQ(FloorBlock::OffsetX(rg), 13); // (41-15)/2
    EXPECT_EQ(FloorBlock::OffsetY(rg), 13);

    const auto block = FloorBlock::Build(rg, kNoDoors);
    const int ox = FloorBlock::OffsetX(rg);
    const int oy = FloorBlock::OffsetY(rg);
    // Every room cell is stamped verbatim at (ox+rx, oy+ry).
    for (int rx = 0; rx < rg.Width(); ++rx) {
        for (int ry = 0; ry < rg.Height(); ++ry) {
            EXPECT_EQ(FloorBlock::At(block, ox + rx, oy + ry), rg.At(rx, ry))
                << "room cell (" << rx << "," << ry << ")";
        }
    }
}

TEST(FloorBlockTest, MarginIsSolidWithoutEntrances) {
    const RoomGen rg = MakeRoom(kNoDoors);
    const auto block = FloorBlock::Build(rg, kNoDoors);
    // No doors -> the whole margin is solid (no corridor anywhere).
    EXPECT_FALSE(Walkable(FloorBlock::At(block, 28, 20))); // would-be east junction
    EXPECT_FALSE(Walkable(FloorBlock::At(block, 40, 20))); // would-be east edge
    EXPECT_FALSE(Walkable(FloorBlock::At(block, 0, 20)));  // would-be west edge
    EXPECT_FALSE(Walkable(FloorBlock::At(block, 20, 0)));  // would-be -y edge
    EXPECT_FALSE(Walkable(FloorBlock::At(block, 20, 40))); // would-be +y edge
}

// --- Corridor strip geometry (RGAisle-math: 5-wide, offset-long) --------------

TEST(FloorBlockTest, CorridorStripGeometryPerDirection) {
    const RoomGen rg = MakeRoom(kAllDoors);
    // EAST: cols 28..40 (junction 28 + floor 29..40), rows 18..22 (door band).
    const FloorBlock::Rect e = FloorBlock::CorridorStrip(FloorBlock::DIR_EAST, rg);
    EXPECT_EQ(e.x0, 28);
    EXPECT_EQ(e.x1, 40);
    EXPECT_EQ(e.y0, 18);
    EXPECT_EQ(e.y1, 22);
    EXPECT_EQ(e.y1 - e.y0 + 1, FloorBlock::kCorridorWidth); // 5-wide

    // WEST: cols 0..12, same row band.
    const FloorBlock::Rect w = FloorBlock::CorridorStrip(FloorBlock::DIR_WEST, rg);
    EXPECT_EQ(w.x0, 0);
    EXPECT_EQ(w.x1, 12);
    EXPECT_EQ(w.y0, 18);
    EXPECT_EQ(w.y1, 22);

    // -y: rows 0..12, col band 18..22.
    const FloorBlock::Rect ny =
        FloorBlock::CorridorStrip(FloorBlock::DIR_NEG_Y, rg);
    EXPECT_EQ(ny.x0, 18);
    EXPECT_EQ(ny.x1, 22);
    EXPECT_EQ(ny.y0, 0);
    EXPECT_EQ(ny.y1, 12);

    // +y: rows 28..40, col band 18..22.
    const FloorBlock::Rect py =
        FloorBlock::CorridorStrip(FloorBlock::DIR_POS_Y, rg);
    EXPECT_EQ(py.x0, 18);
    EXPECT_EQ(py.x1, 22);
    EXPECT_EQ(py.y0, 28);
    EXPECT_EQ(py.y1, 40);
}

TEST(FloorBlockTest, CorridorCarvedOnlyOnSetEntranceSides) {
    const RoomGen rg = MakeRoom(kEastOnly);
    const auto block = FloorBlock::Build(rg, kEastOnly);
    // East corridor band is walkable end-to-end...
    for (int x = 28; x <= 40; ++x) {
        for (int y = 18; y <= 22; ++y) {
            EXPECT_TRUE(Walkable(FloorBlock::At(block, x, y)))
                << "east corridor (" << x << "," << y << ")";
        }
    }
    // ...flanks of the east corridor stay solid (player can't leave sideways)...
    EXPECT_FALSE(Walkable(FloorBlock::At(block, 34, 17)));
    EXPECT_FALSE(Walkable(FloorBlock::At(block, 34, 23)));
    // ...and the unset west side has no corridor.
    EXPECT_FALSE(Walkable(FloorBlock::At(block, 0, 20)));
    EXPECT_FALSE(Walkable(FloorBlock::At(block, 6, 20)));
}

// --- Seam 1: room door / aisle aligns to the corridor junction (no 1-cell gap)

TEST(FloorBlockTest, Seam1_DoorAlignsToCorridorWithNoGapOnCentreline) {
    const RoomGen rg = MakeRoom(kEastOnly);
    const auto block = FloorBlock::Build(rg, kEastOnly);

    // The room's east wall is broken by the code-11 door cells at block (27,17)
    // and (27,23), flanking the 5-wide aisle band (27,18..22).
    EXPECT_EQ(FloorBlock::At(block, 27, 17), 11) << "north door cell";
    EXPECT_EQ(FloorBlock::At(block, 27, 23), 11) << "south door cell";
    for (int y = 18; y <= 22; ++y) {
        EXPECT_TRUE(Walkable(FloorBlock::At(block, 27, y))) << "room aisle row " << y;
    }

    // * No 1-cell break on the centreline: every cell from the room interior edge
    // (26) across the door edge (27), junction (28) and corridor floor (29..40)
    // is walkable -- the whole "door(1)+floor(12)=13" run is contiguous.
    for (int x = 26; x <= 40; ++x) {
        EXPECT_TRUE(Walkable(FloorBlock::At(block, x, 20)))
            << "centreline break at x=" << x;
    }
}

// --- Seam 3: corridor reaches the block edge so neighbour blocks meet ---------

TEST(FloorBlockTest, Seam3_CorridorReachesBlockEdge) {
    // East entrance -> last corridor cell is the block edge col 40, which sits
    // orthogonally adjacent to the +x neighbour block's col 0 once tiled at pitch.
    const RoomGen east = MakeRoom(kEastOnly);
    const auto eblock = FloorBlock::Build(east, kEastOnly);
    for (int y = 18; y <= 22; ++y) {
        EXPECT_TRUE(Walkable(FloorBlock::At(eblock, 40, y))) << "east edge row " << y;
    }

    // West entrance -> reaches col 0 (the -x neighbour seam).
    const RoomGen west = MakeRoom(kWestOnly);
    const auto wblock = FloorBlock::Build(west, kWestOnly);
    for (int y = 18; y <= 22; ++y) {
        EXPECT_TRUE(Walkable(FloorBlock::At(wblock, 0, y))) << "west edge row " << y;
    }
}

// --- ConnectedFloorCells: spawn anchors that are reachable from a corridor ----
// RoomGen::FloorList can hand back a cell trapped in an obstacle pocket; spawning
// the player there (the start room) strands them, and it would fail a corridor
// reachability test for the wrong reason. ConnectedFloorCells filters to the
// door-connected main region. The known case: seed-3 room 0 (FloorBlock per-room
// seed 3+1+0 = 4, doors -y and +y) -- its FloorList median is in such a pocket.
TEST(FloorBlockTest, ConnectedFloorCellsReachCorridorMouths) {
    const std::array<int, 4> ent = {0, 1, 0, 1}; // seed-3 room 0 entrances
    const RoomGen rg = MakeRoom(ent, 4);          // per-room seed 3+1+0
    const auto block = FloorBlock::Build(rg, ent);
    const auto connected = FloorBlock::ConnectedFloorCells(rg);
    ASSERT_FALSE(connected.empty());

    const int ox = FloorBlock::OffsetX(rg);
    const int oy = FloorBlock::OffsetY(rg);

    // Every connected cell is a floor cell that floods to a carved corridor mouth.
    for (const auto &c : connected) {
        EXPECT_EQ(rg.At(c.first, c.second), 0) << "must be a floor cell";
    }
    const auto anchor = connected[connected.size() / 2];
    const auto seen = FloodBlock(block, {ox + anchor.first, oy + anchor.second});
    const int B = FloorBlock::kBlock;
    const bool reachesMouth =
        seen[static_cast<std::size_t>(20) * B + 0] != 0 ||   // -y mouth
        seen[static_cast<std::size_t>(20) * B + 40] != 0;    // +y mouth
    EXPECT_TRUE(reachesMouth) << "connected anchor must reach a corridor mouth";

    // The raw FloorList median is the pocket cell that does NOT reach a mouth --
    // proving the filter removed real isolated cells (connected is a strict subset).
    const auto &fl = rg.FloorList();
    const auto flMid = fl[fl.size() / 2];
    const auto seenFl = FloodBlock(block, {ox + flMid.first, oy + flMid.second});
    EXPECT_FALSE(seenFl[static_cast<std::size_t>(20) * B + 0] != 0 ||
                 seenFl[static_cast<std::size_t>(20) * B + 40] != 0)
        << "the FloorList median for this room is the isolated pocket";
    EXPECT_LT(connected.size(), fl.size()) << "isolated cells were filtered out";
}

// --- DoorSealCells: a locked room must seal the WHOLE opening ------------------
// The clear-room lock seals these cells. RoomGen::CreateAisle carves the EAST
// opening as a 5-wide aisle band (code -2, rows cy..cy+4) plus two flank door
// cells (code 11, rows cy-1/cy+5) on the perimeter column rw-1. Sealing only the
// code-11 cells leaves the 160px aisle band open, so the lock contains nobody.
TEST(FloorBlockTest, DoorSealCoversFullOpeningNotJustCode11) {
    const RoomGen rg = MakeRoom(kEastOnly);
    const auto seal = FloorBlock::DoorSealCells(rg);
    const int rw1 = rg.Width() - 1; // 14 (perimeter column of the east door)
    const auto has = [&](int x, int y) {
        return std::find(seal.begin(), seal.end(), std::make_pair(x, y)) !=
               seal.end();
    };
    EXPECT_TRUE(has(rw1, 4)) << "code-11 flank (cy-1)";
    EXPECT_TRUE(has(rw1, 10)) << "code-11 flank (cy+5)";
    for (int y = 5; y <= 9; ++y) { // the 5-wide aisle band MUST be sealed too
        EXPECT_TRUE(has(rw1, y)) << "aisle band cell unsealed -> lock leaks, y=" << y;
    }
    // Everything sealed is a walkable opening cell on the room perimeter.
    for (const auto &c : seal) {
        const bool perim = (c.first == 0 || c.first == rg.Width() - 1 ||
                            c.second == 0 || c.second == rg.Height() - 1);
        EXPECT_TRUE(perim) << "seal cell off the perimeter (" << c.first << ","
                           << c.second << ")";
        const int code = rg.At(c.first, c.second);
        EXPECT_TRUE(code == -2 || code == 11) << "seal cell not a walkable opening";
    }
}

// --- Phase 2: geometry adapts to a non-15 room size (size-invariant band) ------
TEST(FloorBlockTest, GeometryAdaptsTo21WideRoom) {
    RoomGen::Options o;
    o.randomRoom = false;
    o.roomWidth = 21;
    o.roomHeight = 21;
    o.wallLevel = 1;
    o.obstacleLevel = 1;
    const RoomGen rg(4242, kEastOnly, o);

    EXPECT_EQ(FloorBlock::OffsetX(rg), 10); // (41-21)/2
    EXPECT_EQ(FloorBlock::OffsetY(rg), 10);

    const FloorBlock::Rect e = FloorBlock::CorridorStrip(FloorBlock::DIR_EAST, rg);
    EXPECT_EQ(e.x0, 31); // offset_x + W = 10 + 21 (corridor starts later for a wider room)
    EXPECT_EQ(e.x1, 40); // still reaches the block edge
    EXPECT_EQ(e.y0, 18); // ...but the door band stays centred at 18-22 for ANY size
    EXPECT_EQ(e.y1, 22);

    const auto block = FloorBlock::Build(rg, kEastOnly);
    for (int y = 18; y <= 22; ++y) {
        EXPECT_TRUE(Walkable(FloorBlock::At(block, 31, y))) << "junction, y=" << y;
        EXPECT_TRUE(Walkable(FloorBlock::At(block, 40, y))) << "block edge, y=" << y;
    }
}

// NOLINTEND(readability-magic-numbers)
