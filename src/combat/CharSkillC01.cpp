#include "combat/CharSkillC01.hpp"

#include <algorithm>

namespace Game {

namespace {
constexpr float kMsPerSecond = 1000.0F;
} // namespace

CharSkillC01::CharSkillC01(float skillCd, float inSkillTime)
    : m_SkillCd(skillCd < 0.0F ? 0.0F : skillCd),
      m_InSkillTime(inSkillTime < 0.0F ? 0.0F : inSkillTime),
      // Start ready: a freshly SetUpChar hero has its first ultimate available
      // (skill_ready == skill_cd <= this_skill_time). Matches PlayerDash.
      m_ThisSkillTime(skillCd < 0.0F ? 0.0F : skillCd) {}

// FAITHFUL: C01Controller__RoleSkill @ game_full.c:155986 gate --
// awake && role_attribute.skill_ready && !in_skill. (The awake check is the
// caller's responsibility; the gate this unit owns is skill_ready && !in_skill,
// exactly the recovered nested-if at 155996/156002/156006.)
bool CharSkillC01::TryActivateSkill() {
    if (m_InSkill || !SkillReady()) {
        return false; // already in skill, or still on cooldown -> no-op
    }
    // Enter the skill state (in_skill = true). The hero-specific effect is the
    // get_transform tail-call at 156008 (owner concern); the cooldown spend is
    // deferred to EndSkill (RoleSkillEnd -> ReSetSkillReload), not done here --
    // the C01 body itself does not spend it. Arm the in_skill_time countdown
    // (param_1[0x25]) that C01Controller__Update decrements each frame.
    m_InSkill = true;
    m_InSkillTimeLeft = m_InSkillTime;
    return true;
}

// Models RGController.RoleSkillEnd -> RoleAttributePlayer.ReSetSkillReload:
// leave the skill state and restart the cooldown (this_skill_time = 0).
void CharSkillC01::EndSkill() {
    if (!m_InSkill) {
        return;
    }
    m_InSkill = false;
    m_InSkillTimeLeft = 0.0F; // active window consumed
    m_ThisSkillTime = 0.0F;   // ReSetSkillReload(): restart the cooldown
}

// FAITHFUL: C01Controller__RoleAtk @ game_full.c:156016.
// Branch order preserved: awake gate -> in_item&&press item-trigger early-out
// (stops the 2nd hand first if in_skill) -> hand.SetAttack(value) -> in_skill
// dual-hand mirror Invoke. NO RGRandom draw on this path.
CharSkillC01::AtkDecision CharSkillC01::RoleAtk(bool pressDown,
                                               bool standingOnItem) const {
    AtkDecision d;
    // The decomp's outer awake gate (param_1[3], this+0xC) is the caller's
    // responsibility (this brain has no awake field); a non-awake hero produces
    // no actions, which an all-false decision represents.

    // 156027-156035: in_item && press -> trigger the pickup and return. While in
    // the skill, the second hand is stopped first (virtual *(this+0x17c)).
    if (standingOnItem && pressDown) {
        d.triggerItem = true;
        if (m_InSkill) {
            d.mirrorSecondHand = true;
            d.secondHandAttackValue = false; // stop the 2nd hand (Hand2AtkStop)
        }
        return d; // RGController.TriggerItem(); return;
    }

    // 156036-156051: a press-only, non-melee aim nudge (get_position) is
    // truncated at the tail-call and carries no recoverable pure logic / RNG, so
    // it is intentionally not modeled (see fabrication_flags).

    // 156057: hand.SetAttack(value) on the primary hand (fires/stops the gun).
    d.setHandAttack = true;
    d.handAttackValue = pressDown;

    // 156058-156066: while in_skill, mirror the attack on the second hand 0.1s
    // later via Invoke("Hand2Atk"/"Hand2AtkStop", 0.1f).
    if (m_InSkill) {
        d.mirrorSecondHand = true;
        d.secondHandAttackValue = pressDown;
    }
    return d;
}

// FAITHFUL: C01Controller__Update @ game_full.c:155928 (fully recovered, pure).
// Order preserved: AttributeUpdate -> SkillReload (cooldown count-up, run EVERY
// awake frame, NOT frozen while in_skill) THEN the in_skill_time countdown +
// auto-end (slot 0x17c == RoleSkillEnd) when it reaches <= 0. NO RGRandom draw.
void CharSkillC01::Tick(float dtMs) {
    if (dtMs <= 0.0F) {
        return;
    }
    const float dt = dtMs / kMsPerSecond;

    // 155936: AttributeUpdate() -> SkillReload(dt). this_skill_time counts up,
    // clamped to skill_cd. Unconditional: it advances even while in_skill (the
    // in_skill check below sits AFTER this in the decomp).
    if (m_ThisSkillTime < m_SkillCd) {
        m_ThisSkillTime = std::min(m_ThisSkillTime + dt, m_SkillCd);
    }

    // 155938-155952: while in_skill, in_skill_time (param_1[0x25]) counts DOWN by
    // dt; when it reaches <= 0 the skill auto-ends through RoleSkillEnd (slot
    // 0x17c) and the body returns immediately. EndSkill() models that path
    // (in_skill = false; ReSetSkillReload(): this_skill_time = 0), so the reset is
    // this frame's final cooldown state -- matching the decomp's post-RoleSkillEnd
    // return.
    if (m_InSkill) {
        m_InSkillTimeLeft -= dt;
        if (m_InSkillTimeLeft <= 0.0F) {
            EndSkill();
        }
    }
}

float CharSkillC01::CooldownRemaining() const {
    const float remaining = m_SkillCd - m_ThisSkillTime;
    return remaining > 0.0F ? remaining : 0.0F;
}

} // namespace Game
