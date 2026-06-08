#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include "combat/CharSkillC06.hpp"

using Game::CharSkillC06;

// NOLINTBEGIN(readability-magic-numbers)

TEST(CharSkillC06Test, StartsReadyAndIdle) {
    CharSkillC06 c(5.0F);
    EXPECT_TRUE(c.SkillReady());        // freshly set-up hero: first ultimate ready
    EXPECT_FALSE(c.InSkill());
    EXPECT_FALSE(c.BatteryDeployed());
    EXPECT_FALSE(c.CanCancelSkill());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 0.0F);
    EXPECT_FLOAT_EQ(c.SkillCd(), 5.0F);
}

TEST(CharSkillC06Test, NegativeCdClampsToZero) {
    CharSkillC06 c(-3.0F);              // guard against a bad stat sheet value
    EXPECT_FLOAT_EQ(c.SkillCd(), 0.0F);
    EXPECT_TRUE(c.SkillReady());        // skill_cd 0 -> always ready
}

TEST(CharSkillC06Test, ActivateDeploysWhenReadyAndIdle) {
    CharSkillC06 c(5.0F);
    EXPECT_TRUE(c.TryActivateSkill());  // skill_ready && !in_skill -> deploy
    EXPECT_TRUE(c.InSkill());
    EXPECT_TRUE(c.BatteryDeployed());
    EXPECT_FALSE(c.CanCancelSkill());   // recall locked until TurnSkillCancelable
}

TEST(CharSkillC06Test, ActivateFailsWhileAlreadyDeployed) {
    CharSkillC06 c(5.0F);
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_FALSE(c.TryActivateSkill()); // in_skill (0x55) gate blocks re-deploy
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC06Test, ActivateFailsWhileOnCooldown) {
    CharSkillC06 c(5.0F);
    c.TryActivateSkill();
    c.EndSkill();                       // spend -> this_skill_time = 0
    EXPECT_FALSE(c.SkillReady());
    EXPECT_FALSE(c.TryActivateSkill()); // still cooling down
}

TEST(CharSkillC06Test, CancelFailsBeforeUnlock) {
    // FAITHFUL gate (B): in_skill && the_battery!=null && canCancelSkill.
    // Right after deploy, canCancelSkill is still false -> recall is locked.
    CharSkillC06 c(5.0F);
    c.TryActivateSkill();
    EXPECT_TRUE(c.InSkill());
    EXPECT_FALSE(c.CanCancelSkill());
    EXPECT_FALSE(c.TryCancelSkill());   // canCancelSkill (0x98) false -> no recall
    EXPECT_TRUE(c.InSkill());           // skill stays active
    EXPECT_TRUE(c.BatteryDeployed());
}

TEST(CharSkillC06Test, TurnSkillCancelableUnlocksRecall) {
    CharSkillC06 c(5.0F);
    c.TryActivateSkill();
    c.TurnSkillCancelable();            // delayed Invoke target fires
    EXPECT_TRUE(c.CanCancelSkill());
    EXPECT_TRUE(c.TryCancelSkill());    // now the gate passes -> Dead() + end
    EXPECT_FALSE(c.InSkill());
    EXPECT_FALSE(c.BatteryDeployed());
    EXPECT_FALSE(c.CanCancelSkill());
}

TEST(CharSkillC06Test, TurnSkillCancelableNoOpWhenIdle) {
    CharSkillC06 c(5.0F);
    EXPECT_FALSE(c.InSkill());
    c.TurnSkillCancelable();            // no active skill -> nothing to unlock
    EXPECT_FALSE(c.CanCancelSkill());
}

TEST(CharSkillC06Test, CancelFailsWhenNotInSkill) {
    CharSkillC06 c(5.0F);
    EXPECT_FALSE(c.InSkill());
    EXPECT_FALSE(c.TryCancelSkill());   // in_skill (0x55) gate false -> no recall
}

TEST(CharSkillC06Test, CancelRecallSpendsChargeAndRestartsCooldown) {
    // The recall (RGBatteryController.Dead) ends the window via the RoleSkillEnd
    // spend path: this_skill_time resets to 0 -> back on cooldown.
    CharSkillC06 c(4.0F);
    c.TryActivateSkill();
    c.TurnSkillCancelable();
    EXPECT_TRUE(c.TryCancelSkill());
    EXPECT_FALSE(c.SkillReady());                 // ReSetSkillReload -> 0
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 4.0F);
}

TEST(CharSkillC06Test, EndSkillSpendsTheChargeAndClearsState) {
    CharSkillC06 c(5.0F);
    c.TryActivateSkill();
    c.TurnSkillCancelable();
    EXPECT_TRUE(c.InSkill());
    EXPECT_TRUE(c.CanCancelSkill());
    c.EndSkill();
    EXPECT_FALSE(c.InSkill());
    EXPECT_FALSE(c.BatteryDeployed());
    EXPECT_FALSE(c.CanCancelSkill());            // recall gate reset
    EXPECT_FALSE(c.SkillReady());                // cooldown restarted (=0)
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 5.0F);
}

TEST(CharSkillC06Test, EndSkillIsNoOpWhenNotInSkill) {
    CharSkillC06 c(5.0F);
    EXPECT_TRUE(c.SkillReady());
    c.EndSkill();                        // no skill active -> no spend
    EXPECT_TRUE(c.SkillReady());
    EXPECT_FALSE(c.InSkill());
}

TEST(CharSkillC06Test, CooldownRechargesAndClamps) {
    CharSkillC06 c(2.0F);
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

TEST(CharSkillC06Test, SkillDoesNotAutoEndOnTick) {
    // FAITHFUL DIFFERENCE FROM C01: C06Controller__Update has NO in_skill_time
    // countdown and NO auto-end -- the deployed battery does not expire on a timer.
    // Ticking for a long time while in skill must leave the skill active (it only
    // ends via recall or RoleSkillEnd). The cooldown's SkillReload still advances.
    CharSkillC06 c(3.0F);
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_TRUE(c.InSkill());
    c.EndSkill();                        // park cooldown at 0 to observe SkillReload
    EXPECT_TRUE(c.TryActivateSkill() == false); // on cooldown now
    // Re-enter cleanly for the no-auto-end check after the cooldown.
    c.Tick(3000.0F);                     // recharge fully
    EXPECT_TRUE(c.SkillReady());
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_TRUE(c.InSkill());
    for (int i = 0; i < 20; ++i) {
        c.Tick(1000.0F);                 // 20s of ticks INSIDE the skill
    }
    EXPECT_TRUE(c.InSkill());            // never auto-ends (no timer in Update)
    EXPECT_TRUE(c.BatteryDeployed());
}

TEST(CharSkillC06Test, NonPositiveDtIsIgnored) {
    CharSkillC06 c(3.0F);
    c.TryActivateSkill();
    c.EndSkill();
    const float before = c.CooldownRemaining();
    c.Tick(0.0F);
    c.Tick(-100.0F);
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), before); // no advance
}

TEST(CharSkillC06Test, RedeployAfterRecallRequiresCooldown) {
    // Full loop: deploy -> unlock -> recall (spends) -> recharge -> redeploy.
    CharSkillC06 c(2.0F);
    EXPECT_TRUE(c.TryActivateSkill());
    c.TurnSkillCancelable();
    EXPECT_TRUE(c.TryCancelSkill());     // recall, charge spent
    EXPECT_FALSE(c.TryActivateSkill());  // on cooldown -> cannot redeploy
    c.Tick(2000.0F);                     // recharge
    EXPECT_TRUE(c.SkillReady());
    EXPECT_TRUE(c.TryActivateSkill());   // redeploy
    EXPECT_TRUE(c.InSkill());
    EXPECT_FALSE(c.CanCancelSkill());    // recall locked again on the fresh deploy
}

TEST(CharSkillC06Test, NoRngDrawOnAnyPath) {
    // No recovered C06 body draws from rg_random; exercising the whole brain must
    // leave the stream un-advanced, so two seeded instances stay in lockstep.
    CharSkillC06 a(3.0F);
    CharSkillC06 b(3.0F);
    a.SetSeed(98765);
    b.SetSeed(98765);
    EXPECT_TRUE(a.Seeded());
    for (int i = 0; i < 16; ++i) {
        a.TryActivateSkill();
        a.TurnSkillCancelable();
        a.TryCancelSkill();
        a.Tick(250.0F);
        a.EndSkill();
        a.Tick(250.0F);
    }
    // 'a' ran the full brain; 'b' was untouched. If any draw had leaked, the next
    // draw on 'a' would diverge from 'b'. Same value -> stream truly untouched.
    EXPECT_EQ(a.Rng().Range(0, 1000000), b.Rng().Range(0, 1000000));
}

TEST(CharSkillC06Test, FullCycleIsDeterministic) {
    // Drive identical deploy/unlock/recall/cooldown cycles on two instances and
    // compare observable state at every step -> behaviour is fully deterministic.
    auto run = [](CharSkillC06 &c) -> std::vector<int> {
        std::vector<int> trace;
        for (int i = 0; i < 12; ++i) {
            trace.push_back(c.TryActivateSkill() ? 1 : 0);
            trace.push_back(c.InSkill() ? 1 : 0);
            c.TurnSkillCancelable();
            trace.push_back(c.CanCancelSkill() ? 1 : 0);
            trace.push_back(c.TryCancelSkill() ? 1 : 0);
            trace.push_back(c.SkillReady() ? 1 : 0);
            c.Tick(800.0F);
        }
        return trace;
    };
    CharSkillC06 a(2.0F);
    CharSkillC06 b(2.0F);
    const auto ta = run(a);
    const auto tb = run(b);
    ASSERT_EQ(ta.size(), tb.size());
    for (std::size_t i = 0; i < ta.size(); ++i) {
        EXPECT_EQ(ta[i], tb[i]);
    }
}

// NOLINTEND(readability-magic-numbers)
