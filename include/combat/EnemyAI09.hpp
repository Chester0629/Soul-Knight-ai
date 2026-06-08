#ifndef GAME_ENEMY_AI09_HPP
#define GAME_ENEMY_AI09_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class EnemyAI09
 * @brief Faithful decision/cadence brain for EnemyAI09 (an RGEController subclass).
 *
 * Per-content port: the pure, unit-testable logic layer over the deterministic
 * RGEController/RGRandom base. EnemyAI09 is a melee "charger/reflector": it
 * scouts (clears its chase target), on a re-roll re-picks a random wander
 * direction (RunReflection), and on ShootReflection latches into its attack
 * cadence by disabling can_shoot and scheduling the attack Invoke. Modelled here
 * (all pure; no Unity types):
 *   - FixedUpdate physics: gated on awake (0x18). When awake there are three
 *     mutually exclusive outcomes that branch on inertial_vel (0x44):
 *       * inertial_vel <= 1.0 AND dead (0x38): a one-time "dead stop" -- awake is
 *         cleared (0x18 = 0) and velocity is zeroed (owner). NO friction decay.
 *       * inertial_vel <= 1.0 AND not dead: normal steering. NO decay.
 *       * inertial_vel > 1.0: knockback impulse is ADDED on top of steering and
 *         inertial_vel is decayed by friction (0x50): inertial_vel *= friction.
 *     (Note: unlike EnemyAI03 this FixedUpdate does NOT early-return on the
 *     knockback branch -- steering and knockback are composited -- and the dead
 *     handling lives INSIDE the inertial_vel<=1.0 branch.)  ZERO RNG draws.
 *   - Scout(): gate (skip while dizzy 0xA1 or dead 0x38); when active clears the
 *     chase target (target_obj = null, 0x7C). NO RNG draw.
 *   - RunReflection(): two rg_random.Range(-1f, 1f) float draws, then normalized,
 *     written to move_direction (0x74). The two draws are the only RNG in the
 *     class.
 *   - ShootReflection(): gate (skip while dead 0x38 or dizzy 0xA1); when active
 *     can_shoot = false (0x40) and the attack Invoke is scheduled with shoot_cd
 *     (0x3C). NO RNG draw.
 *   - GetForce(): gated knockback intake -- the base RGEController.GetForce (which
 *     clamps the impulse magnitude to 28) only runs while can_hit (0xB1) is
 *     false; while can_hit is true the force is ignored.
 *
 * Owner concerns (NOT modelled, by design): Animator.SetBool("run"/"atk"),
 * Rigidbody2D velocity writes, Transform position/facing reads, the
 * MonoBehaviour.Invoke("ShootReflection") scheduling, ChildDead/DeadEvent
 * Instantiate, FixedRotation (transform aim) and OnTriggerEnter2D. Those are
 * referenced in comments at their decomp sites.
 *
 * @see IL2CPP skeleton EnemyAI09.cs (damage, can_hit @0xB1, in_atk1, later_time
 *      @0xB4, dead_obj @0xBC); FAITHFUL: EnemyAI09 @ game_full.c:678509-678931.
 */
class EnemyAI09 {
public:
    /**
     * @brief Outcome of one FixedUpdateStep, mirroring the decomp's branch tree
     *        (EnemyAI09__FixedUpdate @ game_full.c:678509-678614).
     *
     * The whole body is gated on awake (0x18, line 678538). When awake, the
     * branch is on inertial_vel (0x44, line 678539):
     *   - Asleep:   awake (0x18) == 0 -> entire body skipped; nothing happens
     *               (NO decay, NO velocity write).
     *   - DeadStop: awake AND inertial_vel <= 1.0 AND dead (0x38) (line 678540)
     *               -> awake is cleared (0x18 = 0, line 678541) and velocity is
     *               zeroed (owner). NO friction decay.
     *   - Steer:    awake AND inertial_vel <= 1.0 AND not dead -> normal steering
     *               (lines 678554-678578). NO decay.
     *   - Knockback: awake AND inertial_vel > 1.0 (line 678580) -> knockback
     *               impulse is composited on top of steering and inertial_vel is
     *               decayed by friction (0x50): inertial_vel *= friction (line
     *               678610). (No early return -- it composites, unlike AI03.)
     */
    enum class StepResult {
        Asleep,    ///< awake == 0: no-op (no decay, no velocity write).
        DeadStop,  ///< inertial_vel <= 1.0 && dead: clears awake, zeroes vel, no decay.
        Steer,     ///< inertial_vel <= 1.0 && not dead: normal steering, no decay.
        Knockback  ///< inertial_vel > 1.0: knockback composited + decay (no early return).
    };

    /// Wander component bounds (rg_random.Range(-1f, 1f) x2, max INCLUSIVE).
    static constexpr float kWanderMin = -1.0F;
    static constexpr float kWanderMax = 1.0F;
    /// inertial_vel must exceed this for the knockback branch to apply (1.0 <).
    static constexpr float kKnockbackThreshold = 1.0F;

    EnemyAI09() = default;

    /// Seed this enemy's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- mutable state (owning entity mirrors these into the real fields) ----
    bool Dead() const { return m_Dead; }     // field 0x38
    void SetDead(bool dead) { m_Dead = dead; }

    bool Dizzy() const { return m_Dizzy; }   // field 0xA1
    void SetDizzy(bool v) { m_Dizzy = v; }

    bool CanShoot() const { return m_CanShoot; } // field 0x40

    bool CanHit() const { return m_CanHit; } // field 0xB1
    void SetCanHit(bool v) { m_CanHit = v; }

    /// Last wander direction chosen by RunReflection (move_direction, 0x74).
    glm::vec2 MoveDirection() const { return m_MoveDirection; }
    /// Whether the enemy currently has a chase target (target_obj, 0x7C).
    bool HasTarget() const { return m_HasTarget; }
    void SetHasTarget(bool v) { m_HasTarget = v; }

    bool Awake() const { return m_Awake; }   // field 0x18
    void SetAwake(bool v) { m_Awake = v; }

    float InertialVel() const { return m_InertialVel; } // field 0x44

    /**
     * @brief Target-acquisition tick. FAITHFUL: EnemyAI09__Scout @ game_full.c:678641.
     *
     * Gate (lines 678651-678656): does nothing while dizzy (0xA1) or dead (0x38).
     * The decomp computes bVar2 = (dizzy == 0); if bVar2 then cVar1 = dead; the
     * body runs only when (bVar2 && cVar1 == 0), i.e. !dizzy && !dead. When
     * active, clears the chase target (target_obj = null, 0x7C, line 678657). The
     * tail (re-detect / re-target) is tail-call-truncated in the decomp
     * (get_transform) and is NOT modelled. NO RNG draw.
     * @return true if active (target cleared); false if gated.
     */
    bool Scout();

    /**
     * @brief Re-pick a random wander direction. FAITHFUL: EnemyAI09__RunReflection
     *        @ game_full.c:678666.
     *
     * Draws two rg_random.Range(-1f, 1f) floats (lines 678705, 678710), builds a
     * Vector2 and normalizes it (FUN_00fa1e04), then stores it as move_direction
     * (set_move_direction, line 678713). Owner: anim.SetBool("run", true) at the
     * tail (line 678719). The preceding target_obj position read (lines
     * 678692-678699) feeds only the owner's facing and is not part of the pure
     * draw logic. No gate: the two draws always happen here.
     * @return the new normalized move_direction (zero if both draws were zero).
     */
    glm::vec2 RunReflection();

    /**
     * @brief Begin the attack cadence. FAITHFUL: EnemyAI09__ShootReflection @
     *        game_full.c:678724.
     *
     * Gate (lines 678734-678741): the decomp computes bVar2 = (dead == 0); if
     * bVar2 then cVar1 = dizzy; it returns when (!bVar2 || cVar1 != 0), i.e. when
     * dead OR dizzy. When active (not dead AND not dizzy): can_shoot = false (0x40,
     * line 678742). Owner: Invoke("ShootReflection", shoot_cd@0x3C) (line 678743)
     * and anim.SetBool (line 678746). The scheduled delay is returned via an
     * out-param for the owner to schedule. No RNG draw.
     * @param outShootCd filled with shoot_cd (the ShootReflection re-fire delay).
     * @param shootCd    this enemy's shoot_cd field value (0x3C).
     * @return true if the cadence latched (was not gated).
     */
    bool ShootReflection(float &outShootCd, float shootCd);

    /**
     * @brief One FixedUpdate physics step. FAITHFUL: EnemyAI09__FixedUpdate @
     *        game_full.c:678509.
     *
     * Models only the pure scalar/branch logic, not the Rigidbody2D writes. Gated
     * on awake (0x18, line 678538): not awake -> complete no-op (NO decay, NO
     * velocity write). When awake the branch is on inertial_vel (0x44, line
     * 678539):
     *   - inertial_vel <= 1.0 && dead (0x38, line 678540): the "dead stop" path --
     *     awake is cleared (0x18 = 0, line 678541) and velocity is zeroed (owner);
     *     no decay. Returns DeadStop.
     *   - inertial_vel <= 1.0 && not dead: normal steering (lines 678554-678578);
     *     no decay. Returns Steer.
     *   - inertial_vel > 1.0 (line 678580): knockback impulse composited onto
     *     steering and inertial_vel decayed by friction (0x50): inertial_vel *=
     *     friction (line 678610). NO early return. Returns Knockback.
     * @param friction the friction field (0x50), inertia decay multiplier.
     * @return the StepResult naming which of the four outcomes occurred.
     */
    StepResult FixedUpdateStep(float friction);

    /**
     * @brief Knockback intake gate. FAITHFUL: EnemyAI09__GetForce @ game_full.c:678815.
     *
     * The base RGEController.GetForce (which clamps the impulse magnitude to 28)
     * is only invoked while can_hit (0xB1) is false (line 678818). While can_hit
     * is true the incoming force is ignored. This models only the gate decision;
     * the owner applies the actual clamped impulse to inertial_vel/force_direction
     * when the gate passes.
     * @return true if the base GetForce should run (can_hit == false).
     */
    bool ShouldApplyForce() const;

    /// Seed/clear knockback (owner calls when GetForce lands; clamp is base 28).
    void SetInertialVel(float vel) { m_InertialVel = vel; }

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};

    bool m_Awake = true;       // 0x18
    bool m_Dead = false;       // 0x38
    bool m_Dizzy = false;      // 0xA1
    bool m_CanShoot = true;    // 0x40
    bool m_CanHit = false;     // 0xB1
    bool m_HasTarget = false;  // target_obj != null (0x7C)

    float m_InertialVel = 0.0F;            // 0x44
    glm::vec2 m_MoveDirection{0.0F, 0.0F}; // 0x74
};

} // namespace Game

#endif /* GAME_ENEMY_AI09_HPP */
