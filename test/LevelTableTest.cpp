#include <gtest/gtest.h>

#include <string>

#include "data/GameData.hpp"
#include "data/RGRandom.hpp"
#include "game/EnemyMaker.hpp"
#include "game/RunState.hpp"

// MAP M1: the level table recovered from res/map_manager/map_<V><N>.prefab
// (MapManager fields), the per-run biome variant, and the EnemyMaker budget.
namespace {

const char *kResourceRoot = RESOURCE_DIR;

TEST(LevelTableTest, ThirtyOneLevelsWithRealPools) {
    Game::GameData gd;
    gd.LoadAll(kResourceRoot);
    ASSERT_EQ(gd.Levels().size(), 31u);
    const Game::LevelDef *b1 = gd.FindLevel("B1");
    ASSERT_NE(b1, nullptr);
    EXPECT_EQ(b1->level, "1-1");
    EXPECT_EQ(b1->chapter, 1);
    EXPECT_EQ(b1->stage, 1);
    EXPECT_EQ(b1->biome, "1-ice");
    EXPECT_EQ(b1->chestLevel, 1);
    EXPECT_EQ(b1->mapLong, 3);
    ASSERT_EQ(b1->floors.size(), 6u);
    EXPECT_EQ(b1->floors[0], "f601");
    ASSERT_EQ(b1->walls.size(), 2u);
    EXPECT_EQ(b1->walls[1].top, "w601");
    EXPECT_EQ(b1->walls[1].face, "w602");
    ASSERT_EQ(b1->obstacles.size(), 12u);
    EXPECT_EQ(b1->obstacles[1].prefab, "box04");
    EXPECT_EQ(b1->obstacles[11].prefab, "brazier01");
    // enemy_list duplicates are the weights: yeti01 appears twice on 1-1.
    ASSERT_EQ(b1->enemies.size(), 5u);
    EXPECT_EQ(b1->enemies[3], "e_yeti01");
    EXPECT_EQ(b1->enemies[4], "e_yeti01");
    EXPECT_EQ(b1->exEnemies, std::vector<std::string>{"ex_miner03"});
    EXPECT_EQ(b1->rooms.type2, std::vector<std::string>{"r_transgete"});
    // Every pool species is a real enemies.json entry.
    for (const std::string &id : b1->enemies) {
        EXPECT_NE(gd.FindEnemy(id), nullptr) << id;
    }
}

TEST(LevelTableTest, BossStageListsBossRoomsAndChapterTwoIsCastle) {
    Game::GameData gd;
    gd.LoadAll(kResourceRoot);
    const Game::LevelDef *b5 = gd.FindLevel("B5");
    ASSERT_NE(b5, nullptr);
    EXPECT_EQ(b5->mapLong, 4);
    EXPECT_EQ(b5->rooms.type2, (std::vector<std::string>{"r1_boss3", "r1_boss4", "r1_boss6"}));
    const Game::LevelDef *c1 = gd.FindLevel("C1");
    ASSERT_NE(c1, nullptr);
    EXPECT_EQ(c1->chapter, 2);
    EXPECT_EQ(c1->biome, "2-castle");
    EXPECT_EQ(c1->floors[0], "f201");
    EXPECT_EQ(c1->chestLevel, 3);
    const Game::LevelDef *g1 = gd.FindLevel("G1");
    ASSERT_NE(g1, nullptr);
    EXPECT_EQ(g1->level, "3-6");
    EXPECT_TRUE(g1->enemies.empty());
}

TEST(LevelTableTest, RunPicksOneVariantPerChapterDeterministically) {
    // Chapter 1 is A or B, chapter 2 C or D, chapter 3 E or F; stable per seed.
    for (int seed : {1, 7, 20240607, 99999}) {
        for (int ch = 1; ch <= 3; ++ch) {
            const char v = Game::BiomeVariantFor(seed, ch);
            EXPECT_EQ(v, Game::BiomeVariantFor(seed, ch));
            EXPECT_TRUE(v == 'A' + 2 * (ch - 1) || v == 'B' + 2 * (ch - 1)) << v;
        }
    }
    const std::string id0 = Game::LevelIdFor(20240607, 0);
    EXPECT_TRUE(id0 == "A1" || id0 == "B1") << id0;
    const std::string id4 = Game::LevelIdFor(20240607, 4);
    EXPECT_EQ(id4[0], id0[0]); // same variant across the chapter
    EXPECT_EQ(id4[1], '5');
    const std::string id7 = Game::LevelIdFor(20240607, 7);
    EXPECT_TRUE(id7 == "C3" || id7 == "D3") << id7;
    // Both variants are reachable across seeds.
    bool sawA = false;
    bool sawB = false;
    for (int s = 0; s < 64; ++s) {
        const char v = Game::BiomeVariantFor(s, 1);
        sawA = sawA || v == 'A';
        sawB = sawB || v == 'B';
    }
    EXPECT_TRUE(sawA && sawB);
}

TEST(EnemyMakerTest, BudgetTableAndBigRoomBump) {
    EXPECT_EQ(Game::EnemyPointRange(1), (std::pair<int, int>{8, 15}));
    EXPECT_EQ(Game::EnemyPointRange(5), (std::pair<int, int>{44, 52}));
    EXPECT_EQ(Game::EnemyPointRange(6), (std::pair<int, int>{60, 60}));
    EXPECT_EQ(Game::EnemyPointRange(0), (std::pair<int, int>{0, 0}));
    // T-05 (INFERRED): +1 per passed size test, both dimensions; level 0 never bumps.
    EXPECT_EQ(Game::EnemyMakerLevel(1, 15, 15), 1);
    EXPECT_EQ(Game::EnemyMakerLevel(1, 17, 15), 2);
    EXPECT_EQ(Game::EnemyMakerLevel(1, 23, 15), 3);
    EXPECT_EQ(Game::EnemyMakerLevel(1, 21, 21), 3);
    EXPECT_EQ(Game::EnemyMakerLevel(0, 21, 21), 0);
    Game::RGRandom rng;
    rng.SetRandomSeed(5);
    for (int i = 0; i < 50; ++i) {
        const int p = Game::RollEnemyPoints(1, rng);
        EXPECT_GE(p, 8);
        EXPECT_LT(p, 15);
    }
}

TEST(EnemyMakerTest, SpendsBudgetByConsume) {
    Game::RGRandom rng;
    rng.SetRandomSeed(11);
    const std::vector<std::string> pool{"cheap", "cheap", "pricey"};
    const auto cost = [](const std::string &id) { return id == "pricey" ? 3 : 1; };
    const auto picks = Game::PlanRoomSpawns(pool, cost, 10, rng);
    int spent = 0;
    for (const auto &p : picks) {
        spent += cost(p);
    }
    EXPECT_GE(spent, 10);
    EXPECT_LT(spent, 13); // overshoots by at most one pick
    EXPECT_TRUE(Game::PlanRoomSpawns({}, cost, 10, rng).empty());
    EXPECT_TRUE(Game::PlanRoomSpawns(pool, cost, 0, rng).empty());
    EXPECT_LE(Game::PlanRoomSpawns(pool, cost, 9999, rng, 12).size(), 12u);
}

} // namespace
