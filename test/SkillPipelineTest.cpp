#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include "combat/CharSkillC01.hpp"
#include "data/GameData.hpp"
#include "sim/SimConfig.hpp"
#include "sim/Simulation.hpp"
#include "sim/WorldCollision.hpp"
#include "sim/WorldInputs.hpp"

using Game::CharSkillC01;
using Game::SkillCooldownProgress;
using Game::Sim::kFixedStepMs;
using Game::Sim::Simulation;
using Game::Sim::WorldInputs;

namespace {
Game::Sim::NullWorldCollision g_NullWorld;

WorldInputs Idle() {
    WorldInputs in;
    in.playerPos = glm::vec2{0.0F, 0.0F};
    in.aimDir = glm::vec2{1.0F, 0.0F};
    in.playerRoomId = 0;
    return in;
}

// One fixed step per Advance (Advance(20ms) -> exactly 1 step, no 8-step cap).
void Steps(Simulation &sim, int n, const WorldInputs &in) {
    for (int i = 0; i < n; ++i) {
        sim.Advance(kFixedStepMs, in);
    }
}
} // namespace

// NOLINTBEGIN(readability-magic-numbers)

// ===================================================================
// Cooldown-loop GROUND TRUTH (ultracode anchor) -- driven at the BRAIN level with
// exact Tick(ms), no step-count fragility. S=5s cd, T=1s window, from ready.
//   cast -> EndSkill resets this_skill_time=0 -> progress 0
//   +0.6*S -> progress 0.6, SkillReady false, CooldownRemaining 0.4*S
//   +S total -> progress 1.0, SkillReady true
// progress = this_skill_time/skill_cd = 1 - CooldownRemaining/SkillCd (ready = 1.0).
// ===================================================================

TEST(SkillPipelineTest, CooldownProgressGroundTruth) {
    CharSkillC01 c(5.0F, 1.0F);
    EXPECT_FLOAT_EQ(SkillCooldownProgress(c), 1.0F); // starts ready
    EXPECT_TRUE(c.SkillReady());

    EXPECT_TRUE(c.TryActivateSkill());               // enter; this_skill_time stays full
    EXPECT_FLOAT_EQ(SkillCooldownProgress(c), 1.0F);

    c.EndSkill();                                    // spend -> this_skill_time = 0
    EXPECT_FLOAT_EQ(SkillCooldownProgress(c), 0.0F); // just cast -> 0
    EXPECT_FALSE(c.SkillReady());

    c.Tick(3000.0F);                                 // +3.0s of 5.0 -> 0.6
    EXPECT_NEAR(SkillCooldownProgress(c), 0.6F, 1e-4F);
    EXPECT_FALSE(c.SkillReady());
    EXPECT_NEAR(c.CooldownRemaining(), 2.0F, 1e-4F);

    c.Tick(2000.0F);                                 // +2.0s -> full
    EXPECT_FLOAT_EQ(SkillCooldownProgress(c), 1.0F);
    EXPECT_TRUE(c.SkillReady());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 0.0F);
}

TEST(SkillPipelineTest, ProgressClampedAndZeroCdAlwaysReady) {
    CharSkillC01 zero(0.0F, 1.0F); // skill_cd 0 -> always ready
    EXPECT_FLOAT_EQ(SkillCooldownProgress(zero), 1.0F);

    CharSkillC01 c(4.0F, 1.0F);
    c.TryActivateSkill();
    c.EndSkill();                  // this_skill_time = 0 -> 0.0
    EXPECT_FLOAT_EQ(SkillCooldownProgress(c), 0.0F);
    c.Tick(100000.0F);             // massive overshoot -> clamps to 1.0, never above
    EXPECT_FLOAT_EQ(SkillCooldownProgress(c), 1.0F);
}

// ===================================================================
// Sim integration: SetPlayerSkill + TickSkill drive the brain; the 0..1 charge
// reaches PlayerStats (the HUD bridge); the cooldown gate blocks recast.
// ===================================================================

TEST(SkillPipelineTest, SimActivatesProgressReachesPlayerStatsAndGatesRecast) {
    Simulation sim(1, &g_NullWorld);
    sim.SetPlayerSkill("c01", /*cd=*/2.0F, /*window=*/1.0F);
    sim.Advance(kFixedStepMs, Idle());
    ASSERT_NE(sim.PlayerSkill(), nullptr);
    EXPECT_FLOAT_EQ(sim.PlayerStats().skillCdProgress, 1.0F); // starts ready, progress reaches stats
    EXPECT_FALSE(sim.PlayerSkill()->InSkill());

    WorldInputs cast = Idle();
    cast.skill = true;
    sim.Advance(kFixedStepMs, cast);
    EXPECT_TRUE(sim.PlayerSkill()->InSkill()); // activated by the skill input

    Steps(sim, 60, Idle()); // past the 1s window -> auto-end -> cooldown started
    EXPECT_FALSE(sim.PlayerSkill()->InSkill());
    EXPECT_FALSE(sim.PlayerSkill()->SkillReady());
    EXPECT_LT(sim.PlayerStats().skillCdProgress, 1.0F); // cooling

    sim.Advance(kFixedStepMs, cast);           // try to recast while cooling
    EXPECT_FALSE(sim.PlayerSkill()->InSkill()); // gate blocks it -> no re-entry

    Steps(sim, 130, Idle()); // finish the 2s cooldown
    EXPECT_TRUE(sim.PlayerSkill()->SkillReady());
    EXPECT_FLOAT_EQ(sim.PlayerStats().skillCdProgress, 1.0F);
}

// ===================================================================
// C01 skill effect: while in_skill, the second hand MIRRORS the primary attack, but
// the FAITHFUL c01 second hand fires 0.1s (kHand2AttackDelay -> 5 fixed ticks) LATER
// via the sim Scheduler -- NOT the same frame -- and is energy-free across the delay.
// (The hand transform/anim is owner presentation; offset stays a placeholder.)
// ===================================================================

TEST(SkillPipelineTest, SecondHandMirrorsAfterDelayAndIsEnergyFree) {
    Simulation sim(55, &g_NullWorld);
    Game::WeaponDef def{};
    def.weaponSpeed = 1.0F;
    def.bulletSpeed = 10.0F;
    def.atk = 4;
    def.deviation = 0;
    sim.EquipWeapon(def, "Gun001", 808);
    sim.SetPlayerSkill("c01", 5.0F, 5.0F); // long window -> stays active

    WorldInputs fire = Idle();
    fire.firing = true;
    fire.skill = true; // activate + fire the same step
    sim.Advance(kFixedStepMs, fire); // tick 1: skill active, primary fires; 2nd hand scheduled +5 ticks
    EXPECT_TRUE(sim.PlayerSkill()->InSkill());
    EXPECT_EQ(sim.Bullets().size(), 1U)        // primary ONLY -- the second hand is deferred 0.1s
        << "the faithful c01 second hand fires 0.1s (5 ticks) later, not the same frame";
    EXPECT_EQ(sim.PlayerShotsLastAdvance(), 1); // primary energy spend

    // Hold the skill active but STOP firing, so no new primary shots while the delay elapses.
    WorldInputs hold = Idle();
    hold.skill = true;
    for (int i = 0; i < 4; ++i) {              // ticks 2..5: still before the 0.1s mark
        sim.Advance(kFixedStepMs, hold);
        EXPECT_EQ(sim.Bullets().size(), 1U) << "before the 5-tick delay elapses: still just the primary";
    }
    sim.Advance(kFixedStepMs, hold);           // tick 6 == fire tick + 5: the second hand fires
    EXPECT_EQ(sim.Bullets().size(), 2U)        // primary + deferred second-hand mirror
        << "the second hand mirrors the shot 0.1s later";
    EXPECT_EQ(sim.PlayerShotsLastAdvance(), 0) // the deferred mirror draws NO energy
        << "the second hand is energy-free (only the primary spent energy, 5 ticks earlier)";
}

TEST(SkillPipelineTest, NoMirrorWhenSkillInactive) {
    Simulation sim(55, &g_NullWorld);
    Game::WeaponDef def{};
    def.weaponSpeed = 1.0F;
    def.bulletSpeed = 10.0F;
    def.atk = 4;
    def.deviation = 0;
    sim.EquipWeapon(def, "Gun001", 808);
    sim.SetPlayerSkill("c01", 5.0F, 5.0F);

    WorldInputs in = Idle();
    in.firing = true; // fire, but NO skill input -> not in_skill -> no mirror
    sim.Advance(kFixedStepMs, in);
    EXPECT_FALSE(sim.PlayerSkill()->InSkill());
    EXPECT_EQ(sim.Bullets().size(), 1U);
}

// NOLINTEND(readability-magic-numbers)
