#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include "combat/CharSkillC04.hpp"
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

// P3.3.b: C04/C08/C10/C13 graduate from StubSkillAdapter (gate+cooldown-only, the
// combat effect a deliberate no-op) to real adapters. Every expected number below is
// read straight from Resources/data/characters.json's roster rows (atk/skill_cd/
// in_skill_time) or from the hero's own brain header constants (kCutBonusNormal=3,
// kCutBonusStrengthened=7, kComboWrap=5, kArmorRestoreEveryNthHit=3,
// kArmorRestoreAmount/kArmorRestoredPerCycle=1) -- see CharSkillC04/C08/C10/C13.hpp.

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

Game::EnemyDef WeakEnemy() {
    Game::EnemyDef def{};
    def.shootCd = 99.0F; // never fires back -- isolates the effect under test.
    def.scoutRate = 99.0F;
    def.friction = 0.9F;
    def.maxHp = 3; // matches kSliceEnemyHp -- one C04 cut (atk 5 + 3 = 8) overkills it.
    return def;
}

bool HasEvent(Simulation &sim, SimEventType type, std::uint32_t entityId,
             const std::string &name) {
    for (const SimEvent &e : sim.DrainEvents()) {
        if (e.type == type && e.entityId == entityId && e.name == name) {
            return true;
        }
    }
    return false;
}
} // namespace

// ===================================================================
// C04 -- charge-and-cut melee hero. characters.json c04: atk=5, skill_cd=11,
// in_skill_time=5 (the window value is carried but C04 has no auto-end timer --
// see CharSkillC04.hpp fabrication_flags (a)).
// ===================================================================

// Ground truth for the cut-damage formula at the BRAIN level (no sim plumbing):
// atk(5) + kCutBonusNormal(3) = 8, or atk(5) + kCutBonusStrengthened(7) = 12 when
// skill_strengthen is on. Mirrors SkillPipelineTest's brain-level ground-truth style.
TEST(CharSkillSimTest, C04CutDamageFormulaGroundTruth) {
    Game::CharSkillC04 c(11.0F);
    ASSERT_TRUE(c.TryActivateSkill());
    Game::CharSkillC04::AtkDecision d = c.RoleAtk(/*pressDown=*/true, /*standingOnItem=*/false,
                                                  /*atk=*/5, /*skillStrengthen=*/false);
    EXPECT_TRUE(d.doCut);
    EXPECT_EQ(d.cutDamage, 8); // 5 + kCutBonusNormal(3)

    Game::CharSkillC04 strengthened(11.0F);
    ASSERT_TRUE(strengthened.TryActivateSkill());
    d = strengthened.RoleAtk(true, false, /*atk=*/5, /*skillStrengthen=*/true);
    EXPECT_TRUE(d.doCut);
    EXPECT_EQ(d.cutDamage, 12); // 5 + kCutBonusStrengthened(7)
}

// Sim integration: activating c04 (atk=5) and pressing fire in reach cuts an enemy
// for exactly 8 (5+3), replacing the normal shot (no bullet spawned, no energy
// spent), and the kill (in-window) refreshes the cooldown to ready immediately
// (SkillReady true, CooldownProgress 1.0) WITHOUT ending in_skill (C04-END: a kill
// alone never closes the ultimate -- see CharSkillAdapters.hpp).
TEST(CharSkillSimTest, C04CutKillsEnemyInReachAndRefreshesCooldownOnNonWrapKill) {
    Simulation sim(41, &g_NullWorld);
    sim.AddEnemy(WeakEnemy(), glm::vec2{30.0F, 0.0F}, /*roomId=*/0, /*seed=*/1); // in reach+arc
    Game::WeaponDef wdef{};
    wdef.bulletSpeed = 10.0F;
    wdef.atk = 1;
    sim.EquipWeapon(wdef, "Gun001", 7);
    sim.SetPlayerSkill("c04", /*skillCd=*/11.0F, /*inSkillTime=*/5.0F, /*atk=*/5);
    ASSERT_TRUE(sim.PlayerSkill()->SkillReady());

    WorldInputs cast = Idle();
    cast.skill = true;
    cast.firing = true; // same-step activate + cut (TickSkill runs before TickWeapon)
    sim.Advance(kFixedStepMs, cast);

    EXPECT_TRUE(HasEvent(sim, SimEventType::AnimTrigger, /*entityId=*/0, "hurt")) << "the cut landed";
    EXPECT_FALSE(sim.EnemyViews()[0].alive);           // 8 dmg > 3 hp -- overkill, one cut
    EXPECT_EQ(sim.Bullets().size(), 0U)                // the cut REPLACED the shot
        << "in_skill_effect suppresses the normal ranged shot on the cut press";
    EXPECT_EQ(sim.PlayerShotsLastAdvance(), 0);        // no energy spent on a cut

    EXPECT_TRUE(sim.PlayerSkill()->SkillReady())       // kill #1 of 5 -> ReflashSkillCd
        << "a non-wrap in-window kill instantly refreshes the cooldown";
    EXPECT_FLOAT_EQ(sim.PlayerStats().skillCdProgress, 1.0F);
    EXPECT_TRUE(sim.PlayerSkill()->InSkill())          // C04-END: the kill alone does not close it
        << "in_skill stays true until a second skill-button press (toggle-off)";
}

// The 5th in-window kill wraps the combo back to 0 (and, either way, the cut window
// always closes) -- ground truth at the BRAIN level (CharSkillC04::KillSomeOne).
// KillSomeOne closes in_skill_effect on EVERY kill (win or wrap alike), so this test
// reopens the window between kills the only way the public API allows
// (EndSkill()+a full Tick()+TryActivateSkill()) -- a brain-level stand-in for "the
// player pressed the skill button again" once each per kill. NOTE: this_skill_time
// is always exactly skill_cd at the moment a kill fires (TryActivateSkill's own
// skill_ready gate guarantees it, and Tick never lets it exceed skill_cd), so
// KillSomeOne's ReflashSkillCd is a same-value write there -- Combo() wrapping to 0
// is the one genuinely distinguishing, testable signal for the 5th kill.
TEST(CharSkillSimTest, C04FifthKillWrapsComboBackToZero) {
    Game::CharSkillC04 c(11.0F);
    for (int i = 1; i <= Game::CharSkillC04::kComboWrap; ++i) {
        ASSERT_TRUE(c.SkillReady());
        ASSERT_TRUE(c.TryActivateSkill());
        ASSERT_TRUE(c.InSkillEffect());
        EXPECT_TRUE(c.KillSomeOne());
        EXPECT_FALSE(c.InSkillEffect()) << "KillSomeOne always closes the cut window";
        if (i < Game::CharSkillC04::kComboWrap) {
            EXPECT_EQ(c.Combo(), i);
        } else {
            EXPECT_EQ(c.Combo(), 0) << "the 5th kill wraps the combo back to 0";
        }
        c.EndSkill();     // the only public way to clear in_skill for the next cast
        c.Tick(11000.0F); // a full cooldown, so the next TryActivateSkill's gate passes
    }
}

// ===================================================================
// C08 -- shield hero. characters.json c08: atk=4, skill_cd=12, shield starts at
// kInitialShieldValue=100 (CharSkillC08.hpp).
// ===================================================================

TEST(CharSkillSimTest, C08ShieldAbsorbsHitsAndDoesNotTouchHpOrArmor) {
    Simulation sim(9, &g_NullWorld);
    Game::CombatStats player;
    player.hp = 1; // c08's own max_hp (characters.json) -- one unabsorbed hit would kill it
    player.maxHp = 1;
    player.armor = 8;
    player.maxArmor = 8;
    sim.SetPlayerStats(player);
    sim.SetPlayerSkill("c08", /*skillCd=*/12.0F, /*inSkillTime=*/5.0F, /*atk=*/4);

    WorldInputs cast = Idle();
    cast.skill = true;
    sim.Advance(kFixedStepMs, cast); // activate the shield
    ASSERT_TRUE(sim.PlayerSkill()->InSkill());

    Game::EnemyDef def{};
    def.shootCd = 0.1F;
    def.scoutRate = 99.0F; // hold still, keep firing at the player
    def.friction = 0.9F;
    sim.AddEnemy(def, glm::vec2{30.0F, 0.0F}, /*roomId=*/0, /*seed=*/2);

    WorldInputs in = Idle();
    for (int i = 0; i < 200 && sim.PlayerStats().hp == 1 && sim.PlayerStats().armor == 8; ++i) {
        sim.Advance(kFixedStepMs, in);
    }
    EXPECT_EQ(sim.PlayerStats().hp, 1);      // untouched -- the shield ate every hit
    EXPECT_EQ(sim.PlayerStats().armor, 8);   // untouched
    EXPECT_LT(sim.PlayerSkill()->InSkill() ? 0 : 1, 1); // still in_skill (sanity: no auto-end)

    // Toggle the shield off (C08-END), then the SAME enemy bullet volume must reach
    // the player normally.
    WorldInputs endCast = Idle();
    endCast.skill = true;
    sim.Advance(kFixedStepMs, endCast);
    EXPECT_FALSE(sim.PlayerSkill()->InSkill());

    for (int i = 0; i < 200 && sim.PlayerStats().hp > 0; ++i) {
        sim.Advance(kFixedStepMs, Idle());
    }
    EXPECT_EQ(sim.PlayerStats().hp, 0) << "after EndSkill, the normal armor->hp pipeline runs";
}

// ===================================================================
// C10 / C13 -- structurally-identical armor-regen passives. characters.json:
// c10 atk=5 skill_cd=12 in_skill_time=5; c13 atk=5 skill_cd=12 in_skill_time=5.
// Every 3rd qualifying player-bullet hit while in_skill restores exactly 1 armor
// (kArmorRestoreEveryNthHit / kHurtCountToRestoreArmor = 3, kArmorRestoreAmount /
// kArmorRestoredPerCycle = 1); the in_skill_time window auto-ends via Tick.
// ===================================================================

namespace {
void RunArmorRegenCase(const std::string &charId) {
    Simulation sim(charId == "c10" ? 1010 : 1013, &g_NullWorld);
    Game::CombatStats player;
    player.hp = 10;
    player.maxHp = 10;
    player.armor = 0; // start at 0 so a +1 restore is directly observable
    player.maxArmor = 5;
    sim.SetPlayerStats(player);
    sim.AddEnemy(WeakEnemy(), glm::vec2{200.0F, 0.0F}, /*roomId=*/0, /*seed=*/3); // far: survives 3 hits
    Game::WeaponDef wdef{};
    wdef.bulletSpeed = 40.0F; // fast: lands within a handful of steps
    wdef.atk = 1;             // low: several hits needed before the enemy (hp 3) dies
    sim.EquipWeapon(wdef, "Gun001", 77);
    sim.SetPlayerSkill(charId, /*skillCd=*/12.0F, /*inSkillTime=*/5.0F, /*atk=*/5);

    WorldInputs cast = Idle();
    cast.skill = true;
    cast.firing = true;
    cast.aimDir = glm::vec2{1.0F, 0.0F};
    sim.Advance(kFixedStepMs, cast);
    ASSERT_TRUE(sim.PlayerSkill()->InSkill());

    WorldInputs fire = Idle();
    fire.firing = true;
    fire.aimDir = glm::vec2{1.0F, 0.0F};
    for (int i = 0; i < 3000 && sim.PlayerStats().armor == 0; ++i) {
        sim.Advance(kFixedStepMs, fire);
    }
    EXPECT_EQ(sim.PlayerStats().armor, 1) << charId << ": the 3rd landed hit restores exactly 1 armor";

    // Keep firing: a 4th qualifying hit must NOT restore a 2nd point (the counter
    // just reset to 0, so hits 4-5 are silent and only the 6th would fire again).
    for (int i = 0; i < 5; ++i) {
        sim.Advance(kFixedStepMs, fire);
    }
    EXPECT_EQ(sim.PlayerStats().armor, 1) << charId << ": no armor restore until the NEXT 3-hit cycle";
}
} // namespace

TEST(CharSkillSimTest, C10ThirdQualifyingHitRestoresOneArmor) { RunArmorRegenCase("c10"); }
TEST(CharSkillSimTest, C13ThirdQualifyingHitRestoresOneArmor) { RunArmorRegenCase("c13"); }

// in_skill_time (5.0s) auto-ends the window via Tick (InSkill true -> false), and
// GameScene's existing edge-detected "Skill: <char> ended" trace (edge on
// ICharSkill::InSkill(), P3.3.a) fires off that same transition -- no new
// SimEventType is needed (see the P3.3.b report).
TEST(CharSkillSimTest, C10AndC13AutoEndAfterInSkillTimeElapses) {
    for (const std::string &charId : {std::string("c10"), std::string("c13")}) {
        Simulation sim(5, &g_NullWorld);
        sim.SetPlayerSkill(charId, /*skillCd=*/12.0F, /*inSkillTime=*/5.0F, /*atk=*/5);
        WorldInputs cast = Idle();
        cast.skill = true;
        sim.Advance(kFixedStepMs, cast);
        ASSERT_TRUE(sim.PlayerSkill()->InSkill()) << charId;

        // 5.0s / 0.02s = 250 steps to exhaust in_skill_time.
        for (int i = 0; i < 249; ++i) {
            sim.Advance(kFixedStepMs, Idle());
            ASSERT_TRUE(sim.PlayerSkill()->InSkill()) << charId << " step " << i;
        }
        sim.Advance(kFixedStepMs, Idle()); // the 250th step crosses 0 -> auto-end
        EXPECT_FALSE(sim.PlayerSkill()->InSkill()) << charId << ": in_skill_time auto-ended it";
        EXPECT_FALSE(sim.PlayerSkill()->SkillReady()) << charId << ": EndSkill spent the cooldown";
    }
}

// ===================================================================
// Golden: c01's faithful mirror behaviour (the P3.3.b reference adapter) is
// untouched by the C04/C08/C10/C13 changes -- same assertions as
// CharSkillDispatchTest::C01MirrorsShotAfterDelay, kept here as a local regression
// anchor for this file's own test binary.
// ===================================================================

TEST(CharSkillSimTest, C01StillMirrorsUnaffectedByOtherAdapterChanges) {
    Simulation sim(55, &g_NullWorld);
    Game::WeaponDef def{};
    def.weaponSpeed = 1.0F;
    def.bulletSpeed = 10.0F;
    def.atk = 4;
    def.deviation = 0;
    sim.EquipWeapon(def, "Gun001", 808);
    sim.SetPlayerSkill("c01", 5.0F, 5.0F);

    WorldInputs fire = Idle();
    fire.firing = true;
    fire.skill = true;
    sim.Advance(kFixedStepMs, fire);
    EXPECT_TRUE(sim.PlayerSkill()->InSkill());
    EXPECT_EQ(sim.Bullets().size(), 1U);
    EXPECT_EQ(sim.PlayerShotsLastAdvance(), 1);
}

// NOLINTEND(readability-magic-numbers)
