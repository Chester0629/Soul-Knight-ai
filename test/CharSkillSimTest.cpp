#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "combat/CharSkillC04.hpp"
#include "data/GameData.hpp"
#include "sim/CharSkillAdapters.hpp"
#include "sim/SimConfig.hpp"
#include "sim/SimEvent.hpp"
#include "sim/Simulation.hpp"
#include "sim/WorldCollision.hpp"
#include "sim/WorldInputs.hpp"

using Game::Sim::CharSkillC04Adapter;
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

// item 2 (R19 kill-attribution fix): WeakEnemy() with an overridden maxHp, for
// tests that need to control EXACTLY which DoT tick crosses hp<=0.
Game::EnemyDef EnemyWithHp(int hp) {
    Game::EnemyDef def = WeakEnemy();
    def.maxHp = hp;
    return def;
}

// item 2 (R20 lock-attribution fix): EnemyWithHp(), also pinned immobile
// (kinematic, EnemyController::ComputeVelocity's turret branch -- see
// EnemyController.cpp) so a multi-step scenario (a bullet travelling a fixed,
// precomputed distance over several Advance() calls) is not thrown off by the
// enemy's own AI steering toward the player in the meantime.
Game::EnemyDef StationaryEnemyWithHp(int hp) {
    Game::EnemyDef def = EnemyWithHp(hp);
    def.kinematic = 1;
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
// item 4 (R18/Codex block): the enemy must survive PAST hit 6 so hits 4/5 (no
// extra armor -- the counter just reset) and hit 6 (a 2nd full 3-hit cycle -> a
// 2nd armor point) are all actually OBSERVED landed hits, not skipped because the
// target died early. WeakEnemy's hp=3 died on/around hit 3, which is exactly why
// the old version of this test never really exercised hits 4-6 -- it just counted
// Advance() calls and hoped. This version counts real "hurt" AnimTrigger events
// for entityId 0 and asserts armor immediately after EACH landed hit.
void RunArmorRegenCase(const std::string &charId) {
    Simulation sim(charId == "c10" ? 1010 : 1013, &g_NullWorld);
    Game::CombatStats player;
    player.hp = 10;
    player.maxHp = 10;
    player.armor = 0; // start at 0 so a +1 restore is directly observable
    player.maxArmor = 5;
    sim.SetPlayerStats(player);
    Game::EnemyDef durable = WeakEnemy();
    durable.maxHp = 50; // item 4: survive well past the 6th landed hit (was 3 -> died at hit 3).
    sim.AddEnemy(durable, glm::vec2{200.0F, 0.0F}, /*roomId=*/0, /*seed=*/3); // far: many hits needed to travel there.
    Game::WeaponDef wdef{};
    wdef.bulletSpeed = 40.0F; // fast: lands within a handful of steps
    wdef.atk = 1;             // low: 1 dmg/hit, needs 50 hits to kill -- plenty of headroom.
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

    int hits = 0;
    int expectedArmor = 0;
    for (int i = 0; i < 6000 && hits < 6; ++i) {
        sim.Advance(kFixedStepMs, fire);
        for (const SimEvent &e : sim.DrainEvents()) {
            if (e.type != SimEventType::AnimTrigger || e.entityId != 0U || e.name != "hurt") {
                continue;
            }
            ++hits;
            if (hits % 3 == 0) {
                ++expectedArmor; // every 3rd qualifying hit -> +1 armor (kArmorRestoreEveryNthHit=3).
            }
            EXPECT_EQ(sim.PlayerStats().armor, expectedArmor)
                << charId << ": armor immediately after landed hit #" << hits;
        }
    }
    ASSERT_EQ(hits, 6) << charId << ": expected to observe 6 landed hits within the step budget";
    // item 4: hits 4 and 5 were directly asserted above (expectedArmor stayed 1,
    // the counter having just reset after hit 3); hit 6 closes a 2nd full 3-hit
    // cycle and grants a 2nd armor point.
    EXPECT_EQ(sim.PlayerStats().armor, 2)
        << charId << ": two full 3-hit cycles (hits 3 and 6) -> 2 armor total";
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

// ===================================================================
// item 1 (R18/Codex block): the skill button must be edge-detected ONCE per
// press, latched until release -- not re-triggered on every fixed sub-step of an
// Advance() call that holds the same WorldInputs::skill across MULTIPLE steps
// (dtMs > kFixedStepMs), and not re-triggered by a shell that simply forwards a
// raw held-key level across consecutive Advance() calls (GameScene does exactly
// that -- WorldInputs::skill is IsKeyDown, not edge-detected shell-side).
// ===================================================================

TEST(CharSkillSimTest, SkillEdgeDetectedWithinOneAdvanceMultipleFixedSubSteps) {
    Simulation sim(71, &g_NullWorld);
    sim.SetPlayerSkill("c08", /*skillCd=*/12.0F, /*inSkillTime=*/5.0F, /*atk=*/4);

    WorldInputs held = Idle();
    held.skill = true;
    // 2 fixed sub-steps (40ms / 20ms), the SAME m_Input held across both.
    sim.Advance(2.0F * kFixedStepMs, held);
    EXPECT_TRUE(sim.PlayerSkill()->InSkill())
        << "a single held press spanning 2 fixed sub-steps of ONE Advance() must "
           "activate exactly once, not toggle itself back off on the 2nd sub-step";
}

TEST(CharSkillSimTest, SkillEdgeDetectedAcrossConsecutiveAdvanceCallsWhileHeld) {
    Simulation sim(72, &g_NullWorld);
    sim.SetPlayerSkill("c08", /*skillCd=*/12.0F, /*inSkillTime=*/5.0F, /*atk=*/4);

    WorldInputs held = Idle();
    held.skill = true;
    sim.Advance(kFixedStepMs, held); // press: activates.
    ASSERT_TRUE(sim.PlayerSkill()->InSkill());

    for (int i = 0; i < 5; ++i) {
        sim.Advance(kFixedStepMs, held); // held down across several MORE frames.
    }
    EXPECT_TRUE(sim.PlayerSkill()->InSkill())
        << "holding skill=true across several separate Advance() calls (the shell's "
           "raw held-key level, never edge-detected there) must not retrigger the "
           "toggle -- only the initial press-edge may activate";
}

TEST(CharSkillSimTest, SkillSecondDistinctPressEndsToggleTypeSkillAfterARelease) {
    Simulation sim(73, &g_NullWorld);
    sim.SetPlayerSkill("c08", /*skillCd=*/12.0F, /*inSkillTime=*/5.0F, /*atk=*/4);

    WorldInputs held = Idle();
    held.skill = true;
    sim.Advance(kFixedStepMs, held); // press 1: activate.
    ASSERT_TRUE(sim.PlayerSkill()->InSkill());

    sim.Advance(kFixedStepMs, Idle()); // release (skill=false).
    ASSERT_TRUE(sim.PlayerSkill()->InSkill()) << "a release alone must not end the skill";

    sim.Advance(kFixedStepMs, held); // press 2: a genuine, distinct press (edge again).
    EXPECT_FALSE(sim.PlayerSkill()->InSkill())
        << "a second DISTINCT press (release in between) ends a toggle-type skill";
}

// ===================================================================
// R19 fix: a press-edge occurring during an Advance() call that runs ZERO fixed
// Step()s (dtMs < kFixedStepMs, e.g. a 10ms frame with kFixedStepMs=20) must NOT be
// lost. Pre-fix, edge detection lived inside TickSkill() (i.e. only ran when Step()
// ran); m_Clock.Advance(10) returns 0 steps, so TickSkill() never saw skill=true --
// and the VERY NEXT Advance() call overwrote m_Input with a different level before
// any Step() ever got a chance to look at it, silently dropping the press. The fix
// (Simulation::Advance) latches the edge at CALL granularity into
// m_SkillPressPending, independent of whether that call runs any Step()s, so a
// later Advance() call's fixed Step() still consumes it.
// ===================================================================

TEST(CharSkillSimTest, SkillPressSurvivesAnAdvanceCallWithZeroFixedSubSteps) {
    Simulation sim(77, &g_NullWorld);
    sim.SetPlayerSkill("c04", /*skillCd=*/11.0F, /*inSkillTime=*/5.0F, /*atk=*/5);

    WorldInputs press = Idle();
    press.skill = true;
    // 10ms < kFixedStepMs(20ms): m_Clock's accumulator does not cross a fixed-step
    // boundary yet, so this call runs ZERO Step()s -- TickSkill() never executes.
    const int steps1 = sim.Advance(10.0F, press);
    ASSERT_EQ(steps1, 0) << "test assumes a 10ms dt yields zero fixed sub-steps";
    ASSERT_FALSE(sim.PlayerSkill()->InSkill())
        << "no Step() ran yet, so the skill cannot have activated yet";

    // Release the button; accumulator now crosses 20ms -> exactly 1 fixed Step()
    // runs THIS call, and that Step() must still see (and consume) the press-edge
    // latched by the PREVIOUS Advance() call, even though m_Input.skill is false by
    // the time this Step() actually executes.
    WorldInputs release = Idle();
    release.skill = false;
    const int steps2 = sim.Advance(10.0F, release);
    ASSERT_EQ(steps2, 1) << "test assumes the 2nd 10ms dt crosses the 20ms boundary";
    EXPECT_TRUE(sim.PlayerSkill()->InSkill())
        << "the press from the zero-step Advance() call must not be dropped -- it "
           "must activate on the first fixed Step() that gets a chance to run, one "
           "activation exactly";
}

TEST(CharSkillSimTest, SkillHeldFor100msActivatesExactlyOnce) {
    Simulation sim(78, &g_NullWorld);
    sim.SetPlayerSkill("c04", /*skillCd=*/11.0F, /*inSkillTime=*/5.0F, /*atk=*/5);

    WorldInputs held = Idle();
    held.skill = true;
    // 100ms / 20ms == 5 fixed sub-steps, all with the SAME held input, in ONE
    // Advance() call.
    const int steps = sim.Advance(100.0F, held);
    ASSERT_EQ(steps, 5);
    EXPECT_TRUE(sim.PlayerSkill()->InSkill())
        << "holding the button for 100ms (5 sub-steps) must activate exactly once, "
           "not toggle itself back off on a later sub-step";
}

TEST(CharSkillSimTest, SkillTwoPulsesSeparatedByAReleaseEndsToggleTypeSkill) {
    Simulation sim(79, &g_NullWorld);
    sim.SetPlayerSkill("c04", /*skillCd=*/11.0F, /*inSkillTime=*/5.0F, /*atk=*/5);

    WorldInputs press = Idle();
    press.skill = true;
    WorldInputs release = Idle();
    release.skill = false;

    sim.Advance(kFixedStepMs, press); // pulse 1: activates the toggle.
    sim.Advance(kFixedStepMs, release); // release between pulses.
    ASSERT_TRUE(sim.PlayerSkill()->InSkill())
        << "release alone (no 2nd press yet) must not end the toggle";

    sim.Advance(kFixedStepMs, press); // pulse 2: a genuine distinct press.
    EXPECT_FALSE(sim.PlayerSkill()->InSkill())
        << "two pulses separated by a release must end a toggle-type skill on the "
           "2nd pulse";
}

// ===================================================================
// item 2 (R18/Codex block, T-37): C04's toggle-close (EndSkill via a 2nd press)
// must not silently wipe a cooldown KillSomeOne already refreshed, and no cut may
// survive past the end regardless of whether a kill ever closed the window.
// ===================================================================

// Brain-level: hits/kills 1..6, asserting cooldown-progress after EACH kill AND
// after the immediately-following toggle-close (EndSkill). skill_cd = 11.0
// (characters.json c04). Every kill (win or wrap) leaves this_skill_time at
// skill_cd going in (TryActivateSkill's own skill_ready gate guarantees it) --
// the genuinely distinguishing signal per item 2 is that EndSkill() no longer
// resets it to 0 after a NON-WRAP refresh (kills 1,2,3,4,6 here), so
// re-activation needs no extra wait for those. The 5th kill is the one genuine
// exception -- FAITHFUL: the wrap does NOT call ReflashSkillCd, so its
// toggle-close DOES spend the cooldown normally and the loop fast-forwards a full
// Tick(11000ms) there before continuing, same as the older
// C04FifthKillWrapsComboBackToZero test above (which fast-forwards every
// iteration, since it predates T-37 and never relies on the no-wait behaviour).
TEST(CharSkillSimTest, C04SixKillSequenceCooldownProgressAndReactivationAfterRefresh) {
    constexpr float kSkillCd = 11.0F; // characters.json c04 skill_cd
    Game::CharSkillC04 c(kSkillCd);
    for (int i = 1; i <= 6; ++i) {
        ASSERT_TRUE(c.SkillReady()) << "kill " << i;
        ASSERT_TRUE(c.TryActivateSkill()) << "kill " << i;
        ASSERT_TRUE(c.InSkillEffect()) << "kill " << i;

        EXPECT_TRUE(c.KillSomeOne()) << "kill " << i;
        EXPECT_FALSE(c.InSkillEffect()) << "kill " << i << ": KillSomeOne always closes the window";
        const int expectedCombo =
            (i % Game::CharSkillC04::kComboWrap == 0) ? 0 : (i % Game::CharSkillC04::kComboWrap);
        EXPECT_EQ(c.Combo(), expectedCombo) << "kill " << i;
        // The 5th kill (of each 5-kill cycle) WRAPS the combo and does NOT refresh
        // (kComboWrap == 5 -> combo 0 here, i.e. i == 5, 10, ...); every other kill
        // refreshes. SkillReady() itself is trivially true right after EITHER
        // (TryActivateSkill's own gate already required it going in, and a same-
        // value ReflashSkillCd is a no-op write) -- RefreshedThisWindow() is the
        // genuinely distinguishing T-37 signal, and it drives what EndSkill does
        // next: preserve readiness after a refresh, spend it normally after a wrap.
        const bool wrapped = (expectedCombo == 0);
        EXPECT_EQ(c.RefreshedThisWindow(), !wrapped) << "kill " << i;
        EXPECT_TRUE(c.SkillReady()) << "kill " << i;
        EXPECT_FLOAT_EQ(c.CooldownRemaining(), 0.0F) << "kill " << i;

        // The toggle-close (2nd skill-button press, modeled by EndSkill()) must NOT
        // spend a just-refreshed cooldown a second time (T-37), and must leave no
        // cut able to fire (in_skill_effect/has_cut both cleared unconditionally)
        // -- but a WRAP kill (no refresh this window) still spends normally, same
        // as C04EndSkillWithoutAKillSpendsCooldownNormally below.
        c.EndSkill();
        EXPECT_EQ(c.SkillReady(), !wrapped)
            << "kill " << i << ": refresh preserved readiness (T-37), wrap spent it normally";
        EXPECT_FLOAT_EQ(c.CooldownRemaining(), wrapped ? kSkillCd : 0.0F) << "kill " << i;
        EXPECT_FALSE(c.InSkillEffect()) << "kill " << i << ": no cut can survive the end";
        EXPECT_FALSE(c.HasCut()) << "kill " << i;
        EXPECT_FALSE(c.InSkill()) << "kill " << i;

        if (wrapped) {
            c.Tick(kSkillCd * 1000.0F); // a genuine wrap DOES require a full wait -- see FifthKill...
            ASSERT_TRUE(c.SkillReady()) << "kill " << i;
        }
    }
}

// Contrast case: a toggle-close with NO kill in the window spends the cooldown
// normally (this_skill_time -> 0), exactly like the decompiled ReSetSkillReload --
// T-37's "skip the reset" carve-out applies ONLY when a kill actually refreshed.
TEST(CharSkillSimTest, C04EndSkillWithoutAKillSpendsCooldownNormally) {
    constexpr float kSkillCd = 11.0F;
    Game::CharSkillC04 c(kSkillCd);
    ASSERT_TRUE(c.TryActivateSkill());
    c.EndSkill(); // toggle-close, no kill happened.
    EXPECT_FALSE(c.SkillReady());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), kSkillCd);
    EXPECT_FALSE(c.InSkillEffect());
    EXPECT_FALSE(c.HasCut());
}

// Sim-level: activate, release, press again (toggle-close) with NO kill in between
// -- the normal ranged shot must resume afterward (no lingering cut window).
TEST(CharSkillSimTest, C04NoCutAfterToggleEndEvenWithoutAKill) {
    Simulation sim(74, &g_NullWorld);
    Game::WeaponDef wdef{};
    wdef.bulletSpeed = 10.0F;
    wdef.atk = 1;
    sim.EquipWeapon(wdef, "Gun001", 7);
    sim.SetPlayerSkill("c04", /*skillCd=*/11.0F, /*inSkillTime=*/5.0F, /*atk=*/5);

    WorldInputs cast = Idle();
    cast.skill = true;
    sim.Advance(kFixedStepMs, cast); // activate only (no fire this step).
    ASSERT_TRUE(sim.PlayerSkill()->InSkill());

    sim.Advance(kFixedStepMs, Idle()); // release the skill button.
    WorldInputs endCast = Idle();
    endCast.skill = true;
    sim.Advance(kFixedStepMs, endCast); // 2nd press: toggle-close, no kill happened.
    ASSERT_FALSE(sim.PlayerSkill()->InSkill());

    WorldInputs fire = Idle();
    fire.firing = true;
    sim.Advance(kFixedStepMs, fire); // fire AFTER the end -- must be a normal shot.
    EXPECT_EQ(sim.Bullets().size(), 1U)
        << "no cut can survive the end -- normal ranged fire resumed";
}

// ===================================================================
// item 5 (R18/Codex block): EntityState::lastHitByPlayer is the LAST damage
// source, not "ever hit by the player" -- and a forced clear (KillAllHostiles /
// SK_FORCE_KILL) must never be attributed to the player. Observed indirectly via
// C04's Combo()/InSkillEffect() (CharSkillBase::Brain(), a test-only backdoor --
// see ICharSkill.hpp), since ICharSkill has no OnEnemyKilled call-count of its
// own: KillSomeOne only advances combo / closes the window when Simulation::Cull
// actually calls ICharSkill::OnEnemyKilled, which happens iff the dying entity's
// lastHitByPlayer was true. The POSITIVE case (a genuine player cut-kill DOES call
// OnEnemyKilled -> refreshes the cooldown) is already covered by
// C04CutKillsEnemyInReachAndRefreshesCooldownOnNonWrapKill above; these add the
// NEGATIVE cases.
// ===================================================================

TEST(CharSkillSimTest, KillAttributionForcedKillDoesNotCallOnEnemyKilled) {
    Simulation sim(81, &g_NullWorld);
    sim.AddEnemy(WeakEnemy(), glm::vec2{500.0F, 0.0F}, /*roomId=*/0, /*seed=*/1);
    sim.SetPlayerSkill("c04", /*skillCd=*/11.0F, /*inSkillTime=*/5.0F, /*atk=*/5);

    WorldInputs cast = Idle();
    cast.skill = true;
    sim.Advance(kFixedStepMs, cast); // open the cut window.
    const auto *adapter = dynamic_cast<const CharSkillC04Adapter *>(sim.PlayerSkill());
    ASSERT_NE(adapter, nullptr);
    ASSERT_TRUE(adapter->Brain().InSkillEffect());

    sim.KillAllHostiles(); // SK_FORCE_KILL test hook -- NOT a player action.
    sim.Advance(kFixedStepMs, Idle()); // Cull() processes the forced death this step.
    EXPECT_FALSE(sim.EnemyViews()[0].alive);
    EXPECT_EQ(adapter->Brain().Combo(), 0)
        << "KillAllHostiles/SK_FORCE_KILL must not call ICharSkill::OnEnemyKilled";
    EXPECT_TRUE(adapter->Brain().InSkillEffect())
        << "a forced kill must not close the cut window either (KillSomeOne never fires)";
}

TEST(CharSkillSimTest, KillAttributionNonPlayerBuffDotDoesNotCallOnEnemyKilled) {
    Simulation sim(82, &g_NullWorld);
    sim.AddEnemy(WeakEnemy(), glm::vec2{500.0F, 0.0F}, /*roomId=*/0, /*seed=*/1);
    sim.SetPlayerSkill("c04", /*skillCd=*/11.0F, /*inSkillTime=*/5.0F, /*atk=*/5);

    WorldInputs cast = Idle();
    cast.skill = true;
    sim.Advance(kFixedStepMs, cast);
    const auto *adapter = dynamic_cast<const CharSkillC04Adapter *>(sim.PlayerSkill());
    ASSERT_NE(adapter, nullptr);
    ASSERT_TRUE(adapter->Brain().InSkillEffect());

    Game::Combat::BuffParams fire;
    fire.kind = Game::Combat::BuffKind::Fire;
    fire.durationSec = 1.0F;
    fire.tickSec = 0.02F; // one fixed step.
    fire.tickDamage = 50; // overkill on the first tick (WeakEnemy hp=3).
    sim.ApplyBuff(/*entityId=*/0, fire, /*fromPlayer=*/false); // NOT player-sourced.

    sim.Advance(kFixedStepMs, Idle()); // the DoT tick kills the enemy this step.
    EXPECT_FALSE(sim.EnemyViews()[0].alive);
    EXPECT_EQ(adapter->Brain().Combo(), 0)
        << "a non-player-sourced buff DoT kill must NOT call OnEnemyKilled";
    EXPECT_TRUE(adapter->Brain().InSkillEffect())
        << "the cut window is untouched by a kill never attributed to the player";
}

TEST(CharSkillSimTest, KillAttributionPlayerSourcedBuffDotCallsOnEnemyKilled) {
    Simulation sim(83, &g_NullWorld);
    sim.AddEnemy(WeakEnemy(), glm::vec2{500.0F, 0.0F}, /*roomId=*/0, /*seed=*/1);
    sim.SetPlayerSkill("c04", /*skillCd=*/11.0F, /*inSkillTime=*/5.0F, /*atk=*/5);

    WorldInputs cast = Idle();
    cast.skill = true;
    sim.Advance(kFixedStepMs, cast);
    const auto *adapter = dynamic_cast<const CharSkillC04Adapter *>(sim.PlayerSkill());
    ASSERT_NE(adapter, nullptr);
    ASSERT_TRUE(adapter->Brain().InSkillEffect());

    Game::Combat::BuffParams fire;
    fire.kind = Game::Combat::BuffKind::Fire;
    fire.durationSec = 1.0F;
    fire.tickSec = 0.02F;
    fire.tickDamage = 50;
    sim.ApplyBuff(/*entityId=*/0, fire, /*fromPlayer=*/true); // player-sourced (e.g. a bullet's buff).

    sim.Advance(kFixedStepMs, Idle());
    EXPECT_FALSE(sim.EnemyViews()[0].alive);
    EXPECT_EQ(adapter->Brain().Combo(), 1) << "a player-sourced buff DoT kill DOES call OnEnemyKilled";
    EXPECT_FALSE(adapter->Brain().InSkillEffect()) << "KillSomeOne closes the cut window on a real kill";
}

// ===================================================================
// item 2 (R19 kill-attribution fix): TWO DoT sources ticking the SAME step must be
// attributed to whichever one ACTUALLY crosses hp<=0, not "was any contributing
// source the player" (the R18 merged-TickResult bug -- see Combat::BuffList::
// TickResult and Simulation::TickBuffs). Documented rule: the non-player portion
// is applied FIRST, then the player portion (skipped if the entity is already
// dead from the 1st).
// ===================================================================

// Enemy hp=1: the non-player poison portion (applied first) alone kills it, so the
// player fire portion is never applied -- OnEnemyKilled must NOT fire.
TEST(CharSkillSimTest, KillAttributionMixedDotsDyingOnNonPlayerTickDoesNotCallOnEnemyKilled) {
    Simulation sim(84, &g_NullWorld);
    sim.AddEnemy(EnemyWithHp(1), glm::vec2{500.0F, 0.0F}, /*roomId=*/0, /*seed=*/1);
    sim.SetPlayerSkill("c04", /*skillCd=*/11.0F, /*inSkillTime=*/5.0F, /*atk=*/5);

    WorldInputs cast = Idle();
    cast.skill = true;
    sim.Advance(kFixedStepMs, cast);
    const auto *adapter = dynamic_cast<const CharSkillC04Adapter *>(sim.PlayerSkill());
    ASSERT_NE(adapter, nullptr);
    ASSERT_TRUE(adapter->Brain().InSkillEffect());

    Game::Combat::BuffParams fire; // player-sourced.
    fire.kind = Game::Combat::BuffKind::Fire;
    fire.durationSec = 1.0F;
    fire.tickSec = 0.02F; // one fixed step.
    fire.tickDamage = 1;
    sim.ApplyBuff(/*entityId=*/0, fire, /*fromPlayer=*/true);

    Game::Combat::BuffParams poison; // NOT player-sourced.
    poison.kind = Game::Combat::BuffKind::Poison;
    poison.durationSec = 1.0F;
    poison.tickSec = 0.02F;
    poison.tickDamage = 1;
    sim.ApplyBuff(/*entityId=*/0, poison, /*fromPlayer=*/false);

    sim.Advance(kFixedStepMs, Idle()); // both ticks fire this step; hp=1 dies on poison.
    EXPECT_FALSE(sim.EnemyViews()[0].alive);
    EXPECT_EQ(adapter->Brain().Combo(), 0)
        << "the enemy died from the non-player poison tick (applied first, hp=1) -- "
           "the player fire tick was never applied, so this must NOT be attributed "
           "to the player";
    EXPECT_TRUE(adapter->Brain().InSkillEffect())
        << "the cut window is untouched by a kill never attributed to the player";
}

// Enemy hp=2: the non-player poison portion (applied first) only brings it to 1,
// so the player fire portion (applied second) is the one that actually kills it --
// OnEnemyKilled MUST fire.
TEST(CharSkillSimTest, KillAttributionMixedDotsDyingOnPlayerTickCallsOnEnemyKilled) {
    Simulation sim(85, &g_NullWorld);
    sim.AddEnemy(EnemyWithHp(2), glm::vec2{500.0F, 0.0F}, /*roomId=*/0, /*seed=*/1);
    sim.SetPlayerSkill("c04", /*skillCd=*/11.0F, /*inSkillTime=*/5.0F, /*atk=*/5);

    WorldInputs cast = Idle();
    cast.skill = true;
    sim.Advance(kFixedStepMs, cast);
    const auto *adapter = dynamic_cast<const CharSkillC04Adapter *>(sim.PlayerSkill());
    ASSERT_NE(adapter, nullptr);
    ASSERT_TRUE(adapter->Brain().InSkillEffect());

    Game::Combat::BuffParams fire; // player-sourced.
    fire.kind = Game::Combat::BuffKind::Fire;
    fire.durationSec = 1.0F;
    fire.tickSec = 0.02F;
    fire.tickDamage = 1;
    sim.ApplyBuff(/*entityId=*/0, fire, /*fromPlayer=*/true);

    Game::Combat::BuffParams poison; // NOT player-sourced.
    poison.kind = Game::Combat::BuffKind::Poison;
    poison.durationSec = 1.0F;
    poison.tickSec = 0.02F;
    poison.tickDamage = 1;
    sim.ApplyBuff(/*entityId=*/0, poison, /*fromPlayer=*/false);

    sim.Advance(kFixedStepMs, Idle()); // both ticks fire this step; hp=2 dies on fire.
    EXPECT_FALSE(sim.EnemyViews()[0].alive);
    EXPECT_EQ(adapter->Brain().Combo(), 1)
        << "the poison tick (applied first) only brought hp to 1 -- the fire tick "
           "(applied second) is the one that actually crossed hp<=0, so this MUST "
           "be attributed to the player";
    EXPECT_FALSE(adapter->Brain().InSkillEffect())
        << "KillSomeOne closes the cut window on a real kill";
}

// ===================================================================
// item 2 (R20 lock-attribution fix): death attribution must be locked by the FIRST
// application that leaves hp<=0, whatever its source -- Simulation::Step's fixed
// order (TickWeapon's melee cut -> TickBuffs' DoT -> ResolveHits' bullets -> Cull)
// must not let a LATER hit in the same step steal or erase the attribution a
// STILL-EARLIER hit already earned. See EntityState::deathLocked /
// EntityState::SetDeathSource (include/sim/EntityState.hpp).
// ===================================================================

// A non-player DoT tick (TickBuffs, early in Step()) kills the enemy; a player
// bullet -- already in flight from an earlier Advance() call -- lands LATER in
// the very same step (ResolveHits, after TickBuffs). Pre-fix, ResolveHits gates
// only on the `dead` FLAG (not yet set -- Cull hasn't run this step), so the
// bullet's SetDeathSource(PlayerBullet) would silently steal the attribution back
// from Other. Post-fix it must stay locked at Other -> OnEnemyKilled must NOT fire.
TEST(CharSkillSimTest, KillAttributionNonPlayerDotLocksOutALaterSameStepPlayerBullet) {
    Simulation sim(90, &g_NullWorld);
    // Stationary, hp=1 so a single tickDamage=1 poison tick is exactly lethal.
    // BulletDef::bulletSpeed is in DATA units, scaled to px/fixed-step by
    // Sim::kDataSpeedToPxPerSec(15) * kFixedStepSeconds(0.02) -- see
    // BrainFactory::MakeWeapon -- so bulletSpeed=100 moves 100*15*0.02=30px per
    // fixed step. Placed at x=115 so the bullet lands EXACTLY on it on the 4th
    // IntegrateBullets call (30, 60, 90, 120), never sooner (the radius-sum
    // overlap gate is kBulletRadius(4)+kEnemyBodyRadius(16)=20px, and
    // 115-90=25 > 20, so step 3 is still a clean miss; 120-115=5 <= 20 lands).
    sim.AddEnemy(StationaryEnemyWithHp(1), glm::vec2{115.0F, 0.0F}, /*roomId=*/0, /*seed=*/1);
    Game::WeaponDef wdef{};
    wdef.bulletSpeed = 100.0F;
    wdef.atk = 1; // irrelevant -- the DoT is what kills it.
    sim.EquipWeapon(wdef, "Gun001", 7);
    sim.SetPlayerSkill("c04", /*skillCd=*/11.0F, /*inSkillTime=*/5.0F, /*atk=*/5);
    const auto *adapter = dynamic_cast<const CharSkillC04Adapter *>(sim.PlayerSkill());
    ASSERT_NE(adapter, nullptr);

    // Step 1: fire the bullet BEFORE the skill window opens (in_skill_effect==false
    // here, so this press is a plain shot -- C04 only hijacks "firing" into a cut
    // once the window is up). Bullet now at x=30, still far from the enemy.
    WorldInputs fire = Idle();
    fire.firing = true;
    sim.Advance(kFixedStepMs, fire);
    ASSERT_EQ(sim.Bullets().size(), 1U) << "the shot fired (no skill window yet to suppress it)";
    ASSERT_TRUE(sim.EnemyViews()[0].alive);

    // Step 2: open the cut window (idle otherwise -- the bullet keeps flying via
    // IntegrateBullets regardless of `firing`). Bullet now at x=60.
    WorldInputs cast = Idle();
    cast.skill = true;
    sim.Advance(kFixedStepMs, cast);
    ASSERT_TRUE(adapter->Brain().InSkillEffect()) << "the cut window is open";

    // Step 3: idle. Bullet now at x=90 (still a clean miss, see the setup comment).
    sim.Advance(kFixedStepMs, Idle());
    ASSERT_TRUE(sim.EnemyViews()[0].alive) << "the bullet has not reached the enemy yet";

    // Arm the lethal non-player DoT right before the landing step: it ticks in
    // TickBuffs, BEFORE this same step's ResolveHits moves the bullet to x=120 and
    // resolves the (now-redundant, but still gate-passing) hit.
    Game::Combat::BuffParams poison; // NOT player-sourced.
    poison.kind = Game::Combat::BuffKind::Poison;
    poison.durationSec = 1.0F;
    poison.tickSec = 0.02F; // ticks on the very next Advance() (one fixed step).
    poison.tickDamage = 1;  // exactly lethal (hp=1).
    sim.ApplyBuff(/*entityId=*/0, poison, /*fromPlayer=*/false);

    sim.Advance(kFixedStepMs, Idle()); // Step 4: DoT tick (lethal, locks Other) -> bullet lands -> Cull.

    EXPECT_FALSE(sim.EnemyViews()[0].alive);
    EXPECT_EQ(adapter->Brain().Combo(), 0)
        << "the non-player poison tick (TickBuffs) killed the enemy BEFORE the "
           "player bullet landed (ResolveHits, later the same step) -- the "
           "Other attribution must stay locked, so OnEnemyKilled must NOT fire";
    EXPECT_TRUE(adapter->Brain().InSkillEffect())
        << "the cut window is untouched by a kill never attributed to the player";
}

// A player melee cut (TickWeapon, early in Step()) kills the enemy; a non-player
// DoT tick lands LATER in the very same step (TickBuffs, after TickWeapon).
// Pre-fix, TickBuffs gates only on `!st.dead` (not yet set -- Cull hasn't run this
// step), so the DoT's SetDeathSource(Other) would silently steal the attribution
// away from PlayerCut. Post-fix it must stay locked at PlayerCut -> OnEnemyKilled
// DOES fire (exactly once, for the real kill).
TEST(CharSkillSimTest, KillAttributionPlayerCutLocksOutALaterSameStepNonPlayerDot) {
    Simulation sim(91, &g_NullWorld);
    sim.AddEnemy(WeakEnemy(), glm::vec2{30.0F, 0.0F}, /*roomId=*/0, /*seed=*/1); // in cut reach+arc.
    sim.SetPlayerSkill("c04", /*skillCd=*/11.0F, /*inSkillTime=*/5.0F, /*atk=*/5);
    const auto *adapter = dynamic_cast<const CharSkillC04Adapter *>(sim.PlayerSkill());
    ASSERT_NE(adapter, nullptr);

    // A non-player DoT tick queued to fire THIS step, in TickBuffs -- AFTER
    // TickWeapon's cut (Step() order: TickWeapon -> ... -> TickBuffs).
    Game::Combat::BuffParams poison; // NOT player-sourced.
    poison.kind = Game::Combat::BuffKind::Poison;
    poison.durationSec = 1.0F;
    poison.tickSec = 0.02F; // ticks on the very next Advance() (one fixed step).
    poison.tickDamage = 50; // magnitude is irrelevant -- the cut already killed it.
    sim.ApplyBuff(/*entityId=*/0, poison, /*fromPlayer=*/false);

    WorldInputs cast = Idle();
    cast.skill = true;
    cast.firing = true; // same-step activate + cut (TickSkill runs before TickWeapon).
    sim.Advance(kFixedStepMs, cast);

    EXPECT_FALSE(sim.EnemyViews()[0].alive); // 8 dmg cut > 3 hp -- overkill.
    EXPECT_EQ(adapter->Brain().Combo(), 1)
        << "the player cut (TickWeapon) killed the enemy BEFORE the non-player "
           "poison tick (TickBuffs, later the same step) -- the PlayerCut "
           "attribution must stay locked, so OnEnemyKilled DOES fire";
    EXPECT_FALSE(adapter->Brain().InSkillEffect())
        << "KillSomeOne closes the cut window on the real (locked) kill";
}

// ===================================================================
// item 2 (R19/R20 kill-attribution fix): a forced death (KillAllHostiles) must set
// an IMMUTABLE EntityState::DeathSource::Forced so a player hit that lands BEFORE
// the next Cull() (KillAllHostiles zeroes hp but does not set `dead` -- only
// Cull() does) cannot re-attribute the death to the player. Regression covers:
// C04's cut window is open (InSkill/InSkillEffect true) and a player bullet is
// STILL IN FLIGHT (fired several steps earlier, has not yet reached the enemy)
// when the forced kill happens; the bullet then lands on its own schedule,
// arriving in the SAME step Cull() processes the forced death -- pre-fix,
// ResolveHits gates only on the `dead` FLAG (still false at that point in the
// step), so that landing hit would call SetDeathSource(PlayerBullet) and
// overwrite deathSource back to a player source before Cull reads it.
// ===================================================================

TEST(CharSkillSimTest, KillAttributionForcedDeathLocksOutAnInFlightPlayerBullet) {
    Simulation sim(86, &g_NullWorld);
    // Stationary, far enough (x=205) that the bullet needs several steps to
    // arrive, well outside C04's ~48px cut reach so the "latch has_cut" press
    // below cannot itself touch it. BulletDef::bulletSpeed is in DATA units,
    // scaled to px/fixed-step by Sim::kDataSpeedToPxPerSec(15) *
    // kFixedStepSeconds(0.02) -- see BrainFactory::MakeWeapon -- so
    // bulletSpeed=100 moves 100*15*0.02=30px per fixed step: the real shot
    // (below) reaches x=180 after 5 idle steps (30*6=180, distance 25 > the
    // kBulletRadius(4)+kEnemyBodyRadius(16)=20px overlap gate -- a clean miss),
    // then x=210 one step later (distance 5 <= 20 -- lands).
    sim.AddEnemy(StationaryEnemyWithHp(1000), glm::vec2{205.0F, 0.0F}, /*roomId=*/0, /*seed=*/1);
    Game::WeaponDef wdef{};
    wdef.bulletSpeed = 100.0F;
    wdef.atk = 1;               // 1 dmg: nowhere near lethal on a 1000-hp enemy.
    sim.EquipWeapon(wdef, "Gun001", 7);
    sim.SetPlayerSkill("c04", /*skillCd=*/11.0F, /*inSkillTime=*/5.0F, /*atk=*/5);
    const auto *adapter = dynamic_cast<const CharSkillC04Adapter *>(sim.PlayerSkill());
    ASSERT_NE(adapter, nullptr);

    // Activate C04 FIRST and confirm the cut window is actually open.
    WorldInputs cast = Idle();
    cast.skill = true;
    sim.Advance(kFixedStepMs, cast);
    ASSERT_TRUE(adapter->Brain().InSkill());
    ASSERT_TRUE(adapter->Brain().InSkillEffect());

    // While in_skill_effect is up, the FIRST press is a cut attempt (has_cut was
    // false), not a shot -- it whiffs (the enemy is out of cut reach) but latches
    // has_cut=true, so the NEXT press falls through to a normal weapon shot
    // (RoleAtk: `if (has_cut != 0) hand.SetAttack(value)`). This is the only way to
    // get a real in-flight player bullet while the window stays open.
    WorldInputs press = Idle();
    press.firing = true;
    sim.Advance(kFixedStepMs, press); // whiff cut, latches has_cut.
    ASSERT_EQ(sim.Bullets().size(), 0U) << "the first press in-window is a cut attempt, not a shot";

    sim.Advance(kFixedStepMs, press); // has_cut is set -- this press fires the gun.
    ASSERT_EQ(sim.Bullets().size(), 1U) << "the second press falls through to a normal shot";

    // Let the bullet fly (still in flight, nowhere near the enemy) while
    // in_skill_effect stays open the whole time.
    for (int i = 0; i < 5; ++i) {
        sim.Advance(kFixedStepMs, Idle());
    }
    ASSERT_TRUE(sim.EnemyViews()[0].alive) << "the bullet has not reached the enemy yet";
    ASSERT_TRUE(adapter->Brain().InSkillEffect()) << "the cut window is still open";

    sim.KillAllHostiles(); // forces hp=0 NOW; entity is not yet `dead` (Cull hasn't run).
    // The in-flight bullet lands THIS step (IntegrateBullets/ResolveHits run before
    // Cull in Step()), then Cull() processes the forced death in the SAME
    // Advance() call -- the exact race the pre-fix code lost.
    sim.Advance(kFixedStepMs, Idle());

    EXPECT_FALSE(sim.EnemyViews()[0].alive);
    EXPECT_EQ(adapter->Brain().Combo(), 0)
        << "KillAllHostiles must lock out the in-flight player bullet landing in "
           "the SAME step as Cull -- OnEnemyKilled must not fire for a "
           "forced kill";
}

// Directly exercises the IMMUTABILITY of DeathSource::Forced (not just the
// damage-already-applied guard the in-flight-bullet test above also trips): a player
// melee cut lands on the entity in the SAME step as Cull(), AFTER KillAllHostiles
// already forced its hp to 0 -- ApplyMeleeCut only gates on the `dead` FLAG (set
// by Cull, not yet true), not on stats.IsDead(), so pre-fix this cut would call
// EntityState::SetDeathSource(PlayerCut) and misattribute the kill.
TEST(CharSkillSimTest, KillAttributionForcedDeathLocksOutASameStepPlayerCut) {
    Simulation sim(87, &g_NullWorld);
    sim.AddEnemy(EnemyWithHp(1000), glm::vec2{20.0F, 0.0F}, /*roomId=*/0, /*seed=*/1); // in cut reach.
    sim.SetPlayerSkill("c04", /*skillCd=*/11.0F, /*inSkillTime=*/5.0F, /*atk=*/5);
    const auto *adapter = dynamic_cast<const CharSkillC04Adapter *>(sim.PlayerSkill());
    ASSERT_NE(adapter, nullptr);

    WorldInputs cast = Idle();
    cast.skill = true;
    sim.Advance(kFixedStepMs, cast); // open the cut window.
    ASSERT_TRUE(adapter->Brain().InSkillEffect());

    sim.KillAllHostiles(); // forces hp=0 NOW; entity is not yet `dead` (Cull hasn't run).

    // Same step: TickWeapon (-> ApplyMeleeCut, gates ONLY on the `dead` flag, still
    // false) runs BEFORE Cull() -- the cut lands on the already-forced-dead entity.
    WorldInputs fire = Idle();
    fire.firing = true;
    sim.Advance(kFixedStepMs, fire);

    EXPECT_FALSE(sim.EnemyViews()[0].alive);
    EXPECT_EQ(adapter->Brain().Combo(), 0)
        << "a player cut landing AFTER KillAllHostiles but BEFORE the next Cull() "
           "must not re-attribute a forced death to the player";
}


// ===================================================================
// P3.3.c -- C03 / C05 / C07 / C11 / C12 in the SIMULATION. Every expected number is
// derived from characters.json (skill_cd / atk / critical / BulletBat.damage via
// GameData) or from a decompile constant cited in the hero's header (CharSkillC03/05/07/
// 11/12.hpp). Positions: 1 Unity unit == kWorldUnitPx (32) sim pixels (T-40).
// ===================================================================

namespace {
const Game::CharacterDef &HeroDef(const std::string &id) {
    static Game::GameData gd;
    static const bool ok = gd.LoadAll(RESOURCE_DIR);
    EXPECT_TRUE(ok);
    const Game::CharacterDef *c = gd.FindCharacter(id);
    EXPECT_NE(c, nullptr) << id;
    static const Game::CharacterDef fallback{};
    return c != nullptr ? *c : fallback;
}

void SetHero(Simulation &sim, const std::string &id) {
    const Game::CharacterDef &d = HeroDef(id);
    sim.SetPlayerSkill(id, d.skillCd, d.inSkillTime, d.atk, &d);
}

WorldInputs Press() {
    WorldInputs in = Idle();
    in.skill = true;
    return in;
}

// Advance @p n fixed steps with the (held-false) idle input; collect every event.
void RunSteps(Simulation &sim, int n, std::vector<SimEvent> *events = nullptr,
         const WorldInputs &in = Idle()) {
    for (int i = 0; i < n; ++i) {
        sim.Advance(kFixedStepMs, in);
        if (events != nullptr) {
            for (SimEvent &e : sim.DrainEvents()) {
                events->push_back(std::move(e));
            }
        }
    }
}

int CountEvent(const std::vector<SimEvent> &evs, const std::string &name) {
    int n = 0;
    for (const SimEvent &e : evs) {
        if (e.name == name) {
            ++n;
        }
    }
    return n;
}

int EnemyHp(const Simulation &sim, std::size_t i) { return sim.EnemyViews()[i].hp; }

// A wall slab: blocks every circle query whose centre lies in x in [lo, hi].
class SlabWorld : public Game::Sim::WorldCollision {
public:
    SlabWorld(float lo, float hi) : m_Lo(lo), m_Hi(hi) {}
    bool Blocks(glm::vec2 pos, float /*radius*/) const override {
        return pos.x >= m_Lo && pos.x <= m_Hi;
    }

private:
    float m_Lo;
    float m_Hi;
};
} // namespace

// --- C03 Wizard: skill_cd 6, atk 3. Press -> 0.4 s -> thunder on the nearest enemy for 8, chain ---

TEST(CharSkillSimTest, C03ThunderStrikesTheNearestEnemyForEightAfterFourTenthsOfASecondAndChains) {
    Simulation sim(31, &g_NullWorld);
    sim.AddEnemy(StationaryEnemyWithHp(50), glm::vec2{160.0F, 0.0F}, 0, 1);  // 5.0 units: first strike
    sim.AddEnemy(StationaryEnemyWithHp(50), glm::vec2{300.0F, 0.0F}, 0, 2);  // 4.4 units from it
    SetHero(sim, "c03");
    std::vector<SimEvent> evs;

    sim.Advance(kFixedStepMs, Press()); // fixed step 1: the press edge
    for (SimEvent &e : sim.DrainEvents()) { evs.push_back(std::move(e)); }
    EXPECT_TRUE(sim.PlayerSkill()->InSkill()); // in_skill = 1; the Invoke is armed
    RunSteps(sim, 18, &evs, Idle()); // steps 2..19: 380 ms elapsed
    EXPECT_EQ(EnemyHp(sim, 0), 50); // nothing yet
    EXPECT_TRUE(sim.PlayerSkill()->InSkill());

    RunSteps(sim, 1, &evs); // step 20 = 400 ms: CreateTunder
    EXPECT_EQ(EnemyHp(sim, 0), 42); // 50 - 8 (StartThunder value3)
    EXPECT_EQ(EnemyHp(sim, 1), 50); // not struck first: it is the farther one
    EXPECT_FALSE(sim.PlayerSkill()->InSkill()); // RoleSkillEnd ran in the same call
    EXPECT_EQ(CountEvent(evs, "skill_effect:thunder_strike"), 1);
    EXPECT_NEAR(sim.PlayerStats().skillCdProgress, 0.0F, 1e-6F); // cooldown restarted: 0 / 6

    RunSteps(sim, 6, &evs); // steps 21..26
    EXPECT_EQ(EnemyHp(sim, 1), 50);
    RunSteps(sim, 1, &evs); // step 27: the 0.15 s hop (the strike step already counts one)
    EXPECT_EQ(EnemyHp(sim, 1), 42); // chain hop: atk 8 on the nearest unhit within 10 units
    EXPECT_EQ(EnemyHp(sim, 0), 42); // not hit twice
    EXPECT_EQ(CountEvent(evs, "skill_effect:thunder_hop"), 1);

    RunSteps(sim, 40, &evs); // far longer than any further hop (a hopped chain does not re-strike)
    EXPECT_EQ(EnemyHp(sim, 0), 42);
    EXPECT_EQ(EnemyHp(sim, 1), 42);
    // Cooldown ticks from step 20: after 47 steps (steps 21..67) -> 47 * 0.02 / 6.
    EXPECT_NEAR(sim.PlayerStats().skillCdProgress, 47 * 0.02F / 6.0F, 2e-3F);
}

TEST(CharSkillSimTest, C03ALoneEnemyIsStruckThenReStruckForDouble) {
    Simulation sim(32, &g_NullWorld);
    sim.AddEnemy(StationaryEnemyWithHp(50), glm::vec2{160.0F, 0.0F}, 0, 1);
    SetHero(sim, "c03");
    sim.Advance(kFixedStepMs, Press());
    RunSteps(sim, 19);
    EXPECT_EQ(EnemyHp(sim, 0), 42); // step 20: 8
    RunSteps(sim, 6);
    EXPECT_EQ(EnemyHp(sim, 0), 42); // step 26: the re-strike has not fired yet
    RunSteps(sim, 1);
    EXPECT_EQ(EnemyHp(sim, 0), 26); // step 27: no other target, count == 0 -> atk * 2 = 16
}

TEST(CharSkillSimTest, C03NoEnemyInRangeStillSpendsTheCooldownAndStrikesNothing) {
    Simulation sim(33, &g_NullWorld);
    sim.AddEnemy(StationaryEnemyWithHp(50), glm::vec2{480.0F, 0.0F}, 0, 1); // 15 units > 14
    SetHero(sim, "c03");
    std::vector<SimEvent> evs;
    sim.Advance(kFixedStepMs, Press());
    RunSteps(sim, 19, &evs);
    EXPECT_FALSE(sim.PlayerSkill()->InSkill()); // ended at step 20 regardless
    EXPECT_EQ(EnemyHp(sim, 0), 50);
    EXPECT_EQ(CountEvent(evs, "skill_effect:thunder_strike"), 0);
    EXPECT_FALSE(sim.PlayerSkill()->SkillReady()); // the charge was spent
    EXPECT_NEAR(sim.PlayerStats().skillCdProgress, 0.0F, 1e-6F);
}

TEST(CharSkillSimTest, C03AWallBetweenThePlayerAndTheEnemyBlocksTheStrike) {
    SlabWorld wall(70.0F, 90.0F); // a slab between x = 0 (player) and x = 160 (enemy)
    Simulation sim(34, &wall);
    sim.AddEnemy(StationaryEnemyWithHp(50), glm::vec2{160.0F, 0.0F}, 0, 1);
    SetHero(sim, "c03");
    sim.Advance(kFixedStepMs, Press());
    RunSteps(sim, 30);
    EXPECT_EQ(EnemyHp(sim, 0), 50); // Linecast on "Wall" blocked: nothing was struck
    EXPECT_FALSE(sim.PlayerSkill()->InSkill());
    EXPECT_FALSE(sim.PlayerSkill()->SkillReady()); // ...but the cooldown was still spent
}

TEST(CharSkillSimTest, C03AThunderKillIsCreditedToThePlayerAndTheEnemyDies) {
    Simulation sim(35, &g_NullWorld);
    sim.AddEnemy(StationaryEnemyWithHp(8), glm::vec2{160.0F, 0.0F}, 0, 1); // exactly one strike
    SetHero(sim, "c03");
    std::vector<SimEvent> evs;
    sim.Advance(kFixedStepMs, Press());
    RunSteps(sim, 19, &evs);
    EXPECT_FALSE(sim.EnemyViews()[0].alive);
    EXPECT_EQ(CountEvent(evs, "death"), 1);
}

// --- C05 Alchemist: skill_cd 6. The ultimate is instant; the bottle volley is NOT PORTED (T-44) ---

TEST(CharSkillSimTest, C05CastEmitsTheBottleCueSpendsTheCooldownAtOnceAndNeverShowsInSkill) {
    Simulation sim(36, &g_NullWorld);
    sim.AddEnemy(StationaryEnemyWithHp(50), glm::vec2{60.0F, 0.0F}, 0, 1);
    SetHero(sim, "c05");
    std::vector<SimEvent> evs;
    sim.Advance(kFixedStepMs, Press());
    for (SimEvent &e : sim.DrainEvents()) { evs.push_back(std::move(e)); }
    EXPECT_EQ(CountEvent(evs, "skill_use"), 1);
    EXPECT_EQ(CountEvent(evs, "skill_effect:bottle_throw n=1 variant=0/4 NOT_PORTED(T-44)"), 1);
    EXPECT_FALSE(sim.PlayerSkill()->InSkill()); // RoleSkillEnd inside the same RoleSkill
    EXPECT_FALSE(sim.PlayerSkill()->SkillReady());
    EXPECT_NEAR(sim.PlayerStats().skillCdProgress, 0.02F / 6.0F, 1e-4F); // 1 step of 6 s
    RunSteps(sim, 150);
    EXPECT_NEAR(sim.PlayerStats().skillCdProgress, 151 * 0.02F / 6.0F, 1e-3F); // 0.503
    EXPECT_EQ(EnemyHp(sim, 0), 50); // the bottle's damage is NOT PORTED: no fake numbers
    RunSteps(sim, 160); // past 6 s
    EXPECT_TRUE(sim.PlayerSkill()->SkillReady());
    EXPECT_FLOAT_EQ(sim.PlayerStats().skillCdProgress, 1.0F);
}

// --- C07 Vampire: skill_cd 8, BulletBat.damage 5 (prefab). 6 bats; hit -> heal/energy, miss -> refund ---

TEST(CharSkillSimTest, C07BatsHitTheEnemyForFiveEachHealThePlayerEveryThirdHitAndEndTheSkill) {
    Simulation sim(37, &g_NullWorld);
    sim.AddEnemy(StationaryEnemyWithHp(100), glm::vec2{160.0F, 0.0F}, 0, 1); // 5 units out
    Game::CombatStats player;
    player.hp = 3;
    player.maxHp = 10;
    player.armor = 5;
    player.maxArmor = 5;
    sim.SetPlayerStats(player);
    SetHero(sim, "c07");
    std::vector<SimEvent> evs;

    sim.Advance(kFixedStepMs, Press());
    for (SimEvent &e : sim.DrainEvents()) { evs.push_back(std::move(e)); }
    ASSERT_TRUE(sim.PlayerSkill()->InSkill()); // stays in_skill until the last bat is back
    EXPECT_EQ(CountEvent(evs, "skill_effect:bat_swarm n=6"), 1);

    int endedAt = -1;
    for (int step = 2; step <= 1500 && endedAt < 0; ++step) {
        sim.Advance(kFixedStepMs, Idle());
        sim.SetPlayerStats(sim.PlayerStats()); // the shell round-trips vitals every frame
        if (!sim.PlayerSkill()->InSkill()) {
            endedAt = step;
        }
    }
    ASSERT_GT(endedAt, 0) << "the bats never all returned";
    EXPECT_EQ(EnemyHp(sim, 0), 100 - 6 * 5); // six bats, BulletBat.damage 5 each, one hit per bat
    EXPECT_EQ(sim.PlayerStats().hp, 5);      // 6 hits -> RestoreHealth(1) on the 3rd and 6th
    // All six bats hit, none missed: no cooldown refund -> the cooldown restarted at 0.
    EXPECT_NEAR(sim.PlayerStats().skillCdProgress, 0.0F, 1e-6F);
    EXPECT_FALSE(sim.PlayerSkill()->SkillReady());
    RunSteps(sim, 50);
    EXPECT_NEAR(sim.PlayerStats().skillCdProgress, 50 * 0.02F / 8.0F, 1e-3F); // 0.125
}

TEST(CharSkillSimTest, C07WithNoEnemyEveryBatMissesAndTheCooldownIsFullyRefunded) {
    Simulation sim(38, &g_NullWorld);
    SetHero(sim, "c07");
    sim.Advance(kFixedStepMs, Press()); // step 1 (the launch call also runs bat step 1)
    EXPECT_TRUE(sim.PlayerSkill()->InSkill());
    RunSteps(sim, 29); // steps 2..30: the bats are still out (they arrive on bat step 31)
    EXPECT_TRUE(sim.PlayerSkill()->InSkill());
    RunSteps(sim, 1); // step 31: the 6th BatBack -> RoleSkillEnd
    EXPECT_FALSE(sim.PlayerSkill()->InSkill());
    // 6 misses * (8 / 6) = 8 = skill_cd: ReSetSkillReload then SkillReload(8) -> ready at once.
    EXPECT_TRUE(sim.PlayerSkill()->SkillReady());
    EXPECT_FLOAT_EQ(sim.PlayerStats().skillCdProgress, 1.0F);
    // ...and the next press re-casts immediately.
    sim.Advance(kFixedStepMs, Idle()); // release
    sim.Advance(kFixedStepMs, Press());
    EXPECT_TRUE(sim.PlayerSkill()->InSkill());
}

TEST(CharSkillSimTest, C07AtFullHealthEveryThirdBatHitGrantsFiveEnergy) {
    Simulation sim(39, &g_NullWorld);
    sim.AddEnemy(StationaryEnemyWithHp(100), glm::vec2{160.0F, 0.0F}, 0, 1);
    Game::CombatStats player;
    player.hp = 10;
    player.maxHp = 10;
    sim.SetPlayerStats(player);
    SetHero(sim, "c07");
    int energy = 0;
    sim.Advance(kFixedStepMs, Press());
    energy += sim.PlayerEnergyGrantedLastAdvance();
    for (int step = 2; step <= 1500 && sim.PlayerSkill()->InSkill(); ++step) {
        sim.Advance(kFixedStepMs, Idle());
        energy += sim.PlayerEnergyGrantedLastAdvance();
    }
    EXPECT_FALSE(sim.PlayerSkill()->InSkill());
    EXPECT_EQ(energy, 10); // 6 hits -> two RestoreEnergy(5)
    EXPECT_EQ(sim.PlayerStats().hp, 10);
    EXPECT_EQ(EnemyHp(sim, 0), 70);
}

// --- C11 Priest: skill_cd 14. Instant cast; the holy light pulses 3 x RestoreHealth(1) ---

TEST(CharSkillSimTest, C11HolyLightHealsThePlayerOnePointPerPulseForThreePulses) {
    Simulation sim(40, &g_NullWorld);
    Game::CombatStats player;
    player.hp = 3;
    player.maxHp = 10;
    sim.SetPlayerStats(player);
    SetHero(sim, "c11");
    std::vector<SimEvent> evs;
    sim.Advance(kFixedStepMs, Press()); // step 1: cast + the first pulse (can_hit starts true)
    for (SimEvent &e : sim.DrainEvents()) { evs.push_back(std::move(e)); }
    EXPECT_EQ(CountEvent(evs, "skill_effect:holy_light"), 1);
    EXPECT_FALSE(sim.PlayerSkill()->InSkill()); // RoleSkillEnd inside the same RoleSkill
    EXPECT_EQ(sim.PlayerStats().hp, 4);
    EXPECT_NEAR(sim.PlayerStats().skillCdProgress, 0.02F / 14.0F, 1e-4F);
    RunSteps(sim, 38); // steps 2..39
    EXPECT_EQ(sim.PlayerStats().hp, 4);
    RunSteps(sim, 1); // step 40: the 0.8 s re-arm
    EXPECT_EQ(sim.PlayerStats().hp, 5);
    RunSteps(sim, 39); // steps 41..79
    EXPECT_EQ(sim.PlayerStats().hp, 5);
    RunSteps(sim, 1); // step 80: 1.6 s
    EXPECT_EQ(sim.PlayerStats().hp, 6);
    RunSteps(sim, 140); // GasEnd (1.8 s) stopped further pulses
    EXPECT_EQ(sim.PlayerStats().hp, 6);
    EXPECT_NEAR(sim.PlayerStats().skillCdProgress, 220 * 0.02F / 14.0F, 2e-3F); // 0.314
}

TEST(CharSkillSimTest, C11APlayerWhoWalksOutOfTheLightIsNotHealedAgain) {
    Simulation sim(41, &g_NullWorld);
    Game::CombatStats player;
    player.hp = 3;
    player.maxHp = 10;
    sim.SetPlayerStats(player);
    SetHero(sim, "c11");
    sim.Advance(kFixedStepMs, Press()); // pulse 1 at the cast position
    ASSERT_EQ(sim.PlayerStats().hp, 4);
    WorldInputs away = Idle();
    away.playerPos = glm::vec2{200.0F, 0.0F}; // 6.25 units > the 4-unit radius
    RunSteps(sim, 150, nullptr, away);
    EXPECT_EQ(sim.PlayerStats().hp, 4); // pulses 2 and 3 found nobody
}

// --- C12 Druid: skill_cd 2.5, atk 5. First cast summons (NOT PORTED), later casts are AtkCut(5) ---

TEST(CharSkillSimTest, C12FirstCastSummonsTwoWolvesThenEveryLaterCastCutsForAtk) {
    Simulation sim(42, &g_NullWorld);
    sim.AddEnemy(StationaryEnemyWithHp(50), glm::vec2{30.0F, 0.0F}, 0, 1); // inside the cut reach
    SetHero(sim, "c12");
    std::vector<SimEvent> evs;

    sim.Advance(kFixedStepMs, Press()); // cast 1: summon phase
    for (SimEvent &e : sim.DrainEvents()) { evs.push_back(std::move(e)); }
    EXPECT_EQ(CountEvent(evs, "skill_use"), 1);
    EXPECT_EQ(CountEvent(evs, "skill_effect:wolf_summon n=2 NOT_PORTED(T-48)"), 1);
    EXPECT_EQ(EnemyHp(sim, 0), 50); // a summon deals no damage
    EXPECT_TRUE(sim.PlayerSkill()->InSkill()); // in_skill = 1 and never cleared (T-49)
    EXPECT_NEAR(sim.PlayerStats().skillCdProgress, 0.02F / 2.5F, 1e-4F);

    RunSteps(sim, 5); // release; a press inside the cooldown does nothing
    sim.Advance(kFixedStepMs, Press());
    EXPECT_EQ(EnemyHp(sim, 0), 50);
    RunSteps(sim, 1);
    RunSteps(sim, 130, &evs); // well past 2.5 s from cast 1
    EXPECT_TRUE(sim.PlayerSkill()->SkillReady());

    sim.Advance(kFixedStepMs, Press()); // cast 2: the AtkCut phase (in_skill is still set)
    for (SimEvent &e : sim.DrainEvents()) { evs.push_back(std::move(e)); }
    EXPECT_EQ(EnemyHp(sim, 0), 45); // role_attribute.atk 5
    EXPECT_EQ(CountEvent(evs, "skill_effect:atk_cut dmg=5"), 1);
    EXPECT_TRUE(sim.PlayerSkill()->InSkill());
    EXPECT_NEAR(sim.PlayerStats().skillCdProgress, 0.02F / 2.5F, 1e-4F); // RoleSkillEnd again

    RunSteps(sim, 126); // 2.52 s
    sim.Advance(kFixedStepMs, Press()); // cast 3: another cut, not another summon
    EXPECT_EQ(EnemyHp(sim, 0), 40);
}

TEST(CharSkillSimTest, C12ACutKillIsAttributedToThePlayerCut) {
    Simulation sim(43, &g_NullWorld);
    sim.AddEnemy(StationaryEnemyWithHp(5), glm::vec2{30.0F, 0.0F}, 0, 1); // one cut of 5 kills it
    SetHero(sim, "c12");
    std::vector<SimEvent> evs;
    sim.Advance(kFixedStepMs, Press()); // summon
    RunSteps(sim, 130);
    ASSERT_TRUE(sim.PlayerSkill()->SkillReady());
    sim.Advance(kFixedStepMs, Press()); // cut
    for (SimEvent &e : sim.DrainEvents()) { evs.push_back(std::move(e)); }
    EXPECT_FALSE(sim.EnemyViews()[0].alive);
    EXPECT_EQ(CountEvent(evs, "death"), 1);
}

// NOLINTEND(readability-magic-numbers)
