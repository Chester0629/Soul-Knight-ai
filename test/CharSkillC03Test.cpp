#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include "combat/CharSkillC03.hpp"

using Game::CharSkillC03;

// NOLINTBEGIN(readability-magic-numbers)

TEST(CharSkillC03Test, StartsReadyAndNotInSkill) {
    CharSkillC03 c(5.0F);
    EXPECT_TRUE(c.SkillReady());        // freshly set-up hero: first ultimate ready
    EXPECT_FALSE(c.InSkill());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 0.0F);
    EXPECT_FLOAT_EQ(c.SkillCd(), 5.0F);
}

TEST(CharSkillC03Test, NegativeSkillCdClampsToZero) {
    CharSkillC03 c(-3.0F);              // guard against bad stat sheet values
    EXPECT_FLOAT_EQ(c.SkillCd(), 0.0F);
    EXPECT_TRUE(c.SkillReady());        // skill_cd 0 -> always ready
}

TEST(CharSkillC03Test, ActivateSucceedsWhenReadyAndIdle) {
    CharSkillC03 c(5.0F);
    EXPECT_TRUE(c.TryActivateSkill());  // skill_ready && !in_skill -> enter
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC03Test, ActivateFailsWhileAlreadyInSkill) {
    CharSkillC03 c(5.0F);
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_FALSE(c.TryActivateSkill()); // in_skill gate (this+0x55) blocks re-entry
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC03Test, ActivateFailsWhileOnCooldown) {
    CharSkillC03 c(5.0F);
    c.TryActivateSkill();
    c.EndSkill();                       // spend -> this_skill_time = 0
    EXPECT_FALSE(c.SkillReady());
    EXPECT_FALSE(c.TryActivateSkill()); // still cooling down (skill_ready gate)
}

TEST(CharSkillC03Test, EndSkillSpendsTheChargeAndLeavesState) {
    // FLAGGED (EndSkillSpend): the in_skill clear + ReSetSkillReload spend modeled
    // here are the base RoleSkillEnd chain, NOT in C03's recovered body (which is a
    // null-guard + get_transform tail-call on this+0x40). Behaviour matches C02's
    // fully-recovered RoleSkillEnd: in_skill = 0; this_skill_time = 0.
    CharSkillC03 c(5.0F);
    c.TryActivateSkill();
    EXPECT_TRUE(c.InSkill());
    c.EndSkill();
    EXPECT_FALSE(c.InSkill());           // in_skill (this+0x55) cleared
    EXPECT_FALSE(c.SkillReady());        // cooldown restarted (this_skill_time = 0)
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 5.0F);
}

TEST(CharSkillC03Test, EndSkillIsNoOpWhenNotInSkill) {
    CharSkillC03 c(5.0F);
    EXPECT_TRUE(c.SkillReady());
    c.EndSkill();                        // no skill active -> no spend
    EXPECT_TRUE(c.SkillReady());
    EXPECT_FALSE(c.InSkill());
}

TEST(CharSkillC03Test, CooldownRechargesAndClamps) {
    CharSkillC03 c(2.0F);
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

TEST(CharSkillC03Test, TickAdvancesCooldownWhileInSkillAndNeverAutoEnds) {
    // FAITHFUL: C03Controller__Update @ 156201 has NO in_skill_time countdown and
    // NO auto-end (matching C02, unlike C01) -- it is just awake-gated
    // AttributeUpdate + SeachUpdate. The cooldown count-up is unconditional (the
    // body has no in_skill branch at all), and Tick must NEVER end the skill on a
    // timer. Drive many ticks inside the skill and assert it stays active.
    CharSkillC03 c(4.0F);
    c.TryActivateSkill();
    c.EndSkill();                        // spend so the cooldown is observably < cd
    EXPECT_FALSE(c.SkillReady());
    EXPECT_TRUE(c.TryActivateSkill() == false); // on cooldown -> cannot re-enter yet
    c.Tick(2000.0F);                     // recharge halfway (2.0 / 4.0)
    EXPECT_FALSE(c.SkillReady());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 2.0F);
    c.Tick(2000.0F);                     // finish recharge
    EXPECT_TRUE(c.SkillReady());
    EXPECT_TRUE(c.TryActivateSkill());   // ready again
    EXPECT_TRUE(c.InSkill());
    // Now inside the skill, hammer Tick: there is no auto-end timer, so it stays in
    // skill no matter how long we tick (cooldown count-up clamps harmlessly).
    for (int i = 0; i < 100; ++i) {
        c.Tick(1000.0F);
        EXPECT_TRUE(c.InSkill());        // never auto-ends
    }
}

TEST(CharSkillC03Test, NonPositiveDtIsIgnored) {
    CharSkillC03 c(3.0F);
    c.TryActivateSkill();
    c.EndSkill();
    const float before = c.CooldownRemaining();
    c.Tick(0.0F);
    c.Tick(-100.0F);
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), before); // no advance
}

TEST(CharSkillC03Test, ZeroCooldownIsAlwaysReadyAndReusable) {
    // skill_cd 0 -> skill_ready every frame; EndSkill leaves this_skill_time 0 which
    // (>= 0 skill_cd) is immediately ready again.
    CharSkillC03 c(0.0F);
    EXPECT_TRUE(c.SkillReady());
    EXPECT_TRUE(c.TryActivateSkill());
    c.EndSkill();
    EXPECT_TRUE(c.SkillReady());          // skill_cd 0 -> ready right after the spend
    EXPECT_TRUE(c.TryActivateSkill());
}

TEST(CharSkillC03Test, NoRngDrawOnAnyPath) {
    // None of the five recovered C03 bodies draws from rg_random (heroes are
    // player-driven); exercising the whole brain must leave the stream un-advanced,
    // so two seeded instances stay in lockstep.
    CharSkillC03 a(3.0F);
    CharSkillC03 b(3.0F);
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

TEST(CharSkillC03Test, FullCycleIsDeterministic) {
    // Drive identical activate/cooldown cycles on two instances and compare the
    // observable state at every step -> behaviour is fully deterministic.
    auto run = [](CharSkillC03 &c) -> std::vector<int> {
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
    CharSkillC03 a(2.0F);
    CharSkillC03 b(2.0F);
    const auto ta = run(a);
    const auto tb = run(b);
    ASSERT_EQ(ta.size(), tb.size());
    for (std::size_t i = 0; i < ta.size(); ++i) {
        EXPECT_EQ(ta[i], tb[i]);
    }
}

// NOLINTEND(readability-magic-numbers)
