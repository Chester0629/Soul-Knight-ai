#include <gtest/gtest.h>

#include <string>

#include "data/GameData.hpp"
#include "game/RunController.hpp"

// M3c SHOP: the pot table (items.csv / res/item pots), the vending machines
// (rgprefab/room/event) and the run's coin spending.
namespace {

const char *kResourceRoot = RESOURCE_DIR;

TEST(ShopTest, PotsCarryRealRestoreAndPrice) {
    Game::GameData gd;
    gd.LoadAll(kResourceRoot);
    const Game::PotDef *e = gd.FindPot("energy_pot");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->energy, 80);
    EXPECT_EQ(e->hp, 0);
    EXPECT_EQ(e->value, 20);
    EXPECT_EQ(e->sprite, "objects_common_20");
    const Game::PotDef *h = gd.FindPot("health_pot_big");
    ASSERT_NE(h, nullptr);
    EXPECT_EQ(h->hp, 4);
    EXPECT_EQ(h->value, 40);
    const Game::PotDef *r = gd.FindPot("restore_pot_big");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->hp, 2);
    EXPECT_EQ(r->energy, 80);
}

TEST(ShopTest, MachinesFromThePrefabs) {
    Game::GameData gd;
    gd.LoadAll(kResourceRoot);
    const Game::MachineDef *mi = gd.FindMachine("vending_machine_i");
    ASSERT_NE(mi, nullptr);
    EXPECT_EQ(mi->price, 10);
    EXPECT_EQ(mi->sprite, "objects3_10");
    EXPECT_EQ(mi->items, (std::vector<std::string>{"energy_pot_big", "energy_pot", "health_pot_big", "health_pot"}));
    const Game::MachineDef *mw = gd.FindMachine("vending_machine_w");
    ASSERT_NE(mw, nullptr);
    EXPECT_EQ(mw->price, 12);
    EXPECT_TRUE(mw->items.empty());
    const Game::MachineDef *wd = gd.FindMachine("water_dispenser");
    ASSERT_NE(wd, nullptr);
    EXPECT_EQ(wd->price, 0);
}

TEST(ShopTest, SpendCoinsOnlyWhenAffordable) {
    Game::RunController run(5);
    run.AddCoins(15);
    EXPECT_FALSE(run.SpendCoins(20));
    EXPECT_EQ(run.State().stats.coins, 15);
    EXPECT_TRUE(run.SpendCoins(10));
    EXPECT_EQ(run.State().stats.coins, 5);
    EXPECT_TRUE(run.SpendCoins(0));
}

TEST(StatueTest, WorshipIndexPersistsAcrossFloorsAndResetsPerRun) {
    Game::RunController run(9);
    EXPECT_EQ(run.State().statueIndex, 0);
    run.SetStatue(6);
    run.AdvanceFloor(Game::PlayerContinuation{});
    EXPECT_EQ(run.State().statueIndex, 6);
    run.ResetRunState();
    EXPECT_EQ(run.State().statueIndex, 0);
}

// P1.12 continuation: shop.json "statues" (statue_0N.prefab TalkStatue) -- the
// 1.07 source ships 7 statue prefabs (statue_index 1..7), not the port's old
// hardcoded 8-slot table.
TEST(StatueTest, StatuesFromThePrefabs) {
    Game::GameData gd;
    gd.LoadAll(kResourceRoot);
    EXPECT_EQ(gd.Statues().size(), 7U);
    const Game::StatueDef *s2 = gd.FindStatue(2);
    ASSERT_NE(s2, nullptr);
    EXPECT_EQ(s2->id, "statue_02");
    EXPECT_EQ(s2->cost, 15);
    EXPECT_EQ(s2->statueIndex, 2);
    EXPECT_EQ(s2->bodySprite, "objects3_3");
    EXPECT_EQ(s2->baseSprite, "objects3_6");
    EXPECT_EQ(s2->lightSprite, "objects3_7");
    EXPECT_FLOAT_EQ(s2->bodyDy, 1.1F);
    EXPECT_NEAR(s2->lightDy, 0.69375F, 1e-4F);
    EXPECT_EQ(gd.FindStatue(99), nullptr);
}

} // namespace
