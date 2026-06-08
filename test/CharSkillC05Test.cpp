#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include "combat/CharSkillC05.hpp"

using Game::CharSkillC05;

// NOLINTBEGIN(readability-magic-numbers)

TEST(CharSkillC05Test, StartsReadyAndNotInSkill) {
    CharSkillC05 c(5.0F);
    EXPECT_TRUE(c.SkillReady());        // freshly set-up hero: first ultimate ready
    EXPECT_FALSE(c.InSkill());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 0.0F);
    EXPECT_FLOAT_EQ(c.SkillCd(), 5.0F);
}

TEST(CharSkillC05Test, NegativeStatClampsToZero) {
    CharSkillC05 c(-3.0F);              // guard against bad stat sheet values
    EXPECT_FLOAT_EQ(c.SkillCd(), 0.0F);
    EXPECT_TRUE(c.SkillReady());        // skill_cd 0 -> always ready
}

TEST(CharSkillC05Test, ActivateSucceedsWhenReadyAndIdle) {
    // FAITHFUL: C05Controller__RoleSkill @ 156562 gate -- skill_ready && !in_skill.
    CharSkillC05 c(5.0F);
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC05Test, ActivateFailsWhileAlreadyInSkill) {
    // 156582: the !in_skill (this+0x55) leg of the gate blocks re-entry.
    CharSkillC05 c(5.0F);
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_FALSE(c.TryActivateSkill());
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC05Test, ActivateFailsWhileOnCooldown) {
    // 156577: the skill_ready (this+0x44 -> get_skill_ready) leg blocks activation.
    CharSkillC05 c(5.0F);
    c.TryActivateSkill();
    c.EndSkill();                       // spend -> this_skill_time = 0
    EXPECT_FALSE(c.SkillReady());
    EXPECT_FALSE(c.TryActivateSkill()); // still cooling down
}

TEST(CharSkillC05Test, EndSkillSpendsTheChargeAndLeavesState) {
    // Models base RoleSkillEnd -> ReSetSkillReload @ 432381 (this_skill_time = 0).
    CharSkillC05 c(5.0F);
    c.TryActivateSkill();
    EXPECT_TRUE(c.InSkill());
    c.EndSkill();
    EXPECT_FALSE(c.InSkill());
    EXPECT_FALSE(c.SkillReady());        // cooldown restarted (this_skill_time = 0)
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 5.0F);
}

TEST(CharSkillC05Test, EndSkillIsNoOpWhenNotInSkill) {
    CharSkillC05 c(5.0F);
    EXPECT_TRUE(c.SkillReady());
    c.EndSkill();                        // no skill active -> no spend
    EXPECT_TRUE(c.SkillReady());
    EXPECT_FALSE(c.InSkill());
}

TEST(CharSkillC05Test, CooldownRechargesAndClamps) {
    // FAITHFUL: SkillReload @ 432455 -- this_skill_time counts UP by dt, clamped
    // to skill_cd. get_skill_ready @ 432483 -- ready iff skill_cd <= this_skill_time.
    CharSkillC05 c(2.0F);
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

TEST(CharSkillC05Test, TickAdvancesCooldownWhileInSkillNoAutoEnd) {
    // KEY DIFFERENCE FROM C01: C05Controller__Update @ 156549 has NO in_skill
    // block -- no in_skill_time (param_1[0x25]) countdown and no RoleSkillEnd
    // auto-end. So while in_skill, Tick must advance ONLY the cooldown and must
    // NOT end the skill. (Compare C01, whose Update auto-ends.) Proof: enter the
    // skill, spend so the cooldown restarts, then tick repeatedly -- the cooldown
    // recharges but the skill never auto-ends.
    CharSkillC05 c(2.0F);
    c.TryActivateSkill();
    EXPECT_TRUE(c.InSkill());
    for (int i = 0; i < 100; ++i) {
        c.Tick(1000.0F);                 // 100 seconds of ticks INSIDE the skill
        EXPECT_TRUE(c.InSkill());        // C05 NEVER auto-ends (no in_skill block)
    }
    EXPECT_TRUE(c.InSkill());
    // The cooldown's SkillReload ran every tick (clamped at skill_cd from
    // activation, so the hero stays ready) -- but the skill itself did not end.
    EXPECT_TRUE(c.SkillReady());
}

TEST(CharSkillC05Test, SkillStaysActiveUntilExplicitEnd) {
    // The active window is owner/EndSkill-driven (base RoleSkillEnd), NOT a C05
    // timer. The skill stays up across arbitrary ticks until EndSkill is called.
    CharSkillC05 c(4.0F);
    c.TryActivateSkill();
    c.Tick(10000.0F);                    // huge tick: still active (no auto-end)
    EXPECT_TRUE(c.InSkill());
    c.EndSkill();                        // explicit end (base RoleSkillEnd chain)
    EXPECT_FALSE(c.InSkill());
    EXPECT_FALSE(c.SkillReady());        // ReSetSkillReload restarted the cooldown
}

TEST(CharSkillC05Test, NonPositiveDtIsIgnored) {
    CharSkillC05 c(3.0F);
    c.TryActivateSkill();
    c.EndSkill();
    const float before = c.CooldownRemaining();
    c.Tick(0.0F);
    c.Tick(-100.0F);
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), before); // no advance
}

TEST(CharSkillC05Test, ReactivateAfterFullRecharge) {
    // End-to-end: activate, end (spend), recharge fully, activate again.
    CharSkillC05 c(2.0F);
    EXPECT_TRUE(c.TryActivateSkill());
    c.EndSkill();
    EXPECT_FALSE(c.SkillReady());
    c.Tick(2000.0F);                     // full recharge
    EXPECT_TRUE(c.SkillReady());
    EXPECT_TRUE(c.TryActivateSkill());   // ready again -> activates
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC05Test, NoRngDrawOnAnyPath) {
    // None of the three recovered C05 bodies draws from rg_random (C05 is a
    // player-driven hero -> ZERO draws). Exercising the whole brain must leave
    // the stream un-advanced, so two seeded instances stay in lockstep.
    CharSkillC05 a(3.0F);
    CharSkillC05 b(3.0F);
    a.SetSeed(12345);
    b.SetSeed(12345);
    EXPECT_TRUE(a.Seeded());
    for (int i = 0; i < 16; ++i) {
        a.TryActivateSkill();
        a.Tick(250.0F);
        a.EndSkill();
        a.Tick(250.0F);
    }
    // 'a' ran the full brain; 'b' was untouched. If any draw had leaked, the next
    // draw on 'a' would diverge from 'b'. Same value -> stream truly untouched.
    EXPECT_EQ(a.Rng().Range(0, 1000000), b.Rng().Range(0, 1000000));
}

TEST(CharSkillC05Test, FullCycleIsDeterministic) {
    // Drive identical activate/cooldown cycles on two instances and compare the
    // observable state at every step -> behaviour is fully deterministic.
    auto run = [](CharSkillC05 &c) -> std::vector<int> {
        std::vector<int> trace;
        for (int i = 0; i < 12; ++i) {
            trace.push_back(c.TryActivateSkill() ? 1 : 0);
            trace.push_back(c.InSkill() ? 1 : 0);
            c.Tick(400.0F);
            c.EndSkill();
            trace.push_back(c.SkillReady() ? 1 : 0);
            c.Tick(1500.0F);
            trace.push_back(c.SkillReady() ? 1 : 0);
        }
        return trace;
    };
    CharSkillC05 a(2.0F);
    CharSkillC05 b(2.0F);
    const auto ta = run(a);
    const auto tb = run(b);
    ASSERT_EQ(ta.size(), tb.size());
    for (std::size_t i = 0; i < ta.size(); ++i) {
        EXPECT_EQ(ta[i], tb[i]);
    }
}

// NOLINTEND(readability-magic-numbers)
