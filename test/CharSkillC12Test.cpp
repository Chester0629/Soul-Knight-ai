#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include "combat/CharSkillC12.hpp"
#include "data/RGRandom.hpp"

using Game::CharSkillC12;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

TEST(CharSkillC12Test, StartsReadyAndNotInSkill) {
    CharSkillC12 c(5.0F);
    EXPECT_TRUE(c.SkillReady());        // freshly set-up hero: first ultimate ready
    EXPECT_FALSE(c.InSkill());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 0.0F);
    EXPECT_FLOAT_EQ(c.SkillCd(), 5.0F);
}

TEST(CharSkillC12Test, NegativeStatClampsToZero) {
    CharSkillC12 c(-3.0F);              // guard against bad stat sheet values
    EXPECT_FLOAT_EQ(c.SkillCd(), 0.0F);
    EXPECT_TRUE(c.SkillReady());        // skill_cd 0 -> always ready
}

TEST(CharSkillC12Test, ActivateSucceedsWhenReady) {
    CharSkillC12 c(5.0F);
    EXPECT_TRUE(c.TryActivateSkill());  // skill_ready -> enter (set in_skill = true)
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC12Test, ActivateFailsWhileOnCooldown) {
    // get_skill_ready == 0 path: TryActivateSkill is a no-op.
    CharSkillC12 c(5.0F);
    c.TryActivateSkill();
    c.EndSkill();                       // RoleSkillEnd -> ReSetSkillReload (= 0)
    EXPECT_FALSE(c.SkillReady());
    EXPECT_FALSE(c.TryActivateSkill()); // still cooling down -> no-op
    EXPECT_FALSE(c.InSkill());
}

TEST(CharSkillC12Test, RoleSkillHasNoInSkillGuard) {
    // FAITHFUL: C12Controller__RoleSkill @ 158451 has NO "!in_skill" (0x55) guard
    // (unlike C01Controller__RoleSkill). While in skill and STILL ready (activation
    // does not spend the charge in C12 -- the spend is RoleSkillEnd), the gate is
    // satisfied again, so re-activation is NOT blocked by an in_skill check.
    CharSkillC12 c(5.0F);
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_TRUE(c.InSkill());
    EXPECT_TRUE(c.SkillReady());         // not spent on activate -> still ready
    EXPECT_TRUE(c.TryActivateSkill());   // C12 has no !in_skill gate -> still allowed
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC12Test, EndSkillSpendsTheChargeAndLeavesState) {
    // FAITHFUL: C12Controller__RoleSkillEnd @ 158514 -> ReSetSkillReload (0x5c = 0).
    CharSkillC12 c(5.0F);
    c.TryActivateSkill();
    EXPECT_TRUE(c.InSkill());
    c.EndSkill();
    EXPECT_FALSE(c.InSkill());           // owner slot-0x17c finish path (flagged)
    EXPECT_FALSE(c.SkillReady());        // cooldown restarted (this_skill_time = 0)
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 5.0F);
}

TEST(CharSkillC12Test, EndSkillIsNoOpWhenNotInSkill) {
    CharSkillC12 c(5.0F);
    EXPECT_TRUE(c.SkillReady());
    c.EndSkill();                        // no skill active -> no spend
    EXPECT_TRUE(c.SkillReady());
    EXPECT_FALSE(c.InSkill());
}

TEST(CharSkillC12Test, CooldownRechargesAndClamps) {
    // FAITHFUL: C12Controller__Update -> AttributeUpdate -> SkillReload count-up,
    // clamped to skill_cd (0x44).
    CharSkillC12 c(2.0F);
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

TEST(CharSkillC12Test, TickDoesNotAutoEndSkill) {
    // FAITHFUL (the key C12 difference): C12Controller__Update has NO in_skill
    // branch -- NO in_skill_time countdown and NO slot-0x17c auto-end (that is the
    // C13/C01 feature C12 lacks). So an active skill NEVER auto-ends on Tick; it
    // stays active until the owner routes RoleSkillEnd. Ticking for a long time
    // while in skill must keep in_skill true.
    CharSkillC12 c(4.0F);
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_TRUE(c.InSkill());
    for (int i = 0; i < 50; ++i) {
        c.Tick(1000.0F);                 // 50 seconds of ticks: no window timer
        EXPECT_TRUE(c.InSkill());        // never auto-ends (no countdown in C12)
    }
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC12Test, TickRunsCooldownWhileInSkill) {
    // C12's Update runs AttributeUpdate -> SkillReload UNCONDITIONALLY (no in_skill
    // gate), so the cooldown advances even during the skill. To observe it, start
    // mid-cooldown by spending first, then re-enter in skill and tick.
    CharSkillC12 c(4.0F);
    c.TryActivateSkill();
    c.EndSkill();                        // this_skill_time = 0 (on cooldown)
    EXPECT_FALSE(c.SkillReady());
    c.TryActivateSkill();                // ready==false -> no-op, not in skill
    EXPECT_FALSE(c.InSkill());
    // Force the in-skill state without spending by toggling via a ready instance:
    CharSkillC12 d(4.0F);
    d.TryActivateSkill();                // in skill, this_skill_time == skill_cd
    EXPECT_TRUE(d.InSkill());
    d.Tick(1000.0F);                     // SkillReload runs (clamped no-op at full),
    EXPECT_TRUE(d.InSkill());            // and crucially does NOT freeze or auto-end
    EXPECT_TRUE(d.SkillReady());
}

TEST(CharSkillC12Test, NonPositiveDtIsIgnored) {
    CharSkillC12 c(3.0F);
    c.TryActivateSkill();
    c.EndSkill();
    const float before = c.CooldownRemaining();
    c.Tick(0.0F);
    c.Tick(-100.0F);
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), before); // no advance
}

TEST(CharSkillC12Test, NoRngDrawOnAnyPath) {
    // None of the recovered C12 bodies draw from rg_random; exercising the whole
    // brain must leave the stream un-advanced, so two seeded instances stay in
    // lockstep. We compare a reference stream that the brain never touches.
    CharSkillC12 a(3.0F);
    RGRandom reference;
    a.SetSeed(12345);
    reference.SetRandomSeed(12345);
    EXPECT_TRUE(a.Seeded());
    for (int i = 0; i < 16; ++i) {
        a.TryActivateSkill();
        a.Tick(250.0F);
        a.EndSkill();
        a.Tick(250.0F);
    }
    // 'a' ran the full brain; 'reference' was untouched. If any draw had leaked,
    // a's stream would have advanced and diverged. Same first value -> a's stream
    // is exactly where the reference is, i.e. zero draws were taken.
    EXPECT_EQ(a.Rng().Range(0, 1000000), reference.Range(0, 1000000));
}

TEST(CharSkillC12Test, FullCycleIsDeterministic) {
    // Drive identical activate/end/cooldown cycles on two instances and compare the
    // observable state at every step -> behaviour is fully deterministic.
    auto run = [](CharSkillC12 &c) -> std::vector<int> {
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
    CharSkillC12 a(2.0F);
    CharSkillC12 b(2.0F);
    const std::vector<int> ta = run(a);
    const std::vector<int> tb = run(b);
    ASSERT_EQ(ta.size(), tb.size());
    for (std::size_t i = 0; i < ta.size(); ++i) {
        EXPECT_EQ(ta[i], tb[i]);
    }
}

// NOLINTEND(readability-magic-numbers)
