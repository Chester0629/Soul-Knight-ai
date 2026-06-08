#ifndef GAME_ENEMYAI01_HPP
#define GAME_ENEMYAI01_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class EnemyAI01
 * @brief Faithful decision/cadence brain for EnemyAI01 (an RGEController subclass).
 *
 * EnemyAI01 is the canonical generic chase+shoot archetype (50 prefabs use it).
 * This is the pure, unit-testable logic layer over the deterministic
 * RGEController/RGRandom base. Modelled here (all pure; no Unity types):
 *   - Scout(): the gate (skip while dead 0x38 or dizzy 0xA1) + the single
 *     advancing rg_random.Range(0, 10) idle re-roll draw; clears the chase
 *     target (target_obj = null, 0x7C). The re-detect/re-target tail is
 *     tail-call-truncated (FUN_010b7dcc) and NOT modelled.
 *   - RunReflection(): the random wander direction (two rg_random.Range(-1, 1)
 *     float draws, then normalized) written to move_direction (0x74). No gate.
 *   - ShootReflection(): the shoot gate (skip while dead 0x38 or dizzy 0xA1) and
 *     the can_shoot = false (0x40) latch; reports the shoot_cd (0x3C) Invoke
 *     delay. NOTE: unlike EnemyAI03 this decomp does NOT set shooting (0x80) --
 *     it only clears can_shoot, triggers the hand, and schedules the re-fire.
 *   - OnGameStateChange(): the awake (0x18) wake gate (game_state == 1 AND the
 *     room-ready flag) modelled as a pure boolean.
 *   - FixedRotation(): only the target_obj (0x7C) null-check branch decision is
 *     recoverable; both branches tail-call into owner get_position/get_transform,
 *     so no pure state is written (returned as a branch enum).
 *   - FixedUpdate physics: gated on awake (0x18) -- a not-awake step is a
 *     complete no-op. When awake there are three states: the not-knockback path
 *     (inertial_vel <= 1.0 OR kinematic) either DIES (dead 0x38 -> awake = 0, no
 *     decay) or steers; otherwise an active knockback (inertial_vel > 1.0 AND not
 *     kinematic) composes force into velocity and decays inertial_vel by friction.
 *
 * Owner concerns (NOT modelled, by design): Animator.SetBool, RGEHand attack
 * triggers, Rigidbody2D velocity writes, MonoBehaviour.Invoke scheduling,
 * Transform get_position/get_transform reads. Referenced in comments at sites.
 *
 * @see IL2CPP skeleton EnemyAI01.cs; FAITHFUL: EnemyAI01 @ game_full.c:675946-676405.
 */
class EnemyAI01 {
public:
    /**
     * @brief Outcome of one FixedUpdateStep, mirroring the decomp's branch tree
     *        (EnemyAI01__FixedUpdate @ game_full.c:675946-676052).
     *
     * The whole body is gated on awake (0x18, line 675975). When awake, the path
     * splits on (inertial_vel <= 1.0 OR kinematic) (line 675976):
     *   - Asleep:    awake (0x18) == 0 -> entire body skipped; nothing happens
     *                (NO friction decay, NO velocity write).
     *   - Dead:      not-knockback path AND dead (0x38) (line 675977) -> awake is
     *                set to 0 (line 675978), velocity zeroed (owner); NO decay.
     *   - Steer:     not-knockback path AND not dead -> normal steering (the block
     *                at 675991-676016); NO decay (inertial_vel <= 1.0 anyway).
     *   - Knockback: inertial_vel (0x44) > 1.0 AND not kinematic (0x58) -> the
     *                else branch (676018-676050) composes force into velocity and
     *                decays inertial_vel *= friction (0x50) (line 676049).
     */
    enum class StepResult {
        Asleep,    ///< awake == 0: no-op (no decay, no velocity write).
        Dead,      ///< not-knockback path && dead: awake -> 0, no decay.
        Steer,     ///< not-knockback path && alive: normal steering, no decay.
        Knockback  ///< inertial_vel > 1.0 && !kinematic: force compose + decay.
    };

    /// Which branch FixedRotation took (EnemyAI01__FixedRotation @ 676345).
    enum class RotationResult {
        AimAtTarget, ///< target_obj (0x7C) != null -> get_position(target) (owner).
        FaceDefault  ///< target_obj == null -> get_transform(self) (owner).
    };

    /// Idle re-roll ceiling drawn by Scout (rg_random.Range(0, 10), max EXCL).
    static constexpr int kScoutRerollCeiling = 10;
    /// Wander component bounds (rg_random.Range(-1f, 1f) x2, max INCL).
    static constexpr float kWanderMin = -1.0F; // hex 0xbf800000
    static constexpr float kWanderMax = 1.0F;  // hex 0x3f800000
    /// inertial_vel must exceed this for the knockback branch (1.0 <), line 675976.
    static constexpr float kKnockbackThreshold = 1.0F;
    /// game_state value that wakes the enemy (OnGameStateChange, line 676385).
    static constexpr int kRunningGameState = 1;

    EnemyAI01() = default;

    /// Seed this enemy's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- mutable state (owning entity mirrors these into the real fields) ----
    bool Dead() const { return m_Dead; }           // 0x38
    void SetDead(bool dead) { m_Dead = dead; }

    bool Dizzy() const { return m_Dizzy; }          // 0xA1
    void SetDizzy(bool dizzy) { m_Dizzy = dizzy; }

    bool Awake() const { return m_Awake; }          // 0x18
    void SetAwake(bool awake) { m_Awake = awake; }

    bool CanShoot() const { return m_CanShoot; }    // 0x40
    void SetCanShoot(bool v) { m_CanShoot = v; }

    bool Kinematic() const { return m_Kinematic; }  // 0x58
    void SetKinematic(bool v) { m_Kinematic = v; }

    /// Last wander direction chosen by RunReflection (move_direction, 0x74).
    glm::vec2 MoveDirection() const { return m_MoveDirection; }
    /// Whether the enemy currently has a chase target (target_obj, 0x7C).
    bool HasTarget() const { return m_HasTarget; }
    void SetHasTarget(bool v) { m_HasTarget = v; }

    float InertialVel() const { return m_InertialVel; } // 0x44
    /// Seed/clear knockback (owner calls when GetForce lands; clamp is base 28).
    void SetInertialVel(float vel) { m_InertialVel = vel; }

    /**
     * @brief Target-acquisition tick. FAITHFUL: EnemyAI01__Scout @ game_full.c:676136.
     *
     * Gated: does nothing while dead (0x38, line 676146) or dizzy (0xA1, line
     * 676149). When active, clears the chase target (target_obj = null, 0x7C, line
     * 676152) and advances the deterministic stream with the single
     * rg_random.Range(0, 10) idle re-roll (line 676155). The original's tail
     * (re-detect / re-target) is tail-call-truncated (FUN_010b7dcc, line 676158)
     * and is NOT modelled.
     * @return the rg_random.Range(0, 10) roll, or -1 when gated (no draw taken).
     */
    int Scout();

    /**
     * @brief Re-pick a random wander direction. FAITHFUL: EnemyAI01__RunReflection
     *        @ game_full.c:676254.
     *
     * Draws two rg_random.Range(-1f, 1f) floats (lines 676293, 676298), builds a
     * Vector2 and normalizes it (FUN_00fa16ec build + FUN_00fa1e04 normalize, lines
     * 676299-676300), then stores it as move_direction (set_move_direction, line
     * 676301). No gate. The preceding target_obj position read (lines 676280-676287)
     * feeds only the owner's facing and is not part of the pure draw logic.
     * Owner: anim.SetBool(walk, true) at the tail (line 676307).
     * @return the new normalized move_direction (zero if both draws were zero).
     */
    glm::vec2 RunReflection();

    /**
     * @brief Begin a shot: clear the shoot gate. FAITHFUL: EnemyAI01__ShootReflection
     *        @ game_full.c:676312.
     *
     * Gated: does nothing while dead (0x38, line 676322) or dizzy (0xA1, line
     * 676325). When active: can_shoot = false (0x40, line 676328). NOTE: this
     * decomp does NOT set shooting (0x80) -- only can_shoot is written here. No RNG
     * draws. Owner: RGEHand.SetAttackTrigger(hand@0x6C) (line 676330) and
     * Invoke("ShootReflection", shoot_cd@0x3C) re-fire schedule (line 676331); the
     * delay is returned for the owner to schedule.
     * @param outShootCd  filled with shoot_cd (ShootReflection re-fire delay).
     * @param shootCd      this enemy's shoot_cd (0x3C) field value.
     * @return true if the shot latched (was not gated).
     */
    bool ShootReflection(float &outShootCd, float shootCd);

    /**
     * @brief Decide aim target for the rotation step. FAITHFUL:
     *        EnemyAI01__FixedRotation @ game_full.c:676345.
     *
     * Builds a zero vector (owner) then branches on target_obj (0x7C) != null
     * (line 676365): non-null -> get_position(target_obj) (line 676375, owner);
     * null -> get_transform(self) (line 676368, owner). Both branches tail-call
     * into owner Transform reads, so no pure state is written -- only the branch
     * decision is recoverable. No RNG. No gate.
     * @return which branch was taken (the rotation source the owner must read).
     */
    RotationResult FixedRotation() const;

    /**
     * @brief React to a global game-state change. FAITHFUL:
     *        EnemyAI01__OnGameStateChange @ game_full.c:676380.
     *
     * Returns immediately unless game_state == 1 (line 676385). When the owning
     * maker reports its room is ready (the_maker.the_room.field(0x10) == 1, lines
     * 676388-676397), latches awake = true (0x18, line 676398) and tail-calls into
     * the owner's wake dispatch (indirect call, line 676401). Modelled as a pure
     * boolean: the caller supplies the room-ready flag and we return whether the
     * enemy woke. No RNG.
     * @param gameState  the new global game state.
     * @param roomReady  the_maker.the_room ready flag (field 0x10 == 1).
     * @return true if this call woke the enemy (awake transitioned to true).
     */
    bool OnGameStateChange(int gameState, bool roomReady);

    /**
     * @brief One FixedUpdate physics step. FAITHFUL: EnemyAI01__FixedUpdate @
     *        game_full.c:675946.
     *
     * Models only the pure scalar/branch logic, not the Rigidbody2D writes. The
     * whole body is gated on awake (0x18, line 675975): when not awake the step is
     * a complete no-op (NO friction decay, NO velocity write, awake unchanged).
     * When awake, the path splits on (inertial_vel <= 1.0 OR kinematic) (line
     * 675976):
     *   - not-knockback path: if dead (0x38, line 675977) -> awake is set to 0
     *     (line 675978) and velocity zeroed (owner); NO decay. Result Dead.
     *     Otherwise normal steering (block 675991-676016); NO decay. Result Steer.
     *   - knockback path (inertial_vel > 1.0 AND not kinematic): the else branch
     *     (676018-676050) composes force into velocity and decays inertial_vel by
     *     friction (0x50): inertial_vel *= friction (line 676049). Result Knockback.
     * @param friction the friction field (0x50), inertia decay multiplier.
     * @return the StepResult naming which of the four outcomes occurred.
     */
    StepResult FixedUpdateStep(float friction);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};

    bool m_Dead = false;      // 0x38
    bool m_Dizzy = false;     // 0xA1
    bool m_Awake = false;     // 0x18
    bool m_CanShoot = true;   // 0x40
    bool m_Kinematic = false; // 0x58
    bool m_HasTarget = false; // target_obj != null (0x7C)

    float m_InertialVel = 0.0F;            // 0x44
    glm::vec2 m_MoveDirection{0.0F, 0.0F}; // 0x74
};

} // namespace Game

#endif /* GAME_ENEMYAI01_HPP */
