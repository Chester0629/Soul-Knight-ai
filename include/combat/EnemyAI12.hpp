#ifndef GAME_ENEMY_AI12_HPP
#define GAME_ENEMY_AI12_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class EnemyAI12
 * @brief Faithful decision/cadence brain for EnemyAI12 (an RGEController subclass).
 *
 * EnemyAI12 is the "fireball caster": a stand-and-shoot enemy that re-rolls a
 * random wander direction (RunReflection), and when it shoots it stops moving
 * (move_direction = zero), latches its shoot gate, schedules a fireball spawn,
 * and clears weapon_lock_target. Modelled here (all pure; no Unity types):
 *   - Scout(): the gate (skip while dead@0x38 or dizzy@0xA1) + clears the chase
 *     target (target_obj = null, 0x7C). NO RNG draw.
 *   - RunReflection(): the random wander direction (two rg_random.Range(-1f, 1f)
 *     float draws, then normalized) written to move_direction (0x74). 2 draws.
 *   - ShootReflection(): the gate (skip while dead or dizzy) + the shoot latch
 *     (can_shoot = false @0x40), move_direction = zero, the scheduled fireball
 *     delay (shoot_cd @0x3C, via Invoke("CreateFireBall")), and clears
 *     weapon_lock_target (0x1C). NO RNG draw.
 *   - FixedUpdate physics: gated on awake (0x18) -- a not-awake step is a
 *     complete no-op. When awake there are three outcomes: the dead (0x38)
 *     branch (only reachable while inertial_vel <= 1.0) flips awake = false,
 *     zeroes velocity (owner) and early-returns (Dead); an active knockback
 *     impulse (inertial_vel > 1.0) composes the knockback velocity, decays by
 *     friction (0x50), and writes velocity; otherwise normal steering applies.
 *
 * Owner concerns (NOT modelled, by design): Animator.SetBool("walk")/SetTrigger,
 * Rigidbody2D.velocity writes, Object.Instantiate (fireball bullet01 @0xB8 /
 * dead_obj @0xBC), Transform reads, and MonoBehaviour.Invoke("CreateFireBall")
 * scheduling. Those are referenced in comments at their decomp sites.
 *
 * @see IL2CPP skeleton EnemyAI12.cs (damage, later_time, bullet01, dead_obj);
 *      FAITHFUL: EnemyAI12 @ game_full.c:679913-680237.
 */
class EnemyAI12 {
public:
    /**
     * @brief Outcome of one FixedUpdateStep, mirroring the decomp's branch tree
     *        (EnemyAI12__FixedUpdate @ game_full.c:679913-680018).
     *
     * The whole body is gated on awake (0x18, line 679942). When awake, there
     * are mutually exclusive movement outcomes:
     *   - Asleep:    awake (0x18) == 0 -> entire body skipped; nothing happens
     *                (NO friction decay, NO velocity write). Line 679942.
     *   - Dead:      inertial_vel (0x44) <= 1.0 AND dead (0x38) -> awake set to
     *                false (0x18 = 0, line 679945), velocity zeroed (owner), then
     *                EARLY-RETURNS via get_transform tail-call (line 679956). The
     *                steering block below does NOT run.
     *   - Steer:     inertial_vel <= 1.0 AND not dead -> normal steering (the
     *                block at 679958-679982). No decay (inertial_vel <= 1.0).
     *   - Knockback: inertial_vel > 1.0 -> the knockback term is added to the
     *                steering velocity, inertial_vel *= friction (0x50, line
     *                680014), then velocity is written.
     */
    enum class StepResult {
        Asleep,    ///< awake == 0: no-op (no decay, no velocity write).
        Dead,      ///< inertial_vel <= 1.0 && dead: awake=false, early return.
        Steer,     ///< inertial_vel <= 1.0 && not dead: normal steering, no decay.
        Knockback  ///< inertial_vel > 1.0: steering + knockback, decays by friction.
    };

    /// Wander component bounds (rg_random.Range(-1f, 1f) x2, max INCL).
    /// FAITHFUL: 0xbf800000 = -1.0f, 0x3f800000 = 1.0f.
    static constexpr float kWanderMin = -1.0F;
    static constexpr float kWanderMax = 1.0F;
    /// inertial_vel must exceed this for the knockback branch to apply (1.0 <).
    /// FAITHFUL: line 679943 (inertial_vel <= 1.0 selects the steering side).
    static constexpr float kKnockbackThreshold = 1.0F;

    EnemyAI12() = default;

    /// Seed this enemy's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- mutable state (owning entity mirrors these into the real fields) ----
    bool Dead() const { return m_Dead; }       // field 0x38
    void SetDead(bool dead) { m_Dead = dead; }

    bool Dizzy() const { return m_Dizzy; }      // field 0xA1
    void SetDizzy(bool dizzy) { m_Dizzy = dizzy; }

    bool Awake() const { return m_Awake; }      // field 0x18
    void SetAwake(bool awake) { m_Awake = awake; }

    bool CanShoot() const { return m_CanShoot; }              // field 0x40
    bool WeaponLockTarget() const { return m_WeaponLockTarget; } // field 0x1C

    /// Last wander direction chosen by RunReflection (move_direction, 0x74).
    glm::vec2 MoveDirection() const { return m_MoveDirection; }
    /// Whether the enemy currently has a chase target (target_obj, 0x7C).
    bool HasTarget() const { return m_HasTarget; }

    float InertialVel() const { return m_InertialVel; } // field 0x44
    /// Seed/clear knockback (owner calls when GetForce lands; clamp is base 28).
    void SetInertialVel(float vel) { m_InertialVel = vel; }

    /**
     * @brief Target-acquisition tick. FAITHFUL: EnemyAI12__Scout @ game_full.c:680022.
     *
     * Gated: does nothing while dead (0x38) or dizzy (0xA1, lines 680032-680037).
     * When active, clears the chase target (target_obj = null, 0x7C, line
     * 680038). The original's tail (line 680040) is a get_transform tail-call and
     * is NOT modelled. NO RNG draws here -- this method does not advance the
     * stream, so a gated and an active Scout are identical for replay purposes.
     * @return true if the tick was active (was not gated).
     */
    bool Scout();

    /**
     * @brief Re-pick a random wander direction. FAITHFUL: EnemyAI12__RunReflection
     *        @ game_full.c:680047.
     *
     * Draws two rg_random.Range(-1f, 1f) floats (lines 680067, 680072), builds a
     * Vector2 and normalizes it (FUN_00fa16ec build + FUN_00fa1e04 normalize),
     * then stores it as move_direction (set_move_direction, line 680075).
     * Owner: anim.SetBool("walk", true) at the tail (line 680081). No gate.
     * @return the new normalized move_direction (zero if both draws were zero).
     */
    glm::vec2 RunReflection();

    /**
     * @brief Begin a shot: stop and latch shoot state. FAITHFUL:
     *        EnemyAI12__ShootReflection @ game_full.c:680101.
     *
     * Gated: does nothing while dead (0x38) or dizzy (0xA1, lines 680113-680118).
     * When active: can_shoot = false (0x40, line 680119), move_direction = zero
     * (0x74, lines 680125-680126), and weapon_lock_target is cleared (0x1C = 0,
     * line 680138). No RNG draws. Owner: Invoke("CreateFireBall", shoot_cd@0x3C)
     * (line 680127), anim.SetTrigger x2 (lines 680132, 680137). The scheduled
     * fireball delay (shoot_cd) is returned via an out-param for the owner.
     * @param outFireBallDelay filled with shoot_cd (CreateFireBall delay).
     * @param shootCd          this enemy's shoot_cd (0x3C) field value.
     * @return true if the shot latched (was not gated).
     */
    bool ShootReflection(float &outFireBallDelay, float shootCd);

    /**
     * @brief One FixedUpdate physics step. FAITHFUL: EnemyAI12__FixedUpdate @
     *        game_full.c:679913.
     *
     * Models only the pure scalar/branch logic, not the Rigidbody2D writes. The
     * whole body is gated on awake (0x18, line 679942): when not awake the step
     * is a complete no-op (NO friction decay, NO velocity write). When awake:
     *   - inertial_vel (0x44) <= 1.0 (line 679943):
     *       * if dead (0x38, line 679944): awake = false (0x18 = 0, line 679945),
     *         velocity zeroed (owner), then EARLY-RETURN (get_transform tail-call
     *         line 679956) -> StepResult::Dead. The steering block does NOT run.
     *       * else: normal steering (679958-679982) -> StepResult::Steer. No decay.
     *   - inertial_vel > 1.0: the knockback term is added to steering velocity,
     *     inertial_vel *= friction (0x50, line 680014) -> StepResult::Knockback.
     * @param friction the friction field (0x50), inertia decay multiplier.
     * @return the StepResult naming which outcome occurred.
     */
    StepResult FixedUpdateStep(float friction);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};

    bool m_Awake = false;            // 0x18
    bool m_Dead = false;             // 0x38
    bool m_Dizzy = false;            // 0xA1
    bool m_CanShoot = true;          // 0x40
    bool m_WeaponLockTarget = false; // 0x1C
    bool m_HasTarget = false;        // target_obj != null (0x7C)

    float m_InertialVel = 0.0F;            // 0x44
    glm::vec2 m_MoveDirection{0.0F, 0.0F}; // 0x74
};

} // namespace Game

#endif /* GAME_ENEMY_AI12_HPP */
