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

TEST(GameDataTest, LoadsFloor1RoomLayouts) {
    GameData gd;
    gd.LoadAll(kResourceRoot);
    const auto &rl = gd.RoomLayouts();
    EXPECT_EQ(rl.size(), 108u); // the 108 floor-1 (r1_*) design rooms
    for (const auto &r : rl) {
        EXPECT_FALSE(r.id.empty());
        // Floor-1 sizes are only 15 or 21 per axis (no 25); all rooms are type 1.
        EXPECT_TRUE(r.width == 15 || r.width == 21) << r.id << " w=" << r.width;
        EXPECT_TRUE(r.height == 15 || r.height == 21) << r.id << " h=" << r.height;
        EXPECT_EQ(r.type, 1) << r.id;
    }
}

TEST(GameDataTest, LoadsDesignRoomInteriors) {
    GameData gd;
    gd.LoadAll(kResourceRoot);
    EXPECT_EQ(gd.DesignRooms().size(), 108u); // one interior per floor-1 design room
    const auto *r1_1 = gd.FindDesignRoom("r1_1");
    ASSERT_NE(r1_1, nullptr);
    EXPECT_EQ(r1_1->width, 15);
    EXPECT_EQ(r1_1->height, 15);
    // r1_1's interior is the 7x7 wall block: 49 obstacles, all obj_index 0 (wall),
    // all inside the room-local grid.
    EXPECT_EQ(r1_1->obstacles.size(), 49u);
    for (const auto &o : r1_1->obstacles) {
        EXPECT_EQ(o.objIndex, 0);
        EXPECT_GE(o.x, 0);
        EXPECT_LT(o.x, r1_1->width);
        EXPECT_GE(o.y, 0);
        EXPECT_LT(o.y, r1_1->height);
    }
    EXPECT_EQ(gd.FindDesignRoom("nope"), nullptr);
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
    // c01 starting stats 6/5/180 (the real values; the JSON previously carried 3/130
    // placeholders -- per its own "_note: per-character stat overrides pending").
    EXPECT_EQ(p.maxHp, 6);
    EXPECT_EQ(p.maxArmor, 5);
    EXPECT_EQ(p.armor, 5);
    EXPECT_EQ(p.maxEnergy, 180);
    EXPECT_EQ(p.energy, 180);
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
