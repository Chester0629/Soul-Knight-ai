#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include "combat/CharSkillC13.hpp"

using Game::CharSkillC13;

// NOLINTBEGIN(readability-magic-numbers)

TEST(CharSkillC13Test, StartsReadyAndNotInSkill) {
    CharSkillC13 c(5.0F, 1.0F);
    EXPECT_TRUE(c.SkillReady());        // freshly set-up hero: first ultimate ready
    EXPECT_FALSE(c.InSkill());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 0.0F);
    EXPECT_FLOAT_EQ(c.SkillCd(), 5.0F);
    EXPECT_FLOAT_EQ(c.InSkillTime(), 1.0F);
    EXPECT_EQ(c.HurtCount(), 0);
    EXPECT_FALSE(c.SkillAtkFlag());
}

TEST(CharSkillC13Test, NegativeStatsClampToZero) {
    CharSkillC13 c(-3.0F, -2.0F);       // guard against bad stat sheet values
    EXPECT_FLOAT_EQ(c.SkillCd(), 0.0F);
    EXPECT_FLOAT_EQ(c.InSkillTime(), 0.0F);
    EXPECT_TRUE(c.SkillReady());        // skill_cd 0 -> always ready
}

TEST(CharSkillC13Test, ActivateSucceedsWhenReadyIdleAndNotChanging) {
    CharSkillC13 c(5.0F, 1.0F);
    EXPECT_TRUE(c.TryActivateSkill(false)); // skill_ready && !in_skill && !changing
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC13Test, ActivateFailsWhileAlreadyInSkill) {
    CharSkillC13 c(5.0F, 1.0F);
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_FALSE(c.TryActivateSkill());  // in_skill gate blocks re-entry
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC13Test, ActivateFailsWhileChanging) {
    // FAITHFUL: C13's EXTRA "changing"(0xA4) gate over the base RoleSkill -- the
    // decomp enters only when (in_skill == 0 && changing == 0).
    CharSkillC13 c(5.0F, 1.0F);
    EXPECT_TRUE(c.SkillReady());
    EXPECT_FALSE(c.TryActivateSkill(true)); // mid-transfiguration -> blocked
    EXPECT_FALSE(c.InSkill());
    EXPECT_TRUE(c.TryActivateSkill(false)); // changing cleared -> now enters
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC13Test, ActivateFailsWhileOnCooldown) {
    CharSkillC13 c(5.0F, 1.0F);
    c.TryActivateSkill();
    c.EndSkill();                        // spend -> this_skill_time = 0
    EXPECT_FALSE(c.SkillReady());
    EXPECT_FALSE(c.TryActivateSkill());  // still cooling down
}

TEST(CharSkillC13Test, EndSkillSpendsTheChargeAndClearsPassive) {
    CharSkillC13 c(5.0F, 1.0F);
    c.TryActivateSkill();
    c.SetSkillAtkFlag(true);
    EXPECT_TRUE(c.InSkill());
    c.EndSkill();
    EXPECT_FALSE(c.InSkill());
    EXPECT_FALSE(c.SkillReady());        // cooldown restarted (this_skill_time = 0)
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 5.0F);
    EXPECT_EQ(c.HurtCount(), 0);         // passive state cleared
    EXPECT_FALSE(c.SkillAtkFlag());
}

TEST(CharSkillC13Test, EndSkillIsNoOpWhenNotInSkill) {
    CharSkillC13 c(5.0F, 1.0F);
    EXPECT_TRUE(c.SkillReady());
    c.EndSkill();                        // no skill active -> no spend
    EXPECT_TRUE(c.SkillReady());
    EXPECT_FALSE(c.InSkill());
}

TEST(CharSkillC13Test, CooldownRechargesAndClamps) {
    CharSkillC13 c(2.0F, 1.0F);
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

TEST(CharSkillC13Test, TickRunsWhileInSkill) {
    // FAITHFUL: C13Controller__Update does NOT early-return on in_skill --
    // AttributeUpdate -> SkillReload runs unconditionally, then the in_skill body
    // runs. Proof Tick executes inside the skill: a tick mid-window decrements
    // in_skill_time (instead of being frozen / a no-op).
    CharSkillC13 c(4.0F, 2.0F);
    EXPECT_TRUE(c.TryActivateSkill());   // enter (in_skill_time armed to 2.0)
    EXPECT_TRUE(c.InSkill());
    c.Tick(1000.0F);                     // -1.0s INSIDE the skill (no auto-end yet)
    EXPECT_TRUE(c.InSkill());
    EXPECT_FLOAT_EQ(c.InSkillTimeRemaining(), 1.0F); // not frozen -> Tick ran in-skill
    EXPECT_TRUE(c.SkillReady());         // unconditional SkillReload (clamped no-op)
}

TEST(CharSkillC13Test, InSkillTimeCountsDownAndAutoEnds) {
    // FAITHFUL: C13Controller__Update decrements in_skill_time (param_1[0x26]) each
    // frame and auto-ends (RoleSkillEnd, slot 0x17c) when it reaches <= 0. The
    // auto-end runs ReSetSkillReload (this_skill_time = 0).
    CharSkillC13 c(5.0F, 1.0F);
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_FLOAT_EQ(c.InSkillTimeRemaining(), 1.0F);

    c.Tick(400.0F);                      // -0.4s -> 0.6 left, still active
    EXPECT_TRUE(c.InSkill());
    EXPECT_FLOAT_EQ(c.InSkillTimeRemaining(), 0.6F);

    c.Tick(400.0F);                      // -0.4s -> 0.2 left, still active
    EXPECT_TRUE(c.InSkill());

    c.Tick(400.0F);                      // -0.4s -> <= 0 -> AUTO-END
    EXPECT_FALSE(c.InSkill());           // skill auto-ended
    EXPECT_FLOAT_EQ(c.InSkillTimeRemaining(), 0.0F);
    EXPECT_FALSE(c.SkillReady());        // RoleSkillEnd -> ReSetSkillReload (=0)
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 5.0F);
}

TEST(CharSkillC13Test, AutoEndOnExactZeroBoundary) {
    // in_skill_time reaching exactly 0 must auto-end (<= 0, not < 0).
    CharSkillC13 c(3.0F, 1.0F);
    c.TryActivateSkill();
    c.Tick(1000.0F);                     // -1.0s -> exactly 0 -> auto-end
    EXPECT_FALSE(c.InSkill());
    EXPECT_FALSE(c.SkillReady());        // cooldown restarted at end
}

TEST(CharSkillC13Test, ManualEndSkillBeforeAutoEnd) {
    CharSkillC13 c(2.0F, 5.0F);
    c.TryActivateSkill();
    c.Tick(1000.0F);                     // -1.0s -> 4.0 left
    EXPECT_TRUE(c.InSkill());
    c.EndSkill();                        // manual end before the window expires
    EXPECT_FALSE(c.InSkill());
    EXPECT_FLOAT_EQ(c.InSkillTimeRemaining(), 0.0F);
    c.Tick(1000.0F);                     // now idle: cooldown recharges, no re-end
    EXPECT_FALSE(c.InSkill());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 1.0F); // 2.0 - 1.0 recharged
}

TEST(CharSkillC13Test, NonPositiveDtIsIgnored) {
    CharSkillC13 c(3.0F, 1.0F);
    c.TryActivateSkill();
    c.EndSkill();
    const float before = c.CooldownRemaining();
    c.Tick(0.0F);
    c.Tick(-100.0F);
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), before); // no advance
}

TEST(CharSkillC13Test, RoleAtkPressFiresPrimaryHandNoMirrorWhenNotInSkill) {
    CharSkillC13 c(5.0F, 1.0F);
    const CharSkillC13::AtkDecision d = c.RoleAtk(true, false);
    EXPECT_FALSE(d.animatorMirror);      // animator mirror only while in_skill
    EXPECT_FALSE(d.triggerItem);
    EXPECT_TRUE(d.setHandAttack);
    EXPECT_TRUE(d.handAttackValue);
}

TEST(CharSkillC13Test, RoleAtkReleaseStopsPrimaryHand) {
    CharSkillC13 c(5.0F, 1.0F);
    const CharSkillC13::AtkDecision d = c.RoleAtk(false, false);
    EXPECT_TRUE(d.setHandAttack);
    EXPECT_FALSE(d.handAttackValue);     // release -> SetAttack(false)
    EXPECT_FALSE(d.animatorMirror);
}

TEST(CharSkillC13Test, RoleAtkMirrorsAnimatorWhileInSkill) {
    // FAITHFUL: in_skill animator.SetBool mirror is the FIRST branch (before the
    // item check), distinct from C01's trailing 0.1s Invoke.
    CharSkillC13 c(5.0F, 1.0F);
    c.TryActivateSkill();
    EXPECT_TRUE(c.InSkill());
    const CharSkillC13::AtkDecision press = c.RoleAtk(true, false);
    EXPECT_TRUE(press.animatorMirror);            // animator.SetBool(.., true)
    EXPECT_TRUE(press.animatorMirrorValue);
    EXPECT_TRUE(press.setHandAttack);
    EXPECT_TRUE(press.handAttackValue);

    const CharSkillC13::AtkDecision release = c.RoleAtk(false, false);
    EXPECT_TRUE(release.animatorMirror);           // animator.SetBool(.., false)
    EXPECT_FALSE(release.animatorMirrorValue);
}

TEST(CharSkillC13Test, RoleAtkItemPressTriggersItemAndSkipsHand) {
    CharSkillC13 c(5.0F, 1.0F);
    const CharSkillC13::AtkDecision d = c.RoleAtk(true, true); // on a pickup, pressing
    EXPECT_TRUE(d.triggerItem);
    EXPECT_FALSE(d.setHandAttack);                 // early return: no hand.SetAttack
    EXPECT_FALSE(d.animatorMirror);                // not in skill -> animator untouched
}

TEST(CharSkillC13Test, RoleAtkItemPressInSkillStillMirrorsAnimatorThenTriggers) {
    // The animator mirror branch runs BEFORE the item check, so an in_skill item
    // press both mirrors the animator AND triggers the item (then returns; no hand).
    CharSkillC13 c(5.0F, 1.0F);
    c.TryActivateSkill();
    const CharSkillC13::AtkDecision d = c.RoleAtk(true, true);
    EXPECT_TRUE(d.animatorMirror);                 // first branch fired
    EXPECT_TRUE(d.animatorMirrorValue);
    EXPECT_TRUE(d.triggerItem);
    EXPECT_FALSE(d.setHandAttack);                 // early return after TriggerItem
}

TEST(CharSkillC13Test, RoleAtkItemReleaseDoesNotTrigger) {
    CharSkillC13 c(5.0F, 1.0F);
    // in_item but NOT pressing -> falls through to the normal hand path.
    const CharSkillC13::AtkDecision d = c.RoleAtk(false, true);
    EXPECT_FALSE(d.triggerItem);
    EXPECT_TRUE(d.setHandAttack);
    EXPECT_FALSE(d.handAttackValue);
}

TEST(CharSkillC13Test, ArmTickDoesNothingWhenNotInSkill) {
    // FAITHFUL: HurtSomeOne's bookkeeping is gated by in_skill (this+0x55).
    CharSkillC13 c(5.0F, 1.0F);
    c.SetSkillAtkFlag(true);             // flag set, but not in skill
    EXPECT_EQ(c.ArmTickFromHit(), 0);
    EXPECT_EQ(c.HurtCount(), 0);
    EXPECT_TRUE(c.SkillAtkFlag());       // flag untouched (gate not entered)
}

TEST(CharSkillC13Test, ArmTickDoesNothingWhenSkillAtkFlagUnset) {
    // In skill but the per-hit skill_atk flag (this+0xAC) is not raised -> no-op.
    CharSkillC13 c(5.0F, 1.0F);
    c.TryActivateSkill();
    EXPECT_FALSE(c.SkillAtkFlag());
    EXPECT_EQ(c.ArmTickFromHit(), 0);
    EXPECT_EQ(c.HurtCount(), 0);
}

TEST(CharSkillC13Test, ArmTickConsumesFlagAndIncrementsCount) {
    // FAITHFUL: when in_skill && skill_atk, the body clears skill_atk and
    // increments hurt_count; the 3rd qualifying hit restores 1 armor and resets.
    CharSkillC13 c(5.0F, 1.0F);
    c.TryActivateSkill();

    c.SetSkillAtkFlag(true);
    EXPECT_EQ(c.ArmTickFromHit(), 0);    // hit 1 -> hurt_count 1, no restore
    EXPECT_EQ(c.HurtCount(), 1);
    EXPECT_FALSE(c.SkillAtkFlag());      // flag consumed

    c.SetSkillAtkFlag(true);
    EXPECT_EQ(c.ArmTickFromHit(), 0);    // hit 2 -> hurt_count 2, no restore
    EXPECT_EQ(c.HurtCount(), 2);

    c.SetSkillAtkFlag(true);
    EXPECT_EQ(c.ArmTickFromHit(), CharSkillC13::kArmorRestoredPerCycle); // hit 3 -> restore 1
    EXPECT_EQ(c.HurtCount(), 0);         // counter reset after the restore
}

TEST(CharSkillC13Test, ArmTickCyclesEveryThirdQualifyingHit) {
    // Two full cycles: a restore on hit 3 and hit 6, none in between.
    CharSkillC13 c(5.0F, 1.0F);
    c.TryActivateSkill();
    std::vector<int> restores;
    for (int hit = 1; hit <= 6; ++hit) {
        c.SetSkillAtkFlag(true);         // owner raises the per-hit flag each hit
        restores.push_back(c.ArmTickFromHit());
    }
    const std::vector<int> expected = {0, 0, CharSkillC13::kArmorRestoredPerCycle,
                                       0, 0, CharSkillC13::kArmorRestoredPerCycle};
    ASSERT_EQ(restores.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(restores[i], expected[i]);
    }
    EXPECT_EQ(c.HurtCount(), 0);
}

TEST(CharSkillC13Test, ArmTickUnflaggedHitsDoNotAdvanceCount) {
    // Only hits whose skill_atk flag was raised count toward the cycle. An
    // un-flagged hit between flagged ones is a no-op and does not advance.
    CharSkillC13 c(5.0F, 1.0F);
    c.TryActivateSkill();
    c.SetSkillAtkFlag(true);
    EXPECT_EQ(c.ArmTickFromHit(), 0);    // flagged hit 1 -> count 1
    EXPECT_EQ(c.ArmTickFromHit(), 0);    // UNflagged hit -> no-op, count stays 1
    EXPECT_EQ(c.HurtCount(), 1);
    c.SetSkillAtkFlag(true);
    EXPECT_EQ(c.ArmTickFromHit(), 0);    // flagged hit 2 -> count 2
    EXPECT_EQ(c.HurtCount(), 2);
}

TEST(CharSkillC13Test, KConstantsMatchDecomp) {
    EXPECT_EQ(CharSkillC13::kHurtCountToRestoreArmor, 3); // "2 < ++hurt_count"
    EXPECT_EQ(CharSkillC13::kArmorRestoredPerCycle, 1);   // RestoreArmor(1)
}

TEST(CharSkillC13Test, NoRngDrawOnAnyPath) {
    // None of the four recovered bodies draws from rg_random; exercising the whole
    // brain must leave the stream un-advanced, so two seeded instances stay in
    // lockstep (a reference stream that does not advance).
    CharSkillC13 a(3.0F, 1.0F);
    CharSkillC13 b(3.0F, 1.0F);
    a.SetSeed(12345);
    b.SetSeed(12345);
    EXPECT_TRUE(a.Seeded());
    for (int i = 0; i < 16; ++i) {
        a.TryActivateSkill(i % 2 == 0);
        a.RoleAtk(true, false);
        a.RoleAtk(false, true);
        a.SetSkillAtkFlag(true);
        a.ArmTickFromHit();
        a.Tick(250.0F);
        a.EndSkill();
        a.Tick(250.0F);
    }
    // 'a' ran the full brain; 'b' was untouched. If any draw had leaked, the next
    // draw on 'a' would diverge from 'b'. Same value -> stream truly untouched.
    EXPECT_EQ(a.Rng().Range(0, 1000000), b.Rng().Range(0, 1000000));
}

TEST(CharSkillC13Test, FullCycleIsDeterministic) {
    // Drive identical activate/attack/hit/cooldown cycles on two instances and
    // compare the observable state at every step -> behaviour is fully deterministic.
    auto run = [](CharSkillC13 &c) -> std::vector<int> {
        std::vector<int> trace;
        for (int i = 0; i < 12; ++i) {
            trace.push_back(c.TryActivateSkill() ? 1 : 0);
            const CharSkillC13::AtkDecision d = c.RoleAtk(true, false);
            trace.push_back(d.animatorMirror ? 1 : 0);
            c.SetSkillAtkFlag(true);
            trace.push_back(c.ArmTickFromHit());
            c.Tick(400.0F);
            c.EndSkill();
            trace.push_back(c.SkillReady() ? 1 : 0);
            c.Tick(1500.0F);
        }
        return trace;
    };
    CharSkillC13 a(2.0F, 1.0F);
    CharSkillC13 b(2.0F, 1.0F);
    const std::vector<int> ta = run(a);
    const std::vector<int> tb = run(b);
    ASSERT_EQ(ta.size(), tb.size());
    for (std::size_t i = 0; i < ta.size(); ++i) {
        EXPECT_EQ(ta[i], tb[i]);
    }
}

// NOLINTEND(readability-magic-numbers)
