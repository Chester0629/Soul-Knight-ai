#include "combat/CharSkillC05.hpp"

#include <algorithm>

namespace Game {

namespace {
constexpr float kMsPerSecond = 1000.0F;
} // namespace

CharSkillC05::CharSkillC05(float skillCd)
    : m_SkillCd(skillCd < 0.0F ? 0.0F : skillCd),
      m_ThisSkillTime(skillCd < 0.0F ? 0.0F : skillCd) {}

// RoleSkill: gate TYPED 1549-1577; the body past it is DISASM 0x265ccc-0x2661ec (T-39).
CharSkillC05::RoleSkillResult CharSkillC05::RoleSkill(int skinIndex, int variantCount,
                                                      bool skillStrengthen) {
    RoleSkillResult r;
    if (m_InSkill || !SkillReady()) {
        return r; // gate: skill_ready && !in_skill (the awake check is the caller's).
    }
    m_InSkill = true; // strb [this,#0x55] = 1 @0x265dc8.
    r.activated = true;
    r.bottleCount = skillStrengthen ? kBottlesStrengthened : kBottlesNormal;
    r.variantCount = variantCount;
    // bottle_objs[GetCurrentSkin(4)] when the array is long enough, else element 0
    // (`cmp length, skin; ble keep-element-0` @0x265efc).
    r.variantIndex = (variantCount > skinIndex && skinIndex >= 0) ? skinIndex : 0;
    float offset = skillStrengthen ? -kFanStepDeg : 0.0F;
    for (int i = 0; i < r.bottleCount; ++i) {
        r.fanOffsetsDeg.push_back(offset);
        offset += kFanStepDeg;
    }
    EndSkill(); // vcall slot 25 (RoleSkillEnd) @0x2661e8, inside the same RoleSkill call.
    return r;
}

bool CharSkillC05::TryActivateSkill() { return RoleSkill(0, 4, false).activated; }

// RoleSkillEnd: TYPED 1584-1638 (in_skill = false @1600; ReSetSkillReload @1606;
// get_skill_ready + shadow_lock SetColor tint @1612-1636 -> T-51, presentation).
void CharSkillC05::EndSkill() {
    m_InSkill = false;
    m_ThisSkillTime = 0.0F; // RoleAttributePlayer.ReSetSkillReload()
}

// Update: TYPED 1517-1526 -> AttributeUpdate -> SkillReload(dt): count up, clamp.
void CharSkillC05::Tick(float dtMs) {
    if (dtMs <= 0.0F) {
        return;
    }
    if (m_ThisSkillTime < m_SkillCd) {
        m_ThisSkillTime = std::min(m_ThisSkillTime + dtMs / kMsPerSecond, m_SkillCd);
    }
}

float CharSkillC05::CooldownRemaining() const {
    const float remaining = m_SkillCd - m_ThisSkillTime;
    return remaining > 0.0F ? remaining : 0.0F;
}

} // namespace Game
