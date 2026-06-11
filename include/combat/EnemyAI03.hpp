#ifndef GAME_ENEMY_AI03_HPP
#define GAME_ENEMY_AI03_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class EnemyAI03
 * @brief Faithful decision/cadence brain for EnemyAI03 (an RGEController subclass).
 *
 * Per-content port: the pure, unit-testable logic layer over the deterministic
 * RGEController/RGRandom base. EnemyAI03 is a "stand-and-shoot reflector": it
 * scouts, then on a re-roll re-picks a random wander direction (RunReflection),
 * and when it shoots it freezes in place (stand_in_shooting) and schedules a
 * stop. Modelled here (all pure; no Unity types):
 *   - Scout(): the gate (skip while dead or dizzy) + the single advancing
 *     rg_random.Range(0, 10) idle re-roll draw; clears the chase target.
 *   - RunReflection(): the random wander direction (two rg_random.Range(-1, 1)
 *     float draws, then normalized) written to move_direction.
 *   - ShootReflection(): the shoot-state latch (shooting = true, can_shoot =
 *     false; clear weapon_lock_target only when not lock_in_shooting) and the
 *     two scheduled delays (shoot_time, shoot_cd). No RNG draws here.
 *   - StopShooting(): the inverse latch (shooting = false, weapon_lock_target =
 *     true).
 *   - Dizzy(): the stun latch (dizzy = true) gated on not-dead.
 *   - FixedUpdate physics: gated on awake (0x18) -- a not-awake step is a
 *     complete no-op. When awake there are three states: stand_in_shooting &&
 *     shooting freezes movement (no steer, no decay); otherwise an active
 *     knockback impulse (inertial_vel > 1.0) replaces steering, decays by
 *     friction, and early-returns; otherwise normal steering applies.
 *
 * Owner concerns (NOT modelled, by design): Animator.SetBool("walk"), the
 * RGEHand.SetAttack toggles, Rigidbody2D velocity writes, and the
 * MonoBehaviour.Invoke("StopShooting"/"ShootReflection") scheduling. Those are
 * referenced in comments at their decomp sites.
 *
 * @see IL2CPP skeleton EnemyAI03.cs (shoot_time @0xAC, stand_in_shooting @0xB0,
 *      lock_in_shooting @0xB1); FAITHFUL: EnemyAI03 @ game_full.c:676732-677115.
 */
class EnemyAI03 {
public:
    /**
     * @brief Outcome of one FixedUpdateStep, mirroring the decomp's branch tree
     *        (EnemyAI03__FixedUpdate @ game_full.c:676754-676874).
     *
     * The whole body is gated on awake (0x18). When awake, there are exactly
     * three mutually exclusive movement outcomes:
     *   - Asleep:    awake (0x18) == 0 -> entire body skipped; nothing happens
     *                (NO friction decay, NO velocity write). Line 676785.
     *   - Frozen:    stand_in_shooting (0xB0) && shooting (0x80) -> the whole
     *                not-frozen block (676790-676870) is skipped; no steering and
     *                no decay. Gate at line 676790.
     *   - Knockback: not frozen AND inertial_vel (0x44) > 1.0 -> the knockback
     *                velocity replaces steering, inertial_vel *= friction (0x50)
     *                (line 676825), then the function EARLY-RETURNS (line 676826)
     *                so the plain steering block (676846-676870) does NOT run.
     *   - Steer:     not frozen AND inertial_vel <= 1.0 -> normal steering
     *                (the plain block at 676846-676870 applies).
     */
    enum class StepResult {
        Asleep,    ///< awake == 0: no-op (no decay, no velocity write).
        Frozen,    ///< stand_in_shooting && shooting: no steer, no decay.
        Knockback, ///< not frozen, inertial_vel > 1.0: knockback + decay, early return.
        Steer      ///< not frozen, inertial_vel <= 1.0: normal steering.
    };

    /// Idle re-roll ceiling drawn by Scout (rg_random.Range(0, 10), max EXCL).
    static constexpr int kScoutRerollCeiling = 10;
    /// Wander component bounds (rg_random.Range(-1f, 1f) x2, max INCL).
    static constexpr float kWanderMin = -1.0F;
    static constexpr float kWanderMax = 1.0F;
    /// inertial_vel must exceed this for the knockback branch to apply (1.0 <).
    static constexpr float kKnockbackThreshold = 1.0F;

    EnemyAI03() = default;

    /// Seed this enemy's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- mutable state (owning entity mirrors these into the real fields) ----
    bool Dead() const { return m_Dead; }
    void SetDead(bool dead) { m_Dead = dead; }

    bool Dizzy() const { return m_Dizzy; }   // field 0xA1
    bool Shooting() const { return m_Shooting; } // field 0x80
    bool CanShoot() const { return m_CanShoot; } // field 0x40
    bool WeaponLockTarget() const { return m_WeaponLockTarget; } // field 0x1C

    bool StandInShooting() const { return m_StandInShooting; } // field 0xB0
    void SetStandInShooting(bool v) { m_StandInShooting = v; }
    bool LockInShooting() const { return m_LockInShooting; }   // field 0xB1
    void SetLockInShooting(bool v) { m_LockInShooting = v; }

    /// Last wander direction chosen by RunReflection (move_direction, 0x74).
    glm::vec2 MoveDirection() const { return m_MoveDirection; }
    /// Whether the enemy currently has a chase target (target_obj, 0x7C).
    bool HasTarget() const { return m_HasTarget; }

    float InertialVel() const { return m_InertialVel; } // field 0x44

    /**
     * @brief Target-acquisition tick. FAITHFUL: EnemyAI03__Scout @ game_full.c:676878.
     *
     * Gated: does nothing while dead (0x38) or dizzy (0xA1). When active, clears
     * the chase target (target_obj = null, 0x7C) and advances the deterministic
     * stream with the single rg_random.Range(0, 10) idle re-roll (line 676897).
     * The original's tail (re-detect / re-target) is tail-call-truncated in the
     * decomp (FUN_010b7dcc) and is NOT modelled. Returns the draw so callers can
     * branch on it if needed; the draw itself is what keeps the stream lockstep.
     * @return the rg_random.Range(0, 10) roll, or -1 when gated (no draw taken).
     */
    int Scout();

    /**
     * @brief Re-pick a random wander direction. FAITHFUL: EnemyAI03__RunReflection
     *        @ game_full.c:676907.
     *
     * Draws two rg_random.Range(-1f, 1f) floats (lines 676946, 676951), builds a
     * Vector2 and normalizes it (FUN_00fa1e04), then stores it as move_direction
     * (0x74). Owner: anim.SetBool("walk", true) at the tail (line 676960). The
     * preceding target_obj position read (line 676933-676940) feeds only the
     * owner's facing and is not part of the pure draw logic.
     * @return the new normalized move_direction (zero if both draws were zero).
     */
    glm::vec2 RunReflection();

    /**
     * @brief Begin a shot: freeze and latch shoot state. FAITHFUL:
     *        EnemyAI03__ShootReflection @ game_full.c:676965.
     *
     * Gated: does nothing while dead (0x38) or dizzy (0xA1). When active:
     * can_shoot = false (0x40), shooting = true (0x80), and weapon_lock_target
     * (0x1C) is cleared ONLY when lock_in_shooting (0xB1) is false. No RNG draws.
     * Owner: zero rigidbody velocity, RGEHand.SetAttack(true), and the two
     * Invokes -- Invoke("StopShooting", shoot_time@0xAC) and
     * Invoke("ShootReflection", shoot_cd@0x3C) (lines 677006-677007). The two
     * scheduled delays are returned via out-params for the owner to schedule.
     * @param outShootTime   filled with shoot_time (StopShooting delay).
     * @param outShootCd      filled with shoot_cd (ShootReflection re-fire delay).
     * @param shootTime       this enemy's shoot_time field value.
     * @param shootCd         this enemy's shoot_cd field value.
     * @return true if the shot latched (was not gated).
     */
    bool ShootReflection(float &outShootTime, float &outShootCd, float shootTime,
                         float shootCd);

    /**
     * @brief End a shot. FAITHFUL: EnemyAI03__StopShooting @ game_full.c:677015.
     * shooting = false (0x80), weapon_lock_target = true (0x1C). Owner:
     * RGEHand.SetAttack(false) on the hand (0x6C). No gate, no RNG.
     */
    void StopShooting();

    /**
     * @brief Get stunned. FAITHFUL: EnemyAI03__Dizzy @ game_full.c:677030.
     * Gated on not-dead (0x38): latches dizzy = true (0xA1). Owner: HitBack()
     * flash and anim.SetBool("walk", false) (line 677045). No RNG.
     * @return true if the stun latched (was not dead).
     */
    bool ApplyDizzy();

    /// Clear the stun (recovery; mirrors the base EndDizzy resume path).
    void ClearDizzy() { m_Dizzy = false; }

    /**
     * @brief One FixedUpdate physics step. FAITHFUL: EnemyAI03__FixedUpdate @
     *        game_full.c:676754.
     *
     * Models only the pure scalar/branch logic, not the Rigidbody2D writes. The
     * whole body is gated on awake (0x18, line 676785): when not awake the step
     * is a complete no-op (NO friction decay, NO velocity write). When awake, the
     * outcome is one of three mutually exclusive states:
     *   - Frozen    while stand_in_shooting (0xB0) && shooting (0x80) (the gate
     *               at line 676790): the not-frozen block is skipped entirely, so
     *               there is no steering AND no decay.
     *   - Knockback while not frozen AND inertial_vel (0x44) > 1.0 (line 676791):
     *               the knockback velocity replaces steering, inertial_vel is
     *               decayed by friction (0x50): inertial_vel *= friction (line
     *               676825), and the function EARLY-RETURNS (line 676826) -- the
     *               plain steering block (676846-676870) does NOT run.
     *   - Steer     while not frozen AND inertial_vel <= 1.0: normal steering
     *               applies (the plain block at 676846-676870).
     * @param awake    the awake field (0x18); false short-circuits to Asleep.
     * @param friction the friction field (0x50), inertia decay multiplier.
     * @return the StepResult naming which of the four outcomes occurred.
     */
    StepResult FixedUpdateStep(bool awake, float friction);

    /// Seed/clear knockback (owner calls when GetForce lands; clamp is base 28).
    void SetInertialVel(float vel) { m_InertialVel = vel; }

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};

    bool m_Dead = false;             // 0x38
    bool m_Dizzy = false;            // 0xA1
    bool m_Shooting = false;         // 0x80
    bool m_CanShoot = true;          // 0x40
    bool m_WeaponLockTarget = false; // 0x1C
    bool m_StandInShooting = false;  // 0xB0
    bool m_LockInShooting = false;   // 0xB1
    bool m_HasTarget = false;        // target_obj != null (0x7C)

    float m_InertialVel = 0.0F;      // 0x44
    glm::vec2 m_MoveDirection{0.0F, 0.0F}; // 0x74
};

} // namespace Game

#endif /* GAME_ENEMY_AI03_HPP */
