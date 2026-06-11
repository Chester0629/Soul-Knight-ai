#ifndef GAME_WEAPON_INSTANCE_HPP
#define GAME_WEAPON_INSTANCE_HPP

#include <vector>

#include <glm/glm.hpp>

#include "data/GameData.hpp"
#include "data/RGRandom.hpp"

namespace Game {
/**
 * @struct BulletSpawn
 * @brief One projectile produced by a single trigger pull.
 *
 * Carries everything GameScene needs to arm a pooled Bullet without re-touching
 * the weapon data: a normalized travel @ref direction, the @ref velocity along
 * it (direction * bullet_speed, in the data's units/sec), and the combat
 * attributes the original @c RGBullet.UpdateAttribute pushes onto each HurtBox
 * (@ref damage, @ref repel, @ref critical, plus the pierce budget).
 *
 * Pierce is modelled faithfully to the two RGBullet overloads: when the weapon
 * carries an int @c through_count budget, @ref pierce holds it and @ref canThrough
 * is true; when the weapon is the bool @c can_through kind, @ref canThrough is
 * true and @ref pierce is 0 (treated as "infinite" by the consumer, matching the
 * original bool overload). A non-piercing bullet has @ref canThrough false and
 * @ref pierce 0.
 */
struct BulletSpawn {
    glm::vec2 direction{0.0F, 0.0F}; ///< Unit travel direction.
    glm::vec2 velocity{0.0F, 0.0F};  ///< direction * bullet_speed (data units/sec).
    int damage = 0;                  ///< atk pushed to the HurtBox.
    float repel = 0.0F;              ///< knockback magnitude.
    int critical = 0;                ///< crit chance % (0..100), rolled on hit.
    bool canThrough = false;         ///< whether the bullet pierces at all.
    int pierce = 0;                  ///< remaining pass-through budget (0 => bool path).
};

/**
 * @struct FirePlan
 * @brief The result of one trigger pull: the bullets to spawn + the cost.
 *
 * Produced by @ref WeaponInstance::BuildFirePlan. GameScene spawns each
 * @ref bullets entry from the weapon's gun-point and spends @ref energyCost once
 * (the original @c MakeConsume spends per pull, not per bullet).
 */
struct FirePlan {
    std::vector<BulletSpawn> bullets; ///< One entry per fired bullet.
    int energyCost = 0;               ///< Energy spent for this pull (== consume).
};

/**
 * @class WeaponInstance
 * @brief Runtime fire-control + multi-bullet fire planning for a player weapon.
 *
 * Wraps a WeaponDef and tracks the shot cooldown derived from the weapon's
 * fire-rate (@c weapon_speed). It answers "may I fire now?", exposes the per-shot
 * energy cost, and (the deepened responsibility) builds a deterministic
 * @ref FirePlan: the N bullets a single trigger pull produces, with their
 * deviation-jittered directions, velocities and combat attributes.
 *
 * Energy gating itself remains the caller's responsibility (check/spend on the
 * player's CombatStats); this class only reports the cost.
 */
class WeaponInstance {
public:
    /// Base shot interval (ms) at weapon_speed == 1; scaled by 1 / weapon_speed.
    static constexpr float kBaseIntervalMs = 200.0F;

    explicit WeaponInstance(const WeaponDef &def);

    /// Tick the cooldown toward ready by @p dtMs milliseconds.
    void Update(float dtMs);

    /// Whether the weapon's cooldown has elapsed.
    bool Ready() const;

    /// If ready, start the cooldown and report a fired shot; else returns false.
    bool TryFire();

    /// Energy consumed per shot (from the weapon's @c consume).
    int EnergyCost() const;

    float CooldownRemainingMs() const { return m_CooldownMs; }
    float FireIntervalMs() const { return m_FireIntervalMs; }
    const WeaponDef &Def() const { return *m_Def; }

    /**
     * @brief How many bullets one trigger pull produces.
     *
     * Mirrors Gun001/Gun002: @c count when > 0, else 1 (a single straight shot).
     */
    int ShotCount() const;

    /**
     * @brief The total spread half-angle (degrees) applied as random deviation.
     *
     * FAITHFUL to Gun001__Attack: spread = deviation + deviation * recoilFactor.
     * Each bullet's random jitter is drawn in [-spread, +spread]. With the default
     * recoil factor (0) this equals the raw @c deviation.
     */
    float SpreadDegrees() const;

    /**
     * @brief Set the controller energy "recoil" factor that widens spread.
     *
     * In the original the spread base is @c deviation, scaled up by an energy
     * component field (energy+0x20) so a hot weapon kicks wider. The default is 0
     * (no extra spread). This is exposed additively so the player layer can feed
     * the real recoil value without changing the data.
     * @param factor Multiplier applied as deviation * factor extra spread.
     */
    void SetRecoilFactor(float factor);

    /**
     * @brief Build the deterministic fire plan for one trigger pull.
     *
     * FAITHFUL to RGWeapon::FireOnce (Gun001__Attack @ game_full.c:315755 +
     * Gun002 count loop) and RGBullet::SetBulletVelocity / UpdateAttribute.
     *
     * For each of @ref ShotCount bullets:
     *   - fixed fan offset  = fanStart + i * angle, fanStart = -(count-1)/2 * angle
     *   - random deviation  = rng.Range(-spread, +spread)  (per-shot, in order)
     *   - the aim direction is rotated by (fan + deviation) degrees (CCW, +y up)
     *   - velocity          = rotatedDir * bullet_speed
     *   - attributes        = atk / repel / critical / (can_through|through_count)
     *
     * The RNG draws happen exactly once per bullet, in shot order, so the same
     * seeded @ref RGRandom stream reproduces the same plan bit-for-bit.
     *
     * @param aimDir The desired aim direction (need not be normalized; a zero or
     *               near-zero vector defaults to +x so a plan is still produced).
     * @param rng    The deterministic per-instance stream the spread draws from.
     * @return The bullets to spawn and the energy to spend this pull.
     */
    FirePlan BuildFirePlan(glm::vec2 aimDir, RGRandom &rng) const;

private:
    const WeaponDef *m_Def;
    float m_FireIntervalMs;
    float m_CooldownMs = 0.0F;
    float m_RecoilFactor = 0.0F;
};
} // namespace Game

#endif /* GAME_WEAPON_INSTANCE_HPP */
