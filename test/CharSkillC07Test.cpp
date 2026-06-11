#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include "combat/CharSkillC07.hpp"

using Game::CharSkillC07;

// NOLINTBEGIN(readability-magic-numbers)

TEST(CharSkillC07Test, StartsReadyAndNotInSkill) {
    CharSkillC07 c(5.0F);
    EXPECT_TRUE(c.SkillReady());        // freshly set-up hero: first ultimate ready
    EXPECT_FALSE(c.InSkill());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 0.0F);
    EXPECT_FLOAT_EQ(c.SkillCd(), 5.0F);
    EXPECT_FLOAT_EQ(c.SkillEndCredit(), 0.0F); // banked credit defaults to 0
}

TEST(CharSkillC07Test, NegativeSkillCdClampsToZero) {
    CharSkillC07 c(-3.0F);              // guard against a bad stat sheet value
    EXPECT_FLOAT_EQ(c.SkillCd(), 0.0F);
    EXPECT_TRUE(c.SkillReady());        // skill_cd 0 -> always ready
}

TEST(CharSkillC07Test, ActivateSucceedsWhenReadyAndIdle) {
    // FAITHFUL: C07Controller__RoleSkill @ 156759 gate (skill_ready && !in_skill).
    CharSkillC07 c(5.0F);
    EXPECT_TRUE(c.TryActivateSkill());  // enter -> in_skill = 1 (spawn tail 0x55)
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC07Test, ActivateFailsWhileAlreadyInSkill) {
    CharSkillC07 c(5.0F);
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_FALSE(c.TryActivateSkill()); // in_skill gate (this+0x55) blocks re-entry
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC07Test, ActivateFailsWhileOnCooldown) {
    CharSkillC07 c(5.0F);
    c.TryActivateSkill();
    c.EndSkill();                       // RoleSkillEnd -> this_skill_time = 0
    EXPECT_FALSE(c.SkillReady());
    EXPECT_FALSE(c.TryActivateSkill()); // still cooling down -> skill_ready gate fails
}

TEST(CharSkillC07Test, EndSkillLeavesStateAndRestartsCooldown) {
    // FAITHFUL: C07Controller__RoleSkillEnd @ 156834 -- in_skill=0 then
    // ReSetSkillReload (this_skill_time=0) with no banked credit (0xa0 == 0).
    CharSkillC07 c(5.0F);
    c.TryActivateSkill();
    EXPECT_TRUE(c.InSkill());
    c.EndSkill();
    EXPECT_FALSE(c.InSkill());
    EXPECT_FALSE(c.SkillReady());        // cooldown restarted (this_skill_time = 0)
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 5.0F);
}

TEST(CharSkillC07Test, EndSkillIsNoOpWhenNotInSkill) {
    CharSkillC07 c(5.0F);
    EXPECT_TRUE(c.SkillReady());
    c.EndSkill();                        // no skill active -> no spend
    EXPECT_TRUE(c.SkillReady());
    EXPECT_FALSE(c.InSkill());
}

TEST(CharSkillC07Test, EndSkillBanksCreditTowardCooldown) {
    // FAITHFUL: RoleSkillEnd does ReSetSkillReload(0) THEN SkillReload(this+0xa0),
    // so a banked credit gives the cooldown a head start, then 0xa0 is consumed.
    CharSkillC07 c(5.0F);
    c.SetSkillEndCredit(2.0F);           // bank 2.0s toward the next cooldown
    c.TryActivateSkill();
    c.EndSkill();                        // this_skill_time = 0 + 2.0 = 2.0
    EXPECT_FALSE(c.SkillReady());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 3.0F); // 5.0 - 2.0 credited
    EXPECT_FLOAT_EQ(c.SkillEndCredit(), 0.0F);    // credit consumed (this+0xa0 = 0)
}

TEST(CharSkillC07Test, EndSkillCreditClampsAtSkillCd) {
    // SkillReload caps this_skill_time at skill_cd; an over-large credit cannot
    // overshoot ready.
    CharSkillC07 c(3.0F);
    c.SetSkillEndCredit(10.0F);          // more than skill_cd
    c.TryActivateSkill();
    c.EndSkill();                        // 0 + 10 -> clamped to 3.0 -> ready
    EXPECT_TRUE(c.SkillReady());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 0.0F);
    EXPECT_FLOAT_EQ(c.SkillEndCredit(), 0.0F);    // still consumed
}

TEST(CharSkillC07Test, NegativeCreditClampsToZero) {
    CharSkillC07 c(4.0F);
    c.SetSkillEndCredit(-5.0F);           // bad value -> clamps to 0 (no credit)
    EXPECT_FLOAT_EQ(c.SkillEndCredit(), 0.0F);
    c.TryActivateSkill();
    c.EndSkill();
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 4.0F); // behaves like plain ReSetSkillReload
}

TEST(CharSkillC07Test, CooldownRechargesAndClamps) {
    CharSkillC07 c(2.0F);
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

TEST(CharSkillC07Test, UpdateHasNoAutoEndSkillStaysActiveThroughTicks) {
    // FAITHFUL (C07 vs C01 difference): C07Controller__Update @ 156746 only does
    // AttributeUpdate + SeachUpdate -- there is NO in_skill_time countdown and NO
    // auto-end. So ticking while in_skill must NOT end the skill; it only advances
    // the (already-clamped) cooldown. The skill ends only via explicit EndSkill.
    CharSkillC07 c(4.0F);
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_TRUE(c.InSkill());
    for (int i = 0; i < 100; ++i) {
        c.Tick(100.0F);                  // 10s of ticking inside the skill
        EXPECT_TRUE(c.InSkill());        // never auto-ends
    }
    EXPECT_TRUE(c.InSkill());
    c.EndSkill();                        // only explicit end leaves the state
    EXPECT_FALSE(c.InSkill());
}

TEST(CharSkillC07Test, NonPositiveDtIsIgnored) {
    CharSkillC07 c(3.0F);
    c.TryActivateSkill();
    c.EndSkill();
    const float before = c.CooldownRemaining();
    c.Tick(0.0F);
    c.Tick(-100.0F);
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), before); // no advance
}

TEST(CharSkillC07Test, NoRngDrawOnAnyPath) {
    // None of the five recovered C07 bodies draws from rg_random; exercising the
    // whole brain must leave the stream un-advanced, so two seeded instances stay
    // in lockstep (a parallel same-seeded reference that does not advance).
    CharSkillC07 a(3.0F);
    CharSkillC07 b(3.0F);
    a.SetSeed(12345);
    b.SetSeed(12345);
    EXPECT_TRUE(a.Seeded());
    for (int i = 0; i < 16; ++i) {
        a.TryActivateSkill();
        a.SetSkillEndCredit(0.5F);
        a.Tick(250.0F);
        a.EndSkill();
        a.Tick(250.0F);
    }
    // 'a' ran the full brain; 'b' was untouched. If any draw had leaked, the next
    // draw on 'a' would diverge from 'b'. Same value -> stream truly untouched.
    EXPECT_EQ(a.Rng().Range(0, 1000000), b.Rng().Range(0, 1000000));
}

TEST(CharSkillC07Test, FullCycleIsDeterministic) {
    // Drive identical activate/end/cooldown cycles on two instances and compare
    // the observable state at every step -> behaviour is fully deterministic.
    auto run = [](CharSkillC07 &c) -> std::vector<int> {
        std::vector<int> trace;
        for (int i = 0; i < 12; ++i) {
            trace.push_back(c.TryActivateSkill() ? 1 : 0);
            trace.push_back(c.InSkill() ? 1 : 0);
            c.Tick(400.0F);
            c.EndSkill();
            trace.push_back(c.SkillReady() ? 1 : 0);
            c.Tick(1500.0F);
        }
        return trace;
    };
    CharSkillC07 a(2.0F);
    CharSkillC07 b(2.0F);
    const auto ta = run(a);
    const auto tb = run(b);
    ASSERT_EQ(ta.size(), tb.size());
    for (std::size_t i = 0; i < ta.size(); ++i) {
        EXPECT_EQ(ta[i], tb[i]);
    }
}

// NOLINTEND(readability-magic-numbers)
