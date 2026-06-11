#include <gtest/gtest.h>

#include "data/GameData.hpp"

using Game::GameData;

namespace {
// RESOURCE_DIR is injected by CMake (points at the repo's Resources/ folder).
const char *kResourceRoot = RESOURCE_DIR;
} // namespace

// NOLINTBEGIN(readability-magic-numbers)

TEST(GameDataTest, LoadsAllTablesWithExpectedCounts) {
    GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    EXPECT_EQ(gd.Weapons().size(), 46u);
    EXPECT_EQ(gd.Bullets().size(), 25u);
    EXPECT_EQ(gd.Enemies().size(), 16u);
    EXPECT_EQ(gd.EnemyGuns().size(), 10u);
    EXPECT_EQ(gd.Buffs().size(), 12u);
}

TEST(GameDataTest, Gun001ScalarStats) {
    GameData gd;
    gd.LoadAll(kResourceRoot);
    const auto *w = gd.FindWeapon("Gun001");
    ASSERT_NE(w, nullptr);
    EXPECT_EQ(w->atk, 15);
    EXPECT_FLOAT_EQ(w->bulletSpeed, 32.0F);
    EXPECT_FLOAT_EQ(w->repel, 2.0F);
    EXPECT_EQ(w->deviation, 5);
    EXPECT_EQ(w->consume, 5);
}

TEST(GameDataTest, Bullet01ScalarStats) {
    GameData gd;
    gd.LoadAll(kResourceRoot);
    const auto *b = gd.FindBullet("Bullet01");
    ASSERT_NE(b, nullptr);
    EXPECT_FLOAT_EQ(b->speed, 10.0F);
    EXPECT_FLOAT_EQ(b->destroyTime, 5.0F);
    EXPECT_EQ(b->camp, 0);
}

TEST(GameDataTest, EnemyAI01ScalarStats) {
    GameData gd;
    gd.LoadAll(kResourceRoot);
    const auto *e = gd.FindEnemy("EnemyAI01");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->aiLevel, 3);
    EXPECT_FLOAT_EQ(e->shootCd, 2.0F);
    EXPECT_FLOAT_EQ(e->friction, 0.6F);
}

TEST(GameDataTest, EnemyGun001ScalarStats) {
    GameData gd;
    gd.LoadAll(kResourceRoot);
    const auto *g = gd.FindEnemyGun("EGun001");
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->atk, 3);
    EXPECT_FLOAT_EQ(g->bulletSpeed, 10.0F);
    EXPECT_EQ(g->deviation, 5);
}

TEST(GameDataTest, PlayerTemplateStats) {
    GameData gd;
    gd.LoadAll(kResourceRoot);
    const auto &p = gd.PlayerTemplate();
    EXPECT_EQ(p.maxHp, 6);
    EXPECT_EQ(p.maxArmor, 3);
    EXPECT_EQ(p.maxEnergy, 130);
    EXPECT_EQ(p.energy, 130);
    EXPECT_EQ(p.atk, 5);
    EXPECT_FLOAT_EQ(p.speed, 6.5F);
    EXPECT_FLOAT_EQ(p.skillCd, 2.5F);
}

TEST(GameDataTest, BuffArmorLoaded) {
    GameData gd;
    gd.LoadAll(kResourceRoot);
    const auto *b = gd.FindBuff("BuffArmor");
    ASSERT_NE(b, nullptr);
    EXPECT_FLOAT_EQ(b->buffTime, 6.0F);
}

TEST(GameDataTest, MissingIdReturnsNull) {
    GameData gd;
    gd.LoadAll(kResourceRoot);
    EXPECT_EQ(gd.FindWeapon("NoSuchGun"), nullptr);
    EXPECT_EQ(gd.FindEnemy("NoSuchEnemy"), nullptr);
    EXPECT_EQ(gd.FindBullet("NoSuchBullet"), nullptr);
}

TEST(GameDataTest, MissingResourceRootFailsGracefully) {
    GameData gd;
    // Non-existent root: DataStore logs + returns empty objects; LoadAll reports
    // failure but must not throw.
    EXPECT_FALSE(gd.LoadAll("does_not_exist_root"));
    EXPECT_EQ(gd.Weapons().size(), 0u);
}

// NOLINTEND(readability-magic-numbers)
