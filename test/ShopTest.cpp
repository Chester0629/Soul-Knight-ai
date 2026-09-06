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

} // namespace
