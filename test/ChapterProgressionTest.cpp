#include <gtest/gtest.h>

#include <string>
#include <typeinfo>
#include <vector>

#include "data/GameData.hpp"
#include "sim/BossBrainAdapters.hpp"
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

// P2.15: the boss pool follows the floor's REAL biome (levels.json), not the fixed
// per-chapter one. Expected ids = enemies.json biome tags (FAITHFUL @ res/enemy/<biome>/).
// boss12_1 / boss12_2 are the two halves of one composite boss (boss12_parent) and are
// deliberately not poolable, so 3-volcano rolls only boss11.
TEST(ChapterProgressionTest, BiomeBossPoolPerRealBiome) {
    const Game::GameData gd = LoadGameData();
    using V = std::vector<std::string>;
    EXPECT_EQ(Ids(gd.BiomeBossPool("1-forest")), (V{"boss07", "boss08"}));
    EXPECT_EQ(Ids(gd.BiomeBossPool("1-ice")), (V{"boss09", "boss10", "boss13"}));
    EXPECT_EQ(Ids(gd.BiomeBossPool("2-castle")), (V{"boss01", "boss02"}));
    EXPECT_EQ(Ids(gd.BiomeBossPool("2-graveyard")), (V{"boss03", "boss04"}));
    EXPECT_EQ(Ids(gd.BiomeBossPool("3-alien")), (V{"boss05", "boss06"}));
    EXPECT_EQ(Ids(gd.BiomeBossPool("3-volcano")), (V{"boss11"}));
    EXPECT_TRUE(gd.BiomeBossPool("no-such-biome").empty());
    for (int c = 1; c <= 6; ++c) {
        EXPECT_EQ(gd.ChapterBossPool(c), gd.BiomeBossPool(Game::ChapterBiome(c))) << c;
    }
}

TEST(ChapterProgressionTest, EveryLevelBiomeHasUsableBosses) {
    const Game::GameData gd = LoadGameData();
    int checked = 0;
    for (char variant = 'A'; variant <= 'F'; ++variant) {
        for (int stage = 1; stage <= 5; ++stage) {
            const std::string id = std::string(1, variant) + std::to_string(stage);
            const Game::LevelDef *lv = gd.FindLevel(id);
            ASSERT_NE(lv, nullptr) << id;
            const auto pool = gd.BiomeBossPool(lv->biome);
            EXPECT_FALSE(pool.empty()) << id << " biome " << lv->biome;
            ++checked;
        }
    }
    EXPECT_EQ(checked, 30);
    for (const char *biome : {"1-forest", "1-ice", "2-castle", "2-graveyard", "3-alien",
                              "3-volcano"}) {
        for (const Game::EnemyDef *b : gd.BiomeBossPool(biome)) {
            EXPECT_GT(b->maxHp, 0) << b->id;
            EXPECT_FALSE(b->enemyClass.empty()) << b->id;
            EXPECT_FALSE(gd.SpriteFrames(b->id, kResourceRoot).empty()) << b->id;
            // The class must be a real registered brain (not the BossAI01 fallback).
            const auto brain = Game::Sim::MakeBossBrain(b->enemyClass, 2.0F);
            ASSERT_NE(brain, nullptr) << b->id;
            if (b->enemyClass != "BossAI01") {
                EXPECT_NE(typeid(*brain), typeid(*Game::Sim::MakeBossBrain("BossAI01", 2.0F)))
                    << b->id << " falls back to BossAI01";
            }
        }
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
