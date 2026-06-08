#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include "combat/CharSkillC08.hpp"

using Game::CharSkillC08;

// NOLINTBEGIN(readability-magic-numbers)

TEST(CharSkillC08Test, StartsReadyNotInSkillWithFullShield) {
    CharSkillC08 c(5.0F);
    EXPECT_TRUE(c.SkillReady());         // freshly set-up hero: first ultimate ready
    EXPECT_FALSE(c.InSkill());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 0.0F);
    EXPECT_FLOAT_EQ(c.SkillCd(), 5.0F);
    EXPECT_EQ(c.ShieldValue(), 100);     // _ctor: *(this+0x90) = 100
    EXPECT_EQ(CharSkillC08::kInitialShieldValue, 100);
}

TEST(CharSkillC08Test, NegativeCdClampsToZeroAndIsAlwaysReady) {
    CharSkillC08 c(-4.0F);                // guard against a bad stat sheet value
    EXPECT_FLOAT_EQ(c.SkillCd(), 0.0F);
    EXPECT_TRUE(c.SkillReady());          // skill_cd 0 -> always ready
    EXPECT_EQ(c.ShieldValue(), 100);
}

TEST(CharSkillC08Test, ActivateSucceedsWhenReadyAndIdle) {
    CharSkillC08 c(5.0F);
    EXPECT_TRUE(c.TryActivateSkill());    // skill_ready && !in_skill -> enter
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC08Test, ActivateFailsWhileAlreadyInSkill) {
    CharSkillC08 c(5.0F);
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_FALSE(c.TryActivateSkill());   // in_skill gate (this+0x55) blocks re-entry
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC08Test, ActivateFailsWhileOnCooldown) {
    CharSkillC08 c(5.0F);
    c.TryActivateSkill();
    c.EndSkill();                         // spend -> this_skill_time = 0
    EXPECT_FALSE(c.SkillReady());
    EXPECT_FALSE(c.TryActivateSkill());   // still cooling down
}

TEST(CharSkillC08Test, EndSkillSpendsTheChargeAndLeavesState) {
    CharSkillC08 c(5.0F);
    c.TryActivateSkill();
    EXPECT_TRUE(c.InSkill());
    c.EndSkill();                         // RoleSkillEnd -> ReSetSkillReload
    EXPECT_FALSE(c.InSkill());
    EXPECT_FALSE(c.SkillReady());         // cooldown restarted (this_skill_time = 0)
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 5.0F);
}

TEST(CharSkillC08Test, EndSkillIsNoOpWhenNotInSkill) {
    CharSkillC08 c(5.0F);
    EXPECT_TRUE(c.SkillReady());
    c.EndSkill();                         // no skill active -> no spend
    EXPECT_TRUE(c.SkillReady());
    EXPECT_FALSE(c.InSkill());
}

TEST(CharSkillC08Test, CooldownRechargesAndClamps) {
    CharSkillC08 c(2.0F);
    c.TryActivateSkill();
    c.EndSkill();                         // this_skill_time = 0
    EXPECT_FALSE(c.SkillReady());
    c.Tick(1000.0F);                      // +1.0s
    EXPECT_FALSE(c.SkillReady());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 1.0F);
    c.Tick(5000.0F);                      // overshoot -> clamps to skill_cd
    EXPECT_TRUE(c.SkillReady());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 0.0F);
}

TEST(CharSkillC08Test, NonPositiveDtIsIgnored) {
    CharSkillC08 c(3.0F);
    c.TryActivateSkill();
    c.EndSkill();
    const float before = c.CooldownRemaining();
    c.Tick(0.0F);
    c.Tick(-100.0F);
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), before); // no advance
}

TEST(CharSkillC08Test, UpdateNeverAutoEndsTheSkill) {
    // FAITHFUL: C08Controller__Update @ 156955 is JUST awake-gated AttributeUpdate +
    // SeachUpdate -- it has NO in_skill_time countdown and NO auto-end branch (unlike
    // C01). Ticking inside the skill must advance the cooldown but NEVER end the
    // skill on a timer; only the explicit RoleSkillEnd path (EndSkill) ends it.
    CharSkillC08 c(4.0F);
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_TRUE(c.InSkill());
    for (int i = 0; i < 50; ++i) {
        c.Tick(1000.0F);                  // 50 seconds of ticks while in skill
        EXPECT_TRUE(c.InSkill());         // never auto-ends
    }
    // Cooldown count-up ran unconditionally (clamped at skill_cd from activation),
    // so the hero stays ready -- the advance is a clamped no-op but is NOT gated out.
    EXPECT_TRUE(c.SkillReady());
}

TEST(CharSkillC08Test, GetHurtDefersToBaseWhenNotInSkill) {
    // FAITHFUL: C08Controller__GetHurt @ 156974 -- if (in_skill == 0) call the base
    // RGController.GetHurt and return. The shield is NOT touched (shield down).
    CharSkillC08 c(5.0F);
    EXPECT_FALSE(c.InSkill());
    const CharSkillC08::HurtDecision d = c.GetHurt(30, /*awake=*/true);
    EXPECT_TRUE(d.deferToBase);
    EXPECT_FALSE(d.absorbedByShield);
    EXPECT_FALSE(d.triggerProcessEffect);
    EXPECT_EQ(c.ShieldValue(), 100);      // shield untouched when not in skill
}

TEST(CharSkillC08Test, GetHurtAbsorbsIntoShieldWhileInSkillAndAwake) {
    // FAITHFUL: 156981 awake guard passes, 156984 *(this+0x90) -= damage. The raised
    // shield eats the hit; the normal HP pipeline is bypassed (no deferToBase).
    CharSkillC08 c(5.0F);
    c.TryActivateSkill();
    EXPECT_TRUE(c.InSkill());
    const CharSkillC08::HurtDecision d = c.GetHurt(30, /*awake=*/true);
    EXPECT_FALSE(d.deferToBase);          // base pipeline bypassed
    EXPECT_TRUE(d.absorbedByShield);
    EXPECT_TRUE(d.triggerProcessEffect);  // owner fires the RGGameProcess effect
    EXPECT_EQ(c.ShieldValue(), 70);       // 100 - 30
}

TEST(CharSkillC08Test, GetHurtShieldAbsorptionAccumulates) {
    CharSkillC08 c(5.0F);
    c.TryActivateSkill();
    c.GetHurt(25, true);
    c.GetHurt(40, true);
    EXPECT_EQ(c.ShieldValue(), 35);       // 100 - 25 - 40 (bare subtraction, no clamp)
}

TEST(CharSkillC08Test, GetHurtBareSubtractionCanGoNegativeNoClampModeled) {
    // The decomp does a bare *(this+0x90) -= damage with NO clamp-to-zero; we model
    // exactly that (see fabrication_flags CharSkillC08-CD04). Overkill goes negative.
    CharSkillC08 c(5.0F);
    c.TryActivateSkill();
    const CharSkillC08::HurtDecision d = c.GetHurt(150, true);
    EXPECT_TRUE(d.absorbedByShield);
    EXPECT_EQ(c.ShieldValue(), -50);      // 100 - 150, no clamp (faithful)
}

TEST(CharSkillC08Test, GetHurtInSkillButNotAwakeIsNoOp) {
    // FAITHFUL: 156981 -- in_skill && awake == 0 -> the decomp early-returns with no
    // effect: no base call, no shield subtraction, no process effect.
    CharSkillC08 c(5.0F);
    c.TryActivateSkill();
    EXPECT_TRUE(c.InSkill());
    const CharSkillC08::HurtDecision d = c.GetHurt(30, /*awake=*/false);
    EXPECT_FALSE(d.deferToBase);
    EXPECT_FALSE(d.absorbedByShield);
    EXPECT_FALSE(d.triggerProcessEffect);
    EXPECT_EQ(c.ShieldValue(), 100);      // untouched: early-return
}

TEST(CharSkillC08Test, GetHurtRevertsToBaseAfterSkillEnds) {
    // Once the skill ends, GetHurt must defer to the base pipeline again (shield
    // down), proving the in_skill branch is the live discriminator.
    CharSkillC08 c(5.0F);
    c.TryActivateSkill();
    c.GetHurt(20, true);                  // absorbed -> shield 80
    EXPECT_EQ(c.ShieldValue(), 80);
    c.EndSkill();
    const CharSkillC08::HurtDecision d = c.GetHurt(20, true);
    EXPECT_TRUE(d.deferToBase);           // shield down -> normal pipeline
    EXPECT_FALSE(d.absorbedByShield);
    EXPECT_EQ(c.ShieldValue(), 80);       // unchanged after skill ended
}

TEST(CharSkillC08Test, NoRngDrawOnAnyPath) {
    // None of the five recovered bodies draws from rg_random; exercising the whole
    // brain must leave the stream un-advanced, so two seeded instances stay in
    // lockstep. (Heroes are player-driven and take ZERO RNG draws here.)
    CharSkillC08 a(3.0F);
    CharSkillC08 b(3.0F);
    a.SetSeed(54321);
    b.SetSeed(54321);
    EXPECT_TRUE(a.Seeded());
    for (int i = 0; i < 16; ++i) {
        a.TryActivateSkill();
        a.GetHurt(10, true);
        a.GetHurt(10, false);
        a.Tick(250.0F);
        a.EndSkill();
        a.Tick(250.0F);
    }
    // 'a' ran the full brain; 'b' was untouched. If any draw had leaked, the next
    // draw on 'a' would diverge from 'b'. Same value -> stream truly untouched.
    EXPECT_EQ(a.Rng().Range(0, 1000000), b.Rng().Range(0, 1000000));
}

TEST(CharSkillC08Test, FullCycleIsDeterministic) {
    // Drive identical activate/hurt/cooldown cycles on two instances and compare the
    // observable state at every step -> behaviour is fully deterministic.
    auto run = [](CharSkillC08 &c) -> std::vector<int> {
        std::vector<int> trace;
        for (int i = 0; i < 12; ++i) {
            trace.push_back(c.TryActivateSkill() ? 1 : 0);
            const CharSkillC08::HurtDecision d = c.GetHurt(15, true);
            trace.push_back(d.absorbedByShield ? 1 : 0);
            trace.push_back(c.ShieldValue());
            c.Tick(400.0F);
            c.EndSkill();
            trace.push_back(c.SkillReady() ? 1 : 0);
            c.Tick(1500.0F);
        }
        return trace;
    };
    CharSkillC08 a(2.0F);
    CharSkillC08 b(2.0F);
    const std::vector<int> ta = run(a);
    const std::vector<int> tb = run(b);
    ASSERT_EQ(ta.size(), tb.size());
    for (std::size_t i = 0; i < ta.size(); ++i) {
        EXPECT_EQ(ta[i], tb[i]);
    }
}

// NOLINTEND(readability-magic-numbers)
