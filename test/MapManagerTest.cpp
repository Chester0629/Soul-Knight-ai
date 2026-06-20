#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <vector>

#include "world/MapManager.hpp"
#include "world/RoomGen.hpp"

using Game::MapManager;
using Game::RoomCell;
using Game::RoomGen;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
MapManager Make(int seed, int mapLong, int prob = 25) {
    MapManager::Options opt;
    opt.mapLong = mapLong;
    opt.ranRoomProbability = prob;
    return MapManager(seed, opt);
}
} // namespace

TEST(MapManagerTest, SameSeedSameFloor) {
    const MapManager a = Make(12345, 8);
    const MapManager b = Make(12345, 8);
    ASSERT_EQ(a.Rooms().size(), b.Rooms().size());
    for (std::size_t i = 0; i < a.Rooms().size(); ++i) {
        EXPECT_EQ(a.Rooms()[i].gridX, b.Rooms()[i].gridX);
        EXPECT_EQ(a.Rooms()[i].gridY, b.Rooms()[i].gridY);
        EXPECT_EQ(a.Rooms()[i].type, b.Rooms()[i].type);
        EXPECT_EQ(a.Rooms()[i].entrance, b.Rooms()[i].entrance);
    }
}

TEST(MapManagerTest, DifferentSeedDifferentFloor) {
    const MapManager a = Make(1, 10);
    const MapManager b = Make(2, 10);
    bool differs = a.Rooms().size() != b.Rooms().size();
    for (std::size_t i = 0; !differs && i < a.Rooms().size(); ++i) {
        differs = a.Rooms()[i].gridX != b.Rooms()[i].gridX ||
                  a.Rooms()[i].gridY != b.Rooms()[i].gridY;
    }
    EXPECT_TRUE(differs);
}

TEST(MapManagerTest, PlacesRequestedRoomCount) {
    // With an auto grid (2*mapLong+1) the tree-walk always reaches mapLong.
    for (int n : {3, 6, 10, 15}) {
        const MapManager m = Make(777 + n, n);
        EXPECT_EQ(static_cast<int>(m.Rooms().size()), n) << "mapLong=" << n;
    }
}

TEST(MapManagerTest, StartRoomAtCentre) {
    const MapManager m = Make(42, 6);
    const int c = m.GridSize() / 2;
    EXPECT_EQ(m.StartIndex(), 0);
    EXPECT_EQ(m.Rooms()[0].gridX, c);
    EXPECT_EQ(m.Rooms()[0].gridY, c);
    EXPECT_EQ(m.Rooms()[0].type, 1);
    EXPECT_TRUE(m.HasRoom(c, c));
}

TEST(MapManagerTest, AllRoomsConnected) {
    for (int seed = 0; seed < 50; ++seed) {
        const MapManager m = Make(seed, 12);
        EXPECT_TRUE(m.Connected()) << "seed=" << seed;
    }
}

TEST(MapManagerTest, EntranceFlagsMatchAdjacency) {
    const MapManager m = Make(2024, 12);
    for (const RoomCell &r : m.Rooms()) {
        // Mapping is fixed by CreateAisle's carve edges: [0]=max-x(+x) [2]=min-x(-x)
        // [1]=min-y(-y, cell y=0) [3]=max-y(+y, cell y=h-1). So the y pair flags the
        // -y/+y neighbour respectively (the door must face the room it connects to).
        EXPECT_EQ(r.entrance[0], m.HasRoom(r.gridX + 1, r.gridY) ? 1 : 0); // +x
        EXPECT_EQ(r.entrance[1], m.HasRoom(r.gridX, r.gridY - 1) ? 1 : 0); // -y
        EXPECT_EQ(r.entrance[2], m.HasRoom(r.gridX - 1, r.gridY) ? 1 : 0); // -x
        EXPECT_EQ(r.entrance[3], m.HasRoom(r.gridX, r.gridY + 1) ? 1 : 0); // +y
        // Every non-start room must connect to at least one neighbour (tree).
        const int doors =
            r.entrance[0] + r.entrance[1] + r.entrance[2] + r.entrance[3];
        EXPECT_GE(doors, 1);
    }
}

TEST(MapManagerTest, NoRoomCellsOverlap) {
    const MapManager m = Make(99, 14);
    for (std::size_t i = 0; i < m.Rooms().size(); ++i) {
        for (std::size_t j = i + 1; j < m.Rooms().size(); ++j) {
            const bool same = m.Rooms()[i].gridX == m.Rooms()[j].gridX &&
                              m.Rooms()[i].gridY == m.Rooms()[j].gridY;
            EXPECT_FALSE(same) << "rooms " << i << " and " << j << " overlap";
        }
    }
}

TEST(MapManagerTest, AtMostOneSpecialAndOneBadass) {
    // High probability so specials are likely; still capped at one each.
    const MapManager m = Make(555, 20, 95);
    int special = 0;
    int badass = 0;
    for (const RoomCell &r : m.Rooms()) {
        if (r.type == 2) {
            ++special;
        }
        if (r.type == 3) {
            ++badass;
        }
    }
    EXPECT_LE(special, 1);
    EXPECT_LE(badass, 1);
}

// PRECONDITION test (superseded for end-to-end connectivity by
// FloorConnectivityTest). Two grid-adjacent rooms must each carve a WALKABLE door
// opening on the SAME band of their shared axis, so the corridors built in the
// inter-block margin line up. This guards the door-side bug (vertical seams became
// solid double-walls while each room's door faced the outer void) and the
// door-band alignment the corridor depends on. The door(11)/aisle(-2) cells are
// obstacle-proof (IsWallIntersect only stamps over all-floor footprints), so the
// check is deterministic; seed-swept so EVERY adjacency is verified, not one
// extrapolated pair.
//
// NOTE: this only proves both room EDGES have aligned openings -- it is NOT the
// reachability guarantee. Under the 41-cell block pitch ("A geometry") the rooms
// are no longer edge-to-edge; the player crosses a carved corridor between them.
// True room-A-interior -> room-B-interior reachability (BFS over the full walkable
// set, catching any 1-cell break in the corridor) is FloorConnectivityTest.
TEST(MapManagerTest, AdjacentRoomsHaveAlignedDoorOpenings) {
    constexpr int kCells = 15; // GameScene's fixed room footprint (kRoomCells).
    const auto walkable = [](int code) {
        return code == 0 || code == -2 || code == 11; // == !Room::IsSolidCell
    };
    const auto buildRoom = [&](int floorSeed, int idx,
                               const std::array<int, 4> &ent) {
        RoomGen::Options o;
        o.randomRoom = false; // GameScene tiles fixed-size rooms.
        o.roomWidth = kCells;
        o.roomHeight = kCells;
        o.wallLevel = 1;
        o.obstacleLevel = 1;
        return RoomGen(floorSeed + 1 + idx, ent, o); // GameScene's per-room seed.
    };

    for (int floorSeed : {1, 2, 3, 7, 12345}) {
        const MapManager m = Make(floorSeed, 7);
        const auto &rooms = m.Rooms();
        std::vector<RoomGen> grids;
        grids.reserve(rooms.size());
        for (std::size_t i = 0; i < rooms.size(); ++i) {
            grids.push_back(
                buildRoom(floorSeed, static_cast<int>(i), rooms[i].entrance));
        }
        for (std::size_t i = 0; i < rooms.size(); ++i) {
            for (std::size_t j = 0; j < rooms.size(); ++j) {
                if (i == j) {
                    continue;
                }
                const int dx = rooms[j].gridX - rooms[i].gridX;
                const int dy = rooms[j].gridY - rooms[i].gridY;
                const RoomGen &a = grids[i];
                const RoomGen &b = grids[j];
                if (dx == 1 && dy == 0) { // b is east of a: a right edge vs b left
                    bool open = false;
                    for (int y = 0; y < kCells && !open; ++y) {
                        open =
                            walkable(a.At(kCells - 1, y)) && walkable(b.At(0, y));
                    }
                    EXPECT_TRUE(open) << "E/W seam blocked: seed=" << floorSeed
                                      << " room " << i << "->" << j;
                } else if (dx == 0 && dy == 1) { // b is +y of a: a top vs b bottom
                    bool open = false;
                    for (int x = 0; x < kCells && !open; ++x) {
                        open =
                            walkable(a.At(x, kCells - 1)) && walkable(b.At(x, 0));
                    }
                    EXPECT_TRUE(open) << "N/S seam blocked: seed=" << floorSeed
                                      << " room " << i << "->" << j;
                }
            }
        }
    }
}

// NOLINTEND(readability-magic-numbers)
