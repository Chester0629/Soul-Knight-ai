#include <gtest/gtest.h>

#include "world/MapManager.hpp"

using Game::MapManager;
using Game::RoomCell;

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
        EXPECT_EQ(r.entrance[0], m.HasRoom(r.gridX + 1, r.gridY) ? 1 : 0); // E
        EXPECT_EQ(r.entrance[1], m.HasRoom(r.gridX, r.gridY + 1) ? 1 : 0); // N
        EXPECT_EQ(r.entrance[2], m.HasRoom(r.gridX - 1, r.gridY) ? 1 : 0); // W
        EXPECT_EQ(r.entrance[3], m.HasRoom(r.gridX, r.gridY - 1) ? 1 : 0); // S
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

// NOLINTEND(readability-magic-numbers)
