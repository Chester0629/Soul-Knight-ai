#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <map>
#include <vector>

#include "data/RGRandom.hpp"
#include "world/RoomGen.hpp"

using Game::RGRandom;
using Game::RoomGen;

// NOLINTBEGIN(readability-magic-numbers)

// P2.20: the two RoomGen stages that decide room DETAIL -- CreateFloor pass 3 (which of the six floor tiles
// a cell wears) and CreateWall (which perimeter pieces wear the decorated wall) -- pinned against an
// INDEPENDENT replica of the original draw order, DISASM tools/arm_disasm.py 0x50d3e4 0x50db6c (CreateFloor)
// and 0x50db6c 0x50e210 (CreateWall). The replica below is written from the disassembly, not from RoomGen:
//   pass 1  : x outer, y inner, one Range(0,2) per cell                         (0x50d4b4)
//   pass 2  : no draw; count = (self==0); if x>0 && left==0: count = (self==0 ? 2 : 1) [ASSIGNED, not
//             added]; +1 per empty right / down / up neighbour                  (0x50d544-0x50d83c)
//   pass 3  : x outer, y inner, ONE draw per cell:
//             edge 1..2 -> Range(0,4) > 2 ? 1 : 0     (0x50d9d4-0x50d9dc)
//             edge 0, 3 -> Range(2,4)                  (0x50d9a0-0x50d9ac)
//             edge >= 4 -> Range(0,4) < 3 ? 5 : 4      (0x50d984-0x50d998)
//   walls   : x = xoff-1 .. xoff+W (inclusive) then y = yoff .. yoff+H-1, window [18,22] skipped; per position
//             two Range(0,100), each `< 30` -> wall_list[1]   (0x50dc24 / 0x50dc54, 0x50df58 / 0x50df88)
namespace {

const std::array<int, 4> kNoDoors = {0, 0, 0, 0};

RoomGen::Options Explicit(int w, int h, int wallLvl, int obstLvl) {
    RoomGen::Options o;
    o.randomRoom = false;
    o.roomWidth = w;
    o.roomHeight = h;
    o.wallLevel = wallLvl;
    o.obstacleLevel = obstLvl;
    return o;
}

struct Replica {
    int w = 0;
    int h = 0;
    std::vector<int> edge;     // x*h+y
    std::vector<int> variant;  // x*h+y
    std::vector<int> draw;     // the raw pass-3 value drawn for the cell
    std::vector<int> southRoll, northRoll, westRoll, eastRoll; // raw Range(0,100), -1 = no draw
    int xoff = 0;
    int yoff = 0;
    long long draws = 0;
};

Replica Replay(int seed, int w, int h) {
    Replica r;
    r.w = w;
    r.h = h;
    r.xoff = (41 - w) / 2;
    r.yoff = (41 - h) / 2;
    RGRandom rng;
    rng.SetRandomSeed(seed);
    const auto idx = [h](int x, int y) { return static_cast<std::size_t>(x * h + y); };
    std::vector<int> noise(static_cast<std::size_t>(w * h));
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            noise[idx(x, y)] = rng.Range(0, 2);
            ++r.draws;
        }
    }
    r.edge.assign(noise.size(), 0);
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            const int self = noise[idx(x, y)];
            int count = self == 0 ? 1 : 0;
            if (x >= 1 && noise[idx(x - 1, y)] == 0) {
                count = self == 0 ? 2 : 1; // assignment (0x50d658 moveq r7, r1)
            }
            if (x < w - 1 && noise[idx(x + 1, y)] == 0) {
                ++count;
            }
            if (y >= 1 && noise[idx(x, y - 1)] == 0) {
                ++count;
            }
            if (y < h - 1 && noise[idx(x, y + 1)] == 0) {
                ++count;
            }
            r.edge[idx(x, y)] = count;
        }
    }
    r.variant.assign(noise.size(), 0);
    r.draw.assign(noise.size(), 0);
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            const int e = r.edge[idx(x, y)];
            int v = 0;
            int d = 0;
            if (e >= 1 && e <= 2) {
                d = rng.Range(0, 4);
                v = d > 2 ? 1 : 0;
            } else if (e <= 3) {
                d = rng.Range(2, 4);
                v = d;
            } else {
                d = rng.Range(0, 4);
                v = d < 3 ? 5 : 4;
            }
            ++r.draws;
            r.variant[idx(x, y)] = v;
            r.draw[idx(x, y)] = d;
        }
    }
    const int nx = w + 2;
    r.southRoll.assign(static_cast<std::size_t>(nx), -1);
    r.northRoll.assign(static_cast<std::size_t>(nx), -1);
    r.westRoll.assign(static_cast<std::size_t>(h), -1);
    r.eastRoll.assign(static_cast<std::size_t>(h), -1);
    for (int x = r.xoff - 1; x < r.xoff + w + 1; ++x) {
        if (x >= 18 && x <= 22) {
            continue;
        }
        r.southRoll[static_cast<std::size_t>(x - (r.xoff - 1))] = rng.Range(0, 100);
        r.northRoll[static_cast<std::size_t>(x - (r.xoff - 1))] = rng.Range(0, 100);
        r.draws += 2;
    }
    for (int y = r.yoff; y < r.yoff + h; ++y) {
        if (y >= 18 && y <= 22) {
            continue;
        }
        r.westRoll[static_cast<std::size_t>(y - r.yoff)] = rng.Range(0, 100);
        r.eastRoll[static_cast<std::size_t>(y - r.yoff)] = rng.Range(0, 100);
        r.draws += 2;
    }
    return r;
}

// 5-sigma binomial tolerance for a proportion p over n samples.
double Tol(double p, double n) { return 5.0 * std::sqrt(p * (1.0 - p) / n); }

} // namespace

// AC1: exactly one pass-3 draw per cell and the per-stage draw total of the original. With no obstacles
// (levels 0/0) the build draws only: W*H noise + W*H variants + 2 per non-gap perimeter position.
TEST(RoomDetailTest, DrawCountIsTheOriginalPerStageTotal) {
    for (const auto &[w, h] : std::vector<std::pair<int, int>>{{15, 15}, {21, 15}, {25, 25}, {11, 11}, {19, 13}}) {
        const Replica rep = Replay(77, w, h);
        RoomGen rg(77, kNoDoors, Explicit(w, h, 0, 0));
        EXPECT_EQ(rg.DrawCount(), rep.draws) << w << "x" << h;
        // closed form (x: W+2 positions minus the gap window, y: H positions minus the gap window)
        const int xoff = (41 - w) / 2;
        const int yoff = (41 - h) / 2;
        int xs = 0;
        for (int x = xoff - 1; x < xoff + w + 1; ++x) {
            xs += (x >= 18 && x <= 22) ? 0 : 1;
        }
        int ys = 0;
        for (int y = yoff; y < yoff + h; ++y) {
            ys += (y >= 18 && y <= 22) ? 0 : 1;
        }
        EXPECT_EQ(rg.DrawCount(), static_cast<long long>(2 * w * h + 2 * (xs + ys))) << w << "x" << h;
    }
    // The 15x15 room spelled out: 225 + 225 + 2 * (12 + 10) = 494.
    EXPECT_EQ(RoomGen(1, kNoDoors, Explicit(15, 15, 0, 0)).DrawCount(), 494);
}

// COUNTEREXAMPLE of the old code: it drew 1-2 values per cell in pass 3 (an extra always-draw Range(2,4)),
// so its total for the same room was larger. The pinned total above excludes that.
TEST(RoomDetailTest, OldTwoDrawsPerCellWouldHaveGivenALargerTotal) {
    const Replica rep = Replay(5, 15, 15);
    long long oldTotal = 225;                // pass 1
    for (int x = 0; x < 15; ++x) {
        for (int y = 0; y < 15; ++y) {
            const int e = rep.edge[static_cast<std::size_t>(x * 15 + y)];
            oldTotal += ((e >= 1 && e <= 2) || e >= 4) ? 2 : 1; // classification draw + always-draw
        }
    }
    oldTotal += 22; // the old code drew ONE wall roll per non-gap position (12 x + 10 y positions), not two
    EXPECT_NE(oldTotal, rep.draws);
    EXPECT_EQ(RoomGen(5, kNoDoors, Explicit(15, 15, 0, 0)).DrawCount(), rep.draws);
}

// AC1: every cell's variant equals the independent replica's -- which also proves the draw ORDER (one wrong or
// missing draw shifts every later cell's value).
TEST(RoomDetailTest, FloorVariantOfEveryCellMatchesTheOriginalMapping) {
    for (const int seed : {1, 2, 3, 12345, 999, 424242}) {
        for (const auto &[w, h] : std::vector<std::pair<int, int>>{{15, 15}, {21, 15}, {25, 25}}) {
            const Replica rep = Replay(seed, w, h);
            RoomGen rg(seed, kNoDoors, Explicit(w, h, 0, 0));
            for (int x = 0; x < w; ++x) {
                for (int y = 0; y < h; ++y) {
                    ASSERT_EQ(rg.FloorVariantAt(x, y), rep.variant[static_cast<std::size_t>(x * h + y)])
                        << "seed " << seed << " " << w << "x" << h << " cell " << x << "," << y;
                }
            }
        }
    }
}

// The class -> index set of the original: edge 1..2 -> {0,1}, edge 0 or 3 -> {2,3}, edge >= 4 -> {4,5}; the
// boundary draw values are exercised (d == 2 / 3 for edge 1..2, d == 2 / 3 for edge >= 4).
TEST(RoomDetailTest, EdgeClassSelectsTheIndexSetAndTheBoundaryDraws) {
    std::map<int, std::array<int, 6>> seen; // class (0..4+) -> count per variant
    bool sawD2Low = false, sawD3Low = false, sawD2High = false, sawD3High = false;
    for (int seed = 1; seed <= 60; ++seed) {
        const Replica rep = Replay(seed, 25, 25);
        for (std::size_t i = 0; i < rep.edge.size(); ++i) {
            const int e = rep.edge[i];
            const int cls = e >= 4 ? 4 : e;
            const int v = rep.variant[i];
            ++seen[cls][static_cast<std::size_t>(v)];
            if (e >= 1 && e <= 2) {
                EXPECT_TRUE(v == 0 || v == 1);
                sawD2Low |= rep.draw[i] == 2 && v == 0;
                sawD3Low |= rep.draw[i] == 3 && v == 1;
            } else if (e == 0 || e == 3) {
                EXPECT_TRUE(v == 2 || v == 3);
            } else {
                EXPECT_TRUE(v == 4 || v == 5);
                sawD2High |= rep.draw[i] == 2 && v == 5;
                sawD3High |= rep.draw[i] == 3 && v == 4;
            }
        }
    }
    EXPECT_TRUE(sawD2Low && sawD3Low && sawD2High && sawD3High);
    // the lightest tiles f505 / f506 (indices 4 and 5) exist now -- the old mapping topped out at 3
    std::array<int, 6> total{};
    for (const auto &kv : seen) {
        for (std::size_t v = 0; v < 6; ++v) {
            total[v] += kv.second[v];
        }
    }
    EXPECT_GT(total[4], 0);
    EXPECT_GT(total[5], 0);
}

// AC1: histogram over >= 10000 cells (60 rooms of 25x25 = 37500 cells) vs the 75/25 and 50/50 splits.
TEST(RoomDetailTest, FloorVariantHistogramMatchesThe75_25Split) {
    long long low1 = 0, low2 = 0;   // edge 1..2: variant 1 / total
    long long mid3 = 0, mid = 0;    // edge 0|3 : variant 3 / total
    long long hi4 = 0, hi = 0;      // edge >= 4: variant 4 / total
    long long cells = 0;
    for (int seed = 1; seed <= 60; ++seed) {
        RoomGen rg(seed, kNoDoors, Explicit(25, 25, 0, 0));
        const Replica rep = Replay(seed, 25, 25);
        for (int x = 0; x < 25; ++x) {
            for (int y = 0; y < 25; ++y) {
                const int e = rep.edge[static_cast<std::size_t>(x * 25 + y)];
                const int v = rg.FloorVariantAt(x, y);
                ++cells;
                if (e >= 1 && e <= 2) {
                    ++low2;
                    low1 += v == 1;
                } else if (e == 0 || e == 3) {
                    ++mid;
                    mid3 += v == 3;
                } else {
                    ++hi;
                    hi4 += v == 4;
                }
            }
        }
    }
    // evidence line (tools/p220_check.py parses it): counts and shares of the three classes
    std::printf("P220 HIST cells=%lld edge12_n=%lld edge12_v1=%lld edge03_n=%lld edge03_v3=%lld edge4_n=%lld edge4_v4=%lld\n",
                cells, low2, low1, mid, mid3, hi, hi4);
    ASSERT_GE(cells, 10000);
    ASSERT_GT(low2, 1000);
    ASSERT_GT(mid, 1000);
    ASSERT_GT(hi, 1000);
    EXPECT_NEAR(static_cast<double>(low1) / static_cast<double>(low2), 0.25, Tol(0.25, static_cast<double>(low2)));
    EXPECT_NEAR(static_cast<double>(mid3) / static_cast<double>(mid), 0.50, Tol(0.50, static_cast<double>(mid)));
    EXPECT_NEAR(static_cast<double>(hi4) / static_cast<double>(hi), 0.25, Tol(0.25, static_cast<double>(hi)));
}

// AC2: every perimeter roll of the replica decides the decorated flag with the strict `< 30` rule; the door-gap
// window has no roll; the boundary values 29 / 30 are exercised.
TEST(RoomDetailTest, WallDecorationFollowsTheTwoRollRuleWithThreshold30) {
    bool saw29 = false, saw30 = false;
    for (const int seed : {1, 7, 99, 12345, 31337}) {
        for (const auto &[w, h] : std::vector<std::pair<int, int>>{{15, 15}, {21, 15}, {25, 25}}) {
            const Replica rep = Replay(seed, w, h);
            RoomGen rg(seed, kNoDoors, Explicit(w, h, 0, 0));
            ASSERT_EQ(rep.xoff, rg.OffsetX());
            ASSERT_EQ(rep.yoff, rg.OffsetY());
            for (int x = rep.xoff - 1; x < rep.xoff + w + 1; ++x) {
                const std::size_t i = static_cast<std::size_t>(x - (rep.xoff - 1));
                const bool gap = x >= 18 && x <= 22;
                EXPECT_EQ(rg.WallPieceRolled(RoomGen::WallSide::South, x), !gap) << "x " << x;
                EXPECT_EQ(rg.WallPieceRolled(RoomGen::WallSide::North, x), !gap) << "x " << x;
                if (gap) {
                    EXPECT_FALSE(rg.WallPieceDecorated(RoomGen::WallSide::South, x));
                    EXPECT_FALSE(rg.WallPieceDecorated(RoomGen::WallSide::North, x));
                    continue;
                }
                EXPECT_EQ(rg.WallPieceDecorated(RoomGen::WallSide::South, x), rep.southRoll[i] < 30);
                EXPECT_EQ(rg.WallPieceDecorated(RoomGen::WallSide::North, x), rep.northRoll[i] < 30);
                saw29 |= rep.southRoll[i] == 29 || rep.northRoll[i] == 29;
                saw30 |= rep.southRoll[i] == 30 || rep.northRoll[i] == 30;
            }
            for (int y = rep.yoff; y < rep.yoff + h; ++y) {
                const std::size_t i = static_cast<std::size_t>(y - rep.yoff);
                const bool gap = y >= 18 && y <= 22;
                EXPECT_EQ(rg.WallPieceRolled(RoomGen::WallSide::West, y), !gap) << "y " << y;
                EXPECT_EQ(rg.WallPieceRolled(RoomGen::WallSide::East, y), !gap) << "y " << y;
                if (gap) {
                    continue;
                }
                EXPECT_EQ(rg.WallPieceDecorated(RoomGen::WallSide::West, y), rep.westRoll[i] < 30);
                EXPECT_EQ(rg.WallPieceDecorated(RoomGen::WallSide::East, y), rep.eastRoll[i] < 30);
                saw29 |= rep.westRoll[i] == 29 || rep.eastRoll[i] == 29;
                saw30 |= rep.westRoll[i] == 30 || rep.eastRoll[i] == 30;
            }
            // out of range -> no roll
            EXPECT_FALSE(rg.WallPieceRolled(RoomGen::WallSide::South, rep.xoff - 2));
            EXPECT_FALSE(rg.WallPieceRolled(RoomGen::WallSide::South, rep.xoff + w + 1));
            EXPECT_FALSE(rg.WallPieceRolled(RoomGen::WallSide::West, rep.yoff - 1));
            EXPECT_FALSE(rg.WallPieceRolled(RoomGen::WallSide::West, rep.yoff + h));
        }
    }
    EXPECT_TRUE(saw29 && saw30) << "the boundary rolls 29 (decorated) and 30 (plain) must both be exercised";
}

// AC2: the decorated share over many perimeter pieces is 30% (5-sigma), and wall pieces are rolled in pairs.
TEST(RoomDetailTest, DecoratedShareIs30Percent) {
    long long rolled = 0, deco = 0;
    for (int seed = 1; seed <= 200; ++seed) {
        RoomGen rg(seed, kNoDoors, Explicit(25, 25, 0, 0));
        for (int x = rg.OffsetX() - 1; x < rg.OffsetX() + 26; ++x) {
            for (const auto side : {RoomGen::WallSide::South, RoomGen::WallSide::North}) {
                if (rg.WallPieceRolled(side, x)) {
                    ++rolled;
                    deco += rg.WallPieceDecorated(side, x);
                }
            }
        }
        for (int y = rg.OffsetY(); y < rg.OffsetY() + 25; ++y) {
            for (const auto side : {RoomGen::WallSide::West, RoomGen::WallSide::East}) {
                if (rg.WallPieceRolled(side, y)) {
                    ++rolled;
                    deco += rg.WallPieceDecorated(side, y);
                }
            }
        }
    }
    std::printf("P220 WALLS rolled=%lld decorated=%lld\n", rolled, deco);
    ASSERT_GT(rolled, 10000);
    EXPECT_NEAR(static_cast<double>(deco) / static_cast<double>(rolled), 0.30, Tol(0.30, static_cast<double>(rolled)));
}

// T-73: the port's border cell -> original piece mapping.
TEST(RoomDetailTest, BorderCellsWearTheRollOfTheirWorldColumnOrRow) {
    const int w = 21, h = 15;
    RoomGen rg(2024, kNoDoors, Explicit(w, h, 0, 0));
    const int xo = rg.OffsetX();
    const int yo = rg.OffsetY();
    for (int x = 0; x < w; ++x) {
        EXPECT_EQ(rg.BorderCellDecorated(x, 0), rg.WallPieceDecorated(RoomGen::WallSide::South, xo + x));
        EXPECT_EQ(rg.BorderCellDecorated(x, h - 1), rg.WallPieceDecorated(RoomGen::WallSide::North, xo + x));
    }
    for (int y = 1; y < h - 1; ++y) {
        EXPECT_EQ(rg.BorderCellDecorated(0, y), rg.WallPieceDecorated(RoomGen::WallSide::West, yo + y));
        EXPECT_EQ(rg.BorderCellDecorated(w - 1, y), rg.WallPieceDecorated(RoomGen::WallSide::East, yo + y));
    }
    // interior and out-of-grid cells never wear it
    for (int x = 1; x < w - 1; ++x) {
        for (int y = 1; y < h - 1; ++y) {
            EXPECT_FALSE(rg.BorderCellDecorated(x, y));
        }
    }
    EXPECT_FALSE(rg.BorderCellDecorated(-1, 0));
    EXPECT_FALSE(rg.BorderCellDecorated(0, h));
}

// Design rooms (no procedural obstacles) draw nothing after the walls, so the floor / wall results are the
// same as for a procedural room of the same size and seed -- the RNG stream is shared by construction.
TEST(RoomDetailTest, DesignRoomsDrawTheSameFloorAndWallRolls) {
    RoomGen::Options proc = Explicit(15, 15, 1, 1);
    RoomGen::Options design = Explicit(15, 15, 1, 1);
    design.proceduralObstacles = false;
    RoomGen a(321, kNoDoors, proc);
    RoomGen b(321, kNoDoors, design);
    for (int x = 0; x < 15; ++x) {
        for (int y = 0; y < 15; ++y) {
            EXPECT_EQ(a.FloorVariantAt(x, y), b.FloorVariantAt(x, y));
            EXPECT_EQ(a.BorderCellDecorated(x, y), b.BorderCellDecorated(x, y));
        }
    }
    EXPECT_LT(b.DrawCount(), a.DrawCount()); // the procedural room keeps drawing obstacles after the walls
}

// NOLINTEND(readability-magic-numbers)

