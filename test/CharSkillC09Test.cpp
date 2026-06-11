#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include "combat/CharSkillC09.hpp"

using Game::CharSkillC09;

// NOLINTBEGIN(readability-magic-numbers)

TEST(CharSkillC09Test, StartsReadyAndBowNotDrawn) {
    CharSkillC09 c(5.0F);
    EXPECT_TRUE(c.SkillReady());        // freshly set-up archer: first draw ready
    EXPECT_FALSE(c.InSkill());          // bow not drawn
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 0.0F);
    EXPECT_FLOAT_EQ(c.SkillCd(), 5.0F);
}

TEST(CharSkillC09Test, NegativeSkillCdClampsToZero) {
    CharSkillC09 c(-3.0F);              // guard against a bad stat sheet value
    EXPECT_FLOAT_EQ(c.SkillCd(), 0.0F);
    EXPECT_TRUE(c.SkillReady());        // skill_cd 0 -> always ready
}

// ---- RoleSkill toggle (C09Controller__RoleSkill @ 157300) ---------------------

TEST(CharSkillC09Test, RoleSkillDrawsBowWhenReadyAndNotDrawn) {
    CharSkillC09 c(5.0F);
    const CharSkillC09::SkillDecision d = c.RoleSkill();
    EXPECT_TRUE(d.drewBow);             // skill_ready && !in_skill -> draw
    EXPECT_FALSE(d.releasedBow);
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC09Test, RoleSkillTogglesReleaseWhenAlreadyDrawn) {
    // The C09 gimmick: pressing skill while drawn RELEASES the bow (slot 0x17c),
    // it does NOT re-draw. This is the key difference from CharSkillC01's
    // enter-only gate, which blocks re-entry instead.
    CharSkillC09 c(5.0F);
    ASSERT_TRUE(c.RoleSkill().drewBow);
    EXPECT_TRUE(c.InSkill());
    const CharSkillC09::SkillDecision d = c.RoleSkill();
    EXPECT_TRUE(d.releasedBow);         // in_skill -> RoleSkillEnd
    EXPECT_FALSE(d.drewBow);
    EXPECT_FALSE(c.InSkill());          // bow released
}

TEST(CharSkillC09Test, RoleSkillSwallowedWhenOnCooldown) {
    CharSkillC09 c(5.0F);
    c.RoleSkill();                      // draw
    c.Release();                        // release -> this_skill_time = 0 (on cooldown)
    EXPECT_FALSE(c.SkillReady());
    const CharSkillC09::SkillDecision d = c.RoleSkill();
    EXPECT_FALSE(d.drewBow);            // not charged -> no draw
    EXPECT_FALSE(d.releasedBow);
    EXPECT_FALSE(c.InSkill());
}

TEST(CharSkillC09Test, ReleaseSpendsTheChargeAndLeavesState) {
    CharSkillC09 c(5.0F);
    c.RoleSkill();                      // draw
    EXPECT_TRUE(c.InSkill());
    c.Release();
    EXPECT_FALSE(c.InSkill());
    EXPECT_FALSE(c.SkillReady());        // cooldown restarted (this_skill_time = 0)
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 5.0F);
}

TEST(CharSkillC09Test, ReleaseIsNoOpWhenBowNotDrawn) {
    CharSkillC09 c(5.0F);
    EXPECT_TRUE(c.SkillReady());
    c.Release();                         // not drawn -> no spend
    EXPECT_TRUE(c.SkillReady());
    EXPECT_FALSE(c.InSkill());
}

// ---- Cooldown via Update (C09Controller__Update @ 157193) ---------------------

TEST(CharSkillC09Test, CooldownRechargesAndClamps) {
    CharSkillC09 c(2.0F);
    c.RoleSkill();                       // draw
    c.Release();                         // this_skill_time = 0
    EXPECT_FALSE(c.SkillReady());
    c.Tick(1000.0F);                     // +1.0s
    EXPECT_FALSE(c.SkillReady());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 1.0F);
    c.Tick(5000.0F);                     // overshoot -> clamps to skill_cd
    EXPECT_TRUE(c.SkillReady());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 0.0F);
}

TEST(CharSkillC09Test, CooldownAdvancesWhileBowIsDrawn) {
    // FAITHFUL: C09's AttributeUpdate -> SkillReload runs unconditionally, BEFORE the
    // (timer-less) in_skill aim branch -- so the cooldown is NOT frozen while drawn.
    // Drain the cooldown first, then draw and verify Tick still advances it.
    CharSkillC09 c(2.0F);
    c.RoleSkill();
    c.Release();                         // this_skill_time = 0, not drawn
    c.Tick(500.0F);                      // +0.5s -> 1.5 remaining
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 1.5F);
    c.Tick(2000.0F);                     // recharge fully -> ready
    ASSERT_TRUE(c.SkillReady());
    c.RoleSkill();                       // draw again (consumes nothing until release)
    ASSERT_TRUE(c.InSkill());
    c.Release();                         // back on cooldown
    c.Tick(1000.0F);                     // +1.0s -> 1.0 remaining
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 1.0F);
    c.RoleSkill();                       // swallowed (still cooling) -- not drawn
    EXPECT_FALSE(c.InSkill());
}

TEST(CharSkillC09Test, BowDoesNotAutoEndOnTick) {
    // FAITHFUL: UNLIKE C01/C10, C09's Update has NO in_skill_time countdown and NO
    // auto-end. A drawn bow persists across ticks until explicitly released; ticking
    // must NEVER clear in_skill (auto-ending would be fabricated logic).
    CharSkillC09 c(3.0F);
    ASSERT_TRUE(c.RoleSkill().drewBow);
    EXPECT_TRUE(c.InSkill());
    for (int i = 0; i < 100; ++i) {
        c.Tick(1000.0F);                 // 100 seconds of ticks -- way past any window
        EXPECT_TRUE(c.InSkill());        // still drawn: no timer, no auto-end
    }
    // Only an explicit release ends it.
    c.Release();
    EXPECT_FALSE(c.InSkill());
}

TEST(CharSkillC09Test, NonPositiveDtIsIgnored) {
    CharSkillC09 c(3.0F);
    c.RoleSkill();
    c.Release();
    const float before = c.CooldownRemaining();
    c.Tick(0.0F);
    c.Tick(-100.0F);
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), before); // no advance
}

// ---- RoleAtk (C09Controller__RoleAtk @ 157566) --------------------------------

TEST(CharSkillC09Test, RoleAtkPressFiresPrimaryHandWhenNotDrawn) {
    CharSkillC09 c(5.0F);                // bow not drawn
    const CharSkillC09::AtkDecision d = c.RoleAtk(true, false, false);
    EXPECT_FALSE(d.triggerItem);
    EXPECT_FALSE(d.arrowShoot);          // bow-block only while drawn
    EXPECT_TRUE(d.setHandAttack);
    EXPECT_TRUE(d.handAttackValue);
}

TEST(CharSkillC09Test, RoleAtkReleaseStopsPrimaryHand) {
    CharSkillC09 c(5.0F);
    const CharSkillC09::AtkDecision d = c.RoleAtk(false, false, false);
    EXPECT_TRUE(d.setHandAttack);
    EXPECT_FALSE(d.handAttackValue);     // release -> SetAttack(false)
    EXPECT_FALSE(d.arrowShoot);
}

TEST(CharSkillC09Test, RoleAtkItemPressTriggersItemWhenNotDrawn) {
    CharSkillC09 c(5.0F);                // not drawn
    const CharSkillC09::AtkDecision d = c.RoleAtk(true, true, false); // on a pickup, press
    EXPECT_TRUE(d.triggerItem);
    EXPECT_FALSE(d.setHandAttack);       // early return: no hand.SetAttack
    EXPECT_FALSE(d.arrowShoot);
}

TEST(CharSkillC09Test, RoleAtkItemReleaseDoesNotTrigger) {
    CharSkillC09 c(5.0F);
    // in_item but NOT pressing -> falls through to the normal hand path.
    const CharSkillC09::AtkDecision d = c.RoleAtk(false, true, false);
    EXPECT_FALSE(d.triggerItem);
    EXPECT_TRUE(d.setHandAttack);
    EXPECT_FALSE(d.handAttackValue);
}

TEST(CharSkillC09Test, RoleAtkDrawnPressLoosesArrowWhenChargedAndNocked) {
    // While the bow is drawn (in_skill), a press with skill_ready && the_bullet!=null
    // looses the arrow (ArrowShoot) and returns -- the gun does NOT fire.
    CharSkillC09 c(5.0F);
    ASSERT_TRUE(c.RoleSkill().drewBow);  // drawn, and still charged (no spend on draw)
    ASSERT_TRUE(c.SkillReady());
    const CharSkillC09::AtkDecision d = c.RoleAtk(true, false, /*bulletNocked=*/true);
    EXPECT_TRUE(d.arrowShoot);
    EXPECT_FALSE(d.setHandAttack);       // early return: primary hand not touched
    EXPECT_FALSE(d.triggerItem);
}

TEST(CharSkillC09Test, RoleAtkDrawnPressFallsToHandWhenNoBulletNocked) {
    // Drawn + charged but no arrow nocked (the_bullet == null) -> the bow-block does
    // NOT fire; the press falls through to hand.SetAttack.
    CharSkillC09 c(5.0F);
    ASSERT_TRUE(c.RoleSkill().drewBow);
    ASSERT_TRUE(c.SkillReady());
    const CharSkillC09::AtkDecision d = c.RoleAtk(true, false, /*bulletNocked=*/false);
    EXPECT_FALSE(d.arrowShoot);          // no bullet -> no shot
    EXPECT_TRUE(d.setHandAttack);
    EXPECT_TRUE(d.handAttackValue);
}

TEST(CharSkillC09Test, RoleAtkDrawnReleaseStillLoosesArrowPerDecomp) {
    // FAITHFUL (157587-157603): the bow-block checks ONLY skill_ready && the_bullet,
    // NOT the press value -- it sits before the value-gated aim nudge at LAB. So even
    // a release (value==0) while drawn with a nocked bullet still looses the arrow.
    // (Value gates the item path and the aim-nudge, not the bow-block.)
    CharSkillC09 c(5.0F);
    ASSERT_TRUE(c.RoleSkill().drewBow);
    ASSERT_TRUE(c.SkillReady());
    // Release with a nocked bullet: per the decomp the bow-block checks only
    // skill_ready && the_bullet (NOT the press value), so it still looses.
    const CharSkillC09::AtkDecision d = c.RoleAtk(false, false, /*bulletNocked=*/true);
    EXPECT_TRUE(d.arrowShoot);
    EXPECT_FALSE(d.setHandAttack);
}

TEST(CharSkillC09Test, RoleAtkItemPressWhileDrawnSkipsItemAndEntersBowBlock) {
    // FAITHFUL (157580-157586): when (in_item && press) AND in_skill, the decomp does
    // NOT TriggerItem -- it falls through to the bow-block. So standing on a pickup and
    // pressing while the bow is drawn looses the arrow instead of using the item.
    CharSkillC09 c(5.0F);
    ASSERT_TRUE(c.RoleSkill().drewBow);
    ASSERT_TRUE(c.SkillReady());
    const CharSkillC09::AtkDecision d = c.RoleAtk(true, /*standingOnItem=*/true,
                                                  /*bulletNocked=*/true);
    EXPECT_FALSE(d.triggerItem);         // drawn -> item path is skipped
    EXPECT_TRUE(d.arrowShoot);           // falls into the bow-block instead
}

TEST(CharSkillC09Test, RoleAtkItemPressWhileDrawnNoBulletFallsToHand) {
    // Same drawn + item-press case, but with no arrow nocked: the bow-block does not
    // fire, and the item path was already skipped (in_skill), so it lands on
    // hand.SetAttack -- the item is NOT triggered.
    CharSkillC09 c(5.0F);
    ASSERT_TRUE(c.RoleSkill().drewBow);
    const CharSkillC09::AtkDecision d = c.RoleAtk(true, /*standingOnItem=*/true,
                                                  /*bulletNocked=*/false);
    EXPECT_FALSE(d.triggerItem);
    EXPECT_FALSE(d.arrowShoot);
    EXPECT_TRUE(d.setHandAttack);
    EXPECT_TRUE(d.handAttackValue);
}

// ---- Determinism / RNG ---------------------------------------------------------

TEST(CharSkillC09Test, NoRngDrawOnAnyPath) {
    // No recovered C09 body draws from rg_random; exercising the whole brain must
    // leave the stream un-advanced, so two seeded instances stay in lockstep.
    CharSkillC09 a(3.0F);
    CharSkillC09 b(3.0F);
    a.SetSeed(98765);
    b.SetSeed(98765);
    EXPECT_TRUE(a.Seeded());
    for (int i = 0; i < 16; ++i) {
        a.RoleSkill();
        a.RoleAtk(true, false, true);
        a.RoleAtk(false, true, false);
        a.Tick(250.0F);
        a.Release();
        a.Tick(250.0F);
    }
    // 'a' ran the full brain; 'b' was untouched. If any draw had leaked, the next
    // draw on 'a' would diverge from 'b'. Same value -> stream truly untouched.
    EXPECT_EQ(a.Rng().Range(0, 1000000), b.Rng().Range(0, 1000000));
}

TEST(CharSkillC09Test, FullCycleIsDeterministic) {
    // Drive identical draw/attack/cooldown cycles on two instances and compare the
    // observable state at every step -> behaviour is fully deterministic.
    auto run = [](CharSkillC09 &c) -> std::vector<int> {
        std::vector<int> trace;
        for (int i = 0; i < 12; ++i) {
            const CharSkillC09::SkillDecision s = c.RoleSkill();
            trace.push_back(s.drewBow ? 1 : (s.releasedBow ? 2 : 0));
            const CharSkillC09::AtkDecision d = c.RoleAtk(true, false, true);
            trace.push_back(d.arrowShoot ? 1 : 0);
            c.Tick(400.0F);
            c.Release();
            trace.push_back(c.SkillReady() ? 1 : 0);
            c.Tick(1500.0F);
        }
        return trace;
    };
    CharSkillC09 a(2.0F);
    CharSkillC09 b(2.0F);
    const std::vector<int> ta = run(a);
    const std::vector<int> tb = run(b);
    ASSERT_EQ(ta.size(), tb.size());
    for (std::size_t i = 0; i < ta.size(); ++i) {
        EXPECT_EQ(ta[i], tb[i]);
    }
}

// NOLINTEND(readability-magic-numbers)
