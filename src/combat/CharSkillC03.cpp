#include "combat/CharSkillC03.hpp"

#include <algorithm>

namespace Game {

namespace {
constexpr float kMsPerSecond = 1000.0F;
} // namespace

CharSkillC03::CharSkillC03(float skillCd)
    : m_SkillCd(skillCd < 0.0F ? 0.0F : skillCd),
      // FLAGGED (StartsReady): start ready -- a freshly SetUpChar hero has its
      // first ultimate available (skill_ready == skill_cd <= this_skill_time).
      // Matches PlayerDash / C01 / C02; not a value read from C03's bodies.
      m_ThisSkillTime(skillCd < 0.0F ? 0.0F : skillCd) {}

// FAITHFUL: C03Controller__RoleSkill @ game_full.c:156214 gate --
// awake && role_attribute.skill_ready && !in_skill. (The awake check is the
// caller's responsibility; the gate this unit owns is skill_ready && !in_skill,
// exactly the recovered nested-if at 156229/156235/156239.) NO RGRandom draw.
bool CharSkillC03::TryActivateSkill() {
    if (m_InSkill || !SkillReady()) {
        return false; // already in skill, or still on cooldown -> no-op
    }
    // Enter the skill state (in_skill = true). The hero-specific effect is the
    // get_transform tail-call at 156241 (the CreateTunder thunder spawn -- owner
    // concern); the cooldown spend is deferred to EndSkill, not done here. C03's
    // RoleSkill body shows only the get_skill_ready GATE (FLAGGED: CooldownModel).
    m_InSkill = true;
    return true;
}

// Models the base RGController.RoleSkillEnd chain: leave the skill state and
// restart the cooldown (this_skill_time = 0).
// FLAGGED ASSUMPTION (EndSkillSpend): C03Controller__RoleSkillEnd @ 156258 does
// NOT contain these instructions -- its recovered body is only a null-guard +
// get_transform tail-call on this+0x40 (an owner-side Component). The in_skill
// clear (this+0x55 = 0) and the ReSetSkillReload spend are reconstructed from the
// base chain (explicit in C02Controller__RoleSkillEnd @ 156134), not read here.
void CharSkillC03::EndSkill() {
    if (!m_InSkill) {
        return;
    }
    m_InSkill = false;      // this+0x55 = 0 (base RoleSkillEnd; flagged)
    m_ThisSkillTime = 0.0F; // ReSetSkillReload(): restart the cooldown (flagged)
}

// FAITHFUL: C03Controller__Update @ game_full.c:156201 (fully recovered, pure).
// Order preserved: awake gate -> AttributeUpdate -> SkillReload (cooldown
// count-up) -> SeachUpdate (owner aim re-acquire). C03's Update has NO
// in_skill_time countdown and NO auto-end (matching C02, unlike C01), so this
// advances ONLY the cooldown and never ends the skill. NO RGRandom draw.
void CharSkillC03::Tick(float dtMs) {
    if (dtMs <= 0.0F) {
        return;
    }
    const float dt = dtMs / kMsPerSecond;

    // 156207: AttributeUpdate() -> SkillReload(dt). this_skill_time counts up,
    // clamped to skill_cd. Unconditional within the awake gate: it advances even
    // while in_skill (C03's Update has no in_skill branch at all).
    if (m_ThisSkillTime < m_SkillCd) {
        m_ThisSkillTime = std::min(m_ThisSkillTime + dt, m_SkillCd);
    }

    // 156208: SeachUpdate() -- aim re-acquire timer (owner concern; no brain
    // state here). There is deliberately NO in_skill_time decrement / auto-end:
    // modeling one would be fabrication (C03's Update body has neither).
}

float CharSkillC03::CooldownRemaining() const {
    const float remaining = m_SkillCd - m_ThisSkillTime;
    return remaining > 0.0F ? remaining : 0.0F;
}

} // namespace Game
