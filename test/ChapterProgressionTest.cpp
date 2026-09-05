#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "data/GameData.hpp"
#include "game/RunState.hpp"

// CHAPTER PROGRESSION (B2-P5): species and bosses are tagged with the AssetRipper
// biome folder they live in, and each chapter spawns from ITS biome's mob pool and
// boss roster (was: two fixed 5-slot rosters that mixed chapters). Ground truth =
// ExportedProject res/enemy/<biome>/*.prefab: ch1 -> 1-ice (bosses 09/10/13),
// ch2 -> 2-castle (01/02), ch3 -> 3-alien (05/06); root-level e_slime01 /
// e_battery01 are chapter-agnostic.
namespace {

const char *kResourceRoot = RESOURCE_DIR;

Game::GameData LoadGameData() {
    Game::GameData gd;
    gd.LoadAll(kResourceRoot);
    return gd;
}

bool Has(const std::vector<const Game::EnemyDef *> &pool, const std::string &id) {
    for (const Game::EnemyDef *d : pool) {
        if (d->id == id) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> Ids(const std::vector<const Game::EnemyDef *> &pool) {
    std::vector<std::string> out;
    for (const Game::EnemyDef *d : pool) {
        out.push_back(d->id);
    }
    return out;
}

TEST(ChapterProgressionTest, ChapterOfFloor) {
    EXPECT_EQ(Game::ChapterOf(0), 1);
    EXPECT_EQ(Game::ChapterOf(4), 1);
    EXPECT_EQ(Game::ChapterOf(5), 2);
    EXPECT_EQ(Game::ChapterOf(14), 3);
    EXPECT_EQ(Game::ChapterOf(15), 1); // wraps past the original's 16-floor cap
    EXPECT_STREQ(Game::ChapterBiome(1), "1-ice");
    EXPECT_STREQ(Game::ChapterBiome(4), "1-ice");
}

TEST(ChapterProgressionTest, SpeciesCarryBiomeAndChapter) {
    const Game::GameData gd = LoadGameData();
    const Game::EnemyDef *boar = gd.FindEnemy("e_boar01");
    const Game::EnemyDef *hammer = gd.FindEnemy("e_hammer");
    const Game::EnemyDef *slime = gd.FindEnemy("e_slime01");
    const Game::EnemyDef *boss09 = gd.FindEnemy("boss09");
    ASSERT_NE(boar, nullptr);
    ASSERT_NE(hammer, nullptr);
    ASSERT_NE(slime, nullptr);
    ASSERT_NE(boss09, nullptr);
    EXPECT_EQ(boar->biome, "1-forest");
    EXPECT_EQ(boar->chapter, 1);
    EXPECT_EQ(hammer->chapter, 2);
    EXPECT_EQ(slime->chapter, 0); // root folder = every chapter
    EXPECT_EQ(boss09->biome, "1-ice");
}

TEST(ChapterProgressionTest, MobPoolIsTheChaptersBiome) {
    const Game::GameData gd = LoadGameData();
    const auto ch1 = gd.ChapterMobPool(1);
    EXPECT_TRUE(Has(ch1, "e_yeti01"));   // 1-ice
    EXPECT_TRUE(Has(ch1, "e_slime01"));  // root: present in every chapter
    EXPECT_FALSE(Has(ch1, "e_hammer"));  // chapter-2 graveyard mob
    EXPECT_FALSE(Has(ch1, "e_boar01"));  // 1-forest is the OTHER ch1 variant
    EXPECT_FALSE(Has(ch1, "boss09"));    // bosses are not mobs
    EXPECT_FALSE(Has(ch1, "ex_yeti01")); // no elites in the base pool
    const auto ch2 = gd.ChapterMobPool(2);
    EXPECT_TRUE(Has(ch2, "e_knight01"));
    EXPECT_FALSE(Has(ch2, "e_yeti01"));
    const auto ch3 = gd.ChapterMobPool(3);
    EXPECT_TRUE(Has(ch3, "e_tentacle"));
    EXPECT_FALSE(Has(ch3, "e_tentacle_temp")); // summoned part, never a room spawn
}

TEST(ChapterProgressionTest, BossRosterIsTheChaptersBosses) {
    const Game::GameData gd = LoadGameData();
    EXPECT_EQ(Ids(gd.ChapterBossPool(1)),
              (std::vector<std::string>{"boss09", "boss10", "boss13"}));
    EXPECT_EQ(Ids(gd.ChapterBossPool(2)), (std::vector<std::string>{"boss01", "boss02"}));
    EXPECT_EQ(Ids(gd.ChapterBossPool(3)),
              (std::vector<std::string>{"boss05", "boss06"})); // not boss06_c
    for (const Game::EnemyDef *b : gd.ChapterBossPool(1)) {
        EXPECT_GT(b->maxHp, 0) << b->id;
        EXPECT_FALSE(b->enemyClass.empty()) << b->id;
    }
}

TEST(ChapterProgressionTest, PoolsAreDeterministicAndSorted) {
    const Game::GameData gd = LoadGameData();
    EXPECT_EQ(gd.ChapterMobPool(1), gd.ChapterMobPool(1));
    EXPECT_EQ(gd.ChapterBossPool(2), gd.ChapterBossPool(2));
    const auto ids = Ids(gd.ChapterMobPool(1));
    for (std::size_t i = 1; i < ids.size(); ++i) {
        EXPECT_LT(ids[i - 1], ids[i]);
    }
}

} // namespace
