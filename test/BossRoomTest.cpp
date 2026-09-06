#include <gtest/gtest.h>

#include <string>

#include "data/GameData.hpp"

// MAP M3: boss room layouts recovered from rgprefab/rooms/r<C>_boss<N>.prefab.
namespace {

const char *kResourceRoot = RESOURCE_DIR;

TEST(BossRoomTest, FourteenLayoutsWithRealObjects) {
    Game::GameData gd;
    gd.LoadAll(kResourceRoot);
    ASSERT_EQ(gd.BossRooms().size(), 14u);
    const Game::BossRoomDef *b1 = gd.FindBossRoom("r1_boss1");
    ASSERT_NE(b1, nullptr);
    EXPECT_EQ(b1->width, 21);
    EXPECT_EQ(b1->height, 21);
    EXPECT_EQ(b1->objects.size(), 44u); // 32 box02 + 8 wall_tree1 + 2 wall503 + 2 wall504
    int boxes = 0;
    int trees = 0;
    for (const auto &o : b1->objects) {
        boxes += o.prefab == "box02";
        trees += o.prefab == "wall_tree1";
        EXPECT_GE(o.x, 0);
        EXPECT_LT(o.x, 21);
        EXPECT_GE(o.y, 0);
        EXPECT_LT(o.y, 21);
    }
    EXPECT_EQ(boxes, 32);
    EXPECT_EQ(trees, 8);
    // r3_boss1 is an empty arena in the prefab.
    const Game::BossRoomDef *e = gd.FindBossRoom("r3_boss1");
    ASSERT_NE(e, nullptr);
    EXPECT_TRUE(e->objects.empty());
}

TEST(BossRoomTest, PrefabSkinsResolveCratesAndWalls) {
    Game::GameData gd;
    gd.LoadAll(kResourceRoot);
    const Game::PrefabSkin *box = gd.FindPrefabSkin("box02");
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->kind, "box");
    EXPECT_EQ(box->sprite, "objects2_31");
    EXPECT_EQ(box->alt, "objects2_30");
    const Game::PrefabSkin *tree = gd.FindPrefabSkin("wall_tree1");
    ASSERT_NE(tree, nullptr);
    EXPECT_EQ(tree->kind, "wall");
    EXPECT_EQ(tree->sprite, "objects2_68");
    EXPECT_FLOAT_EQ(tree->altDy, 0.5F);
    EXPECT_EQ(gd.FindPrefabSkin("no_such_prefab"), nullptr);
}

TEST(BossChestTest, BossChestsCarryTierAndSignatureWeapon) {
    Game::GameData gd;
    gd.LoadAll(kResourceRoot);
    const Game::BossChestDef *c1 = gd.FindBossChest("boss01");
    ASSERT_NE(c1, nullptr);
    EXPECT_EQ(c1->id, "chest_boss01");
    EXPECT_EQ(c1->level, 2);
    ASSERT_EQ(c1->weapons.size(), 1u);
    EXPECT_EQ(c1->weapons[0], "weapon_077");
    EXPECT_NE(gd.FindWeapon("weapon_077"), nullptr);
    const Game::BossChestDef *c10 = gd.FindBossChest("boss10");
    ASSERT_NE(c10, nullptr);
    EXPECT_EQ(c10->weapons, std::vector<std::string>{"weapon_127"});
    EXPECT_EQ(gd.FindBossChest("boss05"), nullptr); // no chest_boss05 prefab in 1.7.10
}

} // namespace
