#include "combat/CharSkillC12.hpp"

#include <algorithm>

namespace Game {

namespace {
constexpr float kMsPerSecond = 1000.0F;
} // namespace

CharSkillC12::CharSkillC12(float skillCd)
    : m_SkillCd(skillCd < 0.0F ? 0.0F : skillCd),
      m_ThisSkillTime(skillCd < 0.0F ? 0.0F : skillCd) {}

// RoleSkill: gate TYPED 3706-3729 (skill_ready only); phases DISASM 0x26b26c-0x26b7d4.
CharSkillC12::RoleSkillResult CharSkillC12::RoleSkill(int atk, int critical,
                                                      bool skillStrengthen) {
    RoleSkillResult r;
    if (!SkillReady()) {
        return r;
    }
    if (m_InSkill) {
        // PHASE CUT: RGHand.AtkCut(hand, facing, atk, critical) then RoleSkillEnd.
        r.phase = Phase::Cut;
        r.cutDamage = atk;
        r.cutCritical = critical;
    } else {
        // PHASE SUMMON: in_skill = 1, two wolf pets, then RoleSkillEnd.
        m_InSkill = true;
        r.phase = Phase::Summon;
        r.wolves = kWolfCount;
        r.strengthened = skillStrengthen;
    }
    EndSkill();
    return r;
}

// RoleSkillEnd: TYPED 3736-3749 -- ReSetSkillReload + UpdateShadowLock; in_skill is NOT
// written here (T-49).
void CharSkillC12::EndSkill() { m_ThisSkillTime = 0.0F; }

// Update: TYPED 3674-3683 -> SkillReload(dt), unconditional.
void CharSkillC12::Tick(float dtMs) {
    if (dtMs <= 0.0F) {
        return;
    }
    if (m_ThisSkillTime < m_SkillCd) {
        m_ThisSkillTime = std::min(m_ThisSkillTime + dtMs / kMsPerSecond, m_SkillCd);
    }
}

float CharSkillC12::CooldownRemaining() const {
    const float remaining = m_SkillCd - m_ThisSkillTime;
    return remaining > 0.0F ? remaining : 0.0F;
}

} // namespace Game
