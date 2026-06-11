#ifndef GAME_ENEMY_AI14_HPP
#define GAME_ENEMY_AI14_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class EnemyAI14
 * @brief Faithful decision/cadence brain for EnemyAI14 (an RGEController subclass).
 *
 * EnemyAI14 is the "laser enemy": it scouts (just dropping its chase target),
 * re-picks a random wander direction (RunReflection), and runs a laser-beam
 * attack cadence (ShootReflection -> OnAtk -> EndAtk) whose beam spawning and
 * scheduling are owner concerns. Modelled here (all pure; no Unity types):
 *   - Scout(): the gate (skip while dead (0x38) or dizzy (0xA1)) + the single
 *     state write it performs -- target_obj = null (0x7C). NO RNG draws.
 *   - RunReflection(): the random wander direction (two rg_random.Range(-1f, 1f)
 *     float draws, lines 680694/680699), normalized, stored as move_direction
 *     (0x74). NO gate (the decomp draws unconditionally). owner: anim.SetBool.
 *   - ShootReflection(): the shoot-state latch, gated on not-dead/not-dizzy:
 *     can_shoot = false (0x40) and weapon_lock_target = false (0x1C). NO RNG.
 *   - OnAtk(): gated on not-dead; move_direction = Vector2.zero (0x74). NO RNG.
 *   - EndAtk(): weapon_lock_target = true (0x1C). NO gate, NO RNG.
 *   - FixedUpdate physics: gated on awake (0x18). When awake, the inertial_vel
 *     (0x44) <= 1.0 branch contains the dead sub-branch (sets awake = 0 then
 *     early-returns), else normal steering; the inertial_vel > 1.0 branch is
 *     knockback steering with inertial_vel *= friction (0x50) decay. NO RNG.
 *
 * Owner concerns (NOT modelled, by design): Animator.SetBool/SetTrigger, the
 * RGWeapon/RGELaser Instantiate + beam spawn (OnAtk/DeadEvent), the
 * MonoBehaviour.Invoke scheduling (ShootReflection schedules OnAtk via shoot_cd
 * @0x3C, then "EndAtk" via 0.5), ReMoveLaser (childCount/GetChild/GetComponent,
 * pure-logic-free), FixedRotation (target_obj position read), DeadEvent
 * (Instantiate dead_obj), OnTriggerEnter2D (get_gameObject), and all Rigidbody2D
 * velocity writes. Those are referenced in comments at their decomp sites.
 *
 * @see IL2CPP skeleton EnemyAI14.cs (damage, can_hit, later_time, atk1_cilp,
 *      bullet01, dead_obj); FAITHFUL: EnemyAI14 @ game_full.c:680521-680912.
 */
class EnemyAI14 {
public:
    /**
     * @brief Outcome of one FixedUpdateStep, mirroring the decomp's branch tree
     *        (EnemyAI14__FixedUpdate @ game_full.c:680521-680626).
     *
     * The whole body is gated on awake (0x18, line 680550). When awake there are
     * two top-level branches on inertial_vel (0x44):
     *   - Asleep:   awake (0x18) == 0 -> entire body skipped; nothing happens
     *               (NO friction decay, NO awake clear). Gate at line 680550.
     *   - Dead:     inertial_vel <= 1.0 AND dead (0x38) != 0 (line 680552):
     *               sets awake = 0 (line 680553), zeroes velocity (owner), then
     *               EARLY-RETURNS via the get_transform tail (line 680564) so the
     *               steering block (680566-680590) does NOT run.
     *   - Steer:    inertial_vel <= 1.0 AND not dead -> normal steering
     *               (the plain block at 680566-680590). NO decay.
     *   - Knockback:inertial_vel > 1.0 (line 680551 false): knockback steering
     *               (680593-680621) replaces plain steering, then
     *               inertial_vel *= friction (0x50, line 680622).
     */
    enum class StepResult {
        Asleep,    ///< awake == 0: no-op (no decay, no awake clear).
        Dead,      ///< inertial_vel <= 1.0 && dead: clears awake, early return.
        Steer,     ///< inertial_vel <= 1.0 && not dead: normal steering, no decay.
        Knockback  ///< inertial_vel > 1.0: knockback steering + friction decay.
    };

    /// Wander component bounds (rg_random.Range(-1f, 1f) x2, max INCL).
    static constexpr float kWanderMin = -1.0F;
    static constexpr float kWanderMax = 1.0F;
    /// inertial_vel <= this takes the steer/dead branch; > this is knockback.
    static constexpr float kKnockbackThreshold = 1.0F;

    EnemyAI14() = default;

    /// Seed this enemy's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- mutable state (owning entity mirrors these into the real fields) ----
    bool Dead() const { return m_Dead; }              // field 0x38
    void SetDead(bool dead) { m_Dead = dead; }

    bool Dizzy() const { return m_Dizzy; }            // field 0xA1
    void SetDizzy(bool dizzy) { m_Dizzy = dizzy; }

    bool Awake() const { return m_Awake; }            // field 0x18
    void SetAwake(bool awakeFlag) { m_Awake = awakeFlag; }

    bool CanShoot() const { return m_CanShoot; }      // field 0x40
    bool WeaponLockTarget() const { return m_WeaponLockTarget; } // field 0x1C

    /// Whether the enemy currently has a chase target (target_obj, 0x7C).
    bool HasTarget() const { return m_HasTarget; }
    void SetHasTarget(bool has) { m_HasTarget = has; }

    /// Last wander direction chosen by RunReflection (move_direction, 0x74).
    glm::vec2 MoveDirection() const { return m_MoveDirection; }

    float InertialVel() const { return m_InertialVel; } // field 0x44
    /// Seed/clear knockback (owner calls when GetForce lands; clamp is base 28).
    void SetInertialVel(float vel) { m_InertialVel = vel; }

    /**
     * @brief Target-acquisition tick. FAITHFUL: EnemyAI14__Scout @
     *        game_full.c:680630.
     *
     * Gated: does nothing while dead (0x38) or dizzy (0xA1) (lines 680640-680644).
     * When active, the only state write the decomp performs is target_obj = null
     * (0x7C, line 680646). The tail get_transform (line 680648) is owner-only and
     * NOT modelled. There are NO rg_random draws in this method.
     * @return true if active (target cleared), false if gated.
     */
    bool Scout();

    /**
     * @brief Re-pick a random wander direction. FAITHFUL:
     *        EnemyAI14__RunReflection @ game_full.c:680655.
     *
     * Draws two rg_random.Range(-1f, 1f) floats (lines 680694, 680699), builds a
     * Vector2, normalizes it (FUN_00fa1e04), then stores it as move_direction
     * (set_move_direction, line 680702). The decomp draws UNCONDITIONALLY (no
     * dead/dizzy gate). The preceding target_obj position read (lines 680681-
     * 680688) feeds only the owner's facing and is not part of the draw logic.
     * owner: anim.SetBool("walk", true) at the tail (line 680708).
     * @return the new normalized move_direction (zero if both draws were zero).
     */
    glm::vec2 RunReflection();

    /**
     * @brief Begin the laser attack: latch shoot state. FAITHFUL:
     *        EnemyAI14__ShootReflection @ game_full.c:680713.
     *
     * Gated: does nothing while dead (0x38) or dizzy (0xA1) (lines 680723-680727).
     * When active: can_shoot = false (0x40, line 680729) and weapon_lock_target
     * = false (0x1C, line 680730). NO RNG draws. owner: Invoke("OnAtk",
     * shoot_cd@0x3C) (line 680731), anim.SetTrigger (line 680733), and
     * Invoke("EndAtk", 0.5f) (line 680734). The two scheduled delays are returned
     * via out-params for the owner to schedule.
     * @param outAtkDelay filled with shootCd (OnAtk delay, 0x3C).
     * @param outEndDelay filled with the literal 0.5 (EndAtk delay, 0x3f000000).
     * @param shootCd     this enemy's shoot_cd field value (0x3C).
     * @return true if the shot latched (was not gated).
     */
    bool ShootReflection(float &outAtkDelay, float &outEndDelay, float shootCd);

    /**
     * @brief Fire the laser. FAITHFUL: EnemyAI14__OnAtk @ game_full.c:680747.
     *
     * Gated: returns immediately while dead (0x38) (lines 680758-680760). When
     * active, the only state write the decomp performs is move_direction =
     * Vector2.zero (0x74, lines 680766-680767) -- the enemy plants itself to fire.
     * owner: Instantiate<RGWeapon>(bullet01) + GetComponent<RGELaser> (the beam,
     * lines 680773-680782). NO RNG draws.
     * @return true if active (move zeroed), false if gated by dead.
     */
    bool OnAtk();

    /**
     * @brief End the laser attack. FAITHFUL: EnemyAI14__EndAtk @
     *        game_full.c:680787.
     *
     * Calls ReMoveLaser (owner: tears down the active beam child), then sets
     * weapon_lock_target = true (0x1C, line 680791). NO gate, NO RNG. The trailing
     * indirect call (line 680794, jumptable "Could not recover") is owner/virtual
     * dispatch and is NOT modelled.
     */
    void EndAtk();

    /**
     * @brief One FixedUpdate physics step. FAITHFUL: EnemyAI14__FixedUpdate @
     *        game_full.c:680521.
     *
     * Models only the pure scalar/branch logic, not the Rigidbody2D writes. The
     * whole body is gated on awake (0x18, line 680550): when not awake the step is
     * a complete no-op (NO friction decay, NO awake clear). When awake:
     *   - inertial_vel (0x44) <= 1.0 (line 680551 true):
     *       * if dead (0x38) != 0 (line 680552): awake = 0 (line 680553), zero
     *         velocity (owner), then EARLY-RETURN (get_transform tail, line
     *         680564) -- the steering block does NOT run. -> StepResult::Dead.
     *       * else: normal steering (680566-680590), NO decay. -> StepResult::Steer.
     *   - inertial_vel > 1.0 (line 680551 false): knockback steering
     *     (680593-680621) then inertial_vel *= friction (0x50, line 680622).
     *     -> StepResult::Knockback.
     * @param friction the friction field (0x50), inertia decay multiplier.
     * @return the StepResult naming which of the four outcomes occurred.
     */
    StepResult FixedUpdateStep(float friction);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};

    bool m_Dead = false;             // 0x38
    bool m_Dizzy = false;            // 0xA1
    bool m_Awake = false;            // 0x18
    bool m_CanShoot = true;          // 0x40
    bool m_WeaponLockTarget = false; // 0x1C
    bool m_HasTarget = false;        // target_obj != null (0x7C)

    float m_InertialVel = 0.0F;      // 0x44
    glm::vec2 m_MoveDirection{0.0F, 0.0F}; // 0x74
};

} // namespace Game

#endif /* GAME_ENEMY_AI14_HPP */
