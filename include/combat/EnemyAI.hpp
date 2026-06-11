#ifndef GAME_ENEMY_AI_HPP
#define GAME_ENEMY_AI_HPP

#include <glm/glm.hpp>

#include "data/GameData.hpp"
#include "data/RGRandom.hpp"

namespace Game {

/// High-level enemy behaviour states.
enum class AIState {
    Idle,   ///< Player out of detection range.
    Chase,  ///< Player detected but out of attack range; close the distance.
    Attack, ///< Player in attack range; hold and shoot.
};

/**
 * @class EnemyAI
 * @brief A data-driven enemy brain, faithful to the original RGEController loop.
 *
 * Classifies the range to the player into Idle / Chase / Attack, produces a
 * desired (normalized) move direction, gates shooting by the enemy's
 * @c shoot_cd, and reproduces the original's deterministic knockback physics
 * (inertial impulse with multiplicative friction decay, wall-slide reflection).
 *
 * Two layers are exposed:
 *   - The legacy high-level @c Update(dtMs, selfPos, playerPos) -> Decision API,
 *     used by Enemy/GameScene, is preserved verbatim.
 *   - A lower-level, RGEController-faithful API: a Scout tick on the
 *     @c scout_rate cadence (advances a per-instance RGRandom stream), a
 *     shoot-cadence gate re-armed by @c TurnCanShoot, knockback via
 *     @c GetForce / @c IntegrateVelocity, and @c TurnTo wall reflection.
 *
 * Engine-free and deterministic: identical seed + identical inputs reproduce
 * identical rolls and motion, so the unit is replay-safe and window-less
 * testable. The owning entity applies the integrated velocity and spawns
 * bullets.
 *
 * @see RGEController.cs (recreation); FAITHFUL notes cite game_full.c lines.
 */
class EnemyAI {
public:
    /// The original's hard cap on a knockback impulse magnitude (RGEController).
    /// FAITHFUL: RGEController__GetForce @ game_full.c:473583 (28.0 clamp).
    static constexpr float kForceCap = 28.0F;

    /// Knockback term is only added to velocity while inertial_vel exceeds this.
    /// FAITHFUL: EnemyAI01__FixedUpdate @ game_full.c:675946 (inertial_vel <= 1).
    static constexpr float kInertiaActiveThreshold = 1.0F;

    struct Decision {
        AIState state = AIState::Idle;
        glm::vec2 moveDir{0.0F, 0.0F}; ///< Unit vector toward target, or zero.
        bool shouldShoot = false;
    };

    /**
     * @param def         The enemy definition (supplies shoot_cd, friction,
     *                    scout_rate, e_size).
     * @param detectRange Distance at which the enemy notices the player.
     * @param attackRange Distance at which the enemy stops and shoots.
     */
    EnemyAI(const EnemyDef &def, float detectRange, float attackRange);

    /// Decide behaviour for this step given self and player world positions.
    /// (Legacy high-level API; preserved for Enemy/GameScene.)
    Decision Update(float dtMs, glm::vec2 selfPos, glm::vec2 playerPos);

    AIState State() const { return m_State; }

    // ---- determinism root ---------------------------------------------------

    /// Seed this enemy's private RNG stream (call once at spawn).
    /// FAITHFUL: RGEController__SetRGRandomSeed.
    void SetSeed(int seed);

    /// @return true once SetSeed has been called.
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- RGEController-faithful low-level loop -------------------------------

    /**
     * @brief Target acquisition + chase decision (one Scout cadence tick).
     *
     * Mirrors EnemyAI01__Scout: gated by !dead && !dizzy, points the move
     * direction toward the (nearest) player, and ALWAYS advances the
     * deterministic stream by one @c Range(0,10) draw so replays line up.
     * @return the chosen unit move direction (zero when gated or no target).
     *
     * FAITHFUL: EnemyAI01__Scout @ game_full.c:676136.
     */
    glm::vec2 Scout(glm::vec2 selfPos, glm::vec2 playerPos);

    /// @return true if a Scout tick is due this step (scout_rate cadence) and
    ///         consumes the accumulated time. Pass dtMs; call Scout when true.
    bool ScoutDue(float dtMs);

    /**
     * @brief Apply an external knockback impulse (server/awake gated upstream).
     *
     * Stores @p direction in force_direction and clamps @p power to
     * @c kForceCap (28) into inertial_vel.
     *
     * FAITHFUL: RGEController__GetForce @ game_full.c:473583.
     */
    void GetForce(glm::vec2 direction, float power);

    /**
     * @brief Compose this fixed step's velocity and decay the knockback.
     *
     * velocity = move_direction * speed * (speed_rate + 1) + force_direction *
     * inertial_vel, but the knockback term is dropped (and decay skipped) while
     * inertial_vel <= 1 or the enemy is kinematic. inertial_vel then decays
     * MULTIPLICATIVELY by friction (a fraction in [0,1)).
     *
     * FAITHFUL: EnemyAI01__FixedUpdate @ game_full.c:675946.
     *
     * @param moveDir   the steering direction (e.g. Scout's result).
     * @param speed     role_attribute.speed.
     * @param speedRate role_attribute.speed_rate.
     * @return the world velocity to feed the rigidbody this step.
     */
    glm::vec2 IntegrateVelocity(glm::vec2 moveDir, float speed, float speedRate);

    /**
     * @brief Wall bounce: reflect BOTH move and force directions about @p normal
     *        so the enemy slides along the wall instead of stalling.
     *
     * FAITHFUL: RGEController__TurnTo @ game_full.c:473370.
     */
    void TurnTo(glm::vec2 normal);

    /// Re-arm the shoot gate (Invoke target of the shoot cadence).
    /// FAITHFUL: RGEController__TurnCanShoot.
    void TurnCanShoot() { m_CanShoot = true; }

    /**
     * @brief Stun for @p durationMs: latch dizzy and stop scouting/shooting.
     * FAITHFUL: RGEController__Dizzy @ game_full.c:473605.
     */
    void Dizzy(float durationMs);

    /// Tick the dizzy timer; clears dizzy when it elapses.
    void UpdateDizzy(float dtMs);

    /// Latch death (once); subsequent ticks become no-ops.
    void SetDead() { m_Dead = true; }

    /// Mark this enemy kinematic (turret-style: immovable, ignores knockback).
    /// FAITHFUL: kinematic field (0x58); EnemyDef does not yet carry it, so the
    /// caller wires it from enemies.json if needed.
    void SetKinematic(bool kinematic) { m_Kinematic = kinematic; }

    // ---- accessors (mostly for tests / the owning entity) -------------------

    bool CanShoot() const { return m_CanShoot; }
    bool IsDizzy() const { return m_Dizzy; }
    bool IsDead() const { return m_Dead; }
    float InertialVel() const { return m_InertialVel; }
    glm::vec2 ForceDirection() const { return m_ForceDirection; }
    glm::vec2 MoveDirection() const { return m_MoveDirection; }
    int ESize() const { return m_Def->eSize; }
    float Friction() const { return m_Def->friction; }
    float ScoutRate() const { return m_Def->scoutRate; }

private:
    const EnemyDef *m_Def;
    float m_DetectRange;
    float m_AttackRange;
    float m_ShootCooldownMs = 0.0F;
    AIState m_State = AIState::Idle;

    RGRandom m_Rng{};

    // RGEController movement / physics state (offsets noted vs. dump.cs):
    glm::vec2 m_MoveDirection{0.0F, 0.0F};  ///< _move_direction (0x74)
    glm::vec2 m_ForceDirection{0.0F, 0.0F}; ///< force_direction (0x48)
    float m_InertialVel = 0.0F;             ///< inertial_vel    (0x44)

    bool m_CanShoot = true;   ///< can_shoot (0x40)
    bool m_Dizzy = false;     ///< dizzy     (0xA1)
    bool m_Dead = false;      ///< dead      (0x38)
    bool m_Kinematic = false; ///< kinematic (0x58)
    float m_DizzyTimerMs = 0.0F;
    float m_ScoutTimerMs = 0.0F; ///< accumulator for the scout_rate cadence.
};

} // namespace Game

#endif /* GAME_ENEMY_AI_HPP */
