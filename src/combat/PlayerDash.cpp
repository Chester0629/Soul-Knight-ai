#include "combat/PlayerDash.hpp"

#include <algorithm>
#include <cmath>

namespace Game {

namespace {
constexpr float kMsPerSecond = 1000.0F;
constexpr float kDirEpsilon = 1e-6F;
} // namespace

PlayerDash::PlayerDash(float skillCd, float inSkillTime)
    : m_SkillCd(skillCd < 0.0F ? 0.0F : skillCd),
      m_InSkillTime(inSkillTime < 0.0F ? 0.0F : inSkillTime),
      // Start ready: SetUpChar leaves a freshly created hero with its first skill
      // available (skill_ready == skill_cd <= this_skill_time).
      m_ThisSkillTime(skillCd < 0.0F ? 0.0F : skillCd) {}

// FAITHFUL: C0xController.RoleSkill gate (skill_ready && !in_skill) +
// RGBaseController.GetForce (force_direction = dir, inertial_vel = min(power, 30)).
bool PlayerDash::TryDash(glm::vec2 dir, float force) {
    if (m_InSkill || !SkillReady()) {
        return false; // already dashing, or still on cooldown -> no-op
    }

    // GetForce: normalize the push direction; a zero move_dir yields a zero
    // impulse (the original passes move_dir straight through, which may be zero).
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    m_ForceDirection =
        (len > kDirEpsilon) ? glm::vec2(dir.x / len, dir.y / len) : glm::vec2(0.0F, 0.0F);

    // inertial_vel = min(power, 30f). Negative power floors at 0 (no reverse cap
    // logic exists in GetForce; only the upper 30f clamp is in the binary).
    float power = force;
    if (power > kForceCap) {
        power = kForceCap;
    }
    if (power < 0.0F) {
        power = 0.0F;
    }
    m_InertialVel = power;

    // Enter the active window (in_skill = true). The cooldown is restarted at the
    // END of the window (RoleSkillEnd -> ReSetSkillReload), not now.
    m_InSkill = true;
    m_ActiveElapsed = 0.0F;
    return true;
}

// FAITHFUL: RoleAttributePlayer.SkillReload(dt) (cooldown count-up, clamped to
// skill_cd) + RoleSkillEnd (active window end -> ReSetSkillReload, this_skill_time = 0).
void PlayerDash::Tick(float dtMs) {
    if (dtMs <= 0.0F) {
        return;
    }
    const float dt = dtMs / kMsPerSecond;

    if (m_InSkill) {
        // Advance the active dash window. The cooldown does NOT recharge while the
        // skill is active; it is reset to 0 only when the window ends (RoleSkillEnd).
        m_ActiveElapsed += dt;
        if (m_ActiveElapsed >= m_InSkillTime) {
            m_InSkill = false;
            m_ActiveElapsed = 0.0F;
            m_ThisSkillTime = 0.0F; // ReSetSkillReload(): restart the cooldown
        }
        return;
    }

    // Not dashing: recharge the cooldown. SkillReload: if (this_skill_time <
    // skill_cd) this_skill_time = min(this_skill_time + dt, skill_cd).
    if (m_ThisSkillTime < m_SkillCd) {
        m_ThisSkillTime = std::min(m_ThisSkillTime + dt, m_SkillCd);
    }
}

float PlayerDash::CooldownRemaining() const {
    const float remaining = m_SkillCd - m_ThisSkillTime;
    return remaining > 0.0F ? remaining : 0.0F;
}

// FAITHFUL: RGController.SetVelocity knockback branch
// (velocity = force_direction * inertial_vel while inertial_vel > 1).
glm::vec2 PlayerDash::CurrentDashVelocity() const {
    if (!m_InSkill || m_InertialVel <= kInertiaActiveThreshold) {
        return glm::vec2(0.0F, 0.0F);
    }
    return m_ForceDirection * m_InertialVel;
}

// FAITHFUL: SetVelocity's inertial_vel *= min(1, friction) per FixedUpdate.
void PlayerDash::DecayImpulse(float frictionFactor) {
    float f = frictionFactor;
    if (f > 1.0F) {
        f = 1.0F; // Mathf.Min(1f, friction)
    }
    if (f < 0.0F) {
        f = 0.0F;
    }
    m_InertialVel *= f;
}

} // namespace Game
