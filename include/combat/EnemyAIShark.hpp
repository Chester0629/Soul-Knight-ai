#ifndef GAME_ENEMY_AI_SHARK_HPP
#define GAME_ENEMY_AI_SHARK_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class EnemyAIShark
 * @brief Faithful decision/cadence brain for EnemyAIShark (an RGEController
 *        subclass) -- a dash + wave special enemy.
 *
 * Per-content port of the pure, unit-testable logic layer over the deterministic
 * RGEController/RGRandom base. The shark scouts for a target, re-rolls a random
 * dash direction (RunReflection), rolls a shoot gate (ShootReflection), triggers
 * a dash (Dash), and during the dash its coroutine ticks a wave-spawn cadence
 * (Dashing/Wave). Modelled here (all pure; no Unity types):
 *   - Scout(): gate (skip while dizzy OR dead) + clears target + the single
 *     advancing rg_random.Range(0, 10) draw that lives in the tail-called
 *     continuation. (decomp: EnemyAIShark__Scout @ 681448 tail-calls FUN_007ff020
 *     / FUN_007ff0e0 which perform the CircleCast + Range(0,10).)
 *   - RunReflection(): ungated; two rg_random.Range(-1f, 1f) float draws, built
 *     into a Vector2, normalized, written to move_direction (0x74).
 *   - ShootReflection(): gate (skip while dead OR dizzy) + the single
 *     rg_random.Range(0, 100) roll. The post-draw body is tail-call-truncated.
 *   - Dash(): can_shoot = false (0x40); schedules the Dashing coroutine with a
 *     shoot_cd (0x3C) delay. No RNG.
 *   - DashingStep(): the Dashing coroutine state machine (MoveNext). On the first
 *     tick it inits the wave timer (0.0) and interval (0.2); on each subsequent
 *     tick it advances the timer by the interval and, while timer < 1.5 and the
 *     enemy is alive, emits a Wave. This is the wave cadence (~7 waves over 1.4s).
 *   - GetForce(): the dash-invulnerability gate (0xB1): base knockback only
 *     applies (and clamps magnitude to 28) when NOT dashing.
 *   - FixedUpdate physics: gated on awake (0x18). When awake: if inertial_vel
 *     (0x44) <= 1.0 there are two sub-cases -- if dead (0x38) the body clears
 *     awake (0x18 = 0) and zeroes velocity (DeadClear), otherwise normal steering
 *     (Steer); if inertial_vel > 1.0 the knockback impulse is added and decays by
 *     friction (0x50): inertial_vel *= friction (Knockback). FixedUpdate takes
 *     ZERO rng draws.
 *
 * Owner concerns (NOT modelled, by design): Animator.SetBool("run"/"dash"),
 * Rigidbody2D.velocity writes, MonoBehaviour.Invoke/StartCoroutine scheduling,
 * Instantiate(wave_bullet)/GetComponent<RGBullet>, RGMusicManager.PlayEffect,
 * CircleCastAll target detection, transform get_position reads. Referenced in
 * comments at their decomp sites.
 *
 * @see IL2CPP skeleton EnemyAIShark.cs (damage, can_hit, in_atk1, later_time,
 *      wave_bullet, bite_atk, bite_range; <Dashing>c__Iterator0 timer@+8,
 *      interval@+0xC).
 *      FAITHFUL: EnemyAIShark @ game_full.c:681339-681929.
 */
class EnemyAIShark {
public:
    /**
     * @brief Outcome of one FixedUpdateStep, mirroring the decomp's branch tree
     *        (EnemyAIShark__FixedUpdate @ game_full.c:681339-681444).
     *
     * The whole body is gated on awake (0x18, line 681368). When awake there are
     * exactly three mutually exclusive movement outcomes:
     *   - Asleep:    awake (0x18) == 0 -> entire body skipped; nothing happens
     *                (NO decay, NO velocity write, NO awake clear). Line 681368.
     *   - DeadClear: inertial_vel (0x44) <= 1.0 AND dead (0x38) -> the controller
     *                clears awake (0x18 = 0, line 681371) and zeroes the rigidbody
     *                velocity; no friction decay (the *= friction only happens on
     *                the > 1.0 path). Lines 681369-681383.
     *   - Steer:     inertial_vel <= 1.0 AND not dead -> normal steering applies
     *                (the move_direction * speed * (speed_rate+1) block at
     *                681384-681408). No decay.
     *   - Knockback: inertial_vel > 1.0 -> the knockback impulse is added to the
     *                steering velocity and inertial_vel decays by friction (0x50):
     *                inertial_vel *= friction (line 681440). Lines 681410-681441.
     */
    enum class StepResult {
        Asleep,    ///< awake == 0: no-op (no decay, no velocity, no awake clear).
        DeadClear, ///< inertial_vel <= 1.0 && dead: clears awake, no decay.
        Steer,     ///< inertial_vel <= 1.0 && !dead: normal steering, no decay.
        Knockback  ///< inertial_vel > 1.0: knockback added, inertial_vel *= friction.
    };

    /// Idle/wander re-roll ceiling drawn by Scout's continuation
    /// (rg_random.Range(0, 10), max EXCLUSIVE; decomp line 681553/681595).
    static constexpr int kScoutRerollCeiling = 10;
    /// Dash-direction component bounds (rg_random.Range(-1f, 1f) x2, max INCL).
    static constexpr float kDirMin = -1.0F;
    static constexpr float kDirMax = 1.0F;
    /// ShootReflection roll ceiling (rg_random.Range(0, 100), max EXCLUSIVE).
    static constexpr int kShootRollCeiling = 100;
    /// inertial_vel must EXCEED this for the knockback branch (1.0 <). Line 681369.
    static constexpr float kKnockbackThreshold = 1.0F;

    /// Wave coroutine: per-tick timer increment (0x3e4ccccd, line 681897).
    static constexpr float kWaveInterval = 0.2F;
    /// Wave coroutine: emit a Wave while timer < this (line 681905, 0x3fc00000).
    static constexpr float kWaveTimerLimit = 1.5F;

    EnemyAIShark() = default;

    /// Seed this enemy's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- mutable state (owning entity mirrors these into the real fields) ----
    bool Dead() const { return m_Dead; }       // field 0x38
    void SetDead(bool dead) { m_Dead = dead; }

    bool Dizzy() const { return m_Dizzy; }      // field 0xA1
    void SetDizzy(bool v) { m_Dizzy = v; }

    bool CanShoot() const { return m_CanShoot; } // field 0x40

    bool Dashing() const { return m_Dashing; }   // field 0xB1 (dash invuln flag)
    void SetDashing(bool v) { m_Dashing = v; }

    bool Awake() const { return m_Awake; }       // field 0x18
    void SetAwake(bool v) { m_Awake = v; }

    /// Last dash direction chosen by RunReflection (move_direction, 0x74).
    glm::vec2 MoveDirection() const { return m_MoveDirection; }
    /// Whether the enemy currently has a chase target (target_obj, 0x7C).
    bool HasTarget() const { return m_HasTarget; }

    float InertialVel() const { return m_InertialVel; } // field 0x44
    void SetInertialVel(float vel) { m_InertialVel = vel; }

    // ---- Dashing coroutine accumulator state (<Dashing>c__Iterator0) --------
    float WaveTimer() const { return m_WaveTimer; }      // iterator +8
    float WaveInterval() const { return m_WaveInterval; } // iterator +0xC

    /**
     * @brief Target-acquisition tick. FAITHFUL: EnemyAIShark__Scout @
     *        game_full.c:681448 (+ tail continuation FUN_007ff020 @ 681498 /
     *        FUN_007ff0e0 @ 681558).
     *
     * Gate (lines 681458-681463): runs ONLY while not dizzy (0xA1) AND not dead
     * (0x38). When active: target_obj = null (0x7C, line 681464) then (in the
     * tail-called continuation) a CircleCastAll detection (owner) followed by the
     * single rg_random.Range(0, 10) draw (line 681553/681595). The continuation
     * also sets min_distance (0x88) = 100.0 when the cast hits something; that
     * branch and the cast are owner/truncated and not modelled -- only the gate,
     * the target clear, and the draw are pure.
     * @return the rg_random.Range(0, 10) roll, or -1 when gated (no draw taken).
     */
    int Scout();

    /**
     * @brief Re-pick a random dash direction. FAITHFUL: EnemyAIShark__RunReflection
     *        @ game_full.c:681600.
     *
     * Ungated (no dead/dizzy guard in the decomp). Reads target_obj position when
     * target_obj != null (line 681626-681633, feeds owner facing only). Draws two
     * rg_random.Range(-1f, 1f) floats (lines 681639, 681644), builds a Vector2,
     * normalizes it (FUN_00fa1e04), and stores it as move_direction (line 681647).
     * Owner: anim.SetBool(StringLiteral_6550, true) at the tail (line 681653).
     * @return the new normalized move_direction (zero if both draws were zero).
     */
    glm::vec2 RunReflection();

    /**
     * @brief Shoot gate roll. FAITHFUL: EnemyAIShark__ShootReflection @
     *        game_full.c:681658.
     *
     * Gate (lines 681668-681673): runs ONLY while not dead (0x38) AND not dizzy
     * (0xA1). When active it draws the single rg_random.Range(0, 100) (line
     * 681676). The post-draw body is tail-call-truncated (FUN_010b7dcc, line
     * 681679) and is NOT recoverable -> not modelled. The draw itself keeps the
     * stream lockstep; the roll is returned for callers.
     * @return the rg_random.Range(0, 100) roll, or -1 when gated (no draw taken).
     */
    int ShootReflection();

    /**
     * @brief Trigger a dash. FAITHFUL: EnemyAIShark__Dash @ game_full.c:681686.
     *
     * can_shoot = false (0x40 = 0, line 681693). Owner: Invoke(StringLiteral_6552,
     * shoot_cd@0x3C) to schedule the dash coroutine (line 681694), and
     * anim.SetBool(StringLiteral_6975, true) (line 681700). No RNG.
     * @param outDashDelay filled with the shoot_cd value (Invoke delay).
     * @param shootCd      this enemy's shoot_cd field (0x3C) value.
     */
    void Dash(float &outDashDelay, float shootCd);

    /**
     * @brief One step of the Dashing coroutine state machine. FAITHFUL:
     *        EnemyAIShark_<Dashing>c__Iterator0__MoveNext @ game_full.c:681870.
     *
     * On the first step (state == 0) it inits the wave timer to 0.0 (iterator +8,
     * line 681896) and the interval to 0.2 (iterator +0xC, line 681897). On each
     * subsequent step (state == 1) it advances timer += interval (line 681890).
     * After the timer update, while the enemy object still exists AND timer < 1.5
     * (line 681905) it emits a Wave (EnemyAIShark__Wave, line 681907) and the
     * coroutine continues (yields WaitForSeconds); otherwise the coroutine ends.
     * "Alive" here mirrors the op_Implicit(self) existence check; we gate the Wave
     * on not-dead so a destroyed shark stops waving.
     * @return true if a Wave should be emitted this step and the coroutine
     *         continues; false when the coroutine terminates (no Wave).
     */
    bool DashingStep();

    /**
     * @brief External-impulse gate. FAITHFUL: EnemyAIShark__GetForce @
     *        game_full.c:681811.
     *
     * The base RGEController__GetForce (which stores force_direction and clamps
     * the magnitude to 28, into inertial_vel) is applied ONLY when not dashing
     * (0xB1 == 0, line 681814). While dashing the impulse is ignored. This models
     * the gate + the base clamp; the awake/server gates of the base are the
     * caller's concern.
     * @param value    incoming impulse magnitude (pre-clamp).
     * @return the magnitude that would be written to inertial_vel (clamped to 28),
     *         or the unchanged current inertial_vel when dashing (impulse ignored).
     */
    float GetForce(float value);

    /// Base knockback magnitude cap (RGEController__GetForce hard cap of 28).
    static constexpr float kForceCap = 28.0F;

    /**
     * @brief One FixedUpdate physics step. FAITHFUL: EnemyAIShark__FixedUpdate @
     *        game_full.c:681339.
     *
     * Models only the pure scalar/branch logic, not the Rigidbody2D writes. The
     * whole body is gated on awake (0x18, line 681368): not awake -> complete
     * no-op. When awake the outcome is one of three states (see StepResult): on
     * the inertial_vel <= 1.0 path either DeadClear (dead -> clears awake) or Steer
     * (alive), and on the inertial_vel > 1.0 path Knockback (decays by friction).
     * @param friction the friction field (0x50), inertia decay multiplier.
     * @return the StepResult naming which outcome occurred.
     */
    StepResult FixedUpdateStep(float friction);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};

    bool m_Dead = false;     // 0x38
    bool m_Dizzy = false;    // 0xA1
    bool m_CanShoot = true;  // 0x40
    bool m_Dashing = false;  // 0xB1 (dash-active / knockback-immune flag)
    bool m_Awake = true;     // 0x18
    bool m_HasTarget = false; // target_obj != null (0x7C)

    float m_InertialVel = 0.0F;             // 0x44
    glm::vec2 m_MoveDirection{0.0F, 0.0F};  // 0x74

    // <Dashing>c__Iterator0 accumulator fields.
    float m_WaveTimer = 0.0F;    // iterator +8
    float m_WaveInterval = 0.0F; // iterator +0xC
    bool m_DashingStarted = false; // tracks state 0 (init) vs state 1 (advance).
};

} // namespace Game

#endif /* GAME_ENEMY_AI_SHARK_HPP */
