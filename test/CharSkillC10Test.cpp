#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include "combat/CharSkillC10.hpp"

using Game::CharSkillC10;

// NOLINTBEGIN(readability-magic-numbers)

// --- Construction / readiness --------------------------------------------------

TEST(CharSkillC10Test, StartsReadyAndIdle) {
    CharSkillC10 c(5.0F, 1.0F);
    EXPECT_TRUE(c.SkillReady());        // freshly set-up hero: first ultimate ready
    EXPECT_FALSE(c.InSkill());
    EXPECT_FALSE(c.Changing());
    EXPECT_FALSE(c.SkillAtkArmed());
    EXPECT_EQ(c.HurtCount(), 0);
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 0.0F);
    EXPECT_FLOAT_EQ(c.SkillCd(), 5.0F);
    EXPECT_FLOAT_EQ(c.InSkillTime(), 1.0F);
}

TEST(CharSkillC10Test, NegativeStatsClampToZero) {
    CharSkillC10 c(-3.0F, -2.0F);       // guard against bad stat sheet values
    EXPECT_FLOAT_EQ(c.SkillCd(), 0.0F);
    EXPECT_FLOAT_EQ(c.InSkillTime(), 0.0F);
    EXPECT_TRUE(c.SkillReady());        // skill_cd 0 -> always ready
}

// --- RoleSkill gate: skill_ready && !in_skill && !changing ---------------------

TEST(CharSkillC10Test, ActivateSucceedsWhenReadyIdleNotChanging) {
    CharSkillC10 c(5.0F, 1.0F);
    EXPECT_TRUE(c.TryActivateSkill());  // skill_ready && !in_skill && !changing
    EXPECT_TRUE(c.InSkill());
    EXPECT_FLOAT_EQ(c.InSkillTimeRemaining(), 1.0F);
}

TEST(CharSkillC10Test, ActivateFailsWhileAlreadyInSkill) {
    CharSkillC10 c(5.0F, 1.0F);
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_FALSE(c.TryActivateSkill()); // in_skill gate blocks re-entry
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC10Test, ActivateFailsWhileChanging) {
    // C10-SPECIFIC gate (this+0xA4): cannot cast while a Transfiguration is running.
    CharSkillC10 c(5.0F, 1.0F);
    EXPECT_TRUE(c.SkillReady());
    c.SetChanging(true);
    EXPECT_TRUE(c.Changing());
    EXPECT_FALSE(c.TryActivateSkill()); // !changing gate blocks activation
    EXPECT_FALSE(c.InSkill());
    c.SetChanging(false);
    EXPECT_TRUE(c.TryActivateSkill());  // gate clears -> now activates
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC10Test, ActivateFailsWhileOnCooldown) {
    CharSkillC10 c(5.0F, 1.0F);
    c.TryActivateSkill();
    c.EndSkill();                       // spend -> this_skill_time = 0
    EXPECT_FALSE(c.SkillReady());
    EXPECT_FALSE(c.TryActivateSkill()); // still cooling down
}

// --- EndSkill (RoleSkillEnd -> ReSetSkillReload) -------------------------------

TEST(CharSkillC10Test, EndSkillSpendsTheChargeAndLeavesState) {
    CharSkillC10 c(5.0F, 1.0F);
    c.TryActivateSkill();
    EXPECT_TRUE(c.InSkill());
    c.EndSkill();
    EXPECT_FALSE(c.InSkill());
    EXPECT_FALSE(c.SkillReady());        // cooldown restarted (this_skill_time = 0)
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 5.0F);
}

TEST(CharSkillC10Test, EndSkillIsNoOpWhenNotInSkill) {
    CharSkillC10 c(5.0F, 1.0F);
    EXPECT_TRUE(c.SkillReady());
    c.EndSkill();                        // no skill active -> no spend
    EXPECT_TRUE(c.SkillReady());
    EXPECT_FALSE(c.InSkill());
}

// --- Update: cooldown count-up + in_skill_time countdown + auto-end ------------

TEST(CharSkillC10Test, CooldownRechargesAndClamps) {
    CharSkillC10 c(2.0F, 1.0F);
    c.TryActivateSkill();
    c.EndSkill();                        // this_skill_time = 0
    EXPECT_FALSE(c.SkillReady());
    c.Tick(1000.0F);                     // +1.0s
    EXPECT_FALSE(c.SkillReady());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 1.0F);
    c.Tick(5000.0F);                     // overshoot -> clamps to skill_cd
    EXPECT_TRUE(c.SkillReady());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 0.0F);
}

TEST(CharSkillC10Test, TickRunsWhileInSkill) {
    // FAITHFUL: C10Controller__Update does NOT early-return on in_skill --
    // AttributeUpdate -> SkillReload runs unconditionally, then the in_skill body
    // decrements param_1[0x26]. Proof Tick executes inside the skill: a mid-window
    // tick decrements in_skill_time (instead of being a no-op).
    CharSkillC10 c(4.0F, 2.0F);
    EXPECT_TRUE(c.TryActivateSkill());    // enter (in_skill_time armed to 2.0)
    EXPECT_TRUE(c.InSkill());
    c.Tick(1000.0F);                      // -1.0s INSIDE the skill (no auto-end yet)
    EXPECT_TRUE(c.InSkill());
    EXPECT_FLOAT_EQ(c.InSkillTimeRemaining(), 1.0F); // not frozen -> Tick ran in-skill
    EXPECT_TRUE(c.SkillReady());          // unconditional SkillReload (clamped no-op)
}

TEST(CharSkillC10Test, InSkillTimeCountsDownAndAutoEnds) {
    // FAITHFUL: C10Controller__Update decrements in_skill_time (param_1[0x26]) each
    // frame and auto-ends (RoleSkillEnd, slot 0x17c) when it reaches <= 0; the
    // auto-end runs ReSetSkillReload (this_skill_time = 0).
    CharSkillC10 c(5.0F, 1.0F);
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_FLOAT_EQ(c.InSkillTimeRemaining(), 1.0F);

    c.Tick(400.0F);                       // -0.4s -> 0.6 left, still active
    EXPECT_TRUE(c.InSkill());
    EXPECT_FLOAT_EQ(c.InSkillTimeRemaining(), 0.6F);

    c.Tick(400.0F);                       // -0.4s -> 0.2 left, still active
    EXPECT_TRUE(c.InSkill());

    c.Tick(400.0F);                       // -0.4s -> <= 0 -> AUTO-END
    EXPECT_FALSE(c.InSkill());            // skill auto-ended
    EXPECT_FLOAT_EQ(c.InSkillTimeRemaining(), 0.0F);
    EXPECT_FALSE(c.SkillReady());         // RoleSkillEnd -> ReSetSkillReload (=0)
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 5.0F);
}

TEST(CharSkillC10Test, AutoEndOnExactZeroBoundary) {
    // in_skill_time reaching exactly 0 must auto-end (<= 0, not < 0).
    CharSkillC10 c(3.0F, 1.0F);
    c.TryActivateSkill();
    c.Tick(1000.0F);                      // -1.0s -> exactly 0 -> auto-end
    EXPECT_FALSE(c.InSkill());
    EXPECT_FALSE(c.SkillReady());         // cooldown restarted at end
}

TEST(CharSkillC10Test, ManualEndSkillBeforeAutoEnd) {
    CharSkillC10 c(2.0F, 5.0F);
    c.TryActivateSkill();
    c.Tick(1000.0F);                      // -1.0s -> 4.0 left
    EXPECT_TRUE(c.InSkill());
    c.EndSkill();                         // manual end before the window expires
    EXPECT_FALSE(c.InSkill());
    EXPECT_FLOAT_EQ(c.InSkillTimeRemaining(), 0.0F);
    c.Tick(1000.0F);                      // now idle: cooldown recharges, no re-end
    EXPECT_FALSE(c.InSkill());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 1.0F); // 2.0 - 1.0 recharged
}

TEST(CharSkillC10Test, NonPositiveDtIsIgnored) {
    CharSkillC10 c(3.0F, 1.0F);
    c.TryActivateSkill();
    c.EndSkill();
    const float before = c.CooldownRemaining();
    c.Tick(0.0F);
    c.Tick(-100.0F);
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), before); // no advance
}

// --- RoleAtk: in_skill animator bool FIRST, then trigger/hand routing ----------

TEST(CharSkillC10Test, RoleAtkPressFiresHandNoSkillAnimWhenNotInSkill) {
    CharSkillC10 c(5.0F, 1.0F);
    const CharSkillC10::AtkDecision d = c.RoleAtk(true, false);
    EXPECT_FALSE(d.setSkillAnim);        // skill animator bool only while in_skill
    EXPECT_FALSE(d.triggerItem);
    EXPECT_TRUE(d.setHandAttack);
    EXPECT_TRUE(d.handAttackValue);
}

TEST(CharSkillC10Test, RoleAtkReleaseStopsHand) {
    CharSkillC10 c(5.0F, 1.0F);
    const CharSkillC10::AtkDecision d = c.RoleAtk(false, false);
    EXPECT_TRUE(d.setHandAttack);
    EXPECT_FALSE(d.handAttackValue);     // release -> SetAttack(false)
    EXPECT_FALSE(d.setSkillAnim);
}

TEST(CharSkillC10Test, RoleAtkSetsSkillAnimWhileInSkillAndStillFiresHand) {
    // C10 difference vs C01: the in_skill animator bool fires FIRST and does NOT
    // short-circuit -- the press still flows through to hand.SetAttack.
    CharSkillC10 c(5.0F, 1.0F);
    c.TryActivateSkill();
    EXPECT_TRUE(c.InSkill());
    const CharSkillC10::AtkDecision press = c.RoleAtk(true, false);
    EXPECT_TRUE(press.setSkillAnim);              // anim.SetBool(6685, true)
    EXPECT_TRUE(press.skillAnimValue);
    EXPECT_TRUE(press.setHandAttack);            // NOT short-circuited
    EXPECT_TRUE(press.handAttackValue);

    const CharSkillC10::AtkDecision release = c.RoleAtk(false, false);
    EXPECT_TRUE(release.setSkillAnim);            // anim.SetBool(6685, false)
    EXPECT_FALSE(release.skillAnimValue);
    EXPECT_TRUE(release.setHandAttack);
    EXPECT_FALSE(release.handAttackValue);
}

TEST(CharSkillC10Test, RoleAtkItemPressTriggersItemAndSkipsHand) {
    CharSkillC10 c(5.0F, 1.0F);
    const CharSkillC10::AtkDecision d = c.RoleAtk(true, true); // on a pickup, pressing
    EXPECT_TRUE(d.triggerItem);
    EXPECT_FALSE(d.setHandAttack);               // early return: no hand.SetAttack
    EXPECT_FALSE(d.setSkillAnim);                // not in skill -> no animator bool
}

TEST(CharSkillC10Test, RoleAtkItemPressInSkillSetsAnimThenTriggers) {
    // in_skill animator bool fires FIRST (before the item branch), THEN the press
    // triggers the item and returns (no hand fire).
    CharSkillC10 c(5.0F, 1.0F);
    c.TryActivateSkill();
    const CharSkillC10::AtkDecision d = c.RoleAtk(true, true);
    EXPECT_TRUE(d.setSkillAnim);                 // anim bool set before the trigger
    EXPECT_TRUE(d.skillAnimValue);
    EXPECT_TRUE(d.triggerItem);
    EXPECT_FALSE(d.setHandAttack);               // item trigger early-out
}

TEST(CharSkillC10Test, RoleAtkItemReleaseDoesNotTrigger) {
    CharSkillC10 c(5.0F, 1.0F);
    // in_item but NOT pressing -> falls through to the normal hand path.
    const CharSkillC10::AtkDecision d = c.RoleAtk(false, true);
    EXPECT_FALSE(d.triggerItem);
    EXPECT_TRUE(d.setHandAttack);
    EXPECT_FALSE(d.handAttackValue);
}

// --- HurtSomeOne: skill_atk armor restore every 3rd qualifying hit -------------

TEST(CharSkillC10Test, HurtDoesNothingWhenNotInSkill) {
    CharSkillC10 c(5.0F, 1.0F);
    c.ArmSkillAtk();                     // armed, but not in skill
    EXPECT_FALSE(c.InSkill());
    EXPECT_FALSE(c.HurtSomeOne());       // in_skill gate fails -> counter untouched
    EXPECT_EQ(c.HurtCount(), 0);
    EXPECT_TRUE(c.SkillAtkArmed());      // skill_atk NOT consumed (short-circuited)
}

TEST(CharSkillC10Test, HurtDoesNothingWhenSkillAtkNotArmed) {
    CharSkillC10 c(5.0F, 1.0F);
    c.TryActivateSkill();
    EXPECT_FALSE(c.SkillAtkArmed());
    EXPECT_FALSE(c.HurtSomeOne());       // skill_atk not armed -> counter untouched
    EXPECT_EQ(c.HurtCount(), 0);
}

TEST(CharSkillC10Test, HurtConsumesSkillAtkAndCountsToThird) {
    // FAITHFUL: in_skill && skill_atk -> consume skill_atk, ++hurt_count; the 3rd
    // qualifying hit (2 < hurt_count) restores 1 armor and resets the counter.
    CharSkillC10 c(5.0F, 1.0F);
    c.TryActivateSkill();

    c.ArmSkillAtk();
    EXPECT_FALSE(c.HurtSomeOne());       // hit 1 -> count 1, no restore
    EXPECT_EQ(c.HurtCount(), 1);
    EXPECT_FALSE(c.SkillAtkArmed());     // consumed

    c.ArmSkillAtk();
    EXPECT_FALSE(c.HurtSomeOne());       // hit 2 -> count 2, no restore
    EXPECT_EQ(c.HurtCount(), 2);

    c.ArmSkillAtk();
    EXPECT_TRUE(c.HurtSomeOne());        // hit 3 -> 2 < 3 -> RestoreArmor + reset
    EXPECT_EQ(c.HurtCount(), 0);         // counter reset
}

TEST(CharSkillC10Test, HurtRequiresReArmEachHit) {
    // skill_atk is consumed per hit; a second hit without re-arming does nothing
    // and does NOT advance the counter.
    CharSkillC10 c(5.0F, 1.0F);
    c.TryActivateSkill();
    c.ArmSkillAtk();
    EXPECT_FALSE(c.HurtSomeOne());       // hit 1 -> count 1, skill_atk consumed
    EXPECT_EQ(c.HurtCount(), 1);
    EXPECT_FALSE(c.HurtSomeOne());       // not re-armed -> no count change
    EXPECT_EQ(c.HurtCount(), 1);
}

TEST(CharSkillC10Test, HurtArmorRestoreCyclesEveryThird) {
    // Two full cycles of 3 qualifying hits -> two armor restores.
    CharSkillC10 c(5.0F, 10.0F);
    c.TryActivateSkill();
    int restores = 0;
    for (int i = 0; i < 6; ++i) {
        c.ArmSkillAtk();
        if (c.HurtSomeOne()) {
            ++restores;
        }
    }
    EXPECT_EQ(restores, 2);              // hits 3 and 6 trigger
    EXPECT_EQ(c.HurtCount(), 0);
}

TEST(CharSkillC10Test, ArmorRestoreConstantsMatchDecomp) {
    EXPECT_EQ(CharSkillC10::kArmorRestoreEveryNthHit, 3); // 2 < hurt_count test
    EXPECT_EQ(CharSkillC10::kArmorRestoreAmount, 1);      // RestoreArmor(1)
}

// --- Determinism: ZERO RNG draws on every path --------------------------------

TEST(CharSkillC10Test, NoRngDrawOnAnyPath) {
    // No recovered C10 body draws from rg_random; exercising the whole brain must
    // leave the stream un-advanced, so two seeded instances stay in lockstep.
    CharSkillC10 a(3.0F, 2.0F);
    CharSkillC10 b(3.0F, 2.0F);
    a.SetSeed(987654);
    b.SetSeed(987654);
    EXPECT_TRUE(a.Seeded());
    for (int i = 0; i < 16; ++i) {
        a.SetChanging(i % 2 == 0);
        a.TryActivateSkill();
        a.SetChanging(false);
        a.TryActivateSkill();
        a.RoleAtk(true, false);
        a.RoleAtk(false, true);
        a.ArmSkillAtk();
        a.HurtSomeOne();
        a.Tick(300.0F);
        a.EndSkill();
        a.Tick(300.0F);
    }
    // 'a' ran the full brain; 'b' was untouched. If any draw had leaked, the next
    // draw on 'a' would diverge from 'b'. Same value -> stream truly untouched.
    EXPECT_EQ(a.Rng().Range(0, 1000000), b.Rng().Range(0, 1000000));
}

TEST(CharSkillC10Test, FullCycleIsDeterministic) {
    // Drive identical activate/attack/hurt/cooldown cycles on two instances and
    // compare the observable state at every step -> fully deterministic.
    auto run = [](CharSkillC10 &c) -> std::vector<int> {
        std::vector<int> trace;
        for (int i = 0; i < 12; ++i) {
            trace.push_back(c.TryActivateSkill() ? 1 : 0);
            const CharSkillC10::AtkDecision d = c.RoleAtk(true, false);
            trace.push_back(d.setSkillAnim ? 1 : 0);
            c.ArmSkillAtk();
            trace.push_back(c.HurtSomeOne() ? 1 : 0);
            c.Tick(700.0F);
            c.EndSkill();
            trace.push_back(c.SkillReady() ? 1 : 0);
            c.Tick(1500.0F);
        }
        return trace;
    };
    CharSkillC10 a(2.0F, 1.0F);
    CharSkillC10 b(2.0F, 1.0F);
    const std::vector<int> ta = run(a);
    const std::vector<int> tb = run(b);
    ASSERT_EQ(ta.size(), tb.size());
    for (std::size_t i = 0; i < ta.size(); ++i) {
        EXPECT_EQ(ta[i], tb[i]);
    }
}

// NOLINTEND(readability-magic-numbers)
