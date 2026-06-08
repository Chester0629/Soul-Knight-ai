#ifndef GAME_ENEMY_AI02_HPP
#define GAME_ENEMY_AI02_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class EnemyAI02
 * @brief Faithful decision/cadence brain for EnemyAI02 (an RGEController subclass).
 *
 * Per-content port of the pure, unit-testable logic over the deterministic
 * RGEController/RGRandom base. EnemyAI02 is a "wander-and-shoot reflector" very
 * close to the base: it scouts (clears its target + advances one idle re-roll),
 * re-picks a random wander direction on RunReflection, and on ShootReflection
 * just fires the hand and shaves its move speed (speed_rate) by 0.5 for the shot.
 * Everything modelled here is pure (no Unity types):
 *   - Scout(): the gate (skip while dead 0x38 or dizzy 0xA1), clear the chase
 *     target (target_obj = null, 0x7C), then the SINGLE rg_random.Range(0, 10)
 *     idle re-roll draw (line 676550), but ONLY when rg_random (0x0C) is non-null.
 *   - RunReflection(): two rg_random.Range(-1f, 1f) float draws (lines 676599,
 *     676604), built into a Vector2 and normalized, stored as move_direction
 *     (0x74). NO gate. The preceding target_obj position read (676586-676593)
 *     feeds only the owner's facing and is not part of the pure draw logic.
 *   - ShootReflection(): the gate (skip while dead/dizzy), then the only pure
 *     state write the decomp performs -- role_attribute.speed_rate (0x70+0x14)
 *     -= 0.5 (line 676646). It does NOT set shooting/can_shoot/weapon_lock_target.
 *     NO RNG draws.
 *   - FixedUpdate physics: gated on awake (0x18) -- a not-awake step is a complete
 *     no-op. When awake there are three outcomes: inertial_vel (0x44) <= 1.0 with
 *     dead (0x38) set clears awake and zeroes velocity (DeadStop); inertial_vel
 *     <= 1.0 alive is normal steering (Steer, NO friction decay); inertial_vel >
 *     1.0 is the knockback branch which composes velocity AND decays
 *     inertial_vel *= friction (0x50, line 676510). Unlike EnemyAI03 there is NO
 *     early return -- both live branches converge on the can_shoot (0x40) facing
 *     check (which is an owner-only get_transform).
 *
 * Owner concerns (NOT modelled, by design): Animator.SetBool("walk"), the
 * RGEHand.SetAttackTrigger toggle, Rigidbody2D velocity writes, Transform
 * facing (get_transform/get_position), and the MonoBehaviour.Invoke(shoot_cd)
 * scheduling. They are referenced in comments at their decomp sites.
 *
 * @see IL2CPP skeleton EnemyAI02.cs (atk_range field); RGEController base offsets.
 *      FAITHFUL: EnemyAI02 @ game_full.c:676409-676731.
 */
class EnemyAI02 {
public:
    /**
     * @brief Outcome of one FixedUpdateStep, mirroring the decomp's branch tree
     *        (EnemyAI02__FixedUpdate @ game_full.c:676438-676517).
     *
     * The whole body is gated on awake (0x18, line 676438). When awake, the
     * inertial_vel (0x44) magnitude splits the body:
     *   - Asleep:    awake == 0 -> entire body skipped (NO decay, NO velocity
     *                write, NO awake clear). Line 676438.
     *   - DeadStop:  inertial_vel <= 1.0 AND dead (0x38) -> awake is cleared
     *                (awake = 0, line 676441) and velocity is zeroed; there is no
     *                friction decay. (The original falls through, but the tail is
     *                a non-returning get_transform; modelled as its own outcome.)
     *   - Steer:     inertial_vel <= 1.0 AND not dead -> normal steering velocity
     *                composition (lines 676454-676478); NO friction decay.
     *   - Knockback: inertial_vel > 1.0 -> steering + knockback force composed
     *                (676480-676509) and inertial_vel *= friction (0x50, line
     *                676510). NO early return -- converges on the can_shoot check.
     */
    enum class StepResult {
        Asleep,    ///< awake == 0: no-op (no decay, no velocity write).
        DeadStop,  ///< inertial_vel <= 1.0 && dead: clears awake, zeroes velocity.
        Steer,     ///< inertial_vel <= 1.0 && alive: normal steering, no decay.
        Knockback  ///< inertial_vel > 1.0: steer + knockback, inertial_vel *= friction.
    };

    /// Idle re-roll ceiling drawn by Scout (rg_random.Range(0, 10), max EXCL).
    static constexpr int kScoutRerollCeiling = 10;
    /// Wander component bounds (rg_random.Range(-1f, 1f) x2, max INCL).
    static constexpr float kWanderMin = -1.0F;
    static constexpr float kWanderMax = 1.0F;
    /// inertial_vel must exceed this for the knockback branch (1.0 <, line 676439).
    static constexpr float kKnockbackThreshold = 1.0F;
    /// ShootReflection shaves speed_rate (0x70+0x14) by this (line 676646).
    static constexpr float kShootSpeedRatePenalty = 0.5F;

    EnemyAI02() = default;

    /// Seed this enemy's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- mutable state (owning entity mirrors these into the real fields) ----
    bool Dead() const { return m_Dead; }
    void SetDead(bool dead) { m_Dead = dead; }

    bool Dizzy() const { return m_Dizzy; }     // field 0xA1
    void SetDizzy(bool dizzy) { m_Dizzy = dizzy; }

    bool Awake() const { return m_Awake; }     // field 0x18
    void SetAwake(bool awake) { m_Awake = awake; }

    /**
     * @brief Whether this enemy was spawned ad-hoc (temp_enemy, 0x19). When set,
     *        OnGameStateChange's wake path is gated out (line 676703: the wake only
     *        proceeds when temp_enemy == 0). Such enemies skip maker registration.
     */
    bool TempEnemy() const { return m_TempEnemy; }
    void SetTempEnemy(bool temp) { m_TempEnemy = temp; }

    /**
     * @brief Room-ready input the owner mirrors from the_maker(0x94).the_room(0x28)
     *        .state(0x10) == 1 (lines 676707-676712). OnGameStateChange only wakes
     *        when this holds (the_maker != null && the_room != null && state == 1).
     *        Defaults false (room not yet finished its spawn animation).
     */
    bool RoomReady() const { return m_RoomReady; }
    void SetRoomReady(bool ready) { m_RoomReady = ready; }

    /**
     * @brief Whether rg_random (0x0C) is wired. Scout's draw is GUARDED on this
     *        (line 676548: if (rg_random != 0) Range(0,10)); a null stream takes
     *        no draw. Defaults true; clear to model an unseeded controller.
     */
    bool HasRng() const { return m_HasRng; }
    void SetHasRng(bool has) { m_HasRng = has; }

    /// Last wander direction chosen by RunReflection (move_direction, 0x74).
    glm::vec2 MoveDirection() const { return m_MoveDirection; }
    /// Whether the enemy currently has a chase target (target_obj, 0x7C).
    bool HasTarget() const { return m_HasTarget; }

    float InertialVel() const { return m_InertialVel; } // field 0x44

    /**
     * @brief Target-acquisition tick. FAITHFUL: EnemyAI02__Scout @ game_full.c:676531.
     *
     * Gated: does nothing while dead (0x38) or dizzy (0xA1) (lines 676541-676546).
     * When active, clears the chase target (target_obj = null, 0x7C, line 676547)
     * and -- only when rg_random (0x0C) is non-null (line 676548) -- advances the
     * deterministic stream with the single rg_random.Range(0, 10) idle re-roll
     * (line 676550). The original's tail (FUN_010b7dcc) is tail-call-truncated and
     * NOT modelled.
     * @return the rg_random.Range(0, 10) roll, or -1 when gated or rng absent
     *         (no draw taken; keeps the stream lockstep with the original).
     */
    int Scout();

    /**
     * @brief Re-pick a random wander direction. FAITHFUL: EnemyAI02__RunReflection
     *        @ game_full.c:676560.
     *
     * Draws two rg_random.Range(-1f, 1f) floats (lines 676599, 676604), builds a
     * Vector2 (FUN_00fa16ec) and normalizes it (FUN_00fa1e04), then stores it as
     * move_direction (set_move_direction, 0x74, line 676607). No gate. Owner:
     * anim.SetBool("walk", true) at the tail (line 676613); the preceding
     * target_obj position read (lines 676586-676593) is owner facing only.
     * @return the new normalized move_direction (zero if both draws were zero).
     */
    glm::vec2 RunReflection();

    /**
     * @brief Fire the hand for one shot. FAITHFUL: EnemyAI02__ShootReflection @
     *        game_full.c:676618.
     *
     * Gated: does nothing while dead (0x38) or dizzy (0xA1) (lines 676629-676634).
     * The ONLY pure state write the decomp performs is shaving the move speed:
     * role_attribute.speed_rate (0x70+0x14) -= 0.5 (line 676646). It does NOT set
     * shooting/can_shoot/weapon_lock_target. No RNG draws. Owner: RGEHand.Set-
     * AttackTrigger(hand@0x6C) (line 676639) and Invoke("...", shoot_cd@0x3C)
     * (line 676640).
     * @param speedRate  in/out: this enemy's role_attribute.speed_rate; on a
     *                    non-gated call it is decremented by 0.5 in place.
     * @return true if the shot latched (was not gated); false leaves speedRate
     *         untouched.
     */
    bool ShootReflection(float &speedRate);

    /**
     * @brief One FixedUpdate physics step. FAITHFUL: EnemyAI02__FixedUpdate @
     *        game_full.c:676409.
     *
     * Models only the pure scalar/branch logic, not the Rigidbody2D writes or the
     * Transform facing. Gated on awake (0x18, line 676438): not awake -> complete
     * no-op (NO decay, NO velocity write, NO awake clear). When awake the
     * inertial_vel (0x44) magnitude splits the body:
     *   - inertial_vel <= 1.0 (line 676439): the steer branch. If dead (0x38, line
     *     676440) this clears awake (awake = 0, line 676441) and zeroes velocity
     *     -> DeadStop; otherwise normal steering -> Steer. NO friction decay in
     *     either case.
     *   - inertial_vel > 1.0 (else, line 676480): steering + knockback force are
     *     composed and inertial_vel *= friction (0x50, line 676510) -> Knockback.
     * There is NO early return; the live paths converge on the can_shoot (0x40)
     * facing check (line 676512), which is owner-only.
     * @param friction the friction field (0x50), inertia decay multiplier (only
     *                 applied on the Knockback path).
     * @return the StepResult naming which of the four outcomes occurred.
     */
    StepResult FixedUpdateStep(float friction);

    /**
     * @brief React to a game-state change (the wake path). FAITHFUL:
     *        EnemyAI02__OnGameStateChange @ game_full.c:676697.
     *
     * Models only the recoverable HEAD: the wake gate + the single pure state
     * write awake(0x18) = 1 (the byte `param_1 + 6`, line 676713). The path
     * proceeds ONLY when gameState == 1 (line 676702) AND temp_enemy (0x19) == 0
     * (lines 676703-676705) AND the_maker (0x94) != null (line 676706) AND
     * the_room (0x28) != null (line 676709) AND the_room.state (0x10) == 1 (line
     * 676712). On that path it sets m_Awake = true; this is the wake path that
     * gates FixedUpdate.
     *
     * The two trailing virtual calls (vtable +0x11c and +0x104, the StartEnemyAI-
     * style dispatch, lines 676714-676717) are jumptable-truncated indirect
     * tail-calls and are NOT modelled here -- owner concern.
     * @param gameState the new game state (the wake trigger is value 1).
     * @return true if this call woke the enemy (awake transitioned to set on this
     *         path); false when gated out (awake unchanged). No RNG draws taken.
     */
    bool OnGameStateChange(int gameState); // TODO[verify]: virtual dispatch tail (owner)

    /// Seed/clear knockback (owner calls when GetForce lands; clamp is base 28).
    void SetInertialVel(float vel) { m_InertialVel = vel; }

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};

    bool m_Dead = false;   // 0x38
    bool m_Dizzy = false;  // 0xA1
    bool m_Awake = false;  // 0x18
    bool m_TempEnemy = false; // 0x19 (OnGameStateChange wake gate, line 676703)
    bool m_RoomReady = false; // owner mirror of the_maker.the_room.state(0x10)==1
    bool m_HasRng = true;  // rg_random (0x0C) != null (Scout draw guard, line 676548)
    bool m_HasTarget = false; // target_obj != null (0x7C)

    float m_InertialVel = 0.0F;             // 0x44
    glm::vec2 m_MoveDirection{0.0F, 0.0F};  // 0x74
};

} // namespace Game

#endif /* GAME_ENEMY_AI02_HPP */
