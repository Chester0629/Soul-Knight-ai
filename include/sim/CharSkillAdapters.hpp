#ifndef GAME_SIM_CHARSKILLADAPTERS_HPP
#define GAME_SIM_CHARSKILLADAPTERS_HPP

#include <memory>
#include <string>

#include "combat/CharSkillC01.hpp"
#include "combat/CharSkillC02.hpp"
#include "combat/CharSkillC03.hpp"
#include "combat/CharSkillC04.hpp"
#include "combat/CharSkillC05.hpp"
#include "combat/CharSkillC06.hpp"
#include "combat/CharSkillC07.hpp"
#include "combat/CharSkillC08.hpp"
#include "combat/CharSkillC09.hpp"
#include "combat/CharSkillC10.hpp"
#include "combat/CharSkillC11.hpp"
#include "combat/CharSkillC12.hpp"
#include "combat/CharSkillC13.hpp"
#include "sim/ICharSkill.hpp"

namespace Game::Sim {

// === (d) per-hero skill adapters (B1-P4a/P3.3.b). The skill-brain SOURCE is NEVER
// modified; each adapter only CALLS public brain methods. C01 (dual-hand mirror),
// C02 (forward dash), C04 (sword cut + kill combo), C08 (shield absorb), C10 and
// C13 (armor-regen passive) are FAITHFUL via the existing sim; C03/C05/C06/C07/
// C09/C11/C12 remain GATE+COOLDOWN-ONLY stubs (trigger/cooldown/window run, the
// combat effect is a deliberate no-op until each effect subsystem stage lands --
// recorded debt). ========================

/// C01 -- dual-hand hero (FAITHFUL). The active window persists (Tick auto-ends it);
/// while in_skill the second hand mirrors the attack (the sim duplicates the shot).
class CharSkillC01Adapter : public CharSkillBase<Game::CharSkillC01> {
public:
    using CharSkillBase::CharSkillBase;
    TriggerResult TryTrigger() override {
        // Enter the active window; do NOT end here -- Tick auto-ends it (the mirror
        // runs across the window). No instant effect.
        return m_Brain.TryActivateSkill() ? TriggerResult{TriggerOutcome::Activated, false}
                                          : TriggerResult{};
    }
    AtkEffect RoleAtk(bool pressDown, bool standingOnItem) override {
        const Game::CharSkillC01::AtkDecision d = m_Brain.RoleAtk(pressDown, standingOnItem);
        AtkEffect e;
        e.mirrorSecondHand = d.mirrorSecondHand;
        e.secondHandValue = d.secondHandAttackValue;
        e.triggerItem = d.triggerItem;
        return e;
    }
};

/// C02 -- dash/charge hero (FAITHFUL). The ultimate is an instant forward dash: on
/// activation it immediately ends (RoleSkillEnd), which spends the cooldown and emits
/// the dash impulse the owner applies to player movement. No in_skill attack hook.
class CharSkillC02Adapter : public CharSkillBase<Game::CharSkillC02> {
public:
    using CharSkillBase::CharSkillBase;
    TriggerResult TryTrigger() override {
        if (!m_Brain.TryActivateSkill()) {
            return {};
        }
        const Game::CharSkillC02::RoleSkillEndDecision d = m_Brain.EndSkill(); // dash + spend
        return {TriggerOutcome::Activated, d.applyDashImpulse};
    }
    AtkEffect RoleAtk(bool /*pressDown*/, bool /*standingOnItem*/) override { return {}; }
};

/// C09 -- bow draw/release TOGGLE (STUB). The toggle self-manages in_skill/cooldown;
/// the effect (drawn-bow modal weapon) is deferred. No attack hook wired here.
class CharSkillC09Adapter : public CharSkillBase<Game::CharSkillC09> {
public:
    using CharSkillBase::CharSkillBase;
    TriggerResult TryTrigger() override {
        (void)m_Brain.RoleSkill(); // toggle draw/release (state self-managed; effect deferred)
        return {TriggerOutcome::Activated, false};
    }
    AtkEffect RoleAtk(bool /*pressDown*/, bool /*standingOnItem*/) override { return {}; }
};

/// Gate+cooldown-only stub for the common `bool TryActivateSkill()` + `void EndSkill()`
/// heroes (C03/C05/C06/C07/C09/C11/C12 today -- C04/C08/C10/C13 graduated to real
/// adapters below, P3.3.b). TryTrigger activates then immediately spends the
/// cooldown (EndSkill), so the trigger/cooldown cycle runs; the ultimate EFFECT is a
/// no-op until that hero's subsystem stage.
template <class TBrain>
class StubSkillAdapter : public CharSkillBase<TBrain> {
public:
    using CharSkillBase<TBrain>::CharSkillBase;
    TriggerResult TryTrigger() override {
        if (!this->m_Brain.TryActivateSkill()) {
            return {};
        }
        this->m_Brain.EndSkill(); // spend the cooldown (effect deferred to the subsystem stage)
        return {TriggerOutcome::Activated, false};
    }
    AtkEffect RoleAtk(bool /*pressDown*/, bool /*standingOnItem*/) override { return {}; }
};

// === P3.3.b: real per-hero adapters for C04/C08/C10/C13, replacing the stub above.
// Each keeps the ultimate ACTIVE across frames (no immediate EndSkill on trigger);
// the window closes only via the brain's own logic (a windowed Tick auto-end, an
// explicit end, or -- C04 -- the kill-chain closing the cut window) exactly like the
// faithful C01/C02 adapters above. ========================================

/// C04 -- charge-and-cut melee hero (FAITHFUL gate/cut/combo; T-36 reach is a
/// documented port choice, see SimConfig.hpp). The ultimate opens a sword-cut
/// window (in_skill_effect); the NEXT press deals one cut (atk+3/+7) INSTEAD of a
/// normal shot (RoleAtkMelee.suppressWeaponFire), and in-window kills
/// (OnEnemyKilled -> KillSomeOne, called EXACTLY as decompiled, no adapter-side
/// gating) drive the 5-kill combo -- every kill but the 5th instantly refreshes the
/// cooldown (ReflashSkillCd, this_skill_time = skill_cd) and closes the cut window;
/// the 5th wraps the combo to 0 with NO refresh and also closes the window.
///
/// PORT CHOICE (C04-END / T-37, full reasoning on CharSkillC04::EndSkill()): the
/// recovered decomp never shows WHEN the underlying RoleSkillEnd (in_skill=false)
/// fires after the cut window closes -- only EndSkillEffect's in_skill_effect=false
/// is recovered, and calling EndSkill() from OnEnemyKilled was tried and rejected
/// (a kill must never itself close in_skill -- only the player's own 2nd press
/// does). So in_skill is left alone by a kill; instead, same as C08 below, a SECOND
/// skill-button press while already in_skill is the self-driven close (TryTrigger
/// toggles EndSkill() then). EndSkill() itself now tracks whether a kill refreshed
/// the cooldown THIS window (CharSkillC04::RefreshedThisWindow) and skips the
/// this_skill_time=0 spend when it did, so the toggle-close correctly preserves
/// whatever cooldown state KillSomeOne last left behind (instant-ready after a
/// refresh, or a full wait after a window that saw no kill) instead of silently
/// erasing it; it also now clears in_skill_effect/has_cut unconditionally so no cut
/// can survive past the end even when no kill ever closed the window.
class CharSkillC04Adapter : public CharSkillBase<Game::CharSkillC04> {
public:
    using CharSkillBase::CharSkillBase;
    TriggerResult TryTrigger() override {
        if (m_Brain.InSkill()) {
            m_Brain.EndSkill(); // PORT CHOICE (C04-END, see class comment above).
            return {TriggerOutcome::Released, false};
        }
        return m_Brain.TryActivateSkill() ? TriggerResult{TriggerOutcome::Activated, false}
                                          : TriggerResult{};
    }
    // C04's normal RoleAtk hook (mirror-shaped AtkEffect) carries no cut info --
    // the sim drives C04 entirely through RoleAtkMelee (below), which needs
    // atk/skillStrengthen the shared AtkEffect signature does not carry.
    AtkEffect RoleAtk(bool /*pressDown*/, bool /*standingOnItem*/) override { return {}; }
    MeleeAtkEffect RoleAtkMelee(bool pressDown, bool standingOnItem, int atk,
                                bool skillStrengthen) override {
        const Game::CharSkillC04::AtkDecision d =
            m_Brain.RoleAtk(pressDown, standingOnItem, atk, skillStrengthen);
        MeleeAtkEffect e;
        e.doCut = d.doCut;
        e.cutDamage = d.cutDamage;
        // FAITHFUL: every branch except the normal-fire / already-cut path skips
        // hand.SetAttack entirely (the item-trigger return, and an uncut press/
        // release both never reach it) -- suppress the ranged shot exactly then.
        e.suppressWeaponFire = !d.setHandAttack;
        return e;
    }
    void OnEnemyKilled() override {
        m_Brain.KillSomeOne(); // FAITHFUL: combo/refresh/wrap exactly as decompiled.
    }
};

/// C08 -- shield hero (FAITHFUL gate/absorb). While the ultimate is up, every
/// incoming hit (OnPlayerHurt) is absorbed straight out of shield_value instead of
/// hitting HP/armor. PORT CHOICE (C08-END): C08's Update has NO in_skill_time
/// countdown and no recovered Cancel/TryCancelSkill -- the only exposed way to
/// leave in_skill is EndSkill(). A second skill-button press while already in_skill
/// is modeled as the toggle-off (press-to-raise / press-to-lower is the common
/// shield-ultimate shape, and the only self-driven close this API surface allows).
class CharSkillC08Adapter : public CharSkillBase<Game::CharSkillC08> {
public:
    using CharSkillBase::CharSkillBase;
    TriggerResult TryTrigger() override {
        if (m_Brain.InSkill()) {
            m_Brain.EndSkill(); // PORT CHOICE (C08-END, T-37; see class comment above).
            return {TriggerOutcome::Released, false};
        }
        return m_Brain.TryActivateSkill() ? TriggerResult{TriggerOutcome::Activated, false}
                                          : TriggerResult{};
    }
    AtkEffect RoleAtk(bool /*pressDown*/, bool /*standingOnItem*/) override { return {}; }
    HurtOutcome OnPlayerHurt(int damage, bool awake) override {
        const Game::CharSkillC08::HurtDecision d = m_Brain.GetHurt(damage, awake);
        if (d.deferToBase) {
            return HurtOutcome{false, damage}; // !in_skill -> normal pipeline, unchanged.
        }
        // in_skill: either the shield absorbed it (awake), or the decomp's silent
        // !awake early-return (no effect at all) -- both skip the normal HP/armor
        // pipeline entirely, so both report absorbed with zero damage applied.
        return HurtOutcome{true, 0};
    }
};

/// C10 -- transforming "beast/paw" hero (FAITHFUL gate/window/armor-regen). The
/// window auto-ends via Tick (in_skill_time countdown -> the brain's own EndSkill,
/// same shape as C01). While active, firing arms skill_atk (RoleAtk's in_skill
/// animator-mirror branch, `setSkillAnim`, IS the in_skill gate); the next landed
/// player-bullet hit (OnPlayerBulletHit -> HurtSomeOne) consumes it, and every 3rd
/// such hit restores 1 armor. PORT CHOICE (T-38): the decomp's skill_atk-arming
/// write (FUN_0026a58c) could not be attributed to a named method (see the
/// header's own TODO), so which in-game trigger arms it is NOT recoverable from
/// the decomp as-is; arming it on an ORDINARY fire press while in_skill is this
/// port's chosen stand-in -- the natural per-hero-fire hook this adapter already
/// has (RoleAtk, called once per player-shot step) -- not a decompile-confirmed
/// trigger. A different real trigger (e.g. only a fully-charged shot, or a
/// specific weapon-fire animation event) would change WHEN skill_atk arms without
/// changing the every-3rd-hit cadence this brain models faithfully once armed.
class CharSkillC10Adapter : public CharSkillBase<Game::CharSkillC10> {
public:
    using CharSkillBase::CharSkillBase;
    TriggerResult TryTrigger() override {
        return m_Brain.TryActivateSkill() ? TriggerResult{TriggerOutcome::Activated, false}
                                          : TriggerResult{};
    }
    AtkEffect RoleAtk(bool pressDown, bool standingOnItem) override {
        const Game::CharSkillC10::AtkDecision d = m_Brain.RoleAtk(pressDown, standingOnItem);
        if (d.setSkillAnim && pressDown) {
            m_Brain.ArmSkillAtk(); // PORT CHOICE (T-38, "arm on ordinary fire"), see
                                   // class comment above -- not decompile-confirmed.
        }
        AtkEffect e;
        e.triggerItem = d.triggerItem;
        return e;
    }
    int OnPlayerBulletHit() override {
        return m_Brain.HurtSomeOne() ? Game::CharSkillC10::kArmorRestoreAmount : 0;
    }
};

/// C13 -- transfiguration / armor-regen hero (FAITHFUL gate/window/armor-regen),
/// structurally the C10 twin: the window auto-ends via Tick, firing while in_skill
/// arms the per-hit passive (animatorMirror IS the in_skill gate, same PORT CHOICE
/// (T-38) "arm on ordinary fire" as C10 above -- game_typed.c:4016-4020 confirms
/// C13Controller__RoleAtk's HEAD only; C13Controller__SkillAtk @ 0026c2fc does NOT
/// set skill_atk either, same unresolved-trigger situation as C10's FUN_0026a58c),
/// and OnPlayerBulletHit's ArmTickFromHit already returns the exact armor delta (0
/// or kArmorRestoredPerCycle) -- no bool->int translation needed.
class CharSkillC13Adapter : public CharSkillBase<Game::CharSkillC13> {
public:
    using CharSkillBase::CharSkillBase;
    TriggerResult TryTrigger() override {
        return m_Brain.TryActivateSkill() ? TriggerResult{TriggerOutcome::Activated, false}
                                          : TriggerResult{};
    }
    AtkEffect RoleAtk(bool pressDown, bool standingOnItem) override {
        const Game::CharSkillC13::AtkDecision d = m_Brain.RoleAtk(pressDown, standingOnItem);
        if (d.animatorMirror && pressDown) {
            m_Brain.SetSkillAtkFlag(true); // PORT CHOICE (T-38), see
                                           // CharSkillC10Adapter comment.
        }
        AtkEffect e;
        e.triggerItem = d.triggerItem;
        return e;
    }
    int OnPlayerBulletHit() override { return m_Brain.ArmTickFromHit(); }
};

/// Dispatch a character id -> its (d) skill adapter. All 13 heroes register. C01/C10/
/// C13 take (skillCd, inSkillTime); the rest take (skillCd). item 6 (R18/Codex
/// block): skillCd/inSkillTime/atk ARE per-character today -- GameScene::OnEnter
/// resolves them from the roster's own CharacterDef (characters.json's skill_cd/
/// in_skill_time/atk columns), falling back to the shared PlayerTemplate only when
/// no CharacterDef is found for charId (an unrecognised id). This dispatcher itself
/// stays timing-agnostic (it only routes charId -> adapter type; the caller already
/// resolved the per-hero numbers before calling MakeCharSkill/SetPlayerSkill), so
/// callers that DO pass a shared/placeholder pair (most unit tests, which often
/// reuse one skillCd/inSkillTime across cases for simplicity) will still see that
/// shared value here -- that is the CALLER's choice, not a limitation of this
/// dispatch layer. Unknown id -> c01 (safe default).
inline std::unique_ptr<ICharSkill> MakeCharSkill(const std::string &charId, float skillCd,
                                                 float inSkillTime) {
    if (charId == "c02") { return std::make_unique<CharSkillC02Adapter>(skillCd); }
    if (charId == "c03") { return std::make_unique<StubSkillAdapter<Game::CharSkillC03>>(skillCd); }
    if (charId == "c04") { return std::make_unique<CharSkillC04Adapter>(skillCd); }
    if (charId == "c05") { return std::make_unique<StubSkillAdapter<Game::CharSkillC05>>(skillCd); }
    if (charId == "c06") { return std::make_unique<StubSkillAdapter<Game::CharSkillC06>>(skillCd); }
    if (charId == "c07") { return std::make_unique<StubSkillAdapter<Game::CharSkillC07>>(skillCd); }
    if (charId == "c08") { return std::make_unique<CharSkillC08Adapter>(skillCd); }
    if (charId == "c09") { return std::make_unique<CharSkillC09Adapter>(skillCd); }
    if (charId == "c10") {
        return std::make_unique<CharSkillC10Adapter>(skillCd, inSkillTime);
    }
    if (charId == "c11") { return std::make_unique<StubSkillAdapter<Game::CharSkillC11>>(skillCd); }
    if (charId == "c12") { return std::make_unique<StubSkillAdapter<Game::CharSkillC12>>(skillCd); }
    if (charId == "c13") {
        return std::make_unique<CharSkillC13Adapter>(skillCd, inSkillTime);
    }
    return std::make_unique<CharSkillC01Adapter>(skillCd, inSkillTime); // c01 + default fallback
}

} // namespace Game::Sim

#endif /* GAME_SIM_CHARSKILLADAPTERS_HPP */
