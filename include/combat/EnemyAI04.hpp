#ifndef GAME_ENEMY_AI04_HPP
#define GAME_ENEMY_AI04_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class EnemyAI04
 * @brief Faithful decision/cadence brain for EnemyAI04 (an RGEController subclass).
 *
 * Per-content port of the pure, unit-testable logic over the deterministic
 * RGEController/RGRandom base. EnemyAI04 is a melee/contact enemy (a `damage`
 * field, OnTriggerEnter2D contact-damage hook). Modelled here (all pure; no Unity
 * types):
 *   - Scout(): the gate (skip while dead (0x38) or dizzy (0xA1)) + the single
 *     advancing rg_random.Range(0, 10) idle re-roll draw (line 677253); clears the
 *     chase target (target_obj = null, 0x7C).
 *   - RunReflection(): the random wander direction -- two rg_random.Range(-1f, 1f)
 *     float draws (lines 677320, 677325), built into a Vector2 and normalized
 *     (FUN_00fa1e04), written to move_direction (0x74).
 *   - FixedUpdate physics: gated on awake (0x18). Unlike EnemyAI03, EnemyAI04
 *     splits FIRST on inertial_vel (0x44) <= 1.0; inside that branch a dead (0x38)
 *     enemy is a terminal "dead stop" that sets awake = 0 (the only state write the
 *     decomp performs here) and zeroes velocity; otherwise it steers. The else
 *     branch (inertial_vel > 1.0) steers + adds force_direction * inertial_vel,
 *     then decays inertial_vel *= friction (0x50, line 677226).
 *
 * Owner concerns (NOT modelled, by design): the Rigidbody2D velocity composition
 * (move_direction * speed * (speed_rate + 1.0), plus force_direction * inertial_vel
 * on the knockback branch), Component.get_transform, Animator.SetBool on the wander
 * tail (line 677334), the DeadEvent PrefabPool spawn, FixedRotation's target-facing
 * transform reads, and OnTriggerEnter2D contact-damage application (the body is
 * tail-truncated to get_gameObject -- the damage/can_hit logic is NOT recoverable
 * and is intentionally NOT modelled). Those are referenced in comments at their
 * decomp sites.
 *
 * @see IL2CPP skeleton EnemyAI04.cs (damage, can_hit, later_time, dead_obj,
 *      hit_object). FAITHFUL: EnemyAI04 @ game_full.c:677116-677405.
 */
class EnemyAI04 {
public:
    /**
     * @brief Outcome of one FixedUpdateStep, mirroring the decomp's branch tree
     *        (EnemyAI04__FixedUpdate @ game_full.c:677125-677230).
     *
     * The whole body is gated on awake (0x18, line 677154). When awake the tree
     * splits FIRST on inertial_vel (0x44) <= 1.0 (line 677155):
     *   - Asleep:   awake (0x18) == 0 -> entire body skipped; nothing happens
     *               (NO friction decay, NO velocity write, NO awake clear).
     *   - DeadStop: awake && inertial_vel <= 1.0 && dead (0x38) != 0 (line 677156):
     *               TERMINAL -- sets awake = 0 (line 677157, the only state write
     *               this method performs), zeroes the rigidbody (owner), and the
     *               steering block does NOT run (get_transform tail does not
     *               return). NO friction decay.
     *   - Steer:    awake && inertial_vel <= 1.0 && not dead: plain steering
     *               (block 677170-677194). NO friction decay.
     *   - Knockback: awake && inertial_vel > 1.0 (else, line 677196): steering +
     *               force_direction * inertial_vel, then inertial_vel *= friction
     *               (0x50, line 677226). This is the ONLY path that decays.
     */
    enum class StepResult {
        Asleep,    ///< awake == 0: no-op (no decay, no velocity write, no awake clear).
        DeadStop,  ///< inertial_vel <= 1.0 && dead: sets awake = 0, terminal, no decay.
        Steer,     ///< inertial_vel <= 1.0 && not dead: normal steering, no decay.
        Knockback  ///< inertial_vel > 1.0: steer + force, inertial_vel *= friction.
    };

    /// Idle re-roll ceiling drawn by Scout (rg_random.Range(0, 10), max EXCL).
    static constexpr int kScoutRerollCeiling = 10;
    /// Wander component bounds (rg_random.Range(-1f, 1f) x2, max INCL).
    static constexpr float kWanderMin = -1.0F;
    static constexpr float kWanderMax = 1.0F;
    /// inertial_vel must be <= this for the dead/steer branch; > for knockback.
    static constexpr float kKnockbackThreshold = 1.0F;

    EnemyAI04() = default;

    /// Seed this enemy's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- mutable state (owning entity mirrors these into the real fields) ----
    bool Awake() const { return m_Awake; }       // 0x18
    void SetAwake(bool a) { m_Awake = a; }

    bool Dead() const { return m_Dead; }         // 0x38
    void SetDead(bool dead) { m_Dead = dead; }

    bool Dizzy() const { return m_Dizzy; }       // 0xA1
    void SetDizzy(bool d) { m_Dizzy = d; }

    /// Whether the enemy currently has a chase target (target_obj, 0x7C).
    bool HasTarget() const { return m_HasTarget; }

    float InertialVel() const { return m_InertialVel; } // 0x44
    /// Seed/clear knockback (owner calls when GetForce lands; base clamp is 28).
    void SetInertialVel(float vel) { m_InertialVel = vel; }

    /// Last wander direction chosen by RunReflection (move_direction, 0x74).
    glm::vec2 MoveDirection() const { return m_MoveDirection; }

    /**
     * @brief Target-acquisition tick. FAITHFUL: EnemyAI04__Scout @ game_full.c:677234.
     *
     * Gate (lines 677244-677249): runs only while NOT dead (0x38) AND NOT dizzy
     * (0xA1). When active: target_obj = null (0x7C, line 677250) then a single
     * rg_random.Range(0, 10) draw (line 677253). The re-detect/re-target tail
     * (FUN_010b7dcc, line 677256) is tail-call-truncated and not modelled.
     * @return the rg_random.Range(0, 10) roll, or -1 when gated (no draw taken).
     */
    int Scout();

    /**
     * @brief Re-pick a random wander direction. FAITHFUL: EnemyAI04__RunReflection
     *        @ game_full.c:677281.
     *
     * Draws two rg_random.Range(-1f, 1f) floats (lines 677320, 677325), builds a
     * Vector2 (FUN_00fa16ec) and normalizes it (FUN_00fa1e04), then stores it as
     * move_direction (set_move_direction, line 677328). The preceding target_obj
     * position read (lines 677307-677314) feeds only owner facing and is not part
     * of the pure draw logic. Owner: anim.SetBool(..., true) at the tail
     * (line 677334). No gate in the decomp.
     * @return the new normalized move_direction (zero if both draws were zero).
     */
    glm::vec2 RunReflection();

    /**
     * @brief One FixedUpdate physics step. FAITHFUL: EnemyAI04__FixedUpdate @
     *        game_full.c:677125.
     *
     * Models only the pure scalar/branch logic, not the Rigidbody2D writes. Gated
     * on awake (0x18, line 677154): not awake -> Asleep no-op. When awake the tree
     * splits on inertial_vel (0x44) <= 1.0 (line 677155):
     *   - inertial_vel <= 1.0 && dead (0x38) (line 677156): DeadStop. Sets
     *     awake = 0 (line 677157, the only state write), the owner zeroes the
     *     rigidbody, and the steering block is skipped (terminal). NO decay.
     *   - inertial_vel <= 1.0 && not dead: Steer (block 677170-677194). NO decay.
     *   - inertial_vel > 1.0 (else, line 677196): Knockback -- the owner adds
     *     force_direction * inertial_vel, then inertial_vel *= friction (0x50,
     *     line 677226). The ONLY decaying path.
     * Takes ZERO rng draws.
     * @param friction the friction field (0x50), inertia decay multiplier.
     * @return the StepResult naming which of the four outcomes occurred.
     */
    StepResult FixedUpdateStep(float friction);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};

    bool m_Awake = false;     // 0x18
    bool m_Dead = false;      // 0x38
    bool m_Dizzy = false;     // 0xA1
    bool m_HasTarget = false; // target_obj != null (0x7C)

    float m_InertialVel = 0.0F;            // 0x44
    glm::vec2 m_MoveDirection{0.0F, 0.0F}; // 0x74
};

} // namespace Game

#endif /* GAME_ENEMY_AI04_HPP */
