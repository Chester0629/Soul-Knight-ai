#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include "combat/CharSkillC02.hpp"

using Game::CharSkillC02;

// NOLINTBEGIN(readability-magic-numbers)

TEST(CharSkillC02Test, StartsReadyAndNotInSkill) {
    CharSkillC02 c(5.0F);
    EXPECT_TRUE(c.SkillReady());        // freshly set-up hero: first ultimate ready
    EXPECT_FALSE(c.InSkill());
    EXPECT_FALSE(c.SkillShoot());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 0.0F);
    EXPECT_FLOAT_EQ(c.SkillCd(), 5.0F);
}

TEST(CharSkillC02Test, NegativeSkillCdClampsToZero) {
    CharSkillC02 c(-3.0F);              // guard against bad stat sheet values
    EXPECT_FLOAT_EQ(c.SkillCd(), 0.0F);
    EXPECT_TRUE(c.SkillReady());        // skill_cd 0 -> always ready
}

TEST(CharSkillC02Test, ActivateSucceedsWhenReadyAndIdle) {
    CharSkillC02 c(5.0F);
    EXPECT_TRUE(c.TryActivateSkill());  // skill_ready && !in_skill -> enter
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC02Test, ActivateFailsWhileAlreadyInSkill) {
    CharSkillC02 c(5.0F);
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_FALSE(c.TryActivateSkill()); // in_skill gate blocks re-entry
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC02Test, ActivateFailsWhileOnCooldown) {
    CharSkillC02 c(5.0F);
    c.TryActivateSkill();
    c.EndSkill();                       // spend -> this_skill_time = 0
    EXPECT_FALSE(c.SkillReady());
    EXPECT_FALSE(c.TryActivateSkill()); // still cooling down
}

TEST(CharSkillC02Test, EndSkillSpendsChargeAndReportsDashAndShadowLock) {
    // FAITHFUL: C02Controller__RoleSkillEnd -- friction drop + in_skill=0 +
    // ReSetSkillReload + UpdateShadowLock + forward GetForce dash impulse.
    CharSkillC02 c(5.0F);
    c.TryActivateSkill();
    EXPECT_TRUE(c.InSkill());
    const CharSkillC02::RoleSkillEndDecision d = c.EndSkill();
    EXPECT_TRUE(d.ended);
    EXPECT_TRUE(d.applyDashImpulse);     // owner runs the GetForce dash
    EXPECT_TRUE(d.updateShadowLock);     // owner refreshes the marker tint
    EXPECT_FALSE(c.InSkill());           // in_skill (this+0x55) = 0
    EXPECT_FALSE(c.SkillReady());        // ReSetSkillReload -> this_skill_time = 0
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 5.0F);
}

TEST(CharSkillC02Test, EndSkillIsNoOpWhenNotInSkill) {
    CharSkillC02 c(5.0F);
    EXPECT_TRUE(c.SkillReady());
    const CharSkillC02::RoleSkillEndDecision d = c.EndSkill(); // no skill active
    EXPECT_FALSE(d.ended);
    EXPECT_FALSE(d.applyDashImpulse);
    EXPECT_FALSE(d.updateShadowLock);
    EXPECT_TRUE(c.SkillReady());         // no spend
    EXPECT_FALSE(c.InSkill());
}

TEST(CharSkillC02Test, SkillEndFrictionDropOperandIsCorrect) {
    // FAITHFUL: immediate operand 0xbe19999a == -0.15f in C02Controller__RoleSkillEnd.
    EXPECT_FLOAT_EQ(CharSkillC02::kSkillEndFrictionDrop, 0.15F);
}

TEST(CharSkillC02Test, CooldownRechargesAndClamps) {
    CharSkillC02 c(2.0F);
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

TEST(CharSkillC02Test, TickAdvancesCooldownWhileInSkillAndNeverAutoEnds) {
    // FAITHFUL CONTRAST WITH C01: C02Controller__Update has NO in_skill_time
    // countdown / auto-end. While in_skill, Tick advances the cooldown (SkillReload
    // runs unconditionally) but NEVER ends the skill -- only the explicit RoleSkillEnd
    // path closes the window. Proof: many ticks inside the skill keep in_skill true.
    CharSkillC02 c(4.0F);
    c.TryActivateSkill();
    c.EndSkill();                        // spend -> this_skill_time = 0, leaves skill
    EXPECT_FALSE(c.SkillReady());
    EXPECT_TRUE(c.TryActivateSkill() == false); // on cooldown now -> cannot re-enter
    // Recharge fully, then enter and tick a long time INSIDE the skill.
    c.Tick(4000.0F);
    EXPECT_TRUE(c.SkillReady());
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_TRUE(c.InSkill());
    for (int i = 0; i < 100; ++i) {
        c.Tick(1000.0F);                 // 100 seconds inside the skill
        EXPECT_TRUE(c.InSkill());        // no auto-end ever
    }
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC02Test, NonPositiveDtIsIgnored) {
    CharSkillC02 c(3.0F);
    c.TryActivateSkill();
    c.EndSkill();
    const float before = c.CooldownRemaining();
    c.Tick(0.0F);
    c.Tick(-100.0F);
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), before); // no advance
}

TEST(CharSkillC02Test, EndSkillShootIsNoOpWhenFlagNotSet) {
    // FAITHFUL: C02Controller__EndSkillShoot gated by skill_shoot (this+0x90).
    CharSkillC02 c(5.0F);
    EXPECT_FALSE(c.SkillShoot());
    const CharSkillC02::EndShootDecision d = c.EndSkillShoot(true); // even strengthened
    EXPECT_FALSE(d.fired);              // gate blocks -> no fire
    EXPECT_EQ(d.resourceCost, 0);
}

TEST(CharSkillC02Test, EndSkillShootFiresWithNoCostWhenNotStrengthened) {
    CharSkillC02 c(5.0F);
    c.SetSkillShoot(true);
    const CharSkillC02::EndShootDecision d = c.EndSkillShoot(false);
    EXPECT_TRUE(d.fired);
    EXPECT_EQ(d.resourceCost, 0);       // not strengthened -> no 50-cost
    EXPECT_FALSE(c.SkillShoot());       // flag consumed (this+0x90 = 0)
}

TEST(CharSkillC02Test, EndSkillShootChargesFiftyWhenStrengthened) {
    // FAITHFUL: strengthened -> role_attribute[+0x24] -= 50 (0x32).
    CharSkillC02 c(5.0F);
    c.SetSkillShoot(true);
    const CharSkillC02::EndShootDecision d = c.EndSkillShoot(true);
    EXPECT_TRUE(d.fired);
    EXPECT_EQ(d.resourceCost, CharSkillC02::kStrengthenedShootCost);
    EXPECT_EQ(d.resourceCost, 50);
    EXPECT_FALSE(c.SkillShoot());       // flag consumed even when strengthened
}

TEST(CharSkillC02Test, EndSkillShootIsOneShot) {
    // The flag is consumed on fire -> a second call is a no-op until re-armed.
    CharSkillC02 c(5.0F);
    c.SetSkillShoot(true);
    EXPECT_TRUE(c.EndSkillShoot(true).fired);
    const CharSkillC02::EndShootDecision again = c.EndSkillShoot(true);
    EXPECT_FALSE(again.fired);          // already consumed
    EXPECT_EQ(again.resourceCost, 0);
}

TEST(CharSkillC02Test, NoRngDrawOnAnyPath) {
    // None of the five recovered C02 bodies draws from rg_random; exercising the
    // whole brain must leave the stream un-advanced, so two seeded instances stay
    // in lockstep (a reference stream that never advances).
    CharSkillC02 a(3.0F);
    CharSkillC02 b(3.0F);
    a.SetSeed(98765);
    b.SetSeed(98765);
    EXPECT_TRUE(a.Seeded());
    for (int i = 0; i < 16; ++i) {
        a.TryActivateSkill();
        a.SetSkillShoot(true);
        a.EndSkillShoot(i % 2 == 0);   // alternate strengthened / not
        a.EndSkill();
        a.Tick(250.0F);
        a.Tick(250.0F);
    }
    // 'a' ran the full brain; 'b' was untouched. If any draw had leaked, the next
    // draw on 'a' would diverge from 'b'. Same value -> stream truly untouched.
    EXPECT_EQ(a.Rng().Range(0, 1000000), b.Rng().Range(0, 1000000));
}

TEST(CharSkillC02Test, FullCycleIsDeterministic) {
    // Drive identical activate/end/shoot/cooldown cycles on two instances and
    // compare the observable state at every step -> behaviour is fully deterministic.
    auto run = [](CharSkillC02 &c) -> std::vector<int> {
        std::vector<int> trace;
        for (int i = 0; i < 12; ++i) {
            trace.push_back(c.TryActivateSkill() ? 1 : 0);
            c.SetSkillShoot(true);
            const CharSkillC02::EndShootDecision s = c.EndSkillShoot(i % 3 == 0);
            trace.push_back(s.fired ? 1 : 0);
            trace.push_back(s.resourceCost);
            const CharSkillC02::RoleSkillEndDecision e = c.EndSkill();
            trace.push_back(e.ended ? 1 : 0);
            trace.push_back(c.SkillReady() ? 1 : 0);
            c.Tick(1500.0F);
            trace.push_back(c.SkillReady() ? 1 : 0);
        }
        return trace;
    };
    CharSkillC02 a(2.0F);
    CharSkillC02 b(2.0F);
    const std::vector<int> ta = run(a);
    const std::vector<int> tb = run(b);
    ASSERT_EQ(ta.size(), tb.size());
    for (std::size_t i = 0; i < ta.size(); ++i) {
        EXPECT_EQ(ta[i], tb[i]);
    }
}

// NOLINTEND(readability-magic-numbers)
