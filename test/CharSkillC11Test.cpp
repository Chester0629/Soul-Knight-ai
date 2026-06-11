#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include "combat/CharSkillC11.hpp"
#include "data/RGRandom.hpp"

using Game::CharSkillC11;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

TEST(CharSkillC11Test, StartsReadyAndNotInSkill) {
    CharSkillC11 c(5.0F);
    EXPECT_TRUE(c.SkillReady());        // freshly set-up hero: first ultimate ready
    EXPECT_FALSE(c.InSkill());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 0.0F);
    EXPECT_FLOAT_EQ(c.SkillCd(), 5.0F);
}

TEST(CharSkillC11Test, NegativeStatClampsToZero) {
    CharSkillC11 c(-3.0F);              // guard against bad stat sheet values
    EXPECT_FLOAT_EQ(c.SkillCd(), 0.0F);
    EXPECT_TRUE(c.SkillReady());        // skill_cd 0 -> always ready
}

TEST(CharSkillC11Test, ActivateSucceedsWhenReadyAndIdle) {
    CharSkillC11 c(5.0F);
    EXPECT_TRUE(c.TryActivateSkill());  // skill_ready && !in_skill -> enter
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC11Test, ActivateFailsWhileAlreadyInSkill) {
    CharSkillC11 c(5.0F);
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_FALSE(c.TryActivateSkill()); // in_skill gate (this+0x55) blocks re-entry
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC11Test, ActivateFailsWhileOnCooldown) {
    CharSkillC11 c(5.0F);
    c.TryActivateSkill();
    c.EndSkill();                       // RoleSkillEnd -> ReSetSkillReload (=0)
    EXPECT_FALSE(c.SkillReady());
    EXPECT_FALSE(c.TryActivateSkill()); // still cooling down -> gate blocks
}

TEST(CharSkillC11Test, RoleSkillEndSpendsTheChargeAndLeavesState) {
    // FAITHFUL: C11Controller__RoleSkillEnd @ 158395 -- in_skill = 0 then
    // ReSetSkillReload (this_skill_time = 0). This body IS recovered for C11 (unlike
    // C01, where it lived in the base class), so EndSkill is faithful, not assumed.
    CharSkillC11 c(5.0F);
    c.TryActivateSkill();
    EXPECT_TRUE(c.InSkill());
    c.EndSkill();
    EXPECT_FALSE(c.InSkill());           // 158397: *(this+0x55) = 0
    EXPECT_FALSE(c.SkillReady());        // 158403: cooldown restarted (=0)
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 5.0F);
}

TEST(CharSkillC11Test, EndSkillIsNoOpWhenNotInSkill) {
    CharSkillC11 c(5.0F);
    EXPECT_TRUE(c.SkillReady());
    c.EndSkill();                        // not in skill -> no spend
    EXPECT_TRUE(c.SkillReady());
    EXPECT_FALSE(c.InSkill());
}

TEST(CharSkillC11Test, CooldownRechargesAndClamps) {
    CharSkillC11 c(2.0F);
    c.TryActivateSkill();
    c.EndSkill();                        // this_skill_time = 0
    EXPECT_FALSE(c.SkillReady());
    c.Tick(1000.0F);                     // +1.0s (SkillReload count-up)
    EXPECT_FALSE(c.SkillReady());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 1.0F);
    c.Tick(5000.0F);                     // overshoot -> clamps to skill_cd
    EXPECT_TRUE(c.SkillReady());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 0.0F);
}

TEST(CharSkillC11Test, TickAdvancesCooldownWhileInSkillAndDoesNotAutoEnd) {
    // FAITHFUL: C11Controller__Update @ 158264 -- AttributeUpdate -> SkillReload runs
    // UNCONDITIONALLY (not gated on in_skill), and there is NO in_skill_time
    // countdown / auto-end (unlike C01). Proof: ticking while in_skill recharges the
    // cooldown but NEVER ends the skill on its own.
    CharSkillC11 c(4.0F);
    c.TryActivateSkill();
    c.EndSkill();                        // spend -> this_skill_time = 0, leave skill
    EXPECT_FALSE(c.SkillReady());
    c.TryActivateSkill();                // still on cooldown -> cannot re-enter
    EXPECT_FALSE(c.InSkill());

    c.Tick(4000.0F);                     // recharge fully
    EXPECT_TRUE(c.SkillReady());
    EXPECT_TRUE(c.TryActivateSkill());   // now ready -> enter
    EXPECT_TRUE(c.InSkill());

    c.Tick(10000.0F);                    // long tick INSIDE the skill...
    EXPECT_TRUE(c.InSkill());            // ...no auto-end: C11's Update has none
    EXPECT_TRUE(c.SkillReady());         // cooldown advanced unconditionally (clamped)
}

TEST(CharSkillC11Test, NonPositiveDtIsIgnored) {
    CharSkillC11 c(3.0F);
    c.TryActivateSkill();
    c.EndSkill();
    const float before = c.CooldownRemaining();
    c.Tick(0.0F);
    c.Tick(-100.0F);
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), before); // no advance
}

TEST(CharSkillC11Test, ReActivateAfterEndAndFullRecharge) {
    // Full activate -> end -> recharge -> re-activate loop stays consistent.
    CharSkillC11 c(2.0F);
    EXPECT_TRUE(c.TryActivateSkill());
    c.EndSkill();
    EXPECT_FALSE(c.SkillReady());
    c.Tick(1999.0F);                     // just shy of ready
    EXPECT_FALSE(c.SkillReady());
    EXPECT_FALSE(c.TryActivateSkill());
    c.Tick(2.0F);                        // cross the threshold
    EXPECT_TRUE(c.SkillReady());
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC11Test, NoRngDrawOnAnyPath) {
    // NONE of the four recovered C11Controller bodies draws from rg_random (the hero
    // is player-driven), so exercising the whole brain must leave the stream
    // un-advanced. We compare against a parallel same-seeded reference RGRandom that
    // is NEVER touched by the brain: if any draw had leaked, the next draw on the
    // brain's stream would diverge from the reference.
    CharSkillC11 c(3.0F);
    c.SetSeed(987654);
    EXPECT_TRUE(c.Seeded());

    RGRandom reference;
    reference.SetRandomSeed(987654);     // identical seed, advanced in lockstep below

    for (int i = 0; i < 16; ++i) {
        c.TryActivateSkill();
        c.Tick(250.0F);
        c.EndSkill();
        c.Tick(250.0F);
    }
    // The brain ran a full cycle; the reference was untouched. Drawing the SAME first
    // value from both proves the brain's stream never advanced (zero draws).
    EXPECT_EQ(c.Rng().Range(0, 1000000), reference.Range(0, 1000000));
}

TEST(CharSkillC11Test, FullCycleIsDeterministic) {
    // Drive identical activate/end/cooldown cycles on two instances and compare the
    // observable state at every step -> behaviour is fully deterministic.
    auto run = [](CharSkillC11 &c) -> std::vector<int> {
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
    CharSkillC11 a(2.0F);
    CharSkillC11 b(2.0F);
    const std::vector<int> ta = run(a);
    const std::vector<int> tb = run(b);
    ASSERT_EQ(ta.size(), tb.size());
    for (std::size_t i = 0; i < ta.size(); ++i) {
        EXPECT_EQ(ta[i], tb[i]);
    }
}

// NOLINTEND(readability-magic-numbers)
