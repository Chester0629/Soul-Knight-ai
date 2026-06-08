#include "combat/CharSkillC11.hpp"

#include <algorithm>

namespace Game {

namespace {
constexpr float kMsPerSecond = 1000.0F;
} // namespace

CharSkillC11::CharSkillC11(float skillCd)
    : m_SkillCd(skillCd < 0.0F ? 0.0F : skillCd),
      // Start ready: a freshly SetUpChar hero has its first ultimate available
      // (skill_ready == skill_cd <= this_skill_time). Matches PlayerDash / C01.
      m_ThisSkillTime(skillCd < 0.0F ? 0.0F : skillCd) {}

// FAITHFUL: C11Controller__RoleSkill @ game_full.c:158277 gate --
// awake && role_attribute.skill_ready && !in_skill. (The awake check, this+0xC, is
// the caller's responsibility; the gate this unit owns is skill_ready && !in_skill,
// exactly the recovered nested-if at 158291/158294/158296.) NO RGRandom draw.
bool CharSkillC11::TryActivateSkill() {
    if (m_InSkill || !SkillReady()) {
        return false; // already in skill, or still on cooldown -> no-op
    }
    // Enter the skill state (in_skill = true, this+0x55). The hero-specific effect
    // is the skill_obj get_transform tail-call at 158298 (owner concern); the
    // cooldown spend is deferred to EndSkill (RoleSkillEnd -> ReSetSkillReload),
    // exactly as the decomp -- RoleSkill itself does NOT spend the charge.
    m_InSkill = true;
    return true;
}

// FAITHFUL: C11Controller__RoleSkillEnd @ game_full.c:158395 (recovered, pure):
//   in_skill = 0 (this+0x55);
//   role_attribute.ReSetSkillReload() (this_skill_time/+0x5c = 0, restart cooldown);
//   UpdateShadowLock() (owner concern -- shadow sprite, not modeled).
void CharSkillC11::EndSkill() {
    if (!m_InSkill) {
        return; // not active -> the brain state is unchanged (guard for re-tests)
    }
    m_InSkill = false;      // 158397: *(this+0x55) = 0
    m_ThisSkillTime = 0.0F; // 158403: ReSetSkillReload() -> this_skill_time = 0
}

// FAITHFUL: C11Controller__Update @ game_full.c:158264 (fully recovered, pure).
// Order preserved: awake gate -> AttributeUpdate (SkillReload cooldown count-up,
// run EVERY awake frame, NOT frozen while in_skill) -> SeachUpdate (owner concern).
// CRITICAL: unlike C01, there is NO in_skill_time countdown and NO auto-end here,
// so Tick advances the cooldown ONLY and never ends the skill. NO RGRandom draw.
void CharSkillC11::Tick(float dtMs) {
    if (dtMs <= 0.0F) {
        return;
    }
    const float dt = dtMs / kMsPerSecond;

    // 158270: AttributeUpdate() -> SkillReload(dt). this_skill_time (+0x5c) counts
    // up, clamped to skill_cd (+0x44). Unconditional: it advances even while
    // in_skill (C11's Update has no in_skill branch at all).
    if (m_ThisSkillTime < m_SkillCd) {
        m_ThisSkillTime = std::min(m_ThisSkillTime + dt, m_SkillCd);
    }

    // 158271: SeachUpdate() -- the 0.05s aim re-acquire is an owner concern with no
    // recoverable pure brain logic / RNG; intentionally not modeled.
}

float CharSkillC11::CooldownRemaining() const {
    const float remaining = m_SkillCd - m_ThisSkillTime;
    return remaining > 0.0F ? remaining : 0.0F;
}

} // namespace Game
