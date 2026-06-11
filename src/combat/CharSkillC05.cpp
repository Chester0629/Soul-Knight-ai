#include "combat/CharSkillC05.hpp"

#include <algorithm>

namespace Game {

namespace {
constexpr float kMsPerSecond = 1000.0F;
} // namespace

CharSkillC05::CharSkillC05(float skillCd)
    : m_SkillCd(skillCd < 0.0F ? 0.0F : skillCd),
      // Start ready: a freshly SetUpChar hero has its first ultimate available
      // (skill_ready == skill_cd <= this_skill_time). Matches PlayerDash/C01.
      m_ThisSkillTime(skillCd < 0.0F ? 0.0F : skillCd) {}

// FAITHFUL: C05Controller__RoleSkill @ game_full.c:156562 gate --
// awake && role_attribute.skill_ready && !in_skill. (The awake check at this+0xC
// is the caller's responsibility; the gate this unit owns is skill_ready &&
// !in_skill, exactly the recovered nested-if at 156571/156577/156582.)
// NO RGRandom draw on this path.
bool CharSkillC05::TryActivateSkill() {
    if (m_InSkill || !SkillReady()) {
        return false; // already in skill, or still on cooldown -> no-op
    }
    // Enter the skill state (in_skill = true). The hero-specific effect is the
    // get_transform tail-call at 156588 (the bottle_objs[] spawn -- owner
    // concern); the cooldown spend is deferred to EndSkill (base RoleSkillEnd ->
    // ReSetSkillReload), not done here -- the C05 body itself does not spend it.
    //
    // NOTE (vs C01): C05's Update has NO in_skill_time countdown, so there is
    // nothing to arm here. The active window is not a recovered C05 timer.
    m_InSkill = true;
    return true;
}

// Models RGController.RoleSkillEnd -> RoleAttributePlayer.ReSetSkillReload @
// 432381 (this_skill_time = 0). This chain is NOT in C05's own recovered body
// (C05's RoleSkillEnd override is empty in the IL2CPP skeleton and was not
// decompiled); it is the base-class behavior, exposed here and flagged as an
// assumption (see fabrication_flags) so TryActivateSkill/EndSkill are testable.
void CharSkillC05::EndSkill() {
    if (!m_InSkill) {
        return;
    }
    m_InSkill = false;
    m_ThisSkillTime = 0.0F; // ReSetSkillReload(): restart the cooldown
}

// FAITHFUL: C05Controller__Update @ game_full.c:156549 (fully recovered, pure).
// The entire recovered body is:
//   if (awake) { AttributeUpdate(); SeachUpdate(); }
// AttributeUpdate -> RoleAttributePlayer.SkillReload(dt) @ 432455: this_skill_time
// (+0x5c) counts UP by dt, clamped to skill_cd (+0x44). SeachUpdate() is owner
// aim re-acquire (no brain logic).
//
// CRITICAL (vs C01): C05's Update has NO in_skill block -- NO param_1[0x25]
// countdown and NO RoleSkillEnd auto-end. So Tick advances ONLY the cooldown,
// even while in_skill. Adding an active-window countdown / auto-end would be
// fabrication. NO RGRandom draw.
void CharSkillC05::Tick(float dtMs) {
    if (dtMs <= 0.0F) {
        return;
    }
    const float dt = dtMs / kMsPerSecond;

    // 156555: AttributeUpdate() -> SkillReload(dt). this_skill_time counts up,
    // clamped to skill_cd. Runs every awake frame (the only timed work in C05's
    // Update). It is NOT gated by in_skill (there is no in_skill branch here).
    if (m_ThisSkillTime < m_SkillCd) {
        m_ThisSkillTime = std::min(m_ThisSkillTime + dt, m_SkillCd);
    }

    // 156556: SeachUpdate() -- owner-side aim re-acquire, no brain state.
    // (No in_skill block exists in C05's Update; nothing else to do here.)
}

float CharSkillC05::CooldownRemaining() const {
    const float remaining = m_SkillCd - m_ThisSkillTime;
    return remaining > 0.0F ? remaining : 0.0F;
}

} // namespace Game
