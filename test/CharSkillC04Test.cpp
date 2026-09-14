#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include "combat/CharSkillC04.hpp"

using Game::CharSkillC04;

// NOLINTBEGIN(readability-magic-numbers)

TEST(CharSkillC04Test, StartsReadyAndIdle) {
    CharSkillC04 c(5.0F);
    EXPECT_TRUE(c.SkillReady());     // freshly set-up hero: first ultimate ready
    EXPECT_FALSE(c.InSkill());
    EXPECT_FALSE(c.InSkillEffect());
    EXPECT_FALSE(c.HasCut());
    EXPECT_EQ(c.Combo(), 0);
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 0.0F);
    EXPECT_FLOAT_EQ(c.SkillCd(), 5.0F);
}

TEST(CharSkillC04Test, NegativeSkillCdClampsToZero) {
    CharSkillC04 c(-3.0F);           // guard against a bad stat sheet value
    EXPECT_FLOAT_EQ(c.SkillCd(), 0.0F);
    EXPECT_TRUE(c.SkillReady());     // skill_cd 0 -> always ready
}

// ---- RoleSkill gate (C04Controller__RoleSkill @ 156371) ------------------------

TEST(CharSkillC04Test, ActivateSucceedsWhenReadyAndIdle) {
    CharSkillC04 c(5.0F);
    EXPECT_TRUE(c.TryActivateSkill());   // skill_ready && !in_skill -> enter
    EXPECT_TRUE(c.InSkill());
    EXPECT_TRUE(c.InSkillEffect());      // opens the sword-cut window
    EXPECT_FALSE(c.HasCut());            // window starts un-cut
}

TEST(CharSkillC04Test, ActivateFailsWhileAlreadyInSkill) {
    CharSkillC04 c(5.0F);
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_FALSE(c.TryActivateSkill());  // in_skill gate blocks re-entry
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC04Test, ActivateFailsWhileOnCooldown) {
    CharSkillC04 c(5.0F);
    c.TryActivateSkill();
    c.EndSkill();                        // spend -> this_skill_time = 0
    EXPECT_FALSE(c.SkillReady());
    EXPECT_FALSE(c.TryActivateSkill());  // still cooling down
}

// ---- EndSkill (RoleSkillEnd -> ReSetSkillReload, flagged reconstruction) --------

TEST(CharSkillC04Test, EndSkillSpendsChargeAndLeavesSkillState) {
    CharSkillC04 c(5.0F);
    c.TryActivateSkill();
    EXPECT_TRUE(c.InSkill());
    c.EndSkill();
    EXPECT_FALSE(c.InSkill());
    EXPECT_FALSE(c.SkillReady());        // cooldown restarted (this_skill_time = 0)
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 5.0F);
}

TEST(CharSkillC04Test, EndSkillIsNoOpWhenNotInSkill) {
    CharSkillC04 c(5.0F);
    EXPECT_TRUE(c.SkillReady());
    c.EndSkill();                        // no skill active -> no spend
    EXPECT_TRUE(c.SkillReady());
    EXPECT_FALSE(c.InSkill());
}

TEST(CharSkillC04Test, EndSkillClearsHasCut) {
    // FAITHFUL (audit fix vs game_typed.c:1214-1263, fully recovered):
    // C04Controller__RoleSkillEnd writes in_skill=false AND has_cut=false before
    // ReSetSkillReload -- so a latched cut is cleared by RoleSkillEnd, not only by
    // the next TryActivateSkill.
    CharSkillC04 c(5.0F);
    c.TryActivateSkill();
    c.RoleAtk(true, false, 10, false);   // cut -> has_cut latched
    EXPECT_TRUE(c.HasCut());
    c.EndSkill();
    EXPECT_FALSE(c.HasCut());            // RoleSkillEnd clears has_cut
    EXPECT_FALSE(c.InSkill());
}

TEST(CharSkillC04Test, EndSkillClosesEffectWindow) {
    // PORT CHOICE (T-37, item 2/R18-Codex block; see EndSkill()'s doc comment):
    // the decompiled RoleSkillEnd itself does NOT touch in_skill_effect (that is
    // normally closed only via a kill / EndSkillEffect) -- but this port's chosen
    // end trigger (a 2nd skill-button press) can fire BEFORE any kill ever closes
    // the window, so EndSkill() now ALSO clears in_skill_effect (and has_cut)
    // unconditionally, so no cut can survive past the end.
    CharSkillC04 c(5.0F);
    c.TryActivateSkill();
    EXPECT_TRUE(c.InSkillEffect());
    c.EndSkill();
    EXPECT_FALSE(c.InSkillEffect())      // window closed by EndSkill (T-37 port choice)
        << "no cut may survive the end, even when no kill ever closed the window";
    EXPECT_FALSE(c.HasCut());
}

// ---- Cooldown (reused PlayerDash/C01 count-up; flagged) ------------------------

TEST(CharSkillC04Test, CooldownRechargesAndClamps) {
    CharSkillC04 c(2.0F);
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

TEST(CharSkillC04Test, NonPositiveDtIsIgnored) {
    CharSkillC04 c(3.0F);
    c.TryActivateSkill();
    c.EndSkill();
    const float before = c.CooldownRemaining();
    c.Tick(0.0F);
    c.Tick(-100.0F);
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), before); // no advance
}

TEST(CharSkillC04Test, TickDoesNotAutoEndSkill) {
    // FAITHFUL: C04 has NO recovered Update / in_skill_time countdown (unlike C01),
    // so ticking inside the skill must NOT auto-end it -- the window persists until
    // a kill or EndSkill.
    CharSkillC04 c(4.0F);
    c.TryActivateSkill();
    EXPECT_TRUE(c.InSkill());
    c.Tick(10000.0F);                    // huge tick
    EXPECT_TRUE(c.InSkill());            // still in skill (no auto-end)
    EXPECT_TRUE(c.InSkillEffect());      // window still open
}

// ---- RoleAtk (C04Controller__RoleAtk @ 156423) --------------------------------

TEST(CharSkillC04Test, RoleAtkPressFiresHandWhenNoEffectWindow) {
    CharSkillC04 c(5.0F);
    const CharSkillC04::AtkDecision d = c.RoleAtk(true, false, 10, false);
    EXPECT_FALSE(d.triggerItem);
    EXPECT_TRUE(d.setHandAttack);
    EXPECT_TRUE(d.handAttackValue);
    EXPECT_FALSE(d.doCut);               // no cut outside the effect window
}

TEST(CharSkillC04Test, RoleAtkReleaseStopsHand) {
    CharSkillC04 c(5.0F);
    const CharSkillC04::AtkDecision d = c.RoleAtk(false, false, 10, false);
    EXPECT_TRUE(d.setHandAttack);
    EXPECT_FALSE(d.handAttackValue);     // release -> SetAttack(false)
    EXPECT_FALSE(d.doCut);
}

TEST(CharSkillC04Test, RoleAtkItemPressTriggersItemWhenNotInSkill) {
    CharSkillC04 c(5.0F);
    const CharSkillC04::AtkDecision d = c.RoleAtk(true, true, 10, false); // on pickup
    EXPECT_TRUE(d.triggerItem);
    EXPECT_FALSE(d.setHandAttack);       // early return: no hand action
    EXPECT_FALSE(d.doCut);
}

TEST(CharSkillC04Test, RoleAtkItemPressBlockedDuringSkill) {
    // FAITHFUL: the item-trigger has a !in_skill guard (156434), so pressing on a
    // pickup DURING the ultimate does NOT trigger the item -- it falls through to
    // the cut path instead.
    CharSkillC04 c(5.0F);
    c.TryActivateSkill();                 // in_skill + effect window up
    const CharSkillC04::AtkDecision d = c.RoleAtk(true, true, 10, false);
    EXPECT_FALSE(d.triggerItem);          // item-trigger blocked during skill
    EXPECT_TRUE(d.doCut);                 // press in window -> cut
}

TEST(CharSkillC04Test, RoleAtkItemReleaseDoesNotTrigger) {
    CharSkillC04 c(5.0F);
    // in_item but NOT pressing -> falls through to the normal hand path.
    const CharSkillC04::AtkDecision d = c.RoleAtk(false, true, 10, false);
    EXPECT_FALSE(d.triggerItem);
    EXPECT_TRUE(d.setHandAttack);
    EXPECT_FALSE(d.handAttackValue);
}

TEST(CharSkillC04Test, RoleAtkPressInWindowCutsWithNormalBonus) {
    // skill_strengthen OFF -> dmg = atk + 3.
    CharSkillC04 c(5.0F);
    c.TryActivateSkill();
    EXPECT_TRUE(c.InSkillEffect());
    const CharSkillC04::AtkDecision d = c.RoleAtk(true, false, 20, false);
    EXPECT_TRUE(d.doCut);
    EXPECT_EQ(d.cutDamage, 20 + CharSkillC04::kCutBonusNormal); // 23
    EXPECT_FALSE(d.setHandAttack);        // the cut path does not also fire the hand
    EXPECT_TRUE(c.HasCut());              // latched
}

TEST(CharSkillC04Test, RoleAtkPressInWindowCutsWithStrengthenedBonus) {
    // skill_strengthen ON -> dmg = atk + 7.
    CharSkillC04 c(5.0F);
    c.TryActivateSkill();
    const CharSkillC04::AtkDecision d = c.RoleAtk(true, false, 20, true);
    EXPECT_TRUE(d.doCut);
    EXPECT_EQ(d.cutDamage, 20 + CharSkillC04::kCutBonusStrengthened); // 27
}

TEST(CharSkillC04Test, RoleAtkCutLatchesSoSecondPressJustFiresHand) {
    // FAITHFUL: has_cut latches after the first cut, so a held button (or second
    // press) takes the "has_cut != 0" branch -> hand.SetAttack, no second cut.
    CharSkillC04 c(5.0F);
    c.TryActivateSkill();
    const CharSkillC04::AtkDecision first = c.RoleAtk(true, false, 10, false);
    EXPECT_TRUE(first.doCut);
    EXPECT_TRUE(c.HasCut());
    const CharSkillC04::AtkDecision second = c.RoleAtk(true, false, 10, false);
    EXPECT_FALSE(second.doCut);           // already cut this window
    EXPECT_TRUE(second.setHandAttack);    // falls to hand.SetAttack
    EXPECT_TRUE(second.handAttackValue);
}

TEST(CharSkillC04Test, RoleAtkReleaseInWindowBeforeCutDoesNothing) {
    // A release while the window is up and uncut takes neither branch (no else for
    // the !press case in the decomp) -> no cut, no hand action.
    CharSkillC04 c(5.0F);
    c.TryActivateSkill();
    const CharSkillC04::AtkDecision d = c.RoleAtk(false, false, 10, false);
    EXPECT_FALSE(d.doCut);
    EXPECT_FALSE(d.setHandAttack);
    EXPECT_FALSE(c.HasCut());             // no cut latched on a release
}

// ---- KillSomeOne combo chain (C04Controller__KillSomeOne @ 156499) -------------

TEST(CharSkillC04Test, KillOutsideEffectWindowIsNoOp) {
    CharSkillC04 c(5.0F);
    c.TryActivateSkill();                 // opens the effect window
    EXPECT_TRUE(c.KillSomeOne());         // first kill: in window -> counts
    EXPECT_FALSE(c.InSkillEffect());      // window closed by the kill
    EXPECT_FALSE(c.KillSomeOne());        // now out of window -> no-op
    EXPECT_EQ(c.Combo(), 1);              // combo unchanged by the no-op kill
}

TEST(CharSkillC04Test, KillInWindowRefreshesCooldownAndClosesWindow) {
    CharSkillC04 c(5.0F);
    c.TryActivateSkill();
    EXPECT_TRUE(c.InSkillEffect());
    EXPECT_FALSE(c.RefreshedThisWindow()); // T-37: fresh window, no kill-refresh yet.
    EXPECT_TRUE(c.KillSomeOne());          // in-window kill (combo 1, not 5th)
    EXPECT_EQ(c.Combo(), 1);
    EXPECT_TRUE(c.RefreshedThisWindow())   // T-37: ReflashSkillCd fired (non-wrap kill).
        << "EndSkill must consult this so a later toggle-close does not re-spend it";
    EXPECT_TRUE(c.SkillReady());           // ReflashSkillCd -> instantly ready again
    EXPECT_FALSE(c.InSkillEffect());       // window closed (EndSkillEffect clears 0x94)
    EXPECT_FALSE(c.HasCut());              // never cut this window -> still false
                                           // (the kill does NOT touch has_cut)

    // T-37: closing the toggle (EndSkill) AFTER a refresh must NOT spend the
    // cooldown a 2nd time -- SkillReady stays true instead of resetting to 0.
    c.EndSkill();
    EXPECT_TRUE(c.SkillReady());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 0.0F);
}

TEST(CharSkillC04Test, FifthKillWrapsComboAndDoesNotRefresh) {
    // FAITHFUL: combo == 5 wraps to 0 with NO ReflashSkillCd. Each kill closes the
    // window; TryActivateSkill re-opens it. T-37: because kills 1-4 each refresh
    // the cooldown (RefreshedThisWindow), the toggle-close (EndSkill) between them
    // no longer needs to wait out a full cooldown to re-activate -- contrast the
    // OLD version of this test, which had to EndSkill+rely on a full recharge
    // before every re-activation because EndSkill used to spend the cooldown
    // unconditionally.
    CharSkillC04 c(5.0F);
    for (int i = 1; i <= 4; ++i) {
        ASSERT_TRUE(c.SkillReady()) << i;
        ASSERT_TRUE(c.TryActivateSkill()) << i; // re-open the window.
        EXPECT_TRUE(c.KillSomeOne()) << i;      // kills 1..4: refresh -> ready.
        EXPECT_EQ(c.Combo(), i);
        EXPECT_TRUE(c.RefreshedThisWindow()) << i;
        c.EndSkill(); // toggle-close; T-37: must not wipe the just-granted refresh.
        EXPECT_TRUE(c.SkillReady()) << i << ": no wait needed after a refreshed close";
    }
    // 5th kill: combo wraps to 0, NO refresh.
    ASSERT_TRUE(c.TryActivateSkill());
    EXPECT_TRUE(c.KillSomeOne());          // 5th in-window kill.
    EXPECT_EQ(c.Combo(), 0);               // wrapped.
    EXPECT_FALSE(c.RefreshedThisWindow())  // the wrap kill itself does NOT refresh...
        << "the 5th kill does not call ReflashSkillCd";
    EXPECT_TRUE(c.SkillReady())            // ...but kill 4's refresh already left
        << "this_skill_time == skill_cd going in, and nothing spent it since";
    // T-37: closing after a wrap-only window (RefreshedThisWindow == false) spends
    // the cooldown NORMALLY -- this is the one case in this loop where EndSkill
    // actually resets this_skill_time to 0, distinguishing "wrap, no refresh" from
    // every non-wrap kill above.
    c.EndSkill();
    EXPECT_FALSE(c.SkillReady())
        << "no refresh happened this window -- EndSkill spends the cooldown normally";
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 5.0F);
}

TEST(CharSkillC04Test, KillDoesNotClearHasCutButReactivationDoes) {
    // FAITHFUL: EndSkillEffect clears ONLY in_skill_effect (0x94); has_cut (0x95)
    // is NOT written =0 by EndSkillEffect / KillSomeOne. So a kill closes the
    // window but LEAVES has_cut latched -- it is cleared by the next RoleSkillEnd
    // (EndSkillClearsHasCut) or by the next TryActivateSkill's window re-open
    // (fabrication_flags item (c)), not by the kill itself.
    CharSkillC04 c(5.0F);
    c.TryActivateSkill();
    c.RoleAtk(true, false, 10, false);     // cut -> has_cut latched
    EXPECT_TRUE(c.HasCut());
    EXPECT_TRUE(c.KillSomeOne());          // closes window; does NOT touch has_cut
    EXPECT_TRUE(c.HasCut());               // FAITHFUL: kill leaves has_cut latched
    EXPECT_FALSE(c.InSkillEffect());       // window closed by the kill
    EXPECT_TRUE(c.SkillReady());           // kill refreshed the cooldown (combo 1)
    // FAITHFUL: in_skill (0x55) is NOT cleared by a kill -- EndSkillEffect @156401
    // writes only in_skill_effect (0x94); in_skill is cleared solely by RoleSkillEnd.
    // So re-activation stays blocked until the skill ends, even though the kill
    // refreshed the cooldown (the latch and the refresh are separate lifecycle events).
    EXPECT_TRUE(c.InSkill());
    EXPECT_FALSE(c.TryActivateSkill());    // blocked: in_skill still latched
    c.EndSkill();                          // RoleSkillEnd: in_skill=0 (+ cooldown, see below)
    EXPECT_FALSE(c.InSkill());
    // T-37 (item 2/R18-Codex block): the KillSomeOne above (combo 1, non-wrap)
    // already refreshed the cooldown (RefreshedThisWindow was true going into this
    // EndSkill), so EndSkill must NOT spend it a 2nd time -- SkillReady stays true
    // instead of the old "ReSetSkillReload always resets it" assumption. This is
    // the exact bug item 2 fixes: the toggle-close must not silently erase a kill's
    // refresh.
    EXPECT_TRUE(c.SkillReady()) << "EndSkill must not wipe the kill-refreshed cooldown (T-37)";
    // The reconstructed activation window-open re-clears has_cut for the new window
    // (no extra recharge wait needed -- the cooldown was already ready).
    EXPECT_TRUE(c.TryActivateSkill());     // fresh activation re-opens the window
    EXPECT_FALSE(c.HasCut());              // RECONSTRUCTED: activation clears has_cut
}

// ---- Determinism: ZERO RNG draws on every path ---------------------------------

TEST(CharSkillC04Test, NoRngDrawOnAnyPath) {
    // No recovered body draws from rg_random; exercising the whole brain must leave
    // the stream un-advanced, so two seeded instances stay in lockstep.
    CharSkillC04 a(3.0F);
    CharSkillC04 b(3.0F);
    a.SetSeed(2024);
    b.SetSeed(2024);
    EXPECT_TRUE(a.Seeded());
    for (int i = 0; i < 16; ++i) {
        a.TryActivateSkill();
        a.RoleAtk(true, true, 12, true);
        a.RoleAtk(false, false, 12, false);
        a.KillSomeOne();
        a.Tick(250.0F);
        a.EndSkill();
        a.Tick(250.0F);
    }
    // 'a' ran the full brain; 'b' was untouched. If any draw had leaked, the next
    // draw on 'a' would diverge from 'b'. Same value -> stream truly untouched.
    EXPECT_EQ(a.Rng().Range(0, 1000000), b.Rng().Range(0, 1000000));
}

TEST(CharSkillC04Test, FullCycleIsDeterministic) {
    // Drive identical activate/attack/kill/cooldown cycles on two instances and
    // compare the observable trace at every step -> fully deterministic.
    auto run = [](CharSkillC04 &c) -> std::vector<float> {
        std::vector<float> trace;
        for (int i = 0; i < 12; ++i) {
            trace.push_back(c.TryActivateSkill() ? 1.0F : 0.0F);
            const CharSkillC04::AtkDecision d = c.RoleAtk(true, false, 15, i % 2 == 0);
            trace.push_back(d.doCut ? static_cast<float>(d.cutDamage) : 0.0F);
            trace.push_back(c.KillSomeOne() ? 1.0F : 0.0F);
            trace.push_back(static_cast<float>(c.Combo()));
            trace.push_back(c.SkillReady() ? 1.0F : 0.0F);
            c.EndSkill();
            c.Tick(1500.0F);
            trace.push_back(c.CooldownRemaining());
            // Use EXPECT (not ASSERT) inside this value-returning lambda.
            EXPECT_GE(c.Combo(), 0);
        }
        return trace;
    };
    CharSkillC04 a(2.0F);
    CharSkillC04 b(2.0F);
    const std::vector<float> ta = run(a);
    const std::vector<float> tb = run(b);
    ASSERT_EQ(ta.size(), tb.size());
    for (std::size_t i = 0; i < ta.size(); ++i) {
        EXPECT_FLOAT_EQ(ta[i], tb[i]);
    }
}

// NOLINTEND(readability-magic-numbers)
