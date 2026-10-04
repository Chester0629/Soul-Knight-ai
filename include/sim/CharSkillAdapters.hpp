#ifndef GAME_SIM_CHARSKILLADAPTERS_HPP
#define GAME_SIM_CHARSKILLADAPTERS_HPP

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

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
#include "combat/SkillWorld.hpp"
#include "data/GameData.hpp"
#include "sim/ICharSkill.hpp"
#include "sim/SimConfig.hpp"

namespace Game::Sim {

// === (d) per-hero skill adapters (B1-P4a/P3.3.b/P3.3.c). The skill-brain SOURCE is
// NEVER modified; each adapter only CALLS public brain methods. C01 (dual-hand
// mirror), C02 (forward dash), C04 (sword cut + kill combo), C08 (shield absorb), C10
// and C13 (armor-regen passive) are FAITHFUL via the existing sim; P3.3.c adds real
// bodies for C03 (thunder chain), C05 (bottle volley cue), C07 (bat swarm), C11 (holy
// light) and C12 (wolf summon cue / melee cut) -- see each adapter. Only C06 and C09
// remain GATE+COOLDOWN-ONLY (trigger/cooldown run, the combat effect is a deliberate
// no-op -- recorded debt). ========================

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
/// heroes (C06 today -- C04/C08/C10/C13 graduated to real adapters in P3.3.b and
/// C03/C05/C07/C11/C12 in P3.3.c). TryTrigger activates then immediately spends the
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

// === P3.3.c: real per-hero adapters for C03/C05/C07/C11/C12, replacing the stub. Each
// brain body is ported from game_typed.c where visible and from the ARM disassembly of
// libil2cpp.so where the typed decompile truncates (T-39); the world effects run in
// TickEffects against the sim's SkillWorldView (pixels in, Unity world units inside:
// 1 unit == kWorldUnitPx == 32 px, T-40). ========================================

namespace detail {

/// Hostiles of @p w in Unity world units (the brains' coordinate system).
inline std::vector<Game::SkillTarget> HostilesInUnits(const ICharSkill::SkillWorldView &w) {
    std::vector<Game::SkillTarget> out;
    out.reserve(w.hostiles.size());
    for (const ICharSkill::HostileView &h : w.hostiles) {
        out.push_back({h.id, h.pos / kWorldUnitPx});
    }
    return out;
}

/// The sim's pixel-space wall LOS query wrapped for a world-unit caller (T-41).
inline Game::SkillLineBlockedFn LosInUnits(const ICharSkill::SkillWorldView &w) {
    if (!w.lineBlocked) {
        return {};
    }
    const std::function<bool(glm::vec2, glm::vec2)> px = w.lineBlocked;
    return [px](glm::vec2 a, glm::vec2 b) { return px(a * kWorldUnitPx, b * kWorldUnitPx); };
}

} // namespace detail

/// C03 -- the Wizard's thunder (real body: TickEffects drives CreateTunder + the chain).
/// Press -> in_skill, Invoke("CreateTunder", 0.4 s) -> nearest hostile within 14 units
/// with a clear wall line gets struck for 8 (BulletThunder.StartThunder value3) and a
/// ThunderChain arcs on (4 hops max, 0.15 s apart, 10 units around the strike point);
/// RoleSkillEnd runs after CreateTunder whether or not a target was found. NOT PORTED
/// (T-43): the electric dizzy roll, aura / thunder visuals, camera shake.
class CharSkillC03Adapter : public CharSkillBase<Game::CharSkillC03> {
public:
    using CharSkillBase::CharSkillBase;
    bool NeedsWorld() const override { return true; }
    TriggerResult TryTrigger() override {
        return m_Brain.TryActivateSkill() ? TriggerResult{TriggerOutcome::Activated, false}
                                          : TriggerResult{};
    }
    AtkEffect RoleAtk(bool /*pressDown*/, bool /*standingOnItem*/) override { return {}; }
    void TickEffects(const SkillWorldView &w, SkillEffectOut &out) override {
        const std::vector<Game::SkillTarget> hostiles = detail::HostilesInUnits(w);
        if (m_Brain.CreateTunderDue()) {
            const glm::vec2 player = w.playerPos / kWorldUnitPx;
            const int pick = Game::CharSkillC03::PickNearestTarget(hostiles, player,
                                                                  detail::LosInUnits(w));
            const Game::CharSkillC03::TunderDecision d = m_Brain.CreateTunder(pick >= 0);
            if (d.strike) {
                out.hits.push_back({hostiles[static_cast<std::size_t>(pick)].id, d.damage});
                out.cues.push_back("thunder_strike");
                Game::ThunderChain chain;
                chain.Start(hostiles[static_cast<std::size_t>(pick)], d.damage,
                            /*intensify=*/false); // T-42: skill_strengthen not wired.
                m_Chains.push_back(chain);
            }
        }
        std::vector<Game::ThunderChain::Hit> hits;
        for (Game::ThunderChain &c : m_Chains) {
            c.Step(w.dtSec, hostiles, hits);
        }
        m_Chains.erase(std::remove_if(m_Chains.begin(), m_Chains.end(),
                                      [](const Game::ThunderChain &c) { return c.Done(); }),
                       m_Chains.end());
        for (const Game::ThunderChain::Hit &h : hits) {
            out.hits.push_back({h.id, h.damage});
            out.cues.push_back("thunder_hop");
        }
    }
    /// Test/debug introspection: chains still arcing.
    std::size_t ActiveChains() const { return m_Chains.size(); }

private:
    std::vector<Game::ThunderChain> m_Chains;
};

/// C05 -- the Alchemist's bottle volley. The ultimate is INSTANT (RoleSkillEnd runs in
/// the same RoleSkill call, DISASM 0x2661e8-0x2661ec): it is never observably "in skill" and the
/// cooldown starts at once. The thrown BulletBottle + Gas zone are NOT PORTED (T-44):
/// the adapter emits the "bottle_throw" cue (count / variant / fan) instead.
class CharSkillC05Adapter : public CharSkillBase<Game::CharSkillC05> {
public:
    /// @param variantCount bottle_objs.Length (CharacterDef::skillRefs["bottle_objs"]).
    CharSkillC05Adapter(float skillCd, int variantCount)
        : CharSkillBase<Game::CharSkillC05>(skillCd), m_VariantCount(variantCount) {}
    bool NeedsWorld() const override { return true; }
    TriggerResult TryTrigger() override {
        const Game::CharSkillC05::RoleSkillResult r =
            m_Brain.RoleSkill(/*skinIndex=*/0, m_VariantCount, /*skillStrengthen=*/false);
        if (!r.activated) {
            return {};
        }
        m_PendingCue = "bottle_throw n=" + std::to_string(r.bottleCount) + " variant=" +
                       std::to_string(r.variantIndex) + "/" + std::to_string(r.variantCount) +
                       " NOT_PORTED(T-44)";
        return {TriggerOutcome::Activated, false};
    }
    AtkEffect RoleAtk(bool /*pressDown*/, bool /*standingOnItem*/) override { return {}; }
    void TickEffects(const SkillWorldView & /*w*/, SkillEffectOut &out) override {
        if (!m_PendingCue.empty()) {
            out.cues.push_back(m_PendingCue);
            m_PendingCue.clear();
        }
    }

private:
    int m_VariantCount = 0;
    std::string m_PendingCue;
};

/// C07 -- the Vampire's bat swarm. Press -> in_skill + 6 BulletBat clones (BatSwarm);
/// each bat that reaches the player calls BatBack: a miss banks skill_cd / 6 of
/// cooldown refund, every 3rd hit restores 1 HP (5 energy at full HP), the 6th bat ends
/// the skill (RoleSkillEnd re-applies the banked refund). The ultimate stays in_skill
/// until the last bat is back. NOT PORTED (T-45): bat knockback / hit effect.
class CharSkillC07Adapter : public CharSkillBase<Game::CharSkillC07> {
public:
    /// @param batDamage BulletBat.damage (CharacterDef::skillParams, prefab data).
    CharSkillC07Adapter(float skillCd, int batDamage)
        : CharSkillBase<Game::CharSkillC07>(skillCd), m_BatDamage(batDamage) {}
    bool NeedsWorld() const override { return true; }
    TriggerResult TryTrigger() override {
        return m_Brain.TryActivateSkill() ? TriggerResult{TriggerOutcome::Activated, false}
                                          : TriggerResult{};
    }
    AtkEffect RoleAtk(bool /*pressDown*/, bool /*standingOnItem*/) override { return {}; }
    void TickEffects(const SkillWorldView &w, SkillEffectOut &out) override {
        const glm::vec2 player = w.playerPos / kWorldUnitPx;
        if (const int n = m_Brain.ConsumeLaunch(); n > 0) {
            m_Swarm.Launch(n, player, m_BatDamage, Game::BatSwarm::kSpeedUnitsPerSec);
            out.cues.push_back("bat_swarm n=" + std::to_string(n));
        }
        const std::vector<Game::SkillTarget> hostiles = detail::HostilesInUnits(w);
        std::vector<Game::BatSwarm::Hit> hits;
        std::vector<bool> backs;
        m_Swarm.Step(w.dtSec, player, hostiles, hits, backs);
        for (const Game::BatSwarm::Hit &h : hits) {
            out.hits.push_back({h.id, h.damage});
        }
        int hp = w.playerHp;
        for (const bool hasHit : backs) {
            const Game::CharSkillC07::BatBackResult r =
                m_Brain.BatBack(hasHit, hp, w.playerMaxHp);
            out.healHp += r.healHp;
            hp += r.healHp;
            out.restoreEnergy += r.restoreEnergy;
        }
    }
    /// Test/debug introspection: bats still airborne.
    std::size_t BatsAirborne() const { return m_Swarm.Alive(); }

private:
    int m_BatDamage = 0;
    Game::BatSwarm m_Swarm;
};

/// C11 -- the Priest's holy light. The ultimate is INSTANT (RoleSkillEnd in the same
/// RoleSkill call); the spawned light pulses every 0.8 s for 1.8 s, healing the player
/// 1 HP per pulse while within 4 units (HolyLightZone). NOT PORTED (T-47): pet heal.
class CharSkillC11Adapter : public CharSkillBase<Game::CharSkillC11> {
public:
    using CharSkillBase::CharSkillBase;
    bool NeedsWorld() const override { return true; }
    TriggerResult TryTrigger() override {
        return m_Brain.TryActivateSkill() ? TriggerResult{TriggerOutcome::Activated, false}
                                          : TriggerResult{};
    }
    AtkEffect RoleAtk(bool /*pressDown*/, bool /*standingOnItem*/) override { return {}; }
    void TickEffects(const SkillWorldView &w, SkillEffectOut &out) override {
        const glm::vec2 player = w.playerPos / kWorldUnitPx;
        if (m_Brain.ConsumeSpawn()) {
            Game::HolyLightZone z;
            z.Start(player, /*buffDigestive=*/false); // T-42: run buff not wired.
            m_Zones.push_back(z);
            out.cues.push_back("holy_light");
        }
        for (Game::HolyLightZone &z : m_Zones) {
            out.healHp += z.Step(w.dtSec, player);
        }
        m_Zones.erase(std::remove_if(m_Zones.begin(), m_Zones.end(),
                                     [](const Game::HolyLightZone &z) { return !z.Alive(); }),
                      m_Zones.end());
    }
    /// Test/debug introspection: lights still alive.
    std::size_t ActiveZones() const { return m_Zones.size(); }

private:
    std::vector<Game::HolyLightZone> m_Zones;
};

/// C12 -- the Druid's two-phase skill (see CharSkillC12): the first cast summons two
/// wolves (NOT PORTED, T-48: cue only) and in_skill stays set (INFERRED, T-49: RoleSkillEnd
/// does not store it and no other clear site was found); every later cast
/// is RGHand.AtkCut(atk) -> the sim's melee cut (T-36 reach). Each cast restarts the
/// cooldown (RoleSkillEnd), 2.5 s.
class CharSkillC12Adapter : public CharSkillBase<Game::CharSkillC12> {
public:
    /// @param atk role_attribute.atk ; @param critical role_attribute.critical.
    CharSkillC12Adapter(float skillCd, int atk, int critical)
        : CharSkillBase<Game::CharSkillC12>(skillCd), m_Atk(atk), m_Critical(critical) {}
    bool NeedsWorld() const override { return true; }
    TriggerResult TryTrigger() override {
        const Game::CharSkillC12::RoleSkillResult r =
            m_Brain.RoleSkill(m_Atk, m_Critical, /*skillStrengthen=*/false);
        if (r.phase == Game::CharSkillC12::Phase::None) {
            return {};
        }
        m_Pending = r;
        return {TriggerOutcome::Activated, false};
    }
    AtkEffect RoleAtk(bool /*pressDown*/, bool /*standingOnItem*/) override { return {}; }
    void TickEffects(const SkillWorldView & /*w*/, SkillEffectOut &out) override {
        using Phase = Game::CharSkillC12::Phase;
        if (m_Pending.phase == Phase::Summon) {
            out.cues.push_back("wolf_summon n=" + std::to_string(m_Pending.wolves) +
                               " NOT_PORTED(T-48)");
        } else if (m_Pending.phase == Phase::Cut) {
            out.meleeCutDamage = m_Pending.cutDamage;
            out.cues.push_back("atk_cut dmg=" + std::to_string(m_Pending.cutDamage));
        }
        m_Pending = {};
    }

private:
    int m_Atk = 0;
    int m_Critical = 0;
    Game::CharSkillC12::RoleSkillResult m_Pending{};
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
///
/// P3.3.c: @p def (optional, the resolved CharacterDef) feeds the heroes whose ultimate
/// reads effect-prefab data -- C05 bottle variant count (skillRefs["bottle_objs"]), C07
/// BulletBat.damage (skillParams: the prefab serializes 5, the ctor default is 2), C12
/// critical -- and @p atk (the resolved role_attribute.atk, Simulation::SetPlayerSkill's
/// atk) seeds C12 AtkCut. A null def -> the decompile ctor defaults.
inline std::unique_ptr<ICharSkill> MakeCharSkill(const std::string &charId, float skillCd,
                                                 float inSkillTime,
                                                 const Game::CharacterDef *def = nullptr,
                                                 int atk = 0) {
    if (charId == "c02") { return std::make_unique<CharSkillC02Adapter>(skillCd); }
    if (charId == "c03") { return std::make_unique<CharSkillC03Adapter>(skillCd); }
    if (charId == "c04") { return std::make_unique<CharSkillC04Adapter>(skillCd); }
    if (charId == "c05") {
        const int variants =
            def != nullptr ? static_cast<int>(def->SkillRefCount("bottle_objs")) : 0;
        return std::make_unique<CharSkillC05Adapter>(skillCd, variants);
    }
    if (charId == "c06") { return std::make_unique<StubSkillAdapter<Game::CharSkillC06>>(skillCd); }
    if (charId == "c07") {
        // BulletBat.damage: the prefab serializes 5 (build_gamedata -> skill_params); the
        // ctor default 2 (game_typed.c:89494-89500) only survives when no data is given.
        const int dmg = def != nullptr
                            ? static_cast<int>(def->SkillParam("BulletBat.damage", 2.0F))
                            : 2;
        return std::make_unique<CharSkillC07Adapter>(skillCd, dmg);
    }
    if (charId == "c08") { return std::make_unique<CharSkillC08Adapter>(skillCd); }
    if (charId == "c09") { return std::make_unique<CharSkillC09Adapter>(skillCd); }
    if (charId == "c10") {
        return std::make_unique<CharSkillC10Adapter>(skillCd, inSkillTime);
    }
    if (charId == "c11") { return std::make_unique<CharSkillC11Adapter>(skillCd); }
    if (charId == "c12") {
        const int a = atk > 0 ? atk : (def != nullptr ? def->atk : 0);
        const int c = def != nullptr ? def->critical : 0;
        return std::make_unique<CharSkillC12Adapter>(skillCd, a, c);
    }
    if (charId == "c13") {
        return std::make_unique<CharSkillC13Adapter>(skillCd, inSkillTime);
    }
    return std::make_unique<CharSkillC01Adapter>(skillCd, inSkillTime); // c01 + default fallback
}

} // namespace Game::Sim

#endif /* GAME_SIM_CHARSKILLADAPTERS_HPP */
