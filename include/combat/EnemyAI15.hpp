#ifndef GAME_ENEMY_AI15_HPP
#define GAME_ENEMY_AI15_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class EnemyAI15
 * @brief Faithful decision/cadence brain for EnemyAI15 (an RGEController subclass).
 *
 * Per-content port: the pure, unit-testable logic layer over the deterministic
 * RGEController/RGRandom base. EnemyAI15 is a "wander-and-reflect shooter": it
 * scouts on the scout_rate cadence (clearing its chase target + advancing the
 * stream by one idle re-roll), re-picks a random wander direction on a movement
 * reflection (RunReflection), and on a shoot reflection fires an attack trigger
 * + advances the stream by one Range(0,100) roll. Modelled here (all pure; no
 * Unity types):
 *   - Scout(): the gate (skip while dead OR dizzy) + the single advancing
 *     rg_random.Range(0, 10) idle re-roll draw (line 681081); clears the chase
 *     target (target_obj = null, 0x7C).
 *   - RunReflection(): the random wander direction -- two rg_random.Range(-1, 1)
 *     float draws (lines 681183, 681188), built into a Vector2 and normalized,
 *     written to move_direction (0x74). NO gate in the decomp.
 *   - ShootReflection(): the gate (skip while dead OR dizzy) + the single
 *     advancing rg_random.Range(0, 100) draw (line 681227). The attack trigger
 *     itself is an owner/Animator concern. No state flags are written here.
 *   - FixedUpdate physics: gated on awake (0x18) -- a not-awake step is a
 *     complete no-op. When awake there are two branches keyed on inertial_vel
 *     (0x44): the knockback branch (inertial_vel > 1.0) decays inertial_vel by
 *     friction (0x50); the non-knockback branch (inertial_vel <= 1.0) has a dead
 *     (0x38) sub-branch that latches awake = 0 (and zeroes velocity), otherwise
 *     plain steering. Unlike EnemyAI03 the dead handling lives INSIDE the
 *     inertial_vel <= 1.0 path and clears the awake flag.
 *
 * Owner concerns (NOT modelled, by design): Animator.SetBool("walk")/SetTrigger,
 * Rigidbody2D velocity writes, the target_obj position reads (FixedRotation /
 * RunReflection facing), and Object.Instantiate of the bullet prefabs in
 * Atk1/Atk2. Those are referenced in comments at their decomp sites. Atk1/Atk2
 * and FixedRotation carry NO RNG draws and write NO modelled state -- they are
 * pure Unity instantiation / transform reads and so are not ported as methods.
 *
 * @see IL2CPP skeleton EnemyAI15.cs (bullet01, bullet02, atk1_clip, atk2_clip);
 *      FAITHFUL: EnemyAI15 @ game_full.c:680925-681338.
 */
class EnemyAI15 {
public:
    /**
     * @brief Outcome of one FixedUpdateStep, mirroring the decomp's branch tree
     *        (EnemyAI15__FixedUpdate @ game_full.c:680925-681030).
     *
     * The whole body is gated on awake (0x18, line 680954). When awake, the
     * branch is keyed on inertial_vel (0x44):
     *   - Asleep:    awake (0x18) == 0 -> entire body skipped; nothing happens
     *                (NO friction decay, NO velocity write, NO awake clear).
     *   - Dead:      inertial_vel (0x44) <= 1.0 AND dead (0x38) != 0 (line
     *                680956): the step latches awake = 0 (line 680957) and the
     *                owner zeroes velocity. The decomp's tail (get_transform) is
     *                truncated; the steering block does NOT execute (it tail-calls
     *                out before reaching it). inertial_vel is NOT decayed here.
     *   - Knockback: inertial_vel (0x44) > 1.0 (line 680955 else-branch): the
     *                knockback velocity is composed and inertial_vel *= friction
     *                (0x50) (line 681026). No early-return needed -- this is the
     *                terminal else branch.
     *   - Steer:     inertial_vel (0x44) <= 1.0 AND dead (0x38) == 0: plain
     *                steering (the block at 680970-680994). No decay.
     */
    enum class StepResult {
        Asleep,    ///< awake == 0: no-op (no decay, no velocity write).
        Dead,      ///< inertial_vel <= 1.0 && dead: latches awake = 0, no decay.
        Knockback, ///< inertial_vel > 1.0: knockback velocity + friction decay.
        Steer      ///< inertial_vel <= 1.0 && !dead: normal steering, no decay.
    };

    /// Idle re-roll ceiling drawn by Scout (rg_random.Range(0, 10), max EXCL).
    static constexpr int kScoutRerollCeiling = 10;
    /// Shoot re-roll ceiling drawn by ShootReflection (Range(0, 100), max EXCL).
    static constexpr int kShootRerollCeiling = 100;
    /// Wander component bounds (rg_random.Range(-1f, 1f) x2, max INCL).
    static constexpr float kWanderMin = -1.0F;
    static constexpr float kWanderMax = 1.0F;
    /// inertial_vel must exceed this for the knockback branch to apply (1.0 <).
    static constexpr float kKnockbackThreshold = 1.0F;

    EnemyAI15() = default;

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

    /// Last wander direction chosen by RunReflection (move_direction, 0x74).
    glm::vec2 MoveDirection() const { return m_MoveDirection; }
    /// Whether the enemy currently has a chase target (target_obj, 0x7C).
    bool HasTarget() const { return m_HasTarget; }

    float InertialVel() const { return m_InertialVel; } // field 0x44
    /// Seed/clear knockback (owner calls when GetForce lands; clamp is base 28).
    void SetInertialVel(float vel) { m_InertialVel = vel; }

    /**
     * @brief Target-acquisition tick. FAITHFUL: EnemyAI15__Scout @
     *        game_full.c:681062.
     *
     * Gated: does nothing while dead (0x38) OR dizzy (0xA1) (lines 681072-681077).
     * When active, clears the chase target (target_obj = null, 0x7C, line 681078)
     * and advances the deterministic stream with the single rg_random.Range(0, 10)
     * idle re-roll (line 681081, max EXCLUSIVE). The original's tail (re-detect /
     * re-target) is tail-call-truncated in the decomp (FUN_010b7dcc) and is NOT
     * modelled. The returned roll is what keeps the stream lockstep.
     * @return the rg_random.Range(0, 10) roll, or -1 when gated (no draw taken).
     */
    int Scout();

    /**
     * @brief Re-pick a random wander direction. FAITHFUL:
     *        EnemyAI15__RunReflection @ game_full.c:681144.
     *
     * NO dead/dizzy gate (the decomp body has none -- it is the move-reflection
     * animation callback). Draws two rg_random.Range(-1f, 1f) floats (lines
     * 681183, 681188, max INCLUSIVE), builds a Vector2 (FUN_00fa16ec) and
     * normalizes it (FUN_00fa1e04), then stores it as move_direction
     * (set_move_direction, 0x74, line 681191). The preceding target_obj position
     * read (lines 681170-681178) feeds only the owner's facing and is not part of
     * the pure draw logic. Owner: anim.SetBool("walk"/"run", true) at the tail
     * (line 681197).
     * @return the new normalized move_direction (zero if both draws were zero).
     */
    glm::vec2 RunReflection();

    /**
     * @brief Shoot reflection tick. FAITHFUL: EnemyAI15__ShootReflection @
     *        game_full.c:681202.
     *
     * Gated: does nothing while dead (0x38) OR dizzy (0xA1) (lines 681212-681219).
     * When active the owner fires the attack trigger (anim.SetTrigger, line
     * 681224) and the stream is advanced by a single rg_random.Range(0, 100) draw
     * (line 681227, max EXCLUSIVE). The decomp writes NO state flags here (the
     * tail is a truncated FUN_010b7dcc); only the RNG draw is modelled.
     * @return the rg_random.Range(0, 100) roll, or -1 when gated (no draw taken).
     */
    int ShootReflection();

    /**
     * @brief One FixedUpdate physics step. FAITHFUL: EnemyAI15__FixedUpdate @
     *        game_full.c:680925.
     *
     * Models only the pure scalar/branch logic, not the Rigidbody2D writes. The
     * whole body is gated on awake (0x18, line 680954): when not awake the step
     * is a complete no-op (NO friction decay, NO velocity write, NO awake clear).
     * When awake, the branch is keyed on inertial_vel (0x44):
     *   - inertial_vel <= 1.0 (line 680955) AND dead (0x38) != 0 (line 680956):
     *     the step latches awake = 0 (line 680957) -- the owner zeroes velocity --
     *     and tail-calls out (the steering block does not run). NO decay.
     *   - inertial_vel <= 1.0 AND dead == 0: plain steering (680970-680994). No
     *     decay.
     *   - inertial_vel > 1.0 (else, line 680996): the knockback velocity is
     *     composed and inertial_vel *= friction (0x50) (line 681026).
     * No RNG draws.
     * @param friction the friction field (0x50), inertia decay multiplier.
     * @return the StepResult naming which of the four outcomes occurred. On the
     *         Dead outcome m_Awake is also cleared (mirrors the decomp write).
     */
    StepResult FixedUpdateStep(float friction);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};

    bool m_Dead = false;  // 0x38
    bool m_Dizzy = false; // 0xA1
    bool m_Awake = true;  // 0x18 (owner flips this once the spawn anim is done)
    bool m_HasTarget = false; // target_obj != null (0x7C)

    float m_InertialVel = 0.0F;            // 0x44
    glm::vec2 m_MoveDirection{0.0F, 0.0F}; // 0x74
};

} // namespace Game

#endif /* GAME_ENEMY_AI15_HPP */
