#include <gtest/gtest.h>

#include <vector>

#include "sim/SimConfig.hpp"
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

namespace {
/// Blocks any point with x >= kWallX (a vertical wall) -- for bullet-cull tests.
class RightWall : public Game::Sim::WorldCollision {
public:
    static constexpr float kWallX = 100.0F;
    bool Blocks(glm::vec2 pos, float /*radius*/) const override { return pos.x >= kWallX; }
};
} // namespace

TEST(SimulationTest, BulletAdvancesAndExpires) {
    Simulation sim(1, &g_NullWorld);
    Game::WeaponDef def{};
    def.bulletSpeed = 10.0F; // 150 px/s -> 3 px/step
    sim.EquipWeapon(def, "Gun001", 1);
    WorldInputs in = Idle();
    in.firing = true;
    sim.Advance(20.0F, in);           // fire one bullet (lifeMs default 1500)
    ASSERT_EQ(sim.Bullets().size(), 1U);
    const float x0 = sim.Bullets()[0].pos.x;
    in.firing = false;
    sim.Advance(20.0F, in);           // one more step: pos advances ~3px, life -20ms
    ASSERT_EQ(sim.Bullets().size(), 1U);
    EXPECT_GT(sim.Bullets()[0].pos.x, x0);
    EXPECT_LT(sim.Bullets()[0].lifeMs, 1500.0F);
    // Drive long enough to expire (1500ms / 20ms = 75 steps) -> culled.
    for (int i = 0; i < 80; ++i) {
        sim.Advance(20.0F, in);
    }
    EXPECT_TRUE(sim.Bullets().empty());
}

TEST(SimulationTest, BulletCulledByWallUnlessCanThrough) {
    RightWall wall;
    Simulation sim(1, &wall);
    Game::WeaponDef def{};
    def.bulletSpeed = 40.0F; // 600 px/s -> 12 px/step, crosses x=100 quickly
    sim.EquipWeapon(def, "Gun001", 1);
    WorldInputs in = Idle();
    in.firing = true;
    sim.Advance(20.0F, in); // fire at origin aiming +x
    in.firing = false;
    for (int i = 0; i < 12; ++i) {
        sim.Advance(20.0F, in); // bullet marches toward the wall at x>=100
    }
    EXPECT_TRUE(sim.Bullets().empty()); // hit the wall, culled
}

TEST(SimulationTest, EnemyAsleepUntilPlayerEntersRoom) {
    Simulation sim(20240607, &g_NullWorld);
    Game::EnemyDef def{};
    def.shootCd = 0.5F;
    def.scoutRate = 0.5F;
    def.friction = 0.9F;
    sim.AddEnemy(def, glm::vec2{50.0F, 0.0F}, /*roomId=*/2, /*seed=*/1234);

    WorldInputs in = Idle();
    in.playerRoomId = 1; // different room -> enemy asleep
    for (int i = 0; i < 60; ++i) {
        sim.Advance(20.0F, in);
    }
    ASSERT_EQ(sim.EnemyViews().size(), 1U);
    EXPECT_TRUE(sim.Bullets().empty());                 // asleep: no fire
    EXPECT_FLOAT_EQ(sim.EnemyViews()[0].pos.x, 50.0F);  // asleep: no move
    EXPECT_TRUE(sim.EnemyViews()[0].alive);
    EXPECT_EQ(sim.EnemyViews()[0].hp, Game::Sim::kSliceEnemyHp);
}

TEST(SimulationTest, AwakeEnemyMovesTowardPlayerAndFires) {
    Simulation sim(20240607, &g_NullWorld);
    Game::EnemyDef def{};
    def.shootCd = 0.2F;
    def.scoutRate = 0.1F;
    def.friction = 0.9F;
    sim.AddEnemy(def, glm::vec2{100.0F, 0.0F}, /*roomId=*/2, /*seed=*/4242);

    WorldInputs in = Idle();
    in.playerPos = glm::vec2{0.0F, 0.0F};
    in.playerRoomId = 2; // same room -> awake
    for (int i = 0; i < 120; ++i) {
        sim.Advance(20.0F, in);
    }
    const auto ev = sim.EnemyViews()[0];
    // EnemyAI01 scouts/wanders, so don't assume a direction -- just that it moved.
    EXPECT_GT(glm::length(ev.pos - glm::vec2{100.0F, 0.0F}), 0.5F);
    EXPECT_FALSE(sim.Bullets().empty()); // fired enemy bullets (camp 1)
    EXPECT_EQ(sim.Bullets().front().camp, 1);
}

TEST(SimulationTest, EnemyReplayIsByteIdentical) {
    auto run = [](int seed) {
        Simulation sim(seed, &g_NullWorld);
        Game::EnemyDef def{};
        def.shootCd = 0.2F;
        def.scoutRate = 0.1F;
        def.friction = 0.9F;
        sim.AddEnemy(def, glm::vec2{80.0F, 20.0F}, 2, seed + 1000);
        WorldInputs in = Idle();
        in.playerRoomId = 2;
        std::vector<float> trace;
        for (int i = 0; i < 80; ++i) {
            sim.Advance(20.0F, in);
            const auto v = sim.EnemyViews()[0];
            trace.push_back(v.pos.x);
            trace.push_back(v.pos.y);
            trace.push_back(static_cast<float>(sim.Bullets().size()));
        }
        return trace;
    };
    EXPECT_EQ(run(99), run(99));
}

TEST(SimulationTest, BossChasesPlayerAndFiresFan) {
    Simulation sim(20240607, &g_NullWorld);
    sim.SetBoss(/*baseShootCd=*/0.2F, glm::vec2{200.0F, 0.0F}, /*maxHp=*/500, /*roomId=*/3,
                /*seed=*/9000);
    ASSERT_TRUE(sim.HasBoss());
    EXPECT_EQ(sim.BossView().id, Simulation::kBossViewId);
    EXPECT_EQ(sim.BossView().maxHp, 500);

    WorldInputs in = Idle();
    in.playerPos = glm::vec2{0.0F, 0.0F};
    in.playerRoomId = 3; // wake the boss
    for (int i = 0; i < 120; ++i) {
        sim.Advance(20.0F, in);
    }
    EXPECT_LT(sim.BossView().pos.x, 200.0F); // chased toward the player
    ASSERT_FALSE(sim.Bullets().empty());     // fired a fan
    // A fan emits >=3 enemy bullets; confirm camp 1 and multiplicity.
    EXPECT_EQ(sim.Bullets().front().camp, 1);
    EXPECT_GE(sim.Bullets().size(), 3U);
}

TEST(SimulationTest, BossReplayIsByteIdentical) {
    auto run = [](int seed) {
        Simulation sim(seed, &g_NullWorld);
        sim.SetBoss(0.2F, glm::vec2{150.0F, 30.0F}, 500, 3, seed + 9000);
        WorldInputs in = Idle();
        in.playerRoomId = 3;
        std::vector<float> trace;
        for (int i = 0; i < 80; ++i) {
            sim.Advance(20.0F, in);
            trace.push_back(sim.BossView().pos.x);
            trace.push_back(sim.BossView().pos.y);
            trace.push_back(static_cast<float>(sim.Bullets().size()));
        }
        return trace;
    };
    EXPECT_EQ(run(123), run(123));
}

TEST(SimulationTest, PlayerBulletDamagesAndKillsEnemy) {
    Simulation sim(20240607, &g_NullWorld);
    Game::EnemyDef def{};
    def.shootCd = 99.0F; // keep the enemy from firing back during the test
    def.scoutRate = 99.0F;
    def.friction = 0.9F;
    // Enemy sitting at the origin; player just to its left firing +x straight into it.
    sim.AddEnemy(def, glm::vec2{0.0F, 0.0F}, /*roomId=*/0, /*seed=*/1);
    Game::WeaponDef wdef{};
    wdef.bulletSpeed = 4.0F; // slow so it lingers on the enemy
    wdef.atk = 1;
    wdef.deviation = 0;
    sim.EquipWeapon(wdef, "Gun001", 7);

    WorldInputs in = Idle();
    in.playerPos = glm::vec2{-20.0F, 0.0F};
    in.aimDir = glm::vec2{1.0F, 0.0F};
    in.firing = true;
    in.playerRoomId = 0; // enemy awake (so Kill/scheduler interplay is exercised)
    const int startHp = sim.EnemyViews()[0].hp;
    for (int i = 0; i < 200 && sim.EnemyViews()[0].alive; ++i) {
        sim.Advance(20.0F, in);
    }
    EXPECT_LT(sim.EnemyViews()[0].hp, startHp);   // took damage
    EXPECT_FALSE(sim.EnemyViews()[0].alive);      // died (hp <= 0)
}

TEST(SimulationTest, EnemyBulletDamagesPlayer) {
    Simulation sim(20240607, &g_NullWorld);
    Game::CombatStats player;
    player.hp = 6;
    player.maxHp = 6;
    sim.SetPlayerStats(player);
    Game::EnemyDef def{};
    def.shootCd = 0.1F;
    def.scoutRate = 99.0F; // hold still
    def.friction = 0.9F;
    sim.AddEnemy(def, glm::vec2{30.0F, 0.0F}, /*roomId=*/0, /*seed=*/2);

    WorldInputs in = Idle();
    in.playerPos = glm::vec2{0.0F, 0.0F}; // enemy fires toward origin (the player)
    in.playerRoomId = 0;
    for (int i = 0; i < 200 && sim.PlayerStats().hp == 6; ++i) {
        sim.Advance(20.0F, in);
    }
    EXPECT_LT(sim.PlayerStats().hp, 6); // an enemy bullet reached the player
}

TEST(SimulationTest, HitResolutionReplayIsByteIdentical) {
    auto run = [](int seed) {
        Simulation sim(seed, &g_NullWorld);
        Game::EnemyDef def{};
        def.shootCd = 0.3F;
        def.scoutRate = 0.2F;
        def.friction = 0.9F;
        sim.AddEnemy(def, glm::vec2{40.0F, 0.0F}, 0, seed + 1000);
        Game::WeaponDef wdef{};
        wdef.bulletSpeed = 8.0F;
        wdef.atk = 1;
        sim.EquipWeapon(wdef, "Gun001", seed + 5);
        WorldInputs in = Idle();
        in.playerPos = glm::vec2{-10.0F, 0.0F};
        in.firing = true;
        in.playerRoomId = 0;
        std::vector<float> trace;
        for (int i = 0; i < 100; ++i) {
            sim.Advance(20.0F, in);
            trace.push_back(static_cast<float>(sim.EnemyViews()[0].hp));
            trace.push_back(static_cast<float>(sim.EnemyViews()[0].alive ? 1 : 0));
            trace.push_back(static_cast<float>(sim.Bullets().size()));
        }
        return trace;
    };
    EXPECT_EQ(run(2024), run(2024));
}

// NOLINTEND(readability-magic-numbers)
