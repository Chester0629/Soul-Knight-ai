#include <gtest/gtest.h>

#include <string>

#include <glm/glm.hpp>

#include "data/GameData.hpp"
#include "sim/SimConfig.hpp"
#include "sim/SimEvent.hpp"
#include "sim/Simulation.hpp"
#include "sim/WorldCollision.hpp"
#include "sim/WorldInputs.hpp"

using Game::Sim::kFixedStepMs;
using Game::Sim::Simulation;
using Game::Sim::SimEvent;
using Game::Sim::SimEventType;
using Game::Sim::WorldInputs;

// NOLINTBEGIN(readability-magic-numbers)

// B1-P4a: (d) ICharSkill dispatch foundation. Proves SetPlayerSkill(charId) routes to
// DIFFERENT per-hero adapters (C01 mirror vs C02 dash, distinct behaviour), the
// cooldown/gate cycle runs for all, and the c03..c13 stubs trigger+cool with NO combat
// effect (gate+cooldown-only -- by design, not a bug).
namespace {
Game::Sim::NullWorldCollision g_NullWorld;

WorldInputs Idle() {
    WorldInputs in;
    in.playerPos = glm::vec2{0.0F, 0.0F};
    in.aimDir = glm::vec2{1.0F, 0.0F};
    in.playerRoomId = 0;
    return in;
}

Game::WeaponDef SingleGun() {
    Game::WeaponDef def{};
    def.weaponSpeed = 1.0F;
    def.bulletSpeed = 10.0F;
    def.atk = 4;
    def.deviation = 0;
    return def;
}

bool HasSkillDash(Simulation &sim) {
    for (const SimEvent &e : sim.DrainEvents()) {
        if (e.type == SimEventType::AnimTrigger && e.entityId == Simulation::kPlayerViewId &&
            e.name == "skill_dash") {
            return true;
        }
    }
    return false;
}
} // namespace

// --- dispatch routes by charId: C01 != C02 != stub ---------------------------
TEST(CharSkillDispatchTest, DispatchSelectsPerHeroAdapter) {
    Simulation sim(1, &g_NullWorld);
    sim.SetPlayerSkill("c01", 2.0F, 1.0F);
    ASSERT_NE(sim.PlayerSkill(), nullptr);
    EXPECT_FLOAT_EQ(sim.PlayerStats().skillCdProgress, 1.0F); // starts ready (after one Advance)
    sim.Advance(kFixedStepMs, Idle());
    EXPECT_TRUE(sim.PlayerSkill()->SkillReady());
}

// --- C01 is FAITHFUL: while in_skill the second hand mirrors the shot (2 bullets) --
TEST(CharSkillDispatchTest, C01MirrorsShotWhileActive) {
    Simulation sim(55, &g_NullWorld);
    sim.EquipWeapon(SingleGun(), "Gun001", 808);
    sim.SetPlayerSkill("c01", 5.0F, 5.0F); // long window -> stays active

    WorldInputs in = Idle();
    in.firing = true;
    in.skill = true; // activate + fire same step
    sim.Advance(kFixedStepMs, in);
    EXPECT_TRUE(sim.PlayerSkill()->InSkill());
    EXPECT_EQ(sim.Bullets().size(), 2U);        // primary + mirror
    EXPECT_EQ(sim.PlayerShotsLastAdvance(), 1); // mirror is energy-free
    EXPECT_FALSE(HasSkillDash(sim));            // C01 does not dash
}

// --- C02 is FAITHFUL (distinct): the ultimate is an instant dash (event), NO mirror --
TEST(CharSkillDispatchTest, C02DashesAndDoesNotMirror) {
    Simulation sim(55, &g_NullWorld);
    sim.EquipWeapon(SingleGun(), "Gun001", 808);
    sim.SetPlayerSkill("c02", 5.0F, 0.0F);

    WorldInputs in = Idle();
    in.firing = true;
    in.skill = true;
    sim.Advance(kFixedStepMs, in);
    EXPECT_TRUE(HasSkillDash(sim));        // C02 emits the dash impulse event (distinct from C01)
    EXPECT_EQ(sim.Bullets().size(), 1U);   // NO second-hand mirror -> just the primary shot
    EXPECT_FALSE(sim.PlayerSkill()->InSkill()); // instant dash: activated + ended same step
    EXPECT_FALSE(sim.PlayerSkill()->SkillReady()); // cooldown spent on the dash
}

// --- stub (c03..c13): trigger + cooldown run, but NO combat effect (by design) ----
TEST(CharSkillDispatchTest, StubHeroTriggersAndCoolsWithNoEffect) {
    Simulation sim(7, &g_NullWorld);
    sim.EquipWeapon(SingleGun(), "Gun001", 808);
    sim.SetPlayerSkill("c03", 2.0F, 0.0F); // a gate+cooldown-only stub
    ASSERT_TRUE(sim.PlayerSkill()->SkillReady());

    WorldInputs in = Idle();
    in.firing = true;
    in.skill = true;
    sim.Advance(kFixedStepMs, in);
    EXPECT_FALSE(HasSkillDash(sim));                 // no dash effect
    EXPECT_EQ(sim.Bullets().size(), 1U);             // no mirror/summon -> just the primary shot
    EXPECT_FALSE(sim.PlayerSkill()->SkillReady());   // but the COOLDOWN engaged (cast spent it)

    for (int i = 0; i < 130; ++i) {
        sim.Advance(kFixedStepMs, Idle()); // finish the 2s cooldown
    }
    EXPECT_TRUE(sim.PlayerSkill()->SkillReady());     // recovers -> the trigger/cooldown cycle works
}

// --- unknown charId falls back to c01 (safe default) -------------------------
TEST(CharSkillDispatchTest, UnknownCharIdFallsBackToC01) {
    Simulation sim(3, &g_NullWorld);
    sim.EquipWeapon(SingleGun(), "Gun001", 808);
    sim.SetPlayerSkill("c99", 5.0F, 5.0F); // unknown -> c01 fallback (mirror)
    WorldInputs in = Idle();
    in.firing = true;
    in.skill = true;
    sim.Advance(kFixedStepMs, in);
    EXPECT_EQ(sim.Bullets().size(), 2U); // behaves as c01 (mirror)
}

// NOLINTEND(readability-magic-numbers)
