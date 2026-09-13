#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "data/GameData.hpp"
#include "sim/BrainFactory.hpp"
#include "sim/EnemyController.hpp"
#include "sim/FireIntent.hpp"
#include "sim/Scheduler.hpp"
#include "sim/Simulation.hpp"

using Game::EnemyDef;
using Game::GameData;
using Game::Sim::BrainFactory;
using Game::Sim::EnemyController;

namespace {
// RESOURCE_DIR is injected by CMake (points at the repo's Resources/ folder).
const char *kResourceRoot = RESOURCE_DIR;
Game::Sim::NullWorldCollision g_NullWorld; // no walls -- mirrors SimulationTest.cpp.
} // namespace

// =============================================================================
// ENEMY RE-KEY (per-species data-driven enemy/boss stats): enemies.json is now
// ONE ENTRY PER SPECIES from the RE'd enemies.csv ground truth (145 rows), not
// 16 AI-class templates. These tests lock the shape of that re-key: real species
// count, real per-species maxHp (boss + a small mob), enemyClass populated for
// dispatch, and that brain dispatch still resolves keyed off enemyClass.
// =============================================================================

// NOLINTBEGIN(readability-magic-numbers)

TEST(EnemyStatsTest, LoadsAllSpeciesFromEnemiesCsv) {
    GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    // enemies.csv has 145 data rows (prefab_name is unique per row); the old
    // per-AI-class table had 16. Any reasonably-complete re-key should be in the
    // same ballpark as the full table, well above the old class-template count.
    // P1.10 UPVERSION (T-27): plus ex_orc05, read straight from its AssetRipper
    // prefab (GameObject/ex_orc05.prefab is outside res/enemy/**, so it has no
    // enemies.csv row) -- 146 total.
    EXPECT_EQ(gd.Enemies().size(), 146u);
}

TEST(EnemyStatsTest, Boss01RealMaxHp) {
    GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const EnemyDef *boss = gd.FindEnemy("boss01");
    ASSERT_NE(boss, nullptr);
    // FAITHFUL @ enemies.csv: boss01 ra_max_hp=600 (every boss used to load flat
    // HP=500 regardless of species -- the bug this re-key fixes).
    EXPECT_EQ(boss->maxHp, 600);
    EXPECT_EQ(boss->hp, 600);
    EXPECT_EQ(boss->enemyClass, "BossAI01");
}

TEST(EnemyStatsTest, SmallEnemyMaxHpMatchesCsv) {
    GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const EnemyDef *succulent = gd.FindEnemy("e_succulent");
    ASSERT_NE(succulent, nullptr);
    // FAITHFUL @ enemies.csv: e_succulent ra_max_hp=8 (every non-boss enemy used
    // to load flat HP=3 regardless of species).
    EXPECT_EQ(succulent->maxHp, 8);
    EXPECT_EQ(succulent->hp, 8);
    EXPECT_EQ(succulent->enemyClass, "EnemyAI06");
    EXPECT_EQ(succulent->kinematic, 1); // turret-style (FAITHFUL @ enemies.csv).
}

TEST(EnemyStatsTest, EveryLoadedSpeciesHasAnEnemyClass) {
    GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    ASSERT_FALSE(gd.Enemies().empty());
    for (const EnemyDef &d : gd.Enemies()) {
        EXPECT_FALSE(d.id.empty());
        EXPECT_FALSE(d.enemyClass.empty()) << "species '" << d.id << "' has no enemyClass";
    }
}

TEST(EnemyStatsTest, FindEnemyByClassResolvesARepresentativeSpecies) {
    GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const EnemyDef *rep = gd.FindEnemyByClass("EnemyAI07");
    ASSERT_NE(rep, nullptr);
    EXPECT_EQ(rep->enemyClass, "EnemyAI07");
    EXPECT_GT(rep->maxHp, 0); // a real species, not a zeroed placeholder.
    EXPECT_EQ(gd.FindEnemyByClass("NoSuchClass"), nullptr);
}

// Brain dispatch keys off enemyClass now (not the species id): a real species
// EnemyDef (id="e_hammer", enemyClass="EnemyAI07" -- int-shoot) must dispatch to
// a DISTINCT brain from a real AI01 species, same as the old class-keyed path.
TEST(EnemyStatsTest, BrainDispatchResolvesForASpeciesClass) {
    GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const EnemyDef *ai01 = gd.FindEnemyByClass("EnemyAI01");
    const EnemyDef *ai07 = gd.FindEnemyByClass("EnemyAI07");
    ASSERT_NE(ai01, nullptr);
    ASSERT_NE(ai07, nullptr);

    auto e01 = BrainFactory::MakeEnemyPtr(*ai01, glm::vec2{0, 0}, 99);
    auto e07 = BrainFactory::MakeEnemyPtr(*ai07, glm::vec2{0, 0}, 99);
    ASSERT_NE(e01, nullptr);
    ASSERT_NE(e07, nullptr);
    // Drive both controllers a few ticks first (same-seeded brains draw IDENTICAL
    // RNG until something actually consumes from their stream -- a fresh, unticked
    // brain would give a false "identical" pass regardless of which adapter was
    // dispatched). Mirrors EnemyBrainDispatchTest's Drive()/MaxWander() probe.
    for (auto *e : {e01.get(), e07.get()}) {
        Game::Sim::Scheduler sched;
        std::vector<Game::Sim::FireIntent> fire;
        e->MutableState().awake = true;
        e->SetTarget(glm::vec2{50.0F, 0.0F});
        e->Activate(sched, fire);
        for (int i = 0; i < 30; ++i) {
            sched.Tick();
        }
    }
    // Distinct brains drew distinct amounts from their (same-seeded) streams (the
    // same divergence probe EnemyBrainDispatchTest uses for the class-keyed path).
    EXPECT_NE(e01->Brain().RngRange(0, 1000000), e07->Brain().RngRange(0, 1000000));
}

// Simulation::AddEnemy seeds the CONTROLLER's hp from the real per-species
// maxHp (FAITHFUL @ enemies.csv), not the flat kSliceEnemyHp every enemy shared.
TEST(EnemyStatsTest, SimulationAddEnemySeedsRealSpeciesMaxHp) {
    GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const EnemyDef *succulent = gd.FindEnemy("e_succulent");
    ASSERT_NE(succulent, nullptr);

    Game::Sim::Simulation sim(1, &g_NullWorld);
    sim.AddEnemy(*succulent, glm::vec2{0.0F, 0.0F}, /*roomId=*/0, /*seed=*/7);
    ASSERT_EQ(sim.EnemyViews().size(), 1u);
    EXPECT_EQ(sim.EnemyViews()[0].hp, 8);
    EXPECT_EQ(sim.EnemyViews()[0].maxHp, 8);
}

// NOLINTEND(readability-magic-numbers)
