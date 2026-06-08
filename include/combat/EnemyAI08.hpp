#ifndef GAME_ENEMY_AI08_HPP
#define GAME_ENEMY_AI08_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class EnemyAI08
 * @brief Faithful decision/cadence brain for EnemyAI08 (an RGEController subclass).
 *
 * Per-content port: the pure, unit-testable logic layer over the deterministic
 * RGEController/RGRandom base. EnemyAI08 is the item/weapon-stealing enemy (a
 * chest-like roamer that drops loot when it disappears). Modelled here (all
 * pure; no Unity types):
 *   - Scout(): the gate (skip while dizzy (0xA1) OR dead (0x38)) + clears the
 *     chase target (target_obj = null, 0x7C) + the single advancing
 *     rg_random.Range(0, 10) idle re-roll draw (line 678249).
 *   - RunReflection(): the random wander direction (two rg_random.Range(-1, 1)
 *     float draws, then normalized) written to move_direction (0x74). The
 *     preceding target_obj position read feeds only the owner's facing.
 *   - GetWeapon(): the single weapon-selection roll rg_random.Range(0, total)
 *     where total is the 0xB4 field (count of weapons to pick from, line 678381).
 *   - DisAppear(): the despawn latch -- awake (0x18) = 0, dead (0x38) = 1.
 *   - FixedUpdate physics: gated on awake (0x18) -- a not-awake step is a
 *     complete no-op. When awake there are three outcomes: an active knockback
 *     impulse (inertial_vel (0x44) > 1.0) replaces steering, decays by friction
 *     (0x50), and early-returns; otherwise when dead (0x38) the enemy stops
 *     (awake = 0, zero velocity); otherwise normal steering applies.
 *
 * Owner concerns (NOT modelled, by design): Animator.SetBool("walk"),
 * Rigidbody2D velocity writes, the item-pickup spawning / Singleton lookups
 * (ItemTrigger / SyncItemTrigger), the FixedRotation aim read, the ChildDead
 * string concat, and the OnTriggerEnter2D gameObject fetch. Those are referenced
 * in comments at their decomp sites. FixedRotation, ChildDead, OnTriggerEnter2D,
 * ItemTrigger and SyncItemTrigger contain NO pure state writes and NO RNG draws
 * (they are inlined/tail-call-truncated owner glue), so they are NOT modelled.
 *
 * @see IL2CPP skeleton EnemyAI08.cs (need_tap, chest_info, total, chest_level,
 *      can_use); FAITHFUL: EnemyAI08 @ game_full.c:678121-678494.
 */
class EnemyAI08 {
public:
    /**
     * @brief Outcome of one FixedUpdateStep, mirroring the decomp's branch tree
     *        (EnemyAI08__FixedUpdate @ game_full.c:678121-678225).
     *
     * The whole body is gated on awake (0x18, line 678150). When awake, there
     * are exactly three mutually exclusive movement outcomes:
     *   - Asleep:    awake (0x18) == 0 -> entire body skipped; nothing happens
     *                (NO friction decay, NO velocity write). Line 678150.
     *   - Knockback: inertial_vel (0x44) > 1.0 (the else branch at line 678151):
     *                the knockback velocity replaces steering, inertial_vel *=
     *                friction (0x50) (line 678222). This is the only branch that
     *                decays; the steer/dead block does NOT run.
     *   - DeadStop:  inertial_vel <= 1.0 AND dead (0x38) != 0 (line 678152):
     *                awake (0x18) = 0 and the rigidbody velocity is zeroed; the
     *                dead branch then tail-returns (get_transform, line 678164),
     *                so the plain steering write does NOT run.
     *   - Steer:     inertial_vel <= 1.0 AND not dead -> normal steering (the
     *                plain block at lines 678166-678190).
     */
    enum class StepResult {
        Asleep,    ///< awake == 0: no-op (no decay, no velocity write).
        Knockback, ///< inertial_vel > 1.0: knockback + decay (only decaying path).
        DeadStop,  ///< inertial_vel <= 1.0 && dead: awake = 0, zero velocity.
        Steer      ///< inertial_vel <= 1.0 && not dead: normal steering.
    };

    /// Idle re-roll ceiling drawn by Scout (rg_random.Range(0, 10), max EXCL).
    static constexpr int kScoutRerollCeiling = 10;
    /// Wander component bounds (rg_random.Range(-1f, 1f) x2, max INCL).
    static constexpr float kWanderMin = -1.0F;
    static constexpr float kWanderMax = 1.0F;
    /// inertial_vel must exceed this for the knockback branch to apply (1.0 <).
    static constexpr float kKnockbackThreshold = 1.0F;

    EnemyAI08() = default;

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

    /// Last wander direction chosen by RunReflection (move_direction, 0x74).
    glm::vec2 MoveDirection() const { return m_MoveDirection; }
    /// Whether the enemy currently has a chase target (target_obj, 0x7C).
    bool HasTarget() const { return m_HasTarget; }

    float InertialVel() const { return m_InertialVel; } // field 0x44
    /// Seed/clear knockback (owner calls when GetForce lands; clamp is base 28).
    void SetInertialVel(float vel) { m_InertialVel = vel; }

    /**
     * @brief Target-acquisition tick. FAITHFUL: EnemyAI08__Scout @ game_full.c:678230.
     *
     * Gated: does nothing while dizzy (0xA1) OR dead (0x38). The decomp evaluates
     * dizzy first (line 678240), and only reads dead when not dizzy (short-circuit,
     * lines 678241-678244); the active branch requires BOTH not-dizzy AND not-dead
     * (line 678245). When active, clears the chase target (target_obj = null, 0x7C,
     * line 678246) and advances the deterministic stream with the single
     * rg_random.Range(0, 10) idle re-roll (line 678249). The original's tail
     * (re-detect / re-target) is tail-call-truncated (FUN_010b7dcc, line 678252)
     * and is NOT modelled.
     * @return the rg_random.Range(0, 10) roll, or -1 when gated (no draw taken).
     */
    int Scout();

    /**
     * @brief Re-pick a random wander direction. FAITHFUL: EnemyAI08__RunReflection
     *        @ game_full.c:678259.
     *
     * Draws two rg_random.Range(-1f, 1f) floats (lines 678298, 678303), builds a
     * Vector2 and normalizes it (FUN_00fa16ec build + FUN_00fa1e04 normalize,
     * lines 678304-678305), then stores it as move_direction (set_move_direction,
     * 0x74, line 678306). Owner: anim.SetBool("walk", true) at the tail (line
     * 678312). The preceding target_obj position read (lines 678285-678292) feeds
     * only the owner's facing and is not part of the pure draw logic. Ungated in
     * the decomp (no dead/dizzy check at the head).
     * @return the new normalized move_direction (zero if both draws were zero).
     */
    glm::vec2 RunReflection();

    /**
     * @brief Roll a weapon selection index. FAITHFUL: EnemyAI08__GetWeapon @
     *        game_full.c:678369.
     *
     * The whole recoverable body is a single rg_random.Range(0, total) draw (line
     * 678381), where total is the 0xB4 field -- the count of weapons to pick from.
     * The tail (use the index to fetch/spawn a weapon) is tail-call-truncated and
     * is owner work; NOT modelled. max EXCLUSIVE.
     * @param total the 0xB4 weapon-count field (Range upper bound, exclusive).
     * @return the rolled index in [0, total), or 0 when total <= 0 (no valid draw;
     *         RGRandom guards an empty/inverted range -- still consumes the call).
     */
    int GetWeapon(int total);

    /**
     * @brief Despawn latch. FAITHFUL: EnemyAI08__DisAppear @ game_full.c:678483.
     *
     * awake (0x18) = 0 (line 678490) and dead (0x38) = 1 (line 678491). The tail
     * (get_transform -> spawn the dropped item / play despawn VFX) is
     * tail-call-truncated and is owner work; NOT modelled. No gate, no RNG.
     */
    void DisAppear();

    /**
     * @brief One FixedUpdate physics step. FAITHFUL: EnemyAI08__FixedUpdate @
     *        game_full.c:678121.
     *
     * Models only the pure scalar/branch logic, not the Rigidbody2D writes. The
     * whole body is gated on awake (0x18, line 678150): when not awake the step is
     * a complete no-op (NO friction decay, NO velocity write). When awake, the
     * outcome is one of three states:
     *   - Knockback while inertial_vel (0x44) > 1.0 (the else branch, line 678151):
     *               the knockback velocity replaces steering and inertial_vel is
     *               decayed by friction (0x50): inertial_vel *= friction (line
     *               678222). This is the ONLY branch that decays.
     *   - DeadStop  while inertial_vel <= 1.0 AND dead (0x38) (line 678152): awake
     *               (0x18) = 0 and the velocity is zeroed; the dead branch then
     *               tail-returns (line 678164), so the steer write does NOT run.
     *   - Steer     while inertial_vel <= 1.0 AND not dead: normal steering (the
     *               plain block at lines 678166-678190).
     * Mutates: on Knockback, inertial_vel *= friction. On DeadStop, awake = false.
     * @param friction the friction field (0x50), inertia decay multiplier.
     * @return the StepResult naming which of the four outcomes occurred.
     */
    StepResult FixedUpdateStep(float friction);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};

    bool m_Awake = false;            // 0x18
    bool m_Dead = false;             // 0x38
    bool m_Dizzy = false;            // 0xA1
    bool m_HasTarget = false;        // target_obj != null (0x7C)

    float m_InertialVel = 0.0F;      // 0x44
    glm::vec2 m_MoveDirection{0.0F, 0.0F}; // 0x74
};

} // namespace Game

#endif /* GAME_ENEMY_AI08_HPP */
