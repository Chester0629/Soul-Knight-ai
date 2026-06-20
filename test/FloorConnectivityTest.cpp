#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

#include "world/FloorBlock.hpp"
#include "world/MapManager.hpp"
#include "world/Room.hpp"
#include "world/RoomGen.hpp"

using Game::FloorBlock;
using Game::MapManager;
using Game::Room;
using Game::RoomCell;
using Game::RoomGen;

// NOLINTBEGIN(readability-magic-numbers)

namespace {

constexpr int kCells = 15;          // GameScene's fixed room footprint.
constexpr int B = FloorBlock::kBlock; // 41

bool Walkable(int code) { return !Room::IsSolidCell(code); }

// GameScene's per-room build: fixed 15x15, seed = floorSeed + 1 + roomIndex.
RoomGen BuildRoom(int floorSeed, int idx, const std::array<int, 4> &ent) {
    RoomGen::Options o;
    o.randomRoom = false;
    o.roomWidth = kCells;
    o.roomHeight = kCells;
    o.wallLevel = 1;
    o.obstacleLevel = 1;
    return RoomGen(floorSeed + 1 + idx, ent, o);
}

// The representative "interior" cell GameScene actually spawns the player/enemy
// on: the median of the room's CORRIDOR-CONNECTED floor cells (FloorBlock filters
// out obstacle-isolated pockets), in block-local coords. Same source of truth as
// GameScene's spawn, so the tested interior is the in-game interior.
std::pair<int, int> InteriorCell(const RoomGen &rg) {
    const auto cells = FloorBlock::ConnectedFloorCells(rg);
    const auto &c = cells[cells.size() / 2];
    return {FloorBlock::OffsetX(rg) + c.first, FloorBlock::OffsetY(rg) + c.second};
}

// A combined two-block walkable grid: block I and block J laid edge-to-edge,
// horizontally (J east of I) or vertically (J at +y of I), exactly as the 41-cell
// blocks tile in-game. Walkable() over this grid is the floor's true walk space.
struct Pair {
    std::vector<int> codes; // row-major x*H + y
    int W = 0;
    int H = 0;
    bool horizontal = true;
    int Code(int x, int y) const {
        if (x < 0 || y < 0 || x >= W || y >= H) {
            return FloorBlock::kMarginCell;
        }
        return codes[static_cast<std::size_t>(x) * static_cast<std::size_t>(H) +
                     static_cast<std::size_t>(y)];
    }
    void Solidify(int x, int y) {
        if (x >= 0 && y >= 0 && x < W && y < H) {
            codes[static_cast<std::size_t>(x) * static_cast<std::size_t>(H) +
                  static_cast<std::size_t>(y)] = FloorBlock::kMarginCell;
        }
    }
};

Pair MakePair(const std::vector<int> &blockI, const std::vector<int> &blockJ,
              bool horizontal) {
    Pair p;
    p.horizontal = horizontal;
    p.W = horizontal ? 2 * B : B;
    p.H = horizontal ? B : 2 * B;
    p.codes.assign(static_cast<std::size_t>(p.W) * static_cast<std::size_t>(p.H),
                   FloorBlock::kMarginCell);
    for (int x = 0; x < p.W; ++x) {
        for (int y = 0; y < p.H; ++y) {
            int code = 0;
            if (horizontal) {
                code = (x < B) ? FloorBlock::At(blockI, x, y)
                               : FloorBlock::At(blockJ, x - B, y);
            } else {
                code = (y < B) ? FloorBlock::At(blockI, x, y)
                               : FloorBlock::At(blockJ, x, y - B);
            }
            p.codes[static_cast<std::size_t>(x) * static_cast<std::size_t>(p.H) +
                    static_cast<std::size_t>(y)] = code;
        }
    }
    return p;
}

// Flood-fill over the FULL walkable cell set; true iff target is reached from
// start (4-neighbour). This is the acceptance the spec demands: reachability,
// not "both seam ends have an opening".
bool Reachable(const Pair &p, std::pair<int, int> start,
               std::pair<int, int> target) {
    if (!Walkable(p.Code(start.first, start.second)) ||
        !Walkable(p.Code(target.first, target.second))) {
        return false;
    }
    std::vector<char> seen(
        static_cast<std::size_t>(p.W) * static_cast<std::size_t>(p.H), 0);
    const auto idx = [&](int x, int y) {
        return static_cast<std::size_t>(x) * static_cast<std::size_t>(p.H) +
               static_cast<std::size_t>(y);
    };
    std::vector<std::pair<int, int>> stack{start};
    seen[idx(start.first, start.second)] = 1;
    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};
    while (!stack.empty()) {
        const auto [cx, cy] = stack.back();
        stack.pop_back();
        if (cx == target.first && cy == target.second) {
            return true;
        }
        for (int d = 0; d < 4; ++d) {
            const int nx = cx + dx[d];
            const int ny = cy + dy[d];
            if (nx < 0 || ny < 0 || nx >= p.W || ny >= p.H) {
                continue;
            }
            if (seen[idx(nx, ny)] != 0 || !Walkable(p.Code(nx, ny))) {
                continue;
            }
            seen[idx(nx, ny)] = 1;
            stack.push_back({nx, ny});
        }
    }
    return false;
}

// The WEAK predicate the spec rejects: "both rooms' edges have a walkable opening
// on the touching seam." It is blind to a break in the middle of the corridor.
bool BothRoomEdgesOpen(const std::vector<int> &blockI,
                       const std::vector<int> &blockJ, bool horizontal) {
    bool iOpen = false;
    bool jOpen = false;
    if (horizontal) { // I's east room edge (col 27) vs J's west room edge (col 13)
        for (int y = 0; y < B; ++y) {
            iOpen = iOpen || Walkable(FloorBlock::At(blockI, 27, y));
            jOpen = jOpen || Walkable(FloorBlock::At(blockJ, 13, y));
        }
    } else { // I's +y room edge (row 27) vs J's -y room edge (row 13)
        for (int x = 0; x < B; ++x) {
            iOpen = iOpen || Walkable(FloorBlock::At(blockI, x, 27));
            jOpen = jOpen || Walkable(FloorBlock::At(blockJ, x, 13));
        }
    }
    return iOpen && jOpen;
}

} // namespace

// === * The acceptance: BFS reachability, every adjacent pair, 5 seeds =========
// From room A's interior spawn cell to room B's interior spawn cell, walking only
// walkable cells across A's corridor -> shared seam -> B's corridor. Run for every
// vertical AND horizontal adjacency under each seed -- no extrapolation from one.
TEST(FloorConnectivityTest, EveryAdjacentPairInteriorReachable) {
    int totalHoriz = 0; // both orientations must be genuinely exercised across the
    int totalVert = 0;  // seed set (a single floor may legitimately be all-vertical).
    for (int floorSeed : {1, 2, 3, 7, 12345}) {
        const MapManager m(floorSeed, MapManager::Options{7, 25, 0});
        const auto &rooms = m.Rooms();
        std::vector<RoomGen> grids;
        std::vector<std::vector<int>> blocks;
        grids.reserve(rooms.size());
        blocks.reserve(rooms.size());
        for (std::size_t i = 0; i < rooms.size(); ++i) {
            grids.push_back(BuildRoom(floorSeed, static_cast<int>(i),
                                      rooms[i].entrance));
            blocks.push_back(FloorBlock::Build(grids[i], rooms[i].entrance));
        }

        int pairsChecked = 0;
        for (std::size_t i = 0; i < rooms.size(); ++i) {
            for (std::size_t j = 0; j < rooms.size(); ++j) {
                if (i == j) {
                    continue;
                }
                const int ddx = rooms[j].gridX - rooms[i].gridX;
                const int ddy = rooms[j].gridY - rooms[i].gridY;
                const bool horiz = (ddx == 1 && ddy == 0);
                const bool vert = (ddx == 0 && ddy == 1);
                if (!horiz && !vert) {
                    continue;
                }
                ++pairsChecked;
                if (horiz) {
                    ++totalHoriz;
                } else {
                    ++totalVert;
                }
                const Pair p = MakePair(blocks[i], blocks[j], horiz);
                const auto si = InteriorCell(grids[i]);
                auto tj = InteriorCell(grids[j]);
                if (horiz) {
                    tj.first += B;
                } else {
                    tj.second += B;
                }
                EXPECT_TRUE(Reachable(p, si, tj))
                    << "seed=" << floorSeed << " " << (horiz ? "H" : "V")
                    << " room " << i << "->" << j;
            }
        }
        EXPECT_GT(pairsChecked, 0) << "seed=" << floorSeed << " had no adjacencies";
    }
    // The spec demands BOTH orientations be tested (no extrapolation). Enforce it
    // as an invariant rather than relying on the RNG happening to produce both.
    EXPECT_GT(totalHoriz, 0) << "no horizontal adjacency exercised across the seeds";
    EXPECT_GT(totalVert, 0) << "no vertical adjacency exercised across the seeds";
}

// === Mutation guards: BFS catches a 1-cell break that "both ends open" misses ==
// The whole point of using reachability instead of the weak edge check: a break
// at the door->junction link (28) OR at the inter-block seam (40<->41) must FAIL
// BFS while the weak "both room edges open" predicate still (wrongly) passes.
TEST(FloorConnectivityTest, BfsCatchesMidCorridorBreakWeakCheckMisses) {
    const std::array<int, 4> eastOnly = {1, 0, 0, 0};
    const std::array<int, 4> westOnly = {0, 0, 1, 0};
    const RoomGen a = BuildRoom(4242, 0, eastOnly);
    const RoomGen b = BuildRoom(4242, 1, westOnly);
    const auto blockA = FloorBlock::Build(a, eastOnly);
    const auto blockB = FloorBlock::Build(b, westOnly);

    const auto sa = InteriorCell(a);
    auto tb = InteriorCell(b);
    tb.first += B; // B sits east of A in the combined grid

    // Sanity: the intact corridor connects the two interiors (proves the anchors
    // are themselves connected, so the mutation results are meaningful).
    const Pair intact = MakePair(blockA, blockB, /*horizontal=*/true);
    ASSERT_TRUE(Reachable(intact, sa, tb)) << "intact pair must connect";
    ASSERT_TRUE(BothRoomEdgesOpen(blockA, blockB, true));

    // Break A: the door->junction link (junction column 28, the whole door band).
    Pair brokenJunction = MakePair(blockA, blockB, true);
    for (int y = 18; y <= 22; ++y) {
        brokenJunction.Solidify(28, y);
    }
    EXPECT_FALSE(Reachable(brokenJunction, sa, tb))
        << "BFS must detect the door->junction (28) break";
    EXPECT_TRUE(BothRoomEdgesOpen(blockA, blockB, true))
        << "weak check is still (wrongly) green -- the false positive BFS catches";

    // Break B: the inter-block seam (A corridor end, combined column 40 <-> 41).
    Pair brokenSeam = MakePair(blockA, blockB, true);
    for (int y = 18; y <= 22; ++y) {
        brokenSeam.Solidify(40, y);
    }
    EXPECT_FALSE(Reachable(brokenSeam, sa, tb))
        << "BFS must detect the floor40<->neighbour41 seam break";
    EXPECT_TRUE(BothRoomEdgesOpen(blockA, blockB, true))
        << "weak check is still (wrongly) green here too";
}

// Vertical twin of the mutation guard: a break at the +y junction (row 28) OR the
// inter-block seam (row 40<->41) must fail BFS while the weak edge check passes --
// proving CorridorStrip's y-axis math is guarded symmetrically with the x-axis.
TEST(FloorConnectivityTest, BfsCatchesVerticalCorridorBreakWeakCheckMisses) {
    const std::array<int, 4> posY = {0, 0, 0, 1}; // A: +y door (toward B above)
    const std::array<int, 4> negY = {0, 1, 0, 0}; // B: -y door (toward A below)
    const RoomGen a = BuildRoom(4242, 0, posY);
    const RoomGen b = BuildRoom(4242, 1, negY);
    const auto blockA = FloorBlock::Build(a, posY);
    const auto blockB = FloorBlock::Build(b, negY);

    const auto sa = InteriorCell(a);
    auto tb = InteriorCell(b);
    tb.second += B; // B sits at +y of A in the combined grid

    const Pair intact = MakePair(blockA, blockB, /*horizontal=*/false);
    ASSERT_TRUE(Reachable(intact, sa, tb)) << "intact vertical pair must connect";
    ASSERT_TRUE(BothRoomEdgesOpen(blockA, blockB, false));

    Pair brokenJunction = MakePair(blockA, blockB, false);
    for (int x = 18; x <= 22; ++x) {
        brokenJunction.Solidify(x, 28); // A's +y junction row
    }
    EXPECT_FALSE(Reachable(brokenJunction, sa, tb))
        << "BFS must detect the +y junction (row 28) break";
    EXPECT_TRUE(BothRoomEdgesOpen(blockA, blockB, false));

    Pair brokenSeam = MakePair(blockA, blockB, false);
    for (int x = 18; x <= 22; ++x) {
        brokenSeam.Solidify(x, 40); // A corridor end <-> B row 41 seam
    }
    EXPECT_FALSE(Reachable(brokenSeam, sa, tb))
        << "BFS must detect the row40<->41 seam break";
    EXPECT_TRUE(BothRoomEdgesOpen(blockA, blockB, false));
}

// NOLINTEND(readability-magic-numbers)
