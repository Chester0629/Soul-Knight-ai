#include "combat/CharSkillC11.hpp"

#include <algorithm>
#include <cmath>

namespace Game {

namespace {
constexpr float kMsPerSecond = 1000.0F;
constexpr float kTimerEpsilonSec = 1.0e-4F;
} // namespace

// ---------------------------------------------------------------------------
// HolyLightZone
// ---------------------------------------------------------------------------

// SetSourceObject (DISASM 0xafa2e4-0xafa3a0): TurnCanHit() -> can_hit = true and the
// 0.8 s re-arm; Invoke(GasEnd, 1.8f); Destroy(gameObject, 4.5f).
void HolyLightZone::Start(glm::vec2 centre, bool buffDigestive) {
    m_Alive = true;
    m_CanHit = true;
    m_IsEnd = false;
    m_Digestive = buffDigestive;
    m_TurnTimer = kPulseIntervalSec;
    m_GasTimer = kGasEndSec;
    m_Life = kLifetimeSec;
    m_Pulses = 0;
    m_Centre = centre;
}

int HolyLightZone::Step(float dtSec, glm::vec2 playerPos) {
    if (!m_Alive) {
        return 0;
    }
    // Update-phase Invokes: TurnCanHit re-arms itself every 0.8 s; GasEnd fires once.
    m_TurnTimer -= dtSec;
    if (m_TurnTimer <= kTimerEpsilonSec) {
        m_CanHit = true; // TurnCanHit: can_hit = true; Invoke(TurnCanHit, 0.8f)
        m_TurnTimer = kPulseIntervalSec;
    }
    m_GasTimer -= dtSec;
    if (m_GasTimer <= kTimerEpsilonSec) {
        m_IsEnd = true; // GasEnd: is_end = true (+ the end animation, presentation).
    }
    int healed = 0;
    // FixedUpdate: if (can_hit && !is_end) { can_hit = false; pulse }.
    if (m_CanHit && !m_IsEnd) {
        m_CanHit = false;
        ++m_Pulses;
        const glm::vec2 d = playerPos - m_Centre;
        if (std::sqrt(d.x * d.x + d.y * d.y) <= kRadiusUnits) {
            healed = m_Digestive ? kHealDigestive : kHealNormal;
        }
    }
    m_Life -= dtSec;
    if (m_Life <= kTimerEpsilonSec) {
        m_Alive = false; // Destroy(gameObject, 4.5f)
    }
    return healed;
}

// ---------------------------------------------------------------------------
// CharSkillC11
// ---------------------------------------------------------------------------

CharSkillC11::CharSkillC11(float skillCd)
    : m_SkillCd(skillCd < 0.0F ? 0.0F : skillCd),
      m_ThisSkillTime(skillCd < 0.0F ? 0.0F : skillCd) {}

// RoleSkill: gate TYPED 3552-3580; body DISASM 0x26ac10-0x26af94 (in_skill = 1 @0x26ad10,
// the light spawn, then RoleSkillEnd @0x26af90 in the same call).
bool CharSkillC11::TryActivateSkill() {
    if (m_InSkill || !SkillReady()) {
        return false;
    }
    m_InSkill = true;
    m_SpawnPending = true;
    EndSkill();
    return true;
}

bool CharSkillC11::ConsumeSpawn() {
    const bool s = m_SpawnPending;
    m_SpawnPending = false;
    return s;
}

// RoleSkillEnd: TYPED 3587-3601 (in_skill = false @3592; ReSetSkillReload @3598;
// UpdateShadowLock @3599 -> T-51).
void CharSkillC11::EndSkill() {
    m_InSkill = false;
    m_ThisSkillTime = 0.0F;
}

// Update: TYPED 3521-3530 -> SkillReload(dt), unconditional.
void CharSkillC11::Tick(float dtMs) {
    if (dtMs <= 0.0F) {
        return;
    }
    if (m_ThisSkillTime < m_SkillCd) {
        m_ThisSkillTime = std::min(m_ThisSkillTime + dtMs / kMsPerSecond, m_SkillCd);
    }
}

float CharSkillC11::CooldownRemaining() const {
    const float remaining = m_SkillCd - m_ThisSkillTime;
    return remaining > 0.0F ? remaining : 0.0F;
}

} // namespace Game
