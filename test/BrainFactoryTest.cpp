#include <gtest/gtest.h>

#include "data/GameData.hpp"
#include "sim/BrainFactory.hpp"
#include "sim/EnemyController.hpp"

#include <vector>

#include "sim/BossController.hpp"
#include "sim/FireIntent.hpp"
#include "sim/WeaponController.hpp"

using Game::Sim::BrainFactory;
using Game::Sim::EnemyController;

// NOLINTBEGIN(readability-magic-numbers)

TEST(BrainFactoryTest, MakeEnemyAppliesDefParamsAndSpawn) {
    Game::EnemyDef def{};
    def.scoutRate = 0.5F;
    def.friction = 0.8F;
    def.kinematic = 0; // not kinematic
    def.shootCd = 1.0F;

    EnemyController e = BrainFactory::MakeEnemy(def, glm::vec2{20.0F, 30.0F}, 42);
    EXPECT_FLOAT_EQ(e.State().pos.x, 20.0F);
    EXPECT_FLOAT_EQ(e.State().pos.y, 30.0F);
    EXPECT_FALSE(e.State().kinematic);
    EXPECT_TRUE(e.Brain().Seeded());
}

TEST(BrainFactoryTest, MakeEnemyHonoursKinematicFlag) {
    Game::EnemyDef def{};
    def.kinematic = 1;
    EnemyController e = BrainFactory::MakeEnemy(def, glm::vec2{0.0F, 0.0F}, 1);
    EXPECT_TRUE(e.State().kinematic);
}

TEST(BrainFactoryTest, MakeBossSeedsAndSpawns) {
    Game::Sim::BossController b = BrainFactory::MakeBoss(2.0F, glm::vec2{4.0F, 5.0F}, 600, 9);
    EXPECT_FLOAT_EQ(b.State().pos.x, 4.0F);
    EXPECT_FLOAT_EQ(b.ShootCdSeconds(), 2.0F);
    EXPECT_TRUE(b.Brain().Seeded());
}

TEST(BrainFactoryTest, MakeWeaponSingleFromDef) {
    Game::WeaponDef def{};
    Game::Sim::WeaponController w = BrainFactory::MakeWeapon(def, "Gun001", 1);
    std::vector<Game::Sim::FireIntent> out;
    w.Tick(true, glm::vec2{0.0F, 0.0F}, glm::vec2{1.0F, 0.0F}, out);
    EXPECT_EQ(out.size(), 1U);
}

// NOLINTEND(readability-magic-numbers)
