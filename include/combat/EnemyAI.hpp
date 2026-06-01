#ifndef GAME_ENEMY_AI_HPP
#define GAME_ENEMY_AI_HPP

#include <glm/glm.hpp>

#include "data/GameData.hpp"

namespace Game {

/// High-level enemy behaviour states.
enum class AIState {
    Idle,   ///< Player out of detection range.
    Chase,  ///< Player detected but out of attack range; close the distance.
    Attack, ///< Player in attack range; hold and shoot.
};

/**
 * @class EnemyAI
 * @brief A minimal, data-driven enemy brain.
 *
 * Classifies the range to the player into Idle / Chase / Attack, produces a
 * desired (normalized) move direction, and gates shooting by the enemy's
 * @c shoot_cd. Deterministic and engine-free, so it is unit-testable without a
 * window. The owning entity applies the movement and spawns the bullet.
 */
class EnemyAI {
public:
    struct Decision {
        AIState state = AIState::Idle;
        glm::vec2 moveDir{0.0F, 0.0F}; ///< Unit vector toward target, or zero.
        bool shouldShoot = false;
    };

    /**
     * @param def         The enemy definition (supplies shoot_cd).
     * @param detectRange Distance at which the enemy notices the player.
     * @param attackRange Distance at which the enemy stops and shoots.
     */
    EnemyAI(const EnemyDef &def, float detectRange, float attackRange);

    /// Decide behaviour for this step given self and player world positions.
    Decision Update(float dtMs, glm::vec2 selfPos, glm::vec2 playerPos);

    AIState State() const { return m_State; }

private:
    const EnemyDef *m_Def;
    float m_DetectRange;
    float m_AttackRange;
    float m_ShootCooldownMs = 0.0F;
    AIState m_State = AIState::Idle;
};

} // namespace Game

#endif /* GAME_ENEMY_AI_HPP */
