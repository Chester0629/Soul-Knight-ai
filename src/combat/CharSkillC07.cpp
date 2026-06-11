#include "combat/CharSkillC07.hpp"

#include <algorithm>

namespace Game {

namespace {
constexpr float kMsPerSecond = 1000.0F;
} // namespace

CharSkillC07::CharSkillC07(float skillCd)
    : m_SkillCd(skillCd < 0.0F ? 0.0F : skillCd),
      // Start ready: a freshly SetUpChar hero has its first ultimate available
      // (skill_ready == skill_cd <= this_skill_time). Matches PlayerDash / C01.
      m_ThisSkillTime(skillCd < 0.0F ? 0.0F : skillCd) {}

// FAITHFUL: C07Controller__RoleSkill @ game_full.c:156759 gate --
// awake && role_attribute.skill_ready && !in_skill. (The awake check, this+0xC,
// is the caller's responsibility; the gate this brain owns is
// skill_ready && !in_skill, exactly the recovered nested-if at 156774/156780/
// 156784.) On success the spawn tail (FUN_00266a0c) sets in_skill = 1 (this+0x55);
// the ParticleSystem.Play / RGWeapon(BulletBat) Instantiate are OWNER concerns.
// NO RGRandom draw on this path.
bool CharSkillC07::TryActivateSkill() {
    if (m_InSkill || !SkillReady()) {
        return false; // already in skill, or still on cooldown -> no-op
    }
    // Enter the skill state (in_skill = true). The cooldown spend is deferred to
    // EndSkill (RoleSkillEnd -> ReSetSkillReload), not done here -- the recovered
    // enter path only sets in_skill = 1, it does not touch this_skill_time(0x5c).
    m_InSkill = true;
    return true;
}

// FAITHFUL: C07Controller__RoleSkillEnd @ game_full.c:156834.
// Recovered body, order preserved:
//   this+0x55 = 0;                                   // leave skill state
//   role_attribute.ReSetSkillReload();               // this_skill_time(0x5c) = 0
//   role_attribute.SkillReload(this+0xa0);           // bank credit toward cd
//   this+0xa0 = 0;                                   // consume the banked credit
//   UpdateShadowLock();                              // OWNER concern (not modeled)
// this+0xa0 is an inherited accumulator never written by C07 (see hpp /
// fabrication_flags); default 0 makes this identical to C01's ReSetSkillReload.
void CharSkillC07::EndSkill() {
    if (!m_InSkill) {
        return;
    }
    m_InSkill = false;
    // ReSetSkillReload(): restart the cooldown (this_skill_time = 0).
    m_ThisSkillTime = 0.0F;
    // SkillReload(this+0xa0): add the banked credit toward the cooldown, clamped
    // to skill_cd (matching RoleAttributePlayer__SkillReload @ 432455, which caps
    // this_skill_time at skill_cd). Then consume the credit (this+0xa0 = 0).
    if (m_SkillEndCredit > 0.0F) {
        m_ThisSkillTime = std::min(m_ThisSkillTime + m_SkillEndCredit, m_SkillCd);
    }
    m_SkillEndCredit = 0.0F;
}

// FAITHFUL: C07Controller__Update @ game_full.c:156746 (fully recovered, pure):
//   if (awake) { AttributeUpdate() -> SkillReload(dt); SeachUpdate(); }
// this_skill_time counts up, clamped to skill_cd, every awake frame. C07's Update
// has NO in_skill_time countdown and NO auto-end (that is C01's Update shape, not
// C07's); in_skill is not even read here. SeachUpdate (aim) is an OWNER concern.
// NO RGRandom draw.
void CharSkillC07::Tick(float dtMs) {
    if (dtMs <= 0.0F) {
        return;
    }
    const float dt = dtMs / kMsPerSecond;

    // AttributeUpdate() -> RoleAttributePlayer__SkillReload(dt) @ 432455:
    // this_skill_time(+0x5c) += dt, capped at skill_cd(+0x44).
    if (m_ThisSkillTime < m_SkillCd) {
        m_ThisSkillTime = std::min(m_ThisSkillTime + dt, m_SkillCd);
    }
}

float CharSkillC07::CooldownRemaining() const {
    const float remaining = m_SkillCd - m_ThisSkillTime;
    return remaining > 0.0F ? remaining : 0.0F;
}

} // namespace Game
