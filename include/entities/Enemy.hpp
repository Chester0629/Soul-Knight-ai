#ifndef GAME_ENEMY_HPP
#define GAME_ENEMY_HPP

#include <memory>
#include <string>

#include <glm/glm.hpp>

#include "Util/Animation.hpp"
#include "Util/Collider.hpp"
#include "Util/GameObject.hpp"

#include "combat/CombatStats.hpp"
#include "combat/EnemyAI.hpp"
#include "data/GameData.hpp"

namespace Game {

/**
 * @class Enemy
 * @brief The 'bat' enemy entity: animation drawable + AI brain + combat stats.
 *
 * The owning scene drives the enemy each step: it calls @ref Think to obtain a
 * movement/shoot decision, then @ref ApplyMove to translate the entity. The base
 * Util::GameObject::Update is intentionally not overridden, so the looping bat
 * animation auto-advances when the object is drawn.
 */
class Enemy : public Util::GameObject {
public:
    /**
     * @param def          The enemy definition (supplies the AI shoot cooldown).
     * @param resourceRoot The Resources root (e.g. the RESOURCE_DIR macro).
     * @param spawnPos      The world-space spawn position.
     * @param detectRange   Distance at which the enemy notices the player.
     * @param attackRange   Distance at which the enemy stops and shoots.
     * @param spriteName    sprite-set basename (e.g. "enemy11"); frames are
     *                      <spriteName>_0..N. Defaults to "bat" (back-compat).
     */
    Enemy(const EnemyDef &def, const std::string &resourceRoot,
          glm::vec2 spawnPos, float detectRange, float attackRange,
          const std::string &spriteName = "bat");

    /**
     * @brief Run the AI for this step and return its decision.
     * @param dtMs     Elapsed time for this step, in milliseconds.
     * @param playerPos The player's world-space position.
     * @return The AI decision (desired move direction + whether to shoot).
     */
    EnemyAI::Decision Think(float dtMs, glm::vec2 playerPos);

    /**
     * @brief Move the enemy along @p dir scaled by its speed and the timestep.
     * @param dir  A (typically unit) direction vector.
     * @param dtMs Elapsed time for this step, in milliseconds.
     */
    void ApplyMove(glm::vec2 dir, float dtMs);

    /// Apply incoming damage to this enemy's combat stats.
    void TakeDamage(int dmg);

    /// @return true once this enemy's HP has been depleted.
    bool IsDead() const;

    /// @return the enemy's current world-space position.
    glm::vec2 Position() const;

    /// @return a circle collider centered on the enemy.
    Util::Collider GetCollider() const;

    /// @return a read-only view of the enemy's combat stats.
    const CombatStats &Stats() const;

    /// @return mutable combat stats (for the faithful Combat::ApplyToEnemy path).
    CombatStats &MutableStats() { return m_Stats; }

    /// @return the AI brain (seed at spawn; feed knockback via GetForce; drive
    ///         motion via IntegrateVelocity).
    EnemyAI &AI() { return m_AI; }

    /// @return base move speed (pixels/second).
    float Speed() const { return m_Speed; }

    /// Which floor room this enemy belongs to (for clear-room door gating).
    void SetRoomId(int id) { m_RoomId = id; }
    int RoomId() const { return m_RoomId; }

private:
    EnemyAI m_AI;
    CombatStats m_Stats{3, 3, 0, 0, 0, 0};
    float m_Speed{60.0F};
    std::shared_ptr<Util::Animation> m_Anim;
    int m_RoomId{-1};
};

} // namespace Game

#endif /* GAME_ENEMY_HPP */
