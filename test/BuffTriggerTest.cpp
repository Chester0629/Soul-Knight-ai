// P3.2: bullets that carry a *BuffTrigger component apply the matching buff on
// hit. Covers (a) the data shape (tools/build_gamedata.py's attach_bullet_buffs()
// -> GameData::BulletDef::buffId/buffProbability) and (b) the sim wiring
// (BrainFactory::MakeWeapon -> WeaponController::Params -> FireIntent ->
// BulletState -> Simulation::RollBulletBuff/ApplyBuff).
#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include "combat/Buff.hpp"
#include "data/GameData.hpp"
#include "sim/SimEvent.hpp"
#include "sim/Simulation.hpp"
#include "sim/WorldCollision.hpp"
#include "sim/WorldInputs.hpp"

using Game::Sim::Simulation;
using Game::Sim::SimEvent;
using Game::Sim::SimEventType;
using Game::Sim::WorldInputs;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
Game::Sim::NullWorldCollision g_NullWorld;
const char *kResourceRoot = RESOURCE_DIR; // CMake-injected, as in GameDataTest.

WorldInputs FiringAt(glm::vec2 aim) {
    WorldInputs in;
    in.playerPos = glm::vec2{0.0F, 0.0F};
    in.aimDir = aim;
    in.firing = true;
    in.playerRoomId = 0;
    return in;
}

/// True iff any drained event this call is a "buff_applied:<kind>:..." AnimTrigger.
bool HasBuffApplied(const std::vector<SimEvent> &events, const char *kindSubstr) {
    const std::string needle = std::string("buff_applied:") + kindSubstr;
    for (const SimEvent &e : events) {
        if (e.type == SimEventType::AnimTrigger && e.name.rfind(needle, 0) == 0) {
            return true;
        }
    }
    return false;
}
} // namespace

// --- (a) data: bullets.json "buff" -> GameData::BulletDef::buffId/buffProbability ---

TEST(BuffTriggerTest, DataBulletFireBulletTrigger) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::BulletDef *b = gd.FindBullet("bullet_4"); // RGBulletBuffTrigger -> buff_fire
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->buffId, "BuffFire");
    EXPECT_FLOAT_EQ(b->buffProbability, 1.0F);
}

TEST(BuffTriggerTest, DataArrowFireBulletTrigger) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::BulletDef *b = gd.FindBullet("bullet_36"); // RGArrowBuffTrigger -> buff_fire
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->buffId, "BuffFire");
    EXPECT_FLOAT_EQ(b->buffProbability, 1.0F);
}

TEST(BuffTriggerTest, DataSwordFireBuffOnlyRow) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    // sword_2_fire doesn't derive from the RGBullet family -- attach_bullet_buffs()
    // appends it as a NEW buff-only bullets.json row (no speed/destroy_time).
    const Game::BulletDef *b = gd.FindBullet("sword_2_fire"); // RGSwordBuffTrigger -> buff_fire
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->buffId, "BuffFire");
    EXPECT_FLOAT_EQ(b->buffProbability, 1.0F);
}

TEST(BuffTriggerTest, DataBulletProbabilityZeroNeverApplies) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::BulletDef *b = gd.FindBullet("bullet_95"); // BuffEffectTrigger, probability: 0
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->buffId, "BuffIce");
    EXPECT_FLOAT_EQ(b->buffProbability, 0.0F);
}

TEST(BuffTriggerTest, DataPoisonBulletTrigger) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::BulletDef *b = gd.FindBullet("bullet_3"); // RGBulletBuffTrigger -> buff_posion
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->buffId, "BuffPoison");
    EXPECT_FLOAT_EQ(b->buffProbability, 1.0F);
}

TEST(BuffTriggerTest, DataElectricBulletTrigger) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::BulletDef *b = gd.FindBullet("bullet_32"); // RGBulletBuffTrigger -> buff_ele
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->buffId, "BuffElectric");
    EXPECT_FLOAT_EQ(b->buffProbability, 1.0F);
}

TEST(BuffTriggerTest, DataIceArrowTrigger) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::BulletDef *b = gd.FindBullet("bullet_38"); // RGArrowBuffTrigger -> buff_ice
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->buffId, "BuffIce");
    EXPECT_FLOAT_EQ(b->buffProbability, 1.0F);
}

TEST(BuffTriggerTest, DataNoBuffBulletStaysEmpty) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::BulletDef *b = gd.FindBullet("bullet_0"); // ReboundBuffEffector only -- NOT a buff trigger.
    ASSERT_NE(b, nullptr);
    EXPECT_TRUE(b->buffId.empty());
    EXPECT_FLOAT_EQ(b->buffProbability, 0.0F);
}

TEST(BuffTriggerTest, DataWeaponResolvesItsBullet) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::WeaponDef *w = gd.FindWeapon("weapon_013"); // Gun001, bullet_4 (fire)
    ASSERT_NE(w, nullptr);
    const Game::BulletDef *b = gd.BulletForWeapon(*w);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->id, "bullet_4");
    EXPECT_EQ(b->buffId, "BuffFire");
}

// --- (b) sim: bullet-trigger wiring auto-applies the buff on a landed hit ---

TEST(BuffTriggerTest, FireBulletWeaponAppliesFireOnHit) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::EnemyDef *edef = gd.FindEnemyByClass("EnemyAI01");
    const Game::WeaponDef *wdef = gd.FindWeapon("weapon_013"); // Gun001 class, bullet_4 (fire, p=1.0)
    ASSERT_NE(edef, nullptr);
    ASSERT_NE(wdef, nullptr);

    Simulation sim(20240607, &g_NullWorld);
    sim.AddEnemy(*edef, glm::vec2{60.0F, 0.0F}, /*roomId=*/0, 21000);
    sim.EquipWeapon(*wdef, "weapon_013", 5, &gd); // P3.2: data threads the bullet's buff in.

    const WorldInputs in = FiringAt(glm::vec2{1.0F, 0.0F});
    bool sawFireApplied = false;
    int hpRightAfterApply = -1;
    for (int i = 0; i < 150 && !sawFireApplied; ++i) {
        sim.Advance(20.0F, in);
        if (HasBuffApplied(sim.DrainEvents(), "Fire")) {
            sawFireApplied = true;
            hpRightAfterApply = sim.EnemyViews()[0].hp;
        }
    }
    ASSERT_TRUE(sawFireApplied) << "bullet_4's RGBulletBuffTrigger (probability 1.0) never applied Fire";

    // The Fire buff's DoT (1 dmg/s, 2s duration -- buffs.json BuffFire) ticks
    // through TickBuffs() even with no further hits; hp must keep dropping.
    bool sawDot = false;
    for (int i = 0; i < 100; ++i) {
        WorldInputs idle = in;
        idle.firing = false; // stop shooting -- isolate the DoT from further direct hits.
        sim.Advance(20.0F, idle);
        if (!sim.EnemyViews()[0].alive) {
            break; // enemy already low-hp from the direct hit; DoT may finish it off.
        }
        if (sim.EnemyViews()[0].hp < hpRightAfterApply) {
            sawDot = true;
            break;
        }
    }
    EXPECT_TRUE(sawDot) << "Fire's tick damage never reduced the enemy's hp after the initial hit";
}

TEST(BuffTriggerTest, ProbabilityZeroBulletNeverAppliesBuff) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::EnemyDef *edef = gd.FindEnemyByClass("EnemyAI01");
    const Game::WeaponDef *wdef = gd.FindWeapon("weapon_175"); // Gun001, bullet_95 (ice, p=0.0)
    ASSERT_NE(edef, nullptr);
    ASSERT_NE(wdef, nullptr);

    Simulation sim(1, &g_NullWorld);
    sim.AddEnemy(*edef, glm::vec2{60.0F, 0.0F}, /*roomId=*/0, 21000);
    sim.EquipWeapon(*wdef, "weapon_175", 5, &gd);

    const WorldInputs in = FiringAt(glm::vec2{1.0F, 0.0F});
    for (int i = 0; i < 150; ++i) {
        sim.Advance(20.0F, in);
        EXPECT_FALSE(HasBuffApplied(sim.DrainEvents(), "Ice"))
            << "bullet_95 has buff.probability 0 -- it must never apply Ice";
        if (!sim.EnemyViews()[0].alive) {
            break;
        }
    }
}

TEST(BuffTriggerTest, ProbabilityRollIsDeterministicUnderFixedSeed) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::EnemyDef *edef = gd.FindEnemyByClass("EnemyAI01");
    const Game::WeaponDef *wdef = gd.FindWeapon("weapon_046"); // Gun003, bullet_9 (RGLaserBuff, p=0.04)
    ASSERT_NE(edef, nullptr);
    ASSERT_NE(wdef, nullptr);

    auto run = [&](int runSeed) {
        Simulation sim(runSeed, &g_NullWorld);
        sim.AddEnemy(*edef, glm::vec2{60.0F, 0.0F}, /*roomId=*/0, runSeed + 1000);
        sim.EquipWeapon(*wdef, "weapon_046", runSeed + 5, &gd);
        const WorldInputs in = FiringAt(glm::vec2{1.0F, 0.0F});
        std::vector<float> trace;
        for (int i = 0; i < 200; ++i) {
            sim.Advance(20.0F, in);
            const auto events = sim.DrainEvents();
            trace.push_back(HasBuffApplied(events, "Ice") ? 1.0F : 0.0F);
            trace.push_back(sim.EnemyViews().empty() ? -1.0F
                                                     : static_cast<float>(sim.EnemyViews()[0].hp));
        }
        return trace;
    };
    const std::vector<float> a = run(777);
    const std::vector<float> b = run(777);
    EXPECT_EQ(a, b); // same seed -> the probability roll (m_HitRng) replays identically.
}

// --- existing golden behaviour: a weapon whose bullet carries NO buff (Gun001's
// default weapon_000/bullet_0) stays exactly as it was before P3.2. ---
TEST(BuffTriggerTest, NoBuffWeaponUnaffectedByP32Wiring) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::EnemyDef *edef = gd.FindEnemyByClass("EnemyAI01");
    const Game::WeaponDef *wdef = gd.FindWeapon("Gun001"); // class fallback -> weapon_000/bullet_0
    ASSERT_NE(edef, nullptr);
    ASSERT_NE(wdef, nullptr);

    auto run = [&](bool passData) {
        Simulation sim(20240607, &g_NullWorld);
        sim.AddEnemy(*edef, glm::vec2{60.0F, 0.0F}, /*roomId=*/0, 21000);
        sim.EquipWeapon(*wdef, "Gun001", 5, passData ? &gd : nullptr);
        const WorldInputs in = FiringAt(glm::vec2{1.0F, 0.0F});
        std::vector<float> trace;
        for (int i = 0; i < 150; ++i) {
            sim.Advance(20.0F, in);
            trace.push_back(static_cast<float>(sim.EnemyViews()[0].hp));
            EXPECT_FALSE(HasBuffApplied(sim.DrainEvents(), "")); // no buff_applied event AT ALL.
        }
        return trace;
    };
    // Passing GameData in (P3.2) must not change a no-buff weapon's trace at all
    // versus the old (nullptr) call sites' behaviour.
    EXPECT_EQ(run(/*passData=*/true), run(/*passData=*/false));
}

// NOLINTEND(readability-magic-numbers)
