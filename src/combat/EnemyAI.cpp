#include "combat/EnemyAI.hpp"

#include <cmath>

namespace Game {

EnemyAI::EnemyAI(const EnemyDef &def, float detectRange, float attackRange)
    : m_Def(&def),
      m_DetectRange(detectRange),
      m_AttackRange(attackRange) {}

EnemyAI::Decision EnemyAI::Update(float dtMs, glm::vec2 selfPos,
                                  glm::vec2 playerPos) {
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
        return d;
    }

    const glm::vec2 dir = dist > 0.0F ? toPlayer / dist : glm::vec2(0.0F, 0.0F);

    if (dist > m_AttackRange) {
        m_State = AIState::Chase;
        d.state = m_State;
        d.moveDir = dir;
        return d;
    }

    // In attack range: hold position and shoot when the cooldown is ready.
    m_State = AIState::Attack;
    d.state = m_State;
    if (m_ShootCooldownMs <= 0.0F) {
        d.shouldShoot = true;
        m_ShootCooldownMs = m_Def->shootCd * 1000.0F; // seconds -> ms
    }
    return d;
}

} // namespace Game
