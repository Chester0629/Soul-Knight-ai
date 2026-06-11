#include "combat/CharSkillC12.hpp"

#include <algorithm>

namespace Game {

namespace {
constexpr float kMsPerSecond = 1000.0F;
} // namespace

CharSkillC12::CharSkillC12(float skillCd)
    : m_SkillCd(skillCd < 0.0F ? 0.0F : skillCd),
      // Start ready: a freshly SetUpChar hero has its first ultimate available
      // (skill_ready == skill_cd (0x44) <= this_skill_time (0x5c)). Matches
      // PlayerDash / CharSkillC01.
      m_ThisSkillTime(skillCd < 0.0F ? 0.0F : skillCd) {}

// FAITHFUL: C12Controller__RoleSkill @ game_full.c:158451 gate --
// awake && role_attribute.skill_ready. (The awake check (this+0xC) is the
// caller's responsibility; the gate this brain owns is skill_ready, the recovered
// get_skill_ready==1 branch at 158507.) NOTE: unlike C01Controller__RoleSkill,
// C12's body has NO "!in_skill" (0x55) guard -- it is intentionally absent here.
bool CharSkillC12::TryActivateSkill() {
    if (!SkillReady()) {
        return false; // still on cooldown -> no-op (get_skill_ready == 0)
    }
    // Enter the skill state. The recovered RoleSkill effect tail (FUN_0026b1dc,
    // reached via the get_transform tail-call at 158510) sets this+0x55 = 1, spawns
    // skill_obj1/2, runs RGHand.AtkCut and tail-calls slot 0x17c -- all owner-side;
    // the only brain-recoverable fact is in_skill = true. The cooldown spend is
    // deferred to EndSkill (RoleSkillEnd -> ReSetSkillReload), which is C12's own
    // recovered body; RoleSkill itself does NOT spend the charge.
    m_InSkill = true;
    return true;
}

// FAITHFUL: C12Controller__RoleSkillEnd @ game_full.c:158514 -- runs
// RoleAttributePlayer.ReSetSkillReload (this_skill_time (0x5c) = 0, restart the
// cooldown) then UpdateShadowLock (owner shadow vfx). This spend IS in C12's own
// body (unlike C01, where it was inherited). The body does not itself clear
// in_skill (0x55) -- that is the owner slot-0x17c finish path -- so we leave the
// skill state here and flag that clear as the owner-path assumption.
void CharSkillC12::EndSkill() {
    if (!m_InSkill) {
        return;
    }
    m_InSkill = false;      // owner slot-0x17c finish path (flagged assumption)
    m_ThisSkillTime = 0.0F; // ReSetSkillReload(): restart the cooldown (0x5c = 0)
}

// FAITHFUL: C12Controller__Update @ game_full.c:158438 (fully recovered, pure):
//   if (awake) { AttributeUpdate(); SeachUpdate(); }
// AttributeUpdate -> SkillReload: this_skill_time (0x5c) counts UP, clamped to
// skill_cd (0x44), EVERY awake frame, UNCONDITIONALLY. C12's Update has NO
// in_skill branch -> NO in_skill_time countdown and NO auto-end (that is the
// C13/C01 feature C12 lacks; do not borrow it). SeachUpdate is the aim re-acquire
// (owner concern), a no-op here. NO RGRandom draw.
void CharSkillC12::Tick(float dtMs) {
    if (dtMs <= 0.0F) {
        return;
    }
    const float dt = dtMs / kMsPerSecond;

    // 158442: AttributeUpdate() -> SkillReload(dt). Counts up, clamped to skill_cd.
    // Unconditional: not frozen while in_skill (C12's Update has no in_skill check).
    if (m_ThisSkillTime < m_SkillCd) {
        m_ThisSkillTime = std::min(m_ThisSkillTime + dt, m_SkillCd);
    }

    // 158443: SeachUpdate() -- aim re-acquire timer (param[0x1b] >= 0.05 -> re-aim),
    // owner-side, no skill brain logic. Intentionally not modeled.
}

float CharSkillC12::CooldownRemaining() const {
    const float remaining = m_SkillCd - m_ThisSkillTime;
    return remaining > 0.0F ? remaining : 0.0F;
}

} // namespace Game
