#ifndef GAME_ENEMY_AI11_HPP
#define GAME_ENEMY_AI11_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class EnemyAI11
 * @brief Faithful decision/cadence brain for EnemyAI11 (an RGEController subclass).
 *
 * Per-content port: the pure, unit-testable logic layer over the deterministic
 * RGEController/RGRandom base. EnemyAI11 is a "wander-and-burst" enemy: it
 * scouts (clearing its chase target), re-picks a random wander direction
 * (RunReflection), and on a shoot freezes its movement and arms an atk_count
 * burst counter that OnAtk drains. Modelled here (all pure; no Unity types):
 *   - FixedUpdateStep(): the awake (0x18) gate + the inertial_vel (0x44) split.
 *     A not-awake step is a complete no-op. When awake there are three states
 *     for inertial_vel <= 1.0 -- a dead (0x38) sub-branch that latches awake = 0
 *     and exits (no decay), otherwise normal steering (no decay) -- and the
 *     knockback branch (inertial_vel > 1.0) that decays inertial_vel *= friction
 *     (0x50, line 679621) and composes the impulse term. ZERO RNG draws.
 *   - Scout(): the gate (skip while dizzy (0xA1) or dead (0x38)) + clear the
 *     chase target (target_obj = null, 0x7C). This override takes NO RNG draw
 *     (unlike the base Scout's Range(0,10)); its tail is get_transform-truncated.
 *   - RunReflection(): the random wander direction (two rg_random.Range(-1, 1)
 *     float draws @ lines 679674/679679, then normalized) written to
 *     move_direction (0x74).
 *   - ShootReflection(): the shoot latch gated on not-dead && not-dizzy --
 *     can_shoot = false (0x40), atk_count = 5 (0xC4), move_direction = zero,
 *     weapon_lock_target = false (0x1C); arms the burst and schedules the
 *     shoot_cd (0x3C) Invoke. ZERO RNG draws.
 *   - OnAtk(): the burst-spawn decision -- the extra bullet01 spawn is gated on
 *     atk_count != 0 (0xC4); bullet02 always spawns. No state write, ZERO draws.
 *   - DrainBurst(): the recovered atk_count-- decrement from the Invoke callback
 *     (FUN_007f7928 r9==2 path, line 679796). Dispatch condition unrecovered ->
 *     TODO[verify].
 *   - ChildDead(): the respawn-Invoke decision, gated on dead_obj != null (0xC0).
 *
 * Owner concerns (NOT modelled, by design): Rigidbody2D velocity writes,
 * Animator.SetBool/SetTrigger, MonoBehaviour.Invoke scheduling, Object.Instantiate
 * of bullet01/bullet02/dead_obj, RGMusicManager.PlayEffect, and the transform /
 * position reads in Scout / FixedRotation / DeadEvent. Those are referenced in
 * comments at their decomp sites.
 *
 * @see IL2CPP skeleton EnemyAI11.cs (damage, later_time @0xB0, atk1_cilp @0xB4,
 *      bullet01 @0xB8, bullet02 @0xBC, dead_obj @0xC0, atk_count @0xC4).
 *      FAITHFUL: EnemyAI11 @ game_full.c:679520-679909.
 */
class EnemyAI11 {
public:
    /**
     * @brief Outcome of one FixedUpdateStep, mirroring the decomp's branch tree
     *        (EnemyAI11__FixedUpdate @ game_full.c:679520-679624).
     *
     * The whole body is gated on awake (0x18, line 679549). When awake, the
     * outer branch splits on inertial_vel (0x44):
     *   - Asleep:    awake (0x18) == 0 -> entire body skipped; nothing happens
     *                (NO friction decay, NO velocity write). Line 679549.
     *   - Dead:      inertial_vel <= 1.0 AND dead (0x38) != 0 -> latch awake = 0
     *                (line 679552), zero velocity, then get_transform-truncated
     *                exit: the steering block does NOT run, NO decay. Line 679551.
     *   - Steer:     inertial_vel <= 1.0 AND not dead -> normal steering (the
     *                block at 679565-679589). NO decay.
     *   - Knockback: inertial_vel (0x44) > 1.0 -> the knockback impulse is added
     *                to steering and inertial_vel *= friction (0x50, line 679621).
     *                This is the ONLY decay site. (The dead check is NOT made on
     *                this branch -- it lives only inside the <= 1.0 branch.)
     */
    enum class StepResult {
        Asleep,    ///< awake == 0: no-op (no decay, no velocity write).
        Dead,      ///< inertial_vel <= 1.0 && dead: latch awake = 0, exit, no decay.
        Steer,     ///< inertial_vel <= 1.0 && not dead: normal steering, no decay.
        Knockback  ///< inertial_vel > 1.0: knockback impulse + friction decay.
    };

    /// Wander component bounds (rg_random.Range(-1f, 1f) x2, max INCLUSIVE).
    static constexpr float kWanderMin = -1.0F;
    static constexpr float kWanderMax = 1.0F;
    /// inertial_vel must exceed this for the knockback branch to apply (1.0 <).
    static constexpr float kKnockbackThreshold = 1.0F;
    /// Burst size armed by ShootReflection (atk_count = 5, 0xC4, line 679712).
    static constexpr int kBurstCount = 5;

    EnemyAI11() = default;

    /// Seed this enemy's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- mutable state (owning entity mirrors these into the real fields) ----
    bool Awake() const { return m_Awake; }      // 0x18
    void SetAwake(bool v) { m_Awake = v; }

    bool Dead() const { return m_Dead; }         // 0x38
    void SetDead(bool dead) { m_Dead = dead; }

    bool Dizzy() const { return m_Dizzy; }       // 0xA1
    void SetDizzy(bool v) { m_Dizzy = v; }

    bool CanShoot() const { return m_CanShoot; } // 0x40
    bool WeaponLockTarget() const { return m_WeaponLockTarget; } // 0x1C

    /// Whether the enemy currently has a chase target (target_obj, 0x7C).
    bool HasTarget() const { return m_HasTarget; }
    void SetHasTarget(bool v) { m_HasTarget = v; }

    /// Last wander direction chosen by RunReflection (move_direction, 0x74).
    glm::vec2 MoveDirection() const { return m_MoveDirection; }

    /// Remaining burst rounds (atk_count, 0xC4).
    int AtkCount() const { return m_AtkCount; }

    float InertialVel() const { return m_InertialVel; } // 0x44
    /// Seed/clear knockback (owner calls when GetForce lands; clamp is base 28).
    void SetInertialVel(float vel) { m_InertialVel = vel; }

    /**
     * @brief One FixedUpdate physics step. FAITHFUL: EnemyAI11__FixedUpdate @
     *        game_full.c:679520.
     *
     * Models only the pure scalar/branch logic, not the Rigidbody2D writes. The
     * whole body is gated on awake (0x18, line 679549): not awake -> complete
     * no-op (NO decay, NO velocity write). When awake the outer branch splits on
     * inertial_vel (0x44, line 679550):
     *   - inertial_vel <= 1.0 AND dead (0x38, line 679551): latch awake = 0
     *     (line 679552), zero velocity, then exit (get_transform-truncated). The
     *     steering block does NOT run and there is NO decay. -> StepResult::Dead.
     *   - inertial_vel <= 1.0 AND not dead: normal steering. -> StepResult::Steer.
     *     NO decay.
     *   - inertial_vel > 1.0: knockback impulse added to steering and
     *     inertial_vel *= friction (0x50, line 679621). -> StepResult::Knockback.
     * @param friction the friction field (0x50), inertia decay multiplier.
     * @return the StepResult naming which of the four outcomes occurred.
     */
    StepResult FixedUpdateStep(float friction);

    /**
     * @brief Target-acquisition tick. FAITHFUL: EnemyAI11__Scout @
     *        game_full.c:679629.
     *
     * Gated: does nothing while dizzy (0xA1) or dead (0x38) -- the decomp tests
     * dizzy first, then dead (lines 679639-679644). When active, clears the chase
     * target (target_obj = null, 0x7C, line 679645). This override's tail is
     * get_transform-truncated and takes NO rg_random draw (it does NOT reproduce
     * the base Scout's Range(0,10)). Returns whether it ran.
     * @return true if the tick ran (was not gated).
     */
    bool Scout();

    /**
     * @brief Re-pick a random wander direction. FAITHFUL:
     *        EnemyAI11__RunReflection @ game_full.c:679654.
     *
     * Draws two rg_random.Range(-1f, 1f) floats (lines 679674, 679679), builds a
     * Vector2 and normalizes it, then stores it as move_direction (0x74, line
     * 679682). Owner: anim.SetBool("...", true) at the tail (line 679688).
     * @return the new normalized move_direction (zero if both draws were zero).
     */
    glm::vec2 RunReflection();

    /**
     * @brief Begin a burst shot: latch shoot state + arm atk_count. FAITHFUL:
     *        EnemyAI11__ShootReflection @ game_full.c:679693.
     *
     * Gated: does nothing while dead (0x38) or dizzy (0xA1) -- the decomp tests
     * dead first, then dizzy (lines 679705-679710). When active:
     * can_shoot = false (0x40, line 679711), atk_count = 5 (0xC4, line 679712),
     * move_direction = Vector2.zero (line 679719), weapon_lock_target = false
     * (0x1C, line 679731). No RNG draws. Owner: Invoke("...", shoot_cd@0x3C)
     * (line 679720) and the two anim.SetTrigger calls (lines 679725, 679730). The
     * scheduled shoot_cd delay is returned via out-param for the owner.
     * @param outShootCd filled with shoot_cd (the burst-fire Invoke delay).
     * @param shootCd    this enemy's shoot_cd (0x3C) field value.
     * @return true if the shot latched (was not gated).
     */
    bool ShootReflection(float &outShootCd, float shootCd);

    /**
     * @brief Burst-spawn decision. FAITHFUL: EnemyAI11__OnAtk @
     *        game_full.c:679740.
     *
     * Pure decision only -- OnAtk writes NO state and takes NO RNG draw. The
     * extra bullet01 (0xB8) spawn is gated on atk_count != 0 (0xC4, line 679749);
     * bullet02 (0xBC) is ALWAYS spawned (lines 679771-679780). Owner:
     * Object.Instantiate of both prefabs + GetComponent<RGBullet>.
     * @return true if bullet01 should also spawn this call (atk_count != 0).
     */
    bool OnAtkSpawnsExtraBullet() const { return m_AtkCount != 0; }

    /**
     * @brief Drain one burst round. FAITHFUL: FUN_007f7928 (the Invoke callback,
     *        r9 == 2 path) @ game_full.c:679796: atk_count = atk_count - 1.
     *
     * The decomp body is a Ghidra-fragmented dispatch (uninitialised r8/r9
     * registers); only the r9 == 2 arm is recoverable and it performs a single
     * atk_count-- before re-Invoking + PlayEffect (owner). The dispatch condition
     * is NOT recoverable -> see fabricationFlags. Models ONLY the decrement.
     * @return the atk_count value AFTER the decrement.
     */
    int DrainBurst();

    /**
     * @brief Multi-part death respawn decision. FAITHFUL: EnemyAI11__ChildDead @
     *        game_full.c:679860.
     *
     * Pure decision only -- ChildDead writes NO state and takes NO RNG draw. The
     * respawn Invoke (StringLiteral_6965, later_time@0xB0) fires only when
     * dead_obj != null (0xC0, line 679874); otherwise it returns. Owner:
     * MonoBehaviour.Invoke("...", later_time).
     * @param hasDeadObj whether dead_obj (0xC0) is non-null.
     * @return true if the respawn Invoke should be scheduled.
     */
    bool ChildDead(bool hasDeadObj) const { return hasDeadObj; }

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
    int m_AtkCount = 0;                    // 0xC4
};

} // namespace Game

#endif /* GAME_ENEMY_AI11_HPP */
