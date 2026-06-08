#include "combat/CharSkillC10.hpp"

#include <algorithm>

namespace Game {

namespace {
constexpr float kMsPerSecond = 1000.0F;
} // namespace

CharSkillC10::CharSkillC10(float skillCd, float inSkillTime)
    : m_SkillCd(skillCd < 0.0F ? 0.0F : skillCd),
      m_InSkillTime(inSkillTime < 0.0F ? 0.0F : inSkillTime),
      // Start ready: a freshly SetUpChar hero has its first ultimate available
      // (skill_ready == skill_cd <= this_skill_time). Matches PlayerDash/C01.
      // [CharSkillC10-CD]
      m_ThisSkillTime(skillCd < 0.0F ? 0.0F : skillCd) {}

// FAITHFUL: C10Controller__RoleSkill @ game_full.c:157874 gate --
// awake && role_attribute.skill_ready && !in_skill && !changing. (The awake check
// is the caller's responsibility -- [CharSkillC10-Awake]; the gate this unit owns
// is skill_ready && !in_skill && !changing, exactly the recovered nested test at
// 157890/157894/157898: skill_ready==1 -> (in_skill==0) && (changing==0).)
bool CharSkillC10::TryActivateSkill() {
    if (m_InSkill || m_Changing || !SkillReady()) {
        // already in skill, mid-transform, or still on cooldown -> no-op.
        // The !changing gate (this+0xA4) is C10-specific; C01/base lack it.
        return false;
    }
    // Enter the skill state (in_skill = true). The hero-specific transform effect
    // is the get_transform tail-call at 157910 (owner concern); the cooldown spend
    // is deferred to EndSkill (RoleSkillEnd -> ReSetSkillReload), not done here --
    // [CharSkillC10-End]. Arm the in_skill_time countdown (param_1[0x26]) that
    // C10Controller__Update decrements each frame.
    m_InSkill = true;
    m_InSkillTimeLeft = m_InSkillTime;
    return true;
}

// Models RGController.RoleSkillEnd -> RoleAttributePlayer.ReSetSkillReload:
// leave the skill state and restart the cooldown (this_skill_time = 0).
// [CharSkillC10-End]
void CharSkillC10::EndSkill() {
    if (!m_InSkill) {
        return;
    }
    m_InSkill = false;
    m_InSkillTimeLeft = 0.0F; // active window consumed
    m_ThisSkillTime = 0.0F;   // ReSetSkillReload(): restart the cooldown
}

// FAITHFUL: C10Controller__RoleAtk @ game_full.c:158107.
// Branch order preserved: awake gate -> (in_skill -> anim.SetBool FIRST, NO
// early-return) -> (in_item && press -> TriggerItem; return) -> (press &&
// has_target head -> non-melee aim nudge, truncated, not modeled) ->
// hand.SetAttack(value). NO RGRandom draw on this path.
CharSkillC10::AtkDecision CharSkillC10::RoleAtk(bool pressDown,
                                               bool standingOnItem) const {
    AtkDecision d;
    // The decomp's outer awake gate ((char)param_1[3], this+0xC) is the caller's
    // responsibility (this brain has no awake field -- [CharSkillC10-Awake]); a
    // non-awake hero produces no actions, which an all-quiet decision represents.

    // 158117-158124: in_skill FIRST -> anim.SetBool(StringLiteral_6685, value).
    // C10-specific and BEFORE the item branch; unlike C01 it does NOT early-return.
    if (m_InSkill) {
        d.setSkillAnim = true;
        d.skillAnimValue = pressDown; // bool mirrors the press
    }

    // 158126-158131: in_item && press -> TriggerItem(); return (skips hand path).
    if (standingOnItem && pressDown) {
        d.triggerItem = true;
        return d; // RGController.TriggerItem(); return;
    }

    // 158132-158155: (press && (*slot 0xe4)()==1) -> if !hand.IsMelee() a non-melee
    // aim nudge via get_position is truncated at the tail-call; it carries no
    // recoverable pure logic / RNG, so it is intentionally not modeled.

    // 158160-158169: hand.SetAttack(value) on the primary hand (fires/stops the gun).
    d.setHandAttack = true;
    d.handAttackValue = pressDown;
    return d;
}

// FAITHFUL: C10Controller__HurtSomeOne @ game_full.c:158177 (fully recovered, pure).
//   if (in_skill && skill_atk) { skill_atk = false;
//     if (++hurt_count > 2) { role_attribute.RestoreArmor(1); hurt_count = 0; } }
//   RGController.HurtSomeOne();   // base reaction = owner concern
// NO RGRandom draw on this path.
bool CharSkillC10::HurtSomeOne() {
    // 158185-158188: in_skill (this+0x55) && skill_atk (this+0xAC). The decomp
    // short-circuits: skill_atk is only read when in_skill is set.
    if (!m_InSkill || !m_SkillAtk) {
        return false; // non-qualifying hit -> counter untouched, base reaction only
    }
    // 158197: consume the skill_atk flag for this hit.
    m_SkillAtk = false;
    // 158198-158203: ++hurt_count; every 3rd qualifying hit (2 < hurt_count)
    // restores 1 armor and resets the counter.
    ++m_HurtCount;
    if (m_HurtCount > kArmorRestoreEveryNthHit - 1) {
        // RoleAttributePlayer.RestoreArmor(1) is the owner side (this+0x44); we
        // report the trigger and reset the counter.
        m_HurtCount = 0;
        return true;
    }
    return false;
}

// FAITHFUL: C10Controller__Update @ game_full.c:157842 (fully recovered, pure).
// Order preserved: AttributeUpdate -> SkillReload (cooldown count-up, run EVERY
// awake frame, NOT frozen while in_skill) THEN the in_skill_time countdown +
// auto-end (slot 0x17c == RoleSkillEnd) when it reaches <= 0. NO RGRandom draw.
void CharSkillC10::Tick(float dtMs) {
    if (dtMs <= 0.0F) {
        return;
    }
    const float dt = dtMs / kMsPerSecond;

    // 157850: AttributeUpdate() -> SkillReload(dt). this_skill_time counts up,
    // clamped to skill_cd. Unconditional: it advances even while in_skill (the
    // in_skill check below sits AFTER this in the decomp). [CharSkillC10-CD]
    if (m_ThisSkillTime < m_SkillCd) {
        m_ThisSkillTime = std::min(m_ThisSkillTime + dt, m_SkillCd);
    }

    // 157854-157870: while in_skill, in_skill_time (param_1[0x26]) counts DOWN by
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

float CharSkillC10::CooldownRemaining() const {
    const float remaining = m_SkillCd - m_ThisSkillTime;
    return remaining > 0.0F ? remaining : 0.0F;
}

} // namespace Game
