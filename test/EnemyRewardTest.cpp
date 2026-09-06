#include <gtest/gtest.h>

#include <map>

#include "data/GameData.hpp"
#include "data/RGRandom.hpp"
#include "game/CoinMagnet.hpp"
#include "game/EnemyReward.hpp"
#include "game/RunController.hpp"

// B3 ECONOMY: enemy death rewards (RGEController.GetReward over the real
// enemies.csv reward_rate / reward_value[4]), the coin / energy values from the
// res/register prefabs, the RGCoin scatter -> magnet motion, and the run's
// coin counter (RGGameProcess.coin_value).
namespace {

const char *kResourceRoot = RESOURCE_DIR;

int Total(const std::vector<Game::RewardDrop> &d) {
    int t = 0;
    for (const auto &x : d) {
        t += x.value;
    }
    return t;
}

TEST(EnemyRewardTest, PrefabValuesAreFaithful) {
    EXPECT_EQ(Game::RewardValue(Game::RewardKind::Gold), 5);   // coin_0.prefab
    EXPECT_EQ(Game::RewardValue(Game::RewardKind::Silver), 3); // coin_1.prefab
    EXPECT_EQ(Game::RewardValue(Game::RewardKind::Copper), 1); // coin_2.prefab
    EXPECT_EQ(Game::RewardValue(Game::RewardKind::Energy), 8); // energy.prefab
}

TEST(EnemyRewardTest, RealTableCarriesRewardValues) {
    Game::GameData gd;
    gd.LoadAll(kResourceRoot);
    const Game::EnemyDef *slime = gd.FindEnemy("e_slime01");
    const Game::EnemyDef *boss = gd.FindEnemy("boss01");
    ASSERT_NE(slime, nullptr);
    ASSERT_NE(boss, nullptr);
    EXPECT_EQ(slime->rewardRate, 15);
    EXPECT_EQ(slime->rewardValue[2], 1); // one copper
    EXPECT_EQ(slime->rewardValue[3], 1); // one energy
    EXPECT_EQ(boss->rewardRate, 100);
    EXPECT_EQ(boss->rewardValue[0], 3);
    EXPECT_EQ(boss->rewardValue[1], 3);
    EXPECT_EQ(boss->rewardValue[3], 10);
}

TEST(EnemyRewardTest, BossAlwaysPaysGoldSilverEnergy) {
    Game::EnemyDef boss;
    boss.rewardRate = 100;
    boss.rewardValue[0] = 3;
    boss.rewardValue[1] = 3;
    boss.rewardValue[2] = 0;
    boss.rewardValue[3] = 10;
    Game::RGRandom rng;
    rng.SetRandomSeed(4242);
    const auto drops = Game::RollEnemyReward(boss, rng);
    ASSERT_EQ(drops.size(), 16u);
    std::map<Game::RewardKind, int> n;
    for (const auto &d : drops) {
        ++n[d.kind];
    }
    EXPECT_EQ(n[Game::RewardKind::Gold], 3);
    EXPECT_EQ(n[Game::RewardKind::Silver], 3);
    EXPECT_EQ(n[Game::RewardKind::Copper], 0);
    EXPECT_EQ(n[Game::RewardKind::Energy], 10);
    EXPECT_EQ(Total(drops), 3 * 5 + 3 * 3 + 10 * 8);
}

TEST(EnemyRewardTest, RateGatesTheDropAndDrawsOnce) {
    Game::EnemyDef never;
    never.rewardRate = 0;
    never.rewardValue[2] = 1;
    Game::EnemyDef always = never;
    always.rewardRate = 100;

    Game::RGRandom a;
    a.SetRandomSeed(7);
    Game::RGRandom b;
    b.SetRandomSeed(7);
    EXPECT_TRUE(Game::RollEnemyReward(never, a).empty());
    EXPECT_EQ(Game::RollEnemyReward(always, b).size(), 1u);
    // Both consumed exactly one draw: the streams stay in lockstep.
    EXPECT_EQ(a.Range(0, 1000000), b.Range(0, 1000000));

    // A 15% mob over many rolls lands near 15% (deterministic seed).
    Game::EnemyDef slime = always;
    slime.rewardRate = 15;
    Game::RGRandom s;
    s.SetRandomSeed(99);
    int hits = 0;
    for (int i = 0; i < 2000; ++i) {
        hits += Game::RollEnemyReward(slime, s).empty() ? 0 : 1;
    }
    EXPECT_GT(hits, 200);
    EXPECT_LT(hits, 400);
}

TEST(CoinMagnetTest, ScattersThenHomesAndCollects) {
    // FAITHFUL prefab numbers: speed 20 units/s, range 6 units.
    Game::CoinMagnet m = Game::CoinMagnet::Make({0.0F, 0.0F}, 20.0F, 6.0F);
    EXPECT_FLOAT_EQ(m.rangePx, 90.0F);
    EXPECT_FLOAT_EQ(m.speedPxPerSec, 300.0F);
    m.GetForce({1.0F, 0.0F}, 6.0F);
    const glm::vec2 farPlayer{1000.0F, 1000.0F};
    // Scatter: drifts +x, decays (0.9 per 20 ms step), never homes on a far player.
    EXPECT_FALSE(m.Step(200.0F, farPlayer));
    EXPECT_GT(m.pos.x, 5.0F);
    EXPECT_FALSE(m.findTarget);
    const float afterScatter = m.pos.x;
    EXPECT_FALSE(m.Step(2000.0F, farPlayer));
    EXPECT_LT(m.pos.x - afterScatter, 10.0F); // friction has killed the impulse

    // Magnet: a player 80 px away (inside 90) latches homing; 300 px/s closes it.
    const glm::vec2 near{m.pos.x + 80.0F, m.pos.y};
    EXPECT_FALSE(m.Step(20.0F, near));
    EXPECT_TRUE(m.findTarget);
    bool collected = false;
    for (int i = 0; i < 50 && !collected; ++i) {
        collected = m.Step(20.0F, near);
    }
    EXPECT_TRUE(collected);
}

TEST(CoinMagnetTest, OutOfRangeStaysPut) {
    Game::CoinMagnet m = Game::CoinMagnet::Make({0.0F, 0.0F}, 20.0F, 6.0F);
    for (int i = 0; i < 100; ++i) {
        EXPECT_FALSE(m.Step(20.0F, {200.0F, 0.0F}));
    }
    EXPECT_FLOAT_EQ(m.pos.x, 0.0F);
    EXPECT_FALSE(m.findTarget);
}

TEST(EnemyRewardTest, RunCountsCoinsAndResets) {
    Game::RunController run(3);
    run.AddCoins(5);
    run.AddCoins(1);
    EXPECT_EQ(run.State().stats.coins, 6);
    run.AdvanceFloor(Game::PlayerContinuation{});
    EXPECT_EQ(run.State().stats.coins, 6); // run-long, like RGGameProcess.coin_value
    run.ResetRunState();
    EXPECT_EQ(run.State().stats.coins, 0);
}

} // namespace
