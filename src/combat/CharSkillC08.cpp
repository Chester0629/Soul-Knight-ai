#include "combat/CharSkillC08.hpp"

#include <algorithm>

namespace Game {

namespace {
constexpr float kMsPerSecond = 1000.0F;
} // namespace

// FAITHFUL: C08Controller___ctor @ game_full.c:156911 -- the one pure-logic line
// is *(this+0x90) = 100 (shield_value). The base RGController___ctor(this, 0) call
// is owner boilerplate. Start ready (this_skill_time == skill_cd) per the sibling
// PlayerDash / C01 / C02 convention (see fabrication_flags CharSkillC08-CD03).
CharSkillC08::CharSkillC08(float skillCd)
    : m_SkillCd(skillCd < 0.0F ? 0.0F : skillCd),
      m_ThisSkillTime(skillCd < 0.0F ? 0.0F : skillCd),
      m_ShieldValue(kInitialShieldValue) {}

// FAITHFUL: C08Controller__RoleSkill @ game_full.c:157131 gate --
// awake && role_attribute.skill_ready && !in_skill. (The awake check (this+0xc) is
// the caller's responsibility; the gate this unit owns is skill_ready && !in_skill,
// exactly the recovered nested-if at 157146/157151/157153.) NO RGRandom draw.
bool CharSkillC08::TryActivateSkill() {
    if (m_InSkill || !SkillReady()) {
        return false; // already in skill, or still on cooldown -> no-op
    }
    // Enter the skill state (in_skill = true). The hero-specific shield-raise effect
    // is the get_transform tail-call at 157156 (owner concern); the cooldown spend is
    // deferred to EndSkill (RoleSkillEnd -> ReSetSkillReload), not done here -- the
    // C08 body itself does not spend it on activation.
    m_InSkill = true;
    return true;
}

// Models RGController.RoleSkillEnd -> RoleAttributePlayer.ReSetSkillReload (the C08
// RoleSkillEnd body is an inlined/truncated tail; see fabrication_flags
// CharSkillC08-CD02): leave the skill state and restart the cooldown
// (this_skill_time = 0).
void CharSkillC08::EndSkill() {
    if (!m_InSkill) {
        return;
    }
    m_InSkill = false;
    m_ThisSkillTime = 0.0F; // ReSetSkillReload(): restart the cooldown
}

// FAITHFUL: C08Controller__GetHurt @ game_full.c:156968.
// Branch order preserved:
//   156974: if (in_skill == 0) -> base RGController.GetHurt(damage, source); return.
//   156981: if (awake == 0) return;            // skill-active guard (this+0xc)
//   156984: *(this+0x90) -= damage;            // shield_value ABSORBS the hit
//   156985+: RGGameProcess singleton effect    // owner/process tail-call
// NO RGRandom draw on any path.
CharSkillC08::HurtDecision CharSkillC08::GetHurt(int damage, bool awake) {
    HurtDecision d;

    // 156974: not in skill -> hand off to the base normal-damage pipeline. The
    // brain does NOT touch shield_value here (the shield is down).
    if (!m_InSkill) {
        d.deferToBase = true;
        return d;
    }

    // 156981: in_skill but not awake -> the decomp early-returns with no effect.
    if (!awake) {
        return d; // all-false: no absorption, no base call, no process effect
    }

    // 156984: shield_value(this+0x90) -= damage. The raised shield eats the hit; the
    // normal HP/armor pipeline is bypassed entirely. No clamp-to-zero is modeled
    // because the decomp does a bare subtraction (see fabrication_flags
    // CharSkillC08-CD04).
    m_ShieldValue -= damage;
    d.absorbedByShield = true;

    // 156985+: the RGGameProcess singleton effect (Singleton<RGGameProcess>__get_Inst
    // tail-call) is an owner/process concern; the brain just reports it must fire.
    d.triggerProcessEffect = true;
    return d;
}

// FAITHFUL: C08Controller__Update @ game_full.c:156955 (fully recovered, pure).
// if (awake) { AttributeUpdate() -> SkillReload (cooldown count-up, run EVERY awake
// frame); SeachUpdate() (owner aim re-acquire). } There is NO in_skill_time
// countdown and NO auto-end in C08's Update (unlike C01) -- so Tick advances the
// cooldown and NEVER ends the skill. NO RGRandom draw.
void CharSkillC08::Tick(float dtMs) {
    if (dtMs <= 0.0F) {
        return;
    }
    const float dt = dtMs / kMsPerSecond;

    // 156963: AttributeUpdate() -> SkillReload(dt). this_skill_time counts up,
    // clamped to skill_cd. Unconditional: the cooldown advances even while in_skill
    // (C08's Update has no in_skill branch at all). 156964: SeachUpdate() is owner.
    if (m_ThisSkillTime < m_SkillCd) {
        m_ThisSkillTime = std::min(m_ThisSkillTime + dt, m_SkillCd);
    }
}

float CharSkillC08::CooldownRemaining() const {
    const float remaining = m_SkillCd - m_ThisSkillTime;
    return remaining > 0.0F ? remaining : 0.0F;
}

} // namespace Game
