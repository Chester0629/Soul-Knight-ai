#ifndef GAME_ENEMY_AI10_HPP
#define GAME_ENEMY_AI10_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class EnemyAI10
 * @brief Faithful decision/cadence brain for EnemyAI10 (an RGEController subclass).
 *
 * Per-content port: the pure, unit-testable logic layer over the deterministic
 * RGEController/RGRandom base. EnemyAI10 is a "chase-and-bounce reflector": it
 * scouts (drops its chase target while idle), re-rolls a random wander direction
 * when it bounces off geometry (RunReflection), and on a shot it disarms its
 * shoot gate and schedules the actual attack (OnAtk), which freezes its movement.
 * Modelled here (all pure; no Unity types):
 *   - Scout(): gate (skip while dizzy or dead) then drops the chase target
 *     (target_obj = null). No RNG draws.
 *   - RunReflection(): two rg_random.Range(-1f, 1f) float draws, normalized and
 *     written to move_direction. (The leading target_obj position read only feeds
 *     the owner's facing.)
 *   - ShootReflection(): gate (skip while dead or dizzy) then can_shoot = false
 *     and returns the shoot_cd delay the owner schedules OnAtk on. No RNG draws.
 *   - OnAtk(): move_direction = Vector2.zero (freeze before the weapon spawns).
 *     No RNG draws.
 *   - FixedRotation(): the only recoverable pure part is the target_obj null
 *     branch decision; everything else is a truncated transform/position read.
 *   - GetHurt(): the temp_enemy dispatch -- not temp_enemy delegates to the base
 *     damage chain; temp_enemy short-circuits (owner shows UI when not dead).
 *   - FixedUpdate physics: gated on awake (0x18). When awake, with
 *     inertial_vel (0x44) <= 1.0 a dead enemy disables itself (awake = 0) and
 *     stops, otherwise it steers (no decay); with inertial_vel > 1.0 the
 *     knockback term is composed and inertial_vel *= friction (0x50).
 *
 * Owner concerns (NOT modelled, by design): Animator.SetBool("walk")/SetTrigger,
 * Rigidbody2D velocity writes, Transform position reads, RGWeapon/dead_obj
 * Instantiate, the MonoBehaviour.Invoke("OnAtk") scheduling, and the UICanvas
 * damage popup. Those are referenced in comments at their decomp sites.
 *
 * @see IL2CPP skeleton EnemyAI10.cs (later_time, bullet01, bullet02, dead_obj);
 *      FAITHFUL: EnemyAI10 @ game_full.c:678932-679515.
 */
class EnemyAI10 {
public:
    /**
     * @brief Outcome of one FixedUpdateStep, mirroring the decomp's branch tree
     *        (EnemyAI10__FixedUpdate @ game_full.c:678932-679036).
     *
     * The whole body is gated on awake (0x18, line 678961). When awake, the
     * inertial_vel (0x44) magnitude selects the path:
     *   - Asleep:    awake == 0 -> entire body skipped; nothing happens (NO
     *                friction decay, NO velocity write). Line 678961.
     *   - DeadStop:  inertial_vel <= 1.0 AND dead (0x38) != 0 -> awake is cleared
     *                (0x18 = 0, line 678964), velocity is zeroed, and the body
     *                returns (the steering block below is unreachable). Lines
     *                678963-678975. NOTE: a dead enemy with inertial_vel > 1.0
     *                does NOT take this path -- it falls through to Knockback.
     *   - Steer:     inertial_vel <= 1.0 AND not dead -> normal steering, no
     *                decay (the plain block at 678977-679001).
     *   - Knockback: inertial_vel > 1.0 -> steering + force composition, then
     *                inertial_vel *= friction (0x50) (line 679033). The decay
     *                happens ONLY on this branch (the else at 679003-679034).
     */
    enum class StepResult {
        Asleep,    ///< awake == 0: no-op (no decay, no velocity write).
        DeadStop,  ///< inertial_vel <= 1.0 && dead: clear awake, zero velocity.
        Steer,     ///< inertial_vel <= 1.0 && not dead: normal steering, no decay.
        Knockback, ///< inertial_vel > 1.0: knockback composition + friction decay.
    };

    /// Wander component bounds (rg_random.Range(-1f, 1f) x2, max INCL).
    /// 0xbf800000 = -1.0f, 0x3f800000 = +1.0f.
    static constexpr float kWanderMin = -1.0F;
    static constexpr float kWanderMax = 1.0F;
    /// inertial_vel must exceed this for the knockback branch to apply (1.0 <).
    static constexpr float kKnockbackThreshold = 1.0F;

    EnemyAI10() = default;

    /// Seed this enemy's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- mutable state (owning entity mirrors these into the real fields) ----
    bool Dead() const { return m_Dead; }       // field 0x38
    void SetDead(bool dead) { m_Dead = dead; }

    bool Dizzy() const { return m_Dizzy; }     // field 0xA1
    void SetDizzy(bool dizzy) { m_Dizzy = dizzy; }

    bool Awake() const { return m_Awake; }     // field 0x18
    void SetAwake(bool awake) { m_Awake = awake; }

    bool CanShoot() const { return m_CanShoot; } // field 0x40

    bool TempEnemy() const { return m_TempEnemy; } // field 0x19
    void SetTempEnemy(bool temp) { m_TempEnemy = temp; }

    bool HasTarget() const { return m_HasTarget; } // target_obj != null (0x7C)
    void SetHasTarget(bool has) { m_HasTarget = has; }

    float InertialVel() const { return m_InertialVel; } // field 0x44
    void SetInertialVel(float vel) { m_InertialVel = vel; }

    /// Last direction written to move_direction (0x74).
    glm::vec2 MoveDirection() const { return m_MoveDirection; }

    /**
     * @brief Result of GetHurt's temp_enemy dispatch.
     * FAITHFUL: EnemyAI10__GetHurt @ game_full.c:679449.
     */
    enum class HurtResult {
        Base,    ///< not temp_enemy -> delegate to RGEController.GetHurt.
        TempUi,  ///< temp_enemy && not dead -> owner shows the UICanvas popup.
        TempDead ///< temp_enemy && dead -> nothing.
    };

    /**
     * @brief Target-acquisition tick. FAITHFUL: EnemyAI10__Scout @ game_full.c:679153.
     *
     * Gate (lines 679163-679168): runs only while not dizzy (0xA1) AND not dead
     * (0x38). When active it clears the chase target (target_obj = null, 0x7C,
     * line 679169). The tail is a get_transform that "does not return" in the
     * decomp (line 679171) and is not modelled. No RNG draws.
     * @return true if the tick was active (cleared the target), false if gated.
     */
    bool Scout();

    /**
     * @brief Re-pick a random wander direction (wall-bounce). FAITHFUL:
     *        EnemyAI10__RunReflection @ game_full.c:679215.
     *
     * Draws two rg_random.Range(-1f, 1f) floats (lines 679254, 679259), builds a
     * Vector2 and normalizes it (FUN_00fa1e04), then stores it as move_direction
     * (0x74, line 679262). The preceding target_obj position read (lines
     * 679241-679248) feeds only the owner's facing; owner tail:
     * anim.SetBool("walk", true) (line 679268).
     * @return the new normalized move_direction (zero if both draws were zero).
     */
    glm::vec2 RunReflection();

    /**
     * @brief Begin a shot. FAITHFUL: EnemyAI10__ShootReflection @ game_full.c:679273.
     *
     * Gate (lines 679283-679290): runs only while not dead (0x38) AND not dizzy
     * (0xA1). When active: can_shoot = false (0x40, line 679291) and the owner
     * schedules Invoke("OnAtk", shoot_cd@0x3C) (line 679292) plus anim triggers
     * (lines 679297-679299) and a vtable call (truncated jumptable, line 679302).
     * No RNG draws. The shoot_cd delay is returned via out-param.
     * @param outOnAtkDelay filled with shoot_cd (the Invoke("OnAtk") delay).
     * @param shootCd       this enemy's shoot_cd field value (0x3C).
     * @return true if the shot latched (was not gated).
     */
    bool ShootReflection(float &outOnAtkDelay, float shootCd);

    /**
     * @brief The scheduled attack. FAITHFUL: EnemyAI10__OnAtk @ game_full.c:679313.
     *
     * Sets move_direction = Vector2.zero (0x74, lines 679329-679330) to freeze in
     * place, then the owner Instantiates the weapon prefab (0xB0) and parents it
     * (truncated get_transform tail, lines 679336-679344). No RNG draws.
     */
    void OnAtk();

    /**
     * @brief Per-fixed-step facing decision. FAITHFUL: EnemyAI10__FixedRotation
     *        @ game_full.c:679370.
     *
     * The only recoverable pure part is the branch on target_obj (0x7C): when
     * there is no target (op_Inequality != 1, line 679391) the owner reads its
     * own transform; when there is a target the owner reads the target's position
     * (line 679400). Both tails are truncated transform/position reads and are
     * NOT modelled. No RNG draws.
     * @return true when a chase target is present (owner aims at the target),
     *         false when none (owner uses its own transform).
     */
    bool FixedRotation() const { return m_HasTarget; }

    /**
     * @brief Take a hit -- temp_enemy dispatch. FAITHFUL: EnemyAI10__GetHurt @
     *        game_full.c:679449.
     *
     * if (!temp_enemy) (0x19 == 0, line 679456) -> delegate to the base damage
     * chain RGEController.GetHurt(damage, source) (line 679457). Otherwise (a
     * temp_enemy): if not dead (0x38 == 0, line 679460) the owner shows the
     * UICanvas damage popup (truncated, line 679462); if dead, nothing. No RNG
     * draws, no state writes in this method itself.
     * @return which dispatch path the hit took.
     */
    HurtResult GetHurt() const;

    /**
     * @brief One FixedUpdate physics step. FAITHFUL: EnemyAI10__FixedUpdate @
     *        game_full.c:678932.
     *
     * Models only the pure scalar/branch logic, not the Rigidbody2D writes. The
     * whole body is gated on awake (0x18, line 678961): not awake -> complete
     * no-op (NO decay, NO velocity write). When awake, inertial_vel (0x44)
     * selects the path:
     *   - inertial_vel <= 1.0 AND dead (0x38): clear awake (0x18 = 0, line
     *     678964) and stop (the body returns; steering does not run) -> DeadStop.
     *   - inertial_vel <= 1.0 AND not dead: normal steering, no decay -> Steer.
     *   - inertial_vel > 1.0: steering + force composition then
     *     inertial_vel *= friction (0x50, line 679033) -> Knockback. The decay
     *     happens ONLY on this branch.
     * NOTE: a dead enemy with inertial_vel > 1.0 takes the Knockback branch (the
     * dead handler is nested under the <= 1.0 path only) -- faithful subtlety.
     * @param friction the friction field (0x50), the inertia decay multiplier.
     * @return the StepResult naming which of the four outcomes occurred.
     */
    StepResult FixedUpdateStep(float friction);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};

    bool m_Dead = false;       // 0x38
    bool m_Dizzy = false;      // 0xA1
    bool m_Awake = false;      // 0x18
    bool m_CanShoot = true;    // 0x40
    bool m_TempEnemy = false;  // 0x19
    bool m_HasTarget = false;  // target_obj != null (0x7C)

    float m_InertialVel = 0.0F;            // 0x44
    glm::vec2 m_MoveDirection{0.0F, 0.0F}; // 0x74
};

} // namespace Game

#endif /* GAME_ENEMY_AI10_HPP */
