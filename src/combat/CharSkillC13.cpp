#include "combat/CharSkillC13.hpp"

#include <algorithm>

namespace Game {

namespace {
constexpr float kMsPerSecond = 1000.0F;
} // namespace

CharSkillC13::CharSkillC13(float skillCd, float inSkillTime)
    : m_SkillCd(skillCd < 0.0F ? 0.0F : skillCd),
      m_InSkillTime(inSkillTime < 0.0F ? 0.0F : inSkillTime),
      // Start ready: a freshly SetUpChar hero has its first ultimate available
      // (skill_ready == skill_cd <= this_skill_time). Matches PlayerDash / C01.
      m_ThisSkillTime(skillCd < 0.0F ? 0.0F : skillCd) {}

// FAITHFUL: C13Controller__RoleSkill @ game_full.c:158606 gate --
// awake && role_attribute.skill_ready && !in_skill && !changing. The awake check
// (param_1[3], this+0x0C) is the caller's responsibility; the gate this unit owns
// is skill_ready && !in_skill && !changing, exactly the recovered branches at
// 158614 (awake), 158622 (skill_ready==1), and the bVar3=(in_skill==0) /
// changing(0xA4) double test at 158628-158636. NO RGRandom draw on this path.
bool CharSkillC13::TryActivateSkill(bool changing) {
    if (m_InSkill || changing || !SkillReady()) {
        return false; // in skill, mid-transfiguration, or on cooldown -> no-op
    }
    // Enter the skill state (in_skill = true). The hero-specific effect is the
    // get_transform tail-call at 158638 (owner concern); the cooldown spend is
    // deferred to EndSkill (RoleSkillEnd -> ReSetSkillReload), not done here --
    // the C13 body itself does not spend it. Arm the in_skill_time countdown
    // (param_1[0x26]) that C13Controller__Update decrements each frame, and clear
    // the passive bookkeeping for the fresh window.
    m_InSkill = true;
    m_InSkillTimeLeft = m_InSkillTime;
    m_HurtCount = 0;
    m_SkillAtk = false;
    return true;
}

// Models RGController.RoleSkillEnd -> RoleAttributePlayer.ReSetSkillReload:
// leave the skill state and restart the cooldown (this_skill_time = 0). Also
// clears the C13 passive state for the next activation.
void CharSkillC13::EndSkill() {
    if (!m_InSkill) {
        return;
    }
    m_InSkill = false;
    m_InSkillTimeLeft = 0.0F; // active window consumed
    m_HurtCount = 0;          // passive counter does not carry across activations
    m_SkillAtk = false;       // per-hit flag cleared
    m_ThisSkillTime = 0.0F;   // ReSetSkillReload(): restart the cooldown
}

// FAITHFUL: C13Controller__RoleAtk @ game_full.c:158758.
// Branch order preserved: awake gate -> (in_skill -> animator.SetBool mirror,
// FIRST) -> (in_item && press -> TriggerItem + return) -> (press && virtual
// predicate && !IsMelee -> aim nudge, owner/truncated) -> hand.SetAttack(value).
// NO RGRandom draw on this path.
CharSkillC13::AtkDecision CharSkillC13::RoleAtk(bool pressDown,
                                               bool standingOnItem) const {
    AtkDecision d;
    // The decomp's outer awake gate (param_1[3], this+0x0C) is the caller's
    // responsibility (this brain has no awake field); a non-awake hero produces
    // no actions, which an all-false decision represents.

    // 158766-158774: while in_skill, mirror the press on the paw/transfiguration
    // animator bool (animator.SetBool(StringLiteral_6685, value)). This is the
    // FIRST branch in the body -- before the item check -- distinct from C01.
    if (m_InSkill) {
        d.animatorMirror = true;
        d.animatorMirrorValue = pressDown;
    }

    // 158775-158780: in_item && press -> trigger the pickup and return early
    // (no hand.SetAttack on this path).
    if (standingOnItem && pressDown) {
        d.triggerItem = true;
        return d; // RGController.TriggerItem(); return;
    }

    // 158781-158799: a press-only, virtual-predicate-gated, non-melee aim nudge
    // (get_position) is truncated at the tail-call and carries no recoverable pure
    // logic / RNG, so it is intentionally not modeled (see fabrication_flags).

    // 158804: hand.SetAttack(value) on the primary hand (fires/stops the gun).
    d.setHandAttack = true;
    d.handAttackValue = pressDown;
    return d;
}

// FAITHFUL: C13Controller__Update @ game_full.c:158547 (fully recovered, pure).
// Order preserved: AttributeUpdate -> SkillReload (cooldown count-up, run EVERY
// awake frame, NOT frozen while in_skill) THEN the in_skill_time countdown +
// auto-end (slot 0x17c == RoleSkillEnd) when it reaches <= 0. NO RGRandom draw.
void CharSkillC13::Tick(float dtMs) {
    if (dtMs <= 0.0F) {
        return;
    }
    const float dt = dtMs / kMsPerSecond;

    // 158554: AttributeUpdate() -> SkillReload(dt). this_skill_time counts up,
    // clamped to skill_cd. Unconditional: it advances even while in_skill (the
    // in_skill check below sits AFTER this in the decomp).
    if (m_ThisSkillTime < m_SkillCd) {
        m_ThisSkillTime = std::min(m_ThisSkillTime + dt, m_SkillCd);
    }

    // 158557-158588: while in_skill, in_skill_time (param_1[0x26]) counts DOWN by
    // dt; when it reaches <= 0 the skill auto-ends through RoleSkillEnd (slot
    // 0x17c) and the body returns immediately. EndSkill() models that path
    // (in_skill = false; ReSetSkillReload(): this_skill_time = 0), so the reset is
    // this frame's final cooldown state -- matching the decomp's post-RoleSkillEnd
    // return. (The IsLocalPlayer -> UICanvas UI refresh at 158572 is owner-side.)
    if (m_InSkill) {
        m_InSkillTimeLeft -= dt;
        if (m_InSkillTimeLeft <= 0.0F) {
            EndSkill();
        }
    }
}

// FAITHFUL: C13Controller__HurtSomeOne @ game_full.c:158810 (the bookkeeping HEAD).
// Models exactly the recovered branches:
//   active = (in_skill != 0);                         // 158818
//   if (active && skill_atk) {                         // 158821 (0xAC)
//     skill_atk = 0;                                   // 158823 consume the flag
//     if (++hurt_count > 2) {                          // 158824-158827 (0xA8)
//       RoleAttributePlayer.RestoreArmor(role_attr, 1);// 158832 (0x44, +1 armor)
//       hurt_count = 0;                                // 158833
//     }
//   }
//   base.HurtSomeOne();                                // 158838 (owner, not modeled)
// NO RGRandom draw on this path. The base.HurtSomeOne() chain (the actual damage
// application) is the owner's concern.
int CharSkillC13::ArmTickFromHit() {
    if (!m_InSkill || !m_SkillAtk) {
        return 0; // not active, or this hit was not flagged -> no passive effect
    }
    m_SkillAtk = false; // consume the per-hit flag (decomp clears 0xAC first)
    ++m_HurtCount;
    if (m_HurtCount > (kHurtCountToRestoreArmor - 1)) { // "2 < hurt_count"
        m_HurtCount = 0;                 // reset the cycle
        return kArmorRestoredPerCycle;   // RestoreArmor(1)
    }
    return 0;
}

float CharSkillC13::CooldownRemaining() const {
    const float remaining = m_SkillCd - m_ThisSkillTime;
    return remaining > 0.0F ? remaining : 0.0F;
}

} // namespace Game
