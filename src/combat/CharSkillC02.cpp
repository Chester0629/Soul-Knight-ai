#include "combat/CharSkillC02.hpp"

#include <algorithm>

namespace Game {

namespace {
constexpr float kMsPerSecond = 1000.0F;
} // namespace

CharSkillC02::CharSkillC02(float skillCd)
    : m_SkillCd(skillCd < 0.0F ? 0.0F : skillCd),
      // Start ready: a freshly SetUpChar hero has its first ultimate available
      // (skill_ready == skill_cd <= this_skill_time). Matches PlayerDash / C01.
      m_ThisSkillTime(skillCd < 0.0F ? 0.0F : skillCd) {}

// FAITHFUL: C02Controller__RoleSkill @ game_full.c:156104 gate --
// awake && role_attribute.skill_ready && !in_skill. (The awake check (this+0xc)
// is the caller's responsibility; the gate this unit owns is skill_ready &&
// !in_skill, exactly the recovered nested-if at 156119/156122/156124.)
// NO RGRandom draw on this path.
bool CharSkillC02::TryActivateSkill() {
    if (m_InSkill || !SkillReady()) {
        return false; // already in skill, or still on cooldown -> no-op
    }
    // Enter the skill state (in_skill = true). The hero-specific dash effect is
    // the get_transform tail-call at 156130 (owner concern); the cooldown spend is
    // deferred to EndSkill (RoleSkillEnd -> ReSetSkillReload), not done here -- the
    // C02 body itself does not spend it on activation.
    m_InSkill = true;
    return true;
}

// FAITHFUL: C02Controller__RoleSkillEnd @ game_full.c:156134.
// Pure portion modeled: friction (this+0x24) -= 0.15; in_skill (this+0x55) = 0;
// ReSetSkillReload() (this_skill_time = 0). The forward GetForce dash impulse
// (get_move_dir + GetForce) and UpdateShadowLock tint are owner concerns, surfaced
// via RoleSkillEndDecision. NO RGRandom draw.
CharSkillC02::RoleSkillEndDecision CharSkillC02::EndSkill() {
    RoleSkillEndDecision d;
    if (!m_InSkill) {
        return d; // not in skill -> no-op (all-false)
    }
    // 156140-156141: get_move_dir(&dir); GetForce(dir, mag) -> forward dash impulse.
    // Owner physics; the brain just reports it must be applied.
    d.applyDashImpulse = true;
    // 156142: friction(this+0x24) -= 0.15 -> lower inertia decay so the dash carries.
    // friction is RGBaseController physics state, NOT a brain field, so it is not
    // carried here; the decrement is owner-applied physics. The magnitude is exposed
    // via kSkillEndFrictionDrop (== -0.15f operand) and asserted in tests. The dash
    // itself is signaled by d.applyDashImpulse above (see fabrication_flags).
    // 156143: in_skill(this+0x55) = 0 -> leave the skill state.
    m_InSkill = false;
    // 156148: ReSetSkillReload() -> restart the cooldown (this_skill_time = 0).
    m_ThisSkillTime = 0.0F;
    // 156149: UpdateShadowLock() -> refresh the targeting marker tint (owner).
    d.updateShadowLock = true;
    d.ended = true;
    return d;
}

// FAITHFUL: C02Controller__EndSkillShoot @ game_full.c:156155.
// skill_shoot(this+0x90) gate -> if strengthened pay a fixed 50-unit resource cost
// out of role_attribute[+0x24] -> clear skill_shoot. NO RGRandom draw.
CharSkillC02::EndShootDecision CharSkillC02::EndSkillShoot(bool skillStrengthened) {
    EndShootDecision d;
    // 156161: if (!skill_shoot) return; -> one-shot gate.
    if (!m_SkillShoot) {
        return d; // skill_shoot not set -> no-op (all-false)
    }
    d.fired = true;
    // 156168-156175: if (role_attribute.skill_strengthen) role_attribute[+0x24] -= 50.
    // The 50-unit cost is paid by the owner against the RoleAttribute resource
    // field; the brain reports how much. NO cost when not strengthened.
    if (skillStrengthened) {
        d.resourceCost = kStrengthenedShootCost;
    }
    // 156177: skill_shoot(this+0x90) = 0 -> consume the flag.
    m_SkillShoot = false;
    return d;
}

// FAITHFUL: C02Controller__Update @ game_full.c:156091 (fully recovered, pure).
// if (awake) { AttributeUpdate() -> SkillReload (cooldown count-up, run EVERY awake
// frame); SeachUpdate() (owner aim re-acquire). } There is NO in_skill_time
// countdown and NO auto-end in C02's Update (unlike C01) -- so Tick advances the
// cooldown and NEVER ends the skill. NO RGRandom draw.
void CharSkillC02::Tick(float dtMs) {
    if (dtMs <= 0.0F) {
        return;
    }
    const float dt = dtMs / kMsPerSecond;

    // 156097: AttributeUpdate() -> SkillReload(dt). this_skill_time counts up,
    // clamped to skill_cd. Unconditional: the cooldown advances even while in_skill
    // (C02's Update has no in_skill branch at all). 156098: SeachUpdate() is owner.
    if (m_ThisSkillTime < m_SkillCd) {
        m_ThisSkillTime = std::min(m_ThisSkillTime + dt, m_SkillCd);
    }
}

float CharSkillC02::CooldownRemaining() const {
    const float remaining = m_SkillCd - m_ThisSkillTime;
    return remaining > 0.0F ? remaining : 0.0F;
}

} // namespace Game
