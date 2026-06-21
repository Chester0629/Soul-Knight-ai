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

// === (d) per-hero skill adapters (B1-P4a). The skill-brain SOURCE is NEVER modified;
// each adapter only CALLS public brain methods. C01 (dual-hand mirror) + C02 (forward
// dash) are FAITHFUL via the existing sim; C03..C13 are GATE+COOLDOWN-ONLY stubs
// (trigger/cooldown/window run, the combat effect is a deliberate no-op until each
// effect subsystem stage lands -- recorded debt). ========================

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
/// heroes (C03/C04/C05/C06/C07/C08/C10/C11/C12/C13). TryTrigger activates then
/// immediately spends the cooldown (EndSkill), so the trigger/cooldown cycle runs;
/// the ultimate EFFECT is a no-op until that hero's subsystem stage. (C13's
/// TryActivateSkill(changing=false) uses its default arg; C06's TryCancelSkill and
/// C04/C10/C13's RoleAtk effects are part of the deferred work.)
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

/// Dispatch a character id -> its (d) skill adapter. All 13 heroes register. C01/C10/
/// C13 take (skillCd, inSkillTime); the rest take (skillCd). per-char skill_cd/
/// in_skill_time overrides are PENDING (one shared player_template), so today every
/// hero runs on the same timings (recorded debt). Unknown id -> c01 (safe default).
inline std::unique_ptr<ICharSkill> MakeCharSkill(const std::string &charId, float skillCd,
                                                 float inSkillTime) {
    if (charId == "c02") { return std::make_unique<CharSkillC02Adapter>(skillCd); }
    if (charId == "c03") { return std::make_unique<StubSkillAdapter<Game::CharSkillC03>>(skillCd); }
    if (charId == "c04") { return std::make_unique<StubSkillAdapter<Game::CharSkillC04>>(skillCd); }
    if (charId == "c05") { return std::make_unique<StubSkillAdapter<Game::CharSkillC05>>(skillCd); }
    if (charId == "c06") { return std::make_unique<StubSkillAdapter<Game::CharSkillC06>>(skillCd); }
    if (charId == "c07") { return std::make_unique<StubSkillAdapter<Game::CharSkillC07>>(skillCd); }
    if (charId == "c08") { return std::make_unique<StubSkillAdapter<Game::CharSkillC08>>(skillCd); }
    if (charId == "c09") { return std::make_unique<CharSkillC09Adapter>(skillCd); }
    if (charId == "c10") {
        return std::make_unique<StubSkillAdapter<Game::CharSkillC10>>(skillCd, inSkillTime);
    }
    if (charId == "c11") { return std::make_unique<StubSkillAdapter<Game::CharSkillC11>>(skillCd); }
    if (charId == "c12") { return std::make_unique<StubSkillAdapter<Game::CharSkillC12>>(skillCd); }
    if (charId == "c13") {
        return std::make_unique<StubSkillAdapter<Game::CharSkillC13>>(skillCd, inSkillTime);
    }
    return std::make_unique<CharSkillC01Adapter>(skillCd, inSkillTime); // c01 + default fallback
}

} // namespace Game::Sim

#endif /* GAME_SIM_CHARSKILLADAPTERS_HPP */
