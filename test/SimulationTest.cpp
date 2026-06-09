#include <gtest/gtest.h>

#include <vector>

#include "sim/Simulation.hpp"
#include "sim/WorldCollision.hpp"
#include "sim/WorldInputs.hpp"

using Game::Sim::Simulation;
using Game::Sim::WorldInputs;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
Game::Sim::NullWorldCollision g_NullWorld;
WorldInputs Idle() {
    WorldInputs in;
    in.playerPos = glm::vec2{0.0F, 0.0F};
    in.aimDir = glm::vec2{1.0F, 0.0F};
    in.playerRoomId = 0;
    return in;
}
} // namespace

TEST(SimulationTest, AdvanceRunsFixedStepsAndStartsEmpty) {
    Simulation sim(/*runSeed=*/123, &g_NullWorld);
    EXPECT_TRUE(sim.Bullets().empty());
    EXPECT_FALSE(sim.HasBoss());
    EXPECT_TRUE(sim.EnemyViews().empty());

    const int steps = sim.Advance(/*dtMs=*/100.0F, Idle()); // 100/20 = 5 steps
    EXPECT_EQ(steps, 5);
    EXPECT_EQ(sim.PlayerShotsLastAdvance(), 0);
    EXPECT_TRUE(sim.Bullets().empty());
    EXPECT_TRUE(sim.DrainEvents().empty());
}

TEST(SimulationTest, EmptyAdvanceIsReplayDeterministic) {
    auto run = [](int seed) {
        Simulation sim(seed, &g_NullWorld);
        for (int i = 0; i < 10; ++i) {
            sim.Advance(20.0F, Idle());
        }
        return sim.Bullets().size();
    };
    EXPECT_EQ(run(7), run(7));
}

TEST(SimulationTest, EquippedWeaponFiresPlayerBulletsOnCadence) {
    Simulation sim(55, &g_NullWorld);
    Game::WeaponDef def{};
    def.weaponSpeed = 1.0F;
    def.bulletSpeed = 10.0F; // *15 = 150 px/s
    def.atk = 4;
    def.deviation = 0;       // clean +x direction
    sim.EquipWeapon(def, "Gun001", 808);

    WorldInputs in = Idle();
    in.firing = true;
    // fireInterval = 0.15s/weaponSpeed -> SecondsToTicks(0.15)=8 ticks; first shot on tick 1.
    sim.Advance(20.0F, in); // 1 step -> first shot fires
    ASSERT_EQ(sim.Bullets().size(), 1U);
    EXPECT_EQ(sim.Bullets()[0].camp, 0);     // player bullet
    EXPECT_EQ(sim.Bullets()[0].damage, 4);   // def.atk
    EXPECT_EQ(sim.PlayerShotsLastAdvance(), 1);

    in.firing = false;
    sim.Advance(20.0F, in);
    EXPECT_EQ(sim.PlayerShotsLastAdvance(), 0); // not firing -> no new shot
    EXPECT_EQ(sim.Bullets().size(), 1U);
}

TEST(SimulationTest, NotFiringProducesNoBullets) {
    Simulation sim(1, &g_NullWorld);
    Game::WeaponDef def{};
    sim.EquipWeapon(def, "Gun001", 1);
    for (int i = 0; i < 10; ++i) {
        sim.Advance(20.0F, Idle()); // firing defaults false
    }
    EXPECT_TRUE(sim.Bullets().empty());
}

// NOLINTEND(readability-magic-numbers)
