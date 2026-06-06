#ifndef GAME_BULLET_HPP
#define GAME_BULLET_HPP

/**
 * @file Bullet.hpp
 * @brief A pooled projectile entity for the Soul Knight rebuild.
 */

#include <string>

#include <glm/glm.hpp>

#include "Util/Collider.hpp"
#include "Util/GameObject.hpp"

namespace Game {

/**
 * @class Bullet
 * @brief A pooled, deterministic projectile.
 *
 * A Bullet is meant to be allocated once and reused from an object pool. After
 * construction it is inactive and invisible; calling @ref Init arms it with a
 * position, velocity, lifetime, damage, and camp. @ref Update advances it each
 * frame and automatically deactivates it once its lifetime expires.
 */
class Bullet : public Util::GameObject {
public:
    /**
     * @brief Constructs an inactive bullet.
     *
     * Loads the bullet sprite from
     * `resourceRoot + "/sprites/bullet_0.png"`, places it on z-index 4, and
     * starts hidden until @ref Init is called.
     *
     * @param resourceRoot The resource directory root (e.g. RESOURCE_DIR).
     */
    explicit Bullet(const std::string &resourceRoot);

    /**
     * @brief (Re)initializes the bullet for reuse from an object pool.
     *
     * @param pos             The spawn position in world space (pixels).
     * @param velocityPxPerSec The velocity in pixels per second.
     * @param lifetimeMs      The lifetime in milliseconds before auto-deactivation.
     * @param damage          The damage dealt on hit.
     * @param camp            The camp: 0 = player bullet, 1 = enemy bullet.
     */
    void Init(glm::vec2 pos, glm::vec2 velocityPxPerSec, float lifetimeMs,
              int damage, int camp);

    /**
     * @brief Rich (re)init carrying the faithful RGBullet attributes.
     *
     * Adds the combat attributes the original RGBullet.UpdateAttribute pushes to
     * each HurtBox so the hit resolver can roll crit and apply knockback:
     * @p repel (knockback magnitude), @p critical (crit chance 0..100), and the
     * pierce model (@p canThrough with a @p pierce budget; pierce 0 + canThrough
     * true == the bool "infinite pierce" path). The 5-arg @ref Init delegates here
     * with zeroed attributes.
     */
    void Init(glm::vec2 pos, glm::vec2 velocityPxPerSec, float lifetimeMs,
              int damage, int camp, float repel, int critical, bool canThrough,
              int pierce);

    /**
     * @brief Advances the bullet by one frame.
     *
     * When active, integrates the position by velocity, decrements the
     * remaining lifetime, and deactivates the bullet once the lifetime reaches
     * zero. Does nothing when inactive.
     *
     * @param dtMs The elapsed time for this step, in milliseconds.
     */
    void Update(float dtMs) override;

    /**
     * @brief Returns whether the bullet is currently active.
     *
     * @return true if active, false otherwise.
     */
    bool Active() const;

    /**
     * @brief Deactivates and hides the bullet, returning it to the pool.
     */
    void Deactivate();

    /**
     * @brief Returns the damage this bullet deals on hit.
     *
     * @return The damage value.
     */
    int Damage() const;

    /**
     * @brief Returns the camp of this bullet.
     *
     * @return 0 for a player bullet, 1 for an enemy bullet.
     */
    int Camp() const;

    /// @return knockback magnitude this bullet imparts (RGBullet repel).
    float Repel() const { return m_Repel; }
    /// @return crit chance 0..100 rolled by the hit resolver.
    int Critical() const { return m_Critical; }
    /// @return whether the bullet pierces (does not despawn on its first hit).
    bool CanThrough() const { return m_CanThrough; }
    /// @return remaining pass-through budget (0 with CanThrough() == "infinite").
    int Pierce() const { return m_Pierce; }
    /// @return current velocity (pixels/second); its direction is the travel dir.
    glm::vec2 Velocity() const { return m_Velocity; }
    /// Consume one pierce hit; @return true if the bullet should now despawn.
    bool ConsumePierce();

    /**
     * @brief Returns the collision shape for this bullet.
     *
     * @return A circle collider centered on the bullet's current position.
     */
    Util::Collider GetCollider() const;

private:
    glm::vec2 m_Velocity{0.0f, 0.0f};
    float m_LifeMs{0.0f};
    int m_Damage{0};
    int m_Camp{0};
    float m_Repel{0.0f};
    int m_Critical{0};
    bool m_CanThrough{false};
    int m_Pierce{0};
    bool m_Active{false};
};

} // namespace Game

#endif // GAME_BULLET_HPP
