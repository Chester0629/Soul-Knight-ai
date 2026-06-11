#include <gtest/gtest.h>

#include <string>

#include "data/GameData.hpp"
#include "data/LootTable.hpp"
#include "data/RGRandom.hpp"

using Game::GameData;
using Game::LootTable;
using Game::RGRandom;
using Game::WeaponDef;

// NOLINTBEGIN(readability-magic-numbers)

// The loot loop end-to-end: a deterministic LootTable roll resolves to a real,
// deterministic WeaponDef (the chest -> pickup -> equip chain GameScene runs).
TEST(LootIntegrationTest, RollResolvesToRealWeaponDeterministically) {
    GameData data;
    data.LoadAll(RESOURCE_DIR);
    LootTable loot;
    loot.LoadAll(RESOURCE_DIR);
    if (data.Weapons().empty() || loot.TierCount() == 0) {
        GTEST_SKIP() << "data tables not available";
    }

    RGRandom a;
    RGRandom b;
    a.SetRandomSeed(123);
    b.SetRandomSeed(123);
    for (int i = 0; i < 32; ++i) {
        const std::string da = loot.Roll(0, a);
        const std::string db = loot.Roll(0, b);
        EXPECT_EQ(da, db) << "roll " << i << " not deterministic";
        const WeaponDef *w = data.ResolveDropWeapon(da);
        ASSERT_NE(w, nullptr);
        EXPECT_FALSE(w->id.empty());
    }
}

// The weapon_NNN trailing-number resolver (stand-in until the #3 map exists).
TEST(LootIntegrationTest, ResolveDropWeaponParsesTrailingNumber) {
    GameData data;
    data.LoadAll(RESOURCE_DIR);
    if (data.Weapons().empty()) {
        GTEST_SKIP() << "weapons table not available";
    }
    const WeaponDef *w = data.ResolveDropWeapon("weapon_003");
    ASSERT_NE(w, nullptr);
    const std::size_t expected = 3 % data.Weapons().size();
    EXPECT_EQ(w, &data.Weapons()[expected]);
}

// An exact Gun* id still resolves directly (FindWeapon path).
TEST(LootIntegrationTest, ResolveDropWeaponExactIdMatch) {
    GameData data;
    data.LoadAll(RESOURCE_DIR);
    if (data.Weapons().empty()) {
        GTEST_SKIP() << "weapons table not available";
    }
    const std::string id = data.Weapons()[0].id;
    const WeaponDef *w = data.ResolveDropWeapon(id);
    ASSERT_NE(w, nullptr);
    EXPECT_EQ(w->id, id);
}

// NOLINTEND(readability-magic-numbers)
