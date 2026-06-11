#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include "combat/CharSkillC01.hpp"

using Game::CharSkillC01;

// NOLINTBEGIN(readability-magic-numbers)

TEST(CharSkillC01Test, StartsReadyAndNotInSkill) {
    CharSkillC01 c(5.0F, 1.0F);
    EXPECT_TRUE(c.SkillReady());        // freshly set-up hero: first ultimate ready
    EXPECT_FALSE(c.InSkill());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 0.0F);
    EXPECT_FLOAT_EQ(c.SkillCd(), 5.0F);
    EXPECT_FLOAT_EQ(c.InSkillTime(), 1.0F);
}

TEST(CharSkillC01Test, NegativeStatsClampToZero) {
    CharSkillC01 c(-3.0F, -2.0F);       // guard against bad stat sheet values
    EXPECT_FLOAT_EQ(c.SkillCd(), 0.0F);
    EXPECT_FLOAT_EQ(c.InSkillTime(), 0.0F);
    EXPECT_TRUE(c.SkillReady());        // skill_cd 0 -> always ready
}

TEST(CharSkillC01Test, ActivateSucceedsWhenReadyAndIdle) {
    CharSkillC01 c(5.0F, 1.0F);
    EXPECT_TRUE(c.TryActivateSkill());  // skill_ready && !in_skill -> enter
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC01Test, ActivateFailsWhileAlreadyInSkill) {
    CharSkillC01 c(5.0F, 1.0F);
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_FALSE(c.TryActivateSkill()); // in_skill gate blocks re-entry
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC01Test, ActivateFailsWhileOnCooldown) {
    CharSkillC01 c(5.0F, 1.0F);
    c.TryActivateSkill();
    c.EndSkill();                       // spend -> this_skill_time = 0
    EXPECT_FALSE(c.SkillReady());
    EXPECT_FALSE(c.TryActivateSkill()); // still cooling down
}

TEST(CharSkillC01Test, EndSkillSpendsTheChargeAndLeavesState) {
    CharSkillC01 c(5.0F, 1.0F);
    c.TryActivateSkill();
    EXPECT_TRUE(c.InSkill());
    c.EndSkill();
    EXPECT_FALSE(c.InSkill());
    EXPECT_FALSE(c.SkillReady());        // cooldown restarted (this_skill_time = 0)
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 5.0F);
}

TEST(CharSkillC01Test, EndSkillIsNoOpWhenNotInSkill) {
    CharSkillC01 c(5.0F, 1.0F);
    EXPECT_TRUE(c.SkillReady());
    c.EndSkill();                        // no skill active -> no spend
    EXPECT_TRUE(c.SkillReady());
    EXPECT_FALSE(c.InSkill());
}

TEST(CharSkillC01Test, CooldownRechargesAndClamps) {
    CharSkillC01 c(2.0F, 1.0F);
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

TEST(CharSkillC01Test, TickRunsWhileInSkill) {
    // FAITHFUL (MUST-FIX 1): C01Controller__Update does NOT early-return on
    // in_skill -- AttributeUpdate -> SkillReload runs unconditionally, then the
    // in_skill body runs. The old Tick early-returned whenever m_InSkill, which
    // would freeze the in_skill_time countdown. Proof Tick now executes inside the
    // skill: a tick mid-window decrements in_skill_time (instead of being a no-op).
    CharSkillC01 c(4.0F, 2.0F);
    EXPECT_TRUE(c.TryActivateSkill());    // enter (in_skill_time armed to 2.0)
    EXPECT_TRUE(c.InSkill());
    c.Tick(1000.0F);                      // -1.0s INSIDE the skill (no auto-end yet)
    EXPECT_TRUE(c.InSkill());
    EXPECT_FLOAT_EQ(c.InSkillTimeRemaining(), 1.0F); // not frozen -> Tick ran in-skill
    // The cooldown's unconditional SkillReload also ran; this_skill_time is already
    // clamped at skill_cd from activation, so the hero stays ready (the advance is a
    // clamped no-op at full charge, but the call is no longer gated out).
    EXPECT_TRUE(c.SkillReady());
}

TEST(CharSkillC01Test, InSkillTimeCountsDownAndAutoEnds) {
    // FAITHFUL (MUST-FIX 2): C01Controller__Update decrements in_skill_time
    // (param_1[0x25]) each frame and auto-ends (RoleSkillEnd, slot 0x17c) when it
    // reaches <= 0. The auto-end runs ReSetSkillReload (this_skill_time = 0).
    CharSkillC01 c(5.0F, 1.0F);
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_TRUE(c.InSkill());
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

TEST(CharSkillC01Test, AutoEndOnExactZeroBoundary) {
    // in_skill_time reaching exactly 0 must auto-end (<= 0, not < 0).
    CharSkillC01 c(3.0F, 1.0F);
    c.TryActivateSkill();
    c.Tick(1000.0F);                      // -1.0s -> exactly 0 -> auto-end
    EXPECT_FALSE(c.InSkill());
    EXPECT_FALSE(c.SkillReady());         // cooldown restarted at end
}

TEST(CharSkillC01Test, ManualEndSkillBeforeAutoEnd) {
    // EndSkill() called before the window expires still ends + re-arms cleanly, and
    // the in_skill_time countdown is cleared (no stale auto-end on a later tick).
    CharSkillC01 c(2.0F, 5.0F);
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

TEST(CharSkillC01Test, NonPositiveDtIsIgnored) {
    CharSkillC01 c(3.0F, 1.0F);
    c.TryActivateSkill();
    c.EndSkill();
    const float before = c.CooldownRemaining();
    c.Tick(0.0F);
    c.Tick(-100.0F);
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), before); // no advance
}

TEST(CharSkillC01Test, RoleAtkPressFiresPrimaryHandNoMirrorWhenNotInSkill) {
    CharSkillC01 c(5.0F, 1.0F);
    const CharSkillC01::AtkDecision d = c.RoleAtk(true, false);
    EXPECT_FALSE(d.triggerItem);
    EXPECT_TRUE(d.setHandAttack);
    EXPECT_TRUE(d.handAttackValue);
    EXPECT_FALSE(d.mirrorSecondHand);    // dual-hand mirror only while in_skill
}

TEST(CharSkillC01Test, RoleAtkReleaseStopsPrimaryHand) {
    CharSkillC01 c(5.0F, 1.0F);
    const CharSkillC01::AtkDecision d = c.RoleAtk(false, false);
    EXPECT_TRUE(d.setHandAttack);
    EXPECT_FALSE(d.handAttackValue);     // release -> SetAttack(false)
    EXPECT_FALSE(d.mirrorSecondHand);
}

TEST(CharSkillC01Test, RoleAtkMirrorsSecondHandWhileInSkill) {
    CharSkillC01 c(5.0F, 1.0F);
    c.TryActivateSkill();
    EXPECT_TRUE(c.InSkill());
    const CharSkillC01::AtkDecision press = c.RoleAtk(true, false);
    EXPECT_TRUE(press.setHandAttack);
    EXPECT_TRUE(press.handAttackValue);
    EXPECT_TRUE(press.mirrorSecondHand);          // Invoke("Hand2Atk", 0.1f)
    EXPECT_TRUE(press.secondHandAttackValue);

    const CharSkillC01::AtkDecision release = c.RoleAtk(false, false);
    EXPECT_TRUE(release.mirrorSecondHand);         // Invoke("Hand2AtkStop", 0.1f)
    EXPECT_FALSE(release.secondHandAttackValue);
}

TEST(CharSkillC01Test, RoleAtkItemPressTriggersItemAndSkipsHand) {
    CharSkillC01 c(5.0F, 1.0F);
    const CharSkillC01::AtkDecision d = c.RoleAtk(true, true); // on a pickup, pressing
    EXPECT_TRUE(d.triggerItem);
    EXPECT_FALSE(d.setHandAttack);                 // early return: no hand.SetAttack
    EXPECT_FALSE(d.mirrorSecondHand);              // not in skill -> 2nd hand untouched
}

TEST(CharSkillC01Test, RoleAtkItemPressInSkillStopsSecondHandFirst) {
    CharSkillC01 c(5.0F, 1.0F);
    c.TryActivateSkill();
    const CharSkillC01::AtkDecision d = c.RoleAtk(true, true);
    EXPECT_TRUE(d.triggerItem);
    EXPECT_FALSE(d.setHandAttack);
    EXPECT_TRUE(d.mirrorSecondHand);               // stop the 2nd hand before trigger
    EXPECT_FALSE(d.secondHandAttackValue);         // Hand2AtkStop
}

TEST(CharSkillC01Test, RoleAtkItemReleaseDoesNotTrigger) {
    CharSkillC01 c(5.0F, 1.0F);
    // in_item but NOT pressing -> falls through to the normal hand path.
    const CharSkillC01::AtkDecision d = c.RoleAtk(false, true);
    EXPECT_FALSE(d.triggerItem);
    EXPECT_TRUE(d.setHandAttack);
    EXPECT_FALSE(d.handAttackValue);
}

TEST(CharSkillC01Test, Hand2AttackDelayIsTenthSecond) {
    EXPECT_FLOAT_EQ(CharSkillC01::kHand2AttackDelay, 0.1F); // 0x3dcccccd operand
}

TEST(CharSkillC01Test, NoRngDrawOnAnyPath) {
    // Neither recovered body draws from rg_random; exercising the whole brain must
    // leave the stream un-advanced, so two seeded instances stay in lockstep.
    CharSkillC01 a(3.0F, 1.0F);
    CharSkillC01 b(3.0F, 1.0F);
    a.SetSeed(12345);
    b.SetSeed(12345);
    EXPECT_TRUE(a.Seeded());
    for (int i = 0; i < 16; ++i) {
        a.TryActivateSkill();
        a.RoleAtk(true, false);
        a.RoleAtk(false, true);
        a.Tick(250.0F);
        a.EndSkill();
        a.Tick(250.0F);
    }
    // 'a' ran the full brain; 'b' was untouched. If any draw had leaked, the next
    // draw on 'a' would diverge from 'b'. Same value -> stream truly untouched.
    EXPECT_EQ(a.Rng().Range(0, 1000000), b.Rng().Range(0, 1000000));
}

TEST(CharSkillC01Test, FullCycleIsDeterministic) {
    // Drive identical activate/attack/cooldown cycles on two instances and compare
    // the observable state at every step -> behaviour is fully deterministic.
    auto run = [](CharSkillC01 &c) {
        std::vector<int> trace;
        for (int i = 0; i < 12; ++i) {
            trace.push_back(c.TryActivateSkill() ? 1 : 0);
            const CharSkillC01::AtkDecision d = c.RoleAtk(true, false);
            trace.push_back(d.mirrorSecondHand ? 1 : 0);
            c.Tick(400.0F);
            c.EndSkill();
            trace.push_back(c.SkillReady() ? 1 : 0);
            c.Tick(1500.0F);
        }
        return trace;
    };
    CharSkillC01 a(2.0F, 1.0F);
    CharSkillC01 b(2.0F, 1.0F);
    const auto ta = run(a);
    const auto tb = run(b);
    ASSERT_EQ(ta.size(), tb.size());
    for (std::size_t i = 0; i < ta.size(); ++i) {
        EXPECT_EQ(ta[i], tb[i]);
    }
}

// NOLINTEND(readability-magic-numbers)
