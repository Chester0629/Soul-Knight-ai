#include <gtest/gtest.h>

#include <string>

#include "data/GameData.hpp"

// WEAPON RE-KEY (B2-P4): weapons.json is one entry per PREFAB (194) carrying real
// per-item stats + weapon_class (gun-brain dispatch key) + the icon and bullet
// sprites resolved from the AssetRipper project, replacing the 46 class
// templates. Ground truth: weapons.csv (the Gun001 family is 54 prefabs;
// weapon_000 is the atk-3 / consume-0 starter; the old "Gun001" template had
// silently been weapon_043, atk 15 / consume 5). weapon_001's icon and bullet
// PNGs were resolved by hand through the prefab SpriteRenderer guid chain.
namespace {

const char *kResourceRoot = RESOURCE_DIR;

Game::GameData LoadGameData() {
    Game::GameData gd;
    gd.LoadAll(kResourceRoot);
    return gd;
}

TEST(WeaponStatsTest, AllPrefabsLoadWithClasses) {
    const Game::GameData gd = LoadGameData();
    EXPECT_EQ(gd.Weapons().size(), 194u);
    int gun001 = 0;
    for (const Game::WeaponDef &w : gd.Weapons()) {
        if (w.weaponClass == "Gun001") {
            ++gun001;
        }
    }
    EXPECT_EQ(gun001, 54);
}

TEST(WeaponStatsTest, PerPrefabStatsAreFaithful) {
    const Game::GameData gd = LoadGameData();
    const Game::WeaponDef *strong = gd.FindWeapon("weapon_043");
    ASSERT_NE(strong, nullptr);
    EXPECT_EQ(strong->weaponClass, "Gun001");
    EXPECT_EQ(strong->atk, 15);
    EXPECT_EQ(strong->consume, 5);
    const Game::WeaponDef *starter = gd.FindWeapon("weapon_000");
    ASSERT_NE(starter, nullptr);
    EXPECT_EQ(starter->weaponClass, "Gun001");
    EXPECT_EQ(starter->atk, 3);
    EXPECT_EQ(starter->consume, 0);
}

TEST(WeaponStatsTest, ClassIdResolvesToFirstPrefabOfThatClass) {
    const Game::GameData gd = LoadGameData();
    const Game::WeaponDef *w = gd.FindWeapon("Gun001");
    ASSERT_NE(w, nullptr);
    EXPECT_EQ(w->id, "weapon_000"); // the faithful floor-0 starter
    EXPECT_EQ(w->weaponClass, "Gun001");
    EXPECT_EQ(gd.FindWeaponByClass("Gun001"), w);
}

TEST(WeaponStatsTest, DropIdsResolveExactly) {
    const Game::GameData gd = LoadGameData();
    const Game::WeaponDef *w = gd.ResolveDropWeapon("weapon_043");
    ASSERT_NE(w, nullptr);
    EXPECT_EQ(w->id, "weapon_043"); // was 43 % 46 -> an arbitrary class
}

TEST(WeaponStatsTest, IconAndBulletSpritesResolve) {
    const Game::GameData gd = LoadGameData();
    const Game::WeaponDef *w = gd.FindWeapon("weapon_001");
    ASSERT_NE(w, nullptr);
    EXPECT_EQ(w->icon, "weapons_107.png");
    EXPECT_EQ(w->bulletSprite, "bullet_5.png");
    int icons = 0;
    for (const Game::WeaponDef &d : gd.Weapons()) {
        icons += d.icon.empty() ? 0 : 1;
    }
    EXPECT_EQ(icons, 194); // every weapon prefab has a SpriteRenderer icon
}

TEST(WeaponStatsTest, UnknownIsNull) {
    const Game::GameData gd = LoadGameData();
    EXPECT_EQ(gd.FindWeapon("NoSuchGun"), nullptr);
    EXPECT_EQ(gd.FindWeaponByClass("NoSuchClass"), nullptr);
}

} // namespace
