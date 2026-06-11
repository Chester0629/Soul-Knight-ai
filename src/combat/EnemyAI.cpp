#include "combat/EnemyAI.hpp"

#include <cmath>

namespace Game {

namespace {

/// Unity Vector2.normalized semantics: zero vector stays zero (no NaN), and a
/// near-zero vector below Unity's epsilon also returns zero.
glm::vec2 SafeNormalize(glm::vec2 v) {
    const float mag = std::sqrt(v.x * v.x + v.y * v.y);
    if (mag < 1e-5F) {
        return glm::vec2(0.0F, 0.0F);
    }
    return v / mag;
}

/// Unity Vector2.Reflect(inDirection, inNormal) =
///   inDirection - 2 * dot(inNormal, inDirection) * inNormal.
glm::vec2 Reflect(glm::vec2 inDirection, glm::vec2 inNormal) {
    const float dot = inNormal.x * inDirection.x + inNormal.y * inDirection.y;
    return inDirection - 2.0F * dot * inNormal;
}

} // namespace

EnemyAI::EnemyAI(const EnemyDef &def, float detectRange, float attackRange)
    : m_Def(&def),
      m_DetectRange(detectRange),
      m_AttackRange(attackRange) {}

void EnemyAI::SetSeed(int seed) {
    // FAITHFUL: RGEController__SetRGRandomSeed - seed the per-instance stream.
    m_Rng.SetRandomSeed(seed);
}

EnemyAI::Decision EnemyAI::Update(float dtMs, glm::vec2 selfPos,
                                  glm::vec2 playerPos) {
    // Dead / dizzy gate: no decision, no shooting, no movement.
    UpdateDizzy(dtMs);
    if (m_Dead || m_Dizzy) {
        Decision gated;
        gated.state = m_State;
        return gated;
    }

    if (m_ShootCooldownMs > 0.0F) {
        m_ShootCooldownMs -= dtMs;
        if (m_ShootCooldownMs < 0.0F) {
            m_ShootCooldownMs = 0.0F;
        }
    }

    const glm::vec2 toPlayer = playerPos - selfPos;
    const float dist =
        std::sqrt(toPlayer.x * toPlayer.x + toPlayer.y * toPlayer.y);

    Decision d;

    if (dist > m_DetectRange) {
        m_State = AIState::Idle;
        d.state = m_State;
        m_MoveDirection = glm::vec2(0.0F, 0.0F);
        return d;
    }

    const glm::vec2 dir = dist > 0.0F ? toPlayer / dist : glm::vec2(0.0F, 0.0F);

    if (dist > m_AttackRange) {
        m_State = AIState::Chase;
        d.state = m_State;
        d.moveDir = dir;
        m_MoveDirection = dir;
        return d;
    }

    // In attack range: hold position and shoot when the cooldown is ready.
    m_State = AIState::Attack;
    d.state = m_State;
    m_MoveDirection = glm::vec2(0.0F, 0.0F);
    // Shoot gate mirrors the original: gated by can_shoot && !dead && !dizzy,
    // with the cadence period == shoot_cd re-armed via TurnCanShoot.
    if (m_CanShoot && m_ShootCooldownMs <= 0.0F) {
        d.shouldShoot = true;
        m_CanShoot = false;
        m_ShootCooldownMs = m_Def->shootCd * 1000.0F; // seconds -> ms
        TurnCanShoot();                               // re-arm for next period
    }
    return d;
}

// FAITHFUL: EnemyAI01__Scout @ game_full.c:676136
glm::vec2 EnemyAI::Scout(glm::vec2 selfPos, glm::vec2 playerPos) {
    // Gated: do nothing meaningful while dead or dizzy. The original returns
    // early BEFORE advancing the RNG, so a gated tick draws nothing.
    if (m_Dead || m_Dizzy) {
        return glm::vec2(0.0F, 0.0F);
    }

    // Acquire nearest player (single-target here) and steer toward it.
    const glm::vec2 toTarget = playerPos - selfPos;
    m_MoveDirection = SafeNormalize(toTarget);

    // Advance the deterministic stream (wander seed); matches EnemyAI01's
    // rg_random.Range(0,10) draw made on every scout tick.
    (void)m_Rng.Range(0, 10);

    return m_MoveDirection;
}

bool EnemyAI::ScoutDue(float dtMs) {
    // scout_rate is the Scout() InvokeRepeating period in seconds.
    const float periodMs = m_Def->scoutRate * 1000.0F;
    if (periodMs <= 0.0F) {
        return true; // degenerate cadence: every step.
    }
    m_ScoutTimerMs += dtMs;
    if (m_ScoutTimerMs >= periodMs) {
        m_ScoutTimerMs -= periodMs;
        return true;
    }
    return false;
}

// FAITHFUL: RGEController__GetForce @ game_full.c:473583 (hard cap 28)
void EnemyAI::GetForce(glm::vec2 direction, float power) {
    if (m_Dead) {
        return; // matches the awake/alive gate upstream of the original call.
    }
    m_ForceDirection = direction;
    if (power > kForceCap) {
        power = kForceCap;
    }
    m_InertialVel = power;
}

// FAITHFUL: EnemyAI01__FixedUpdate @ game_full.c:675946
glm::vec2 EnemyAI::IntegrateVelocity(glm::vec2 moveDir, float speed,
                                     float speedRate) {
    const glm::vec2 walk =
        SafeNormalize(moveDir) * speed * (speedRate + 1.0F);

    // Knockback only contributes while the impulse is "active" (>1) and the
    // body is not kinematic; otherwise it is ignored and not decayed.
    // (kinematic enemies are turrets; EnemyDef does not yet expose the field,
    //  so this port assumes non-kinematic - see manual_flags.)
    if (m_InertialVel <= kInertiaActiveThreshold || m_Kinematic) {
        return walk;
    }

    const glm::vec2 knock = SafeNormalize(m_ForceDirection) * m_InertialVel;
    const glm::vec2 velocity = walk + knock;

    // MULTIPLICATIVE friction decay (friction is a fraction in [0,1)).
    m_InertialVel *= m_Def->friction;
    return velocity;
}

// FAITHFUL: RGEController__TurnTo @ game_full.c:473370
void EnemyAI::TurnTo(glm::vec2 normal) {
    // Reflect BOTH the move direction and the knockback force about the wall
    // normal so the enemy slides along the wall.
    m_MoveDirection = Reflect(m_MoveDirection, normal);
    m_ForceDirection = Reflect(m_ForceDirection, normal);
}

// FAITHFUL: RGEController__Dizzy @ game_full.c:473605
void EnemyAI::Dizzy(float durationMs) {
    if (m_Dizzy) {
        return; // already stunned; the original returns early.
    }
    m_Dizzy = true;
    m_DizzyTimerMs = durationMs;
    // EndCycle equivalent: stop steering while stunned.
    m_MoveDirection = glm::vec2(0.0F, 0.0F);
}

void EnemyAI::UpdateDizzy(float dtMs) {
    if (!m_Dizzy) {
        return;
    }
    m_DizzyTimerMs -= dtMs;
    if (m_DizzyTimerMs <= 0.0F) {
        m_DizzyTimerMs = 0.0F;
        m_Dizzy = false; // EndDizzy: resume the AI cycle.
    }
}

} // namespace Game
