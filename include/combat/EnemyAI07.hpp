#ifndef GAME_ENEMY_AI07_HPP
#define GAME_ENEMY_AI07_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class EnemyAI07
 * @brief Faithful decision/cadence brain for EnemyAI07 (an RGEController subclass).
 *
 * Per-content port: the pure, unit-testable logic layer over the deterministic
 * RGEController/RGRandom base. EnemyAI07 is a "rich 3-attack" enemy that wanders,
 * re-rolls a heading on RunReflection, and on ShootReflection rolls a 0..100
 * value to pick an attack. Atk1 latches an in_atk1 flag (which gates incoming
 * knockback) and stops the enemy in place. Modelled here (all pure; no Unity
 * types):
 *   - ctor:  need_tap (0xAC) is initialised to true (EnemyAI07___ctor).
 *   - Scout(): the gate (skip while dead or dizzy) + the single advancing
 *     rg_random.Range(0, 10) idle re-roll draw; clears the chase target.
 *   - RunReflection(): the random wander direction (two rg_random.Range(-1, 1)
 *     float draws, then normalized) written to move_direction. NOT dead/dizzy
 *     gated -- the decomp draws unconditionally here.
 *   - ShootReflection(): the attack-selection roll (gate: skip while dead or
 *     dizzy; the single rg_random.Range(0, 100) draw). The branch that maps the
 *     roll to Atk1/Atk2/Atk3 is tail-call-truncated in the decomp and is NOT
 *     modelled; the roll is returned for the caller.
 *   - Atk1(): latches in_atk1 = true (0xAD) and zeroes move_direction. No RNG.
 *   - GetForceGate(): GetForce is forwarded to the base ONLY when !in_atk1
 *     (0xAD) -- modelled as a pure predicate. No RNG.
 *   - Atk2Active(): Atk2's pure gate (skip while dead or dizzy); its body is all
 *     Unity (ResourcesUtil.Load / Instantiate) and is NOT modelled.
 *   - FixedUpdate physics: gated on awake (0x18) -- a not-awake step is a
 *     complete no-op. When awake: if inertial_vel (0x44) <= 1.0 there is a dead
 *     sub-branch that clears awake (awake = 0) and zeroes velocity (Dead);
 *     otherwise normal steering (Steer). If inertial_vel > 1.0 the knockback
 *     impulse replaces steering and decays by friction (inertial_vel *= friction)
 *     (Knockback).
 *
 * Owner concerns (NOT modelled, by design): Animator.SetBool("walk"), the
 * Rigidbody2D velocity writes, ResourcesUtil.Load + Object.Instantiate of the
 * bullet/weapon prefabs, Transform get_position reads (facing), and the base
 * RGEController.GetForce magnitude clamp. Those are referenced in comments at
 * their decomp sites.
 *
 * @see IL2CPP skeleton EnemyAI07.cs (need_tap @0xAC, in_atk1 @0xAD, bullet01,
 *      atk1_cilp, atk2_clip, point_1); FAITHFUL: EnemyAI07 @
 *      game_full.c:677681-678117.
 */
class EnemyAI07 {
public:
    /**
     * @brief Outcome of one FixedUpdateStep, mirroring the decomp's branch tree
     *        (EnemyAI07__FixedUpdate @ game_full.c:677722-677827).
     *
     * The whole body is gated on awake (0x18, line 677751). When awake there are
     * exactly four mutually exclusive outcomes:
     *   - Asleep:    awake (0x18) == 0 -> entire body skipped; nothing happens
     *                (NO friction decay, NO velocity write). Line 677751.
     *   - Dead:      inertial_vel (0x44) <= 1.0 AND dead (0x38) -> awake is
     *                cleared (awake = 0, line 677754), velocity is zeroed and the
     *                function returns. No steering, no friction decay. Line 677753.
     *   - Steer:     inertial_vel <= 1.0 AND not dead -> normal steering (the
     *                plain block at 677767-677791). No friction decay.
     *   - Knockback: inertial_vel > 1.0 -> the knockback velocity replaces
     *                steering and inertial_vel is decayed by friction (0x50):
     *                inertial_vel *= friction (line 677823). Line 677793 else.
     */
    enum class StepResult {
        Asleep,    ///< awake == 0: no-op (no decay, no velocity write).
        Dead,      ///< inertial_vel <= 1.0 && dead: clears awake, zeroes velocity.
        Steer,     ///< inertial_vel <= 1.0 && not dead: normal steering, no decay.
        Knockback  ///< inertial_vel > 1.0: knockback + decay (inertial_vel *= friction).
    };

    /// Idle re-roll ceiling drawn by Scout (rg_random.Range(0, 10), max EXCL).
    static constexpr int kScoutRerollCeiling = 10;
    /// Attack-selection roll ceiling in ShootReflection (Range(0, 100), max EXCL).
    static constexpr int kAttackRollCeiling = 100;
    /// Wander component bounds (rg_random.Range(-1f, 1f) x2, max INCL).
    static constexpr float kWanderMin = -1.0F;
    static constexpr float kWanderMax = 1.0F;
    /// inertial_vel must be <= this for the non-knockback branch (line 677752).
    static constexpr float kKnockbackThreshold = 1.0F;

    EnemyAI07();

    /// Seed this enemy's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- mutable state (owning entity mirrors these into the real fields) ----
    bool Dead() const { return m_Dead; }     // field 0x38
    void SetDead(bool dead) { m_Dead = dead; }

    bool Awake() const { return m_Awake; }   // field 0x18
    void SetAwake(bool a) { m_Awake = a; }

    bool Dizzy() const { return m_Dizzy; }   // field 0xA1
    void SetDizzy(bool d) { m_Dizzy = d; }

    bool NeedTap() const { return m_NeedTap; }  // field 0xAC (ctor sets true)
    bool InAtk1() const { return m_InAtk1; }    // field 0xAD

    /// Last wander direction chosen by RunReflection / cleared by Atk1 (0x74).
    glm::vec2 MoveDirection() const { return m_MoveDirection; }
    /// Whether the enemy currently has a chase target (target_obj, 0x7C).
    bool HasTarget() const { return m_HasTarget; }

    float InertialVel() const { return m_InertialVel; } // field 0x44
    /// Seed/clear knockback (owner calls when GetForce lands; clamp is base 28).
    void SetInertialVel(float vel) { m_InertialVel = vel; }

    /**
     * @brief Target-acquisition tick. FAITHFUL: EnemyAI07__Scout @
     *        game_full.c:677831.
     *
     * Gated: does nothing while dead (0x38) or dizzy (0xA1) (lines 677841-677846).
     * When active, clears the chase target (target_obj = null, 0x7C, line 677847)
     * and advances the deterministic stream with the single rg_random.Range(0, 10)
     * idle re-roll (line 677850). The original's tail (re-detect / re-target) is
     * tail-call-truncated (FUN_010b7dcc, line 677853) and is NOT modelled.
     * @return the rg_random.Range(0, 10) roll, or -1 when gated (no draw taken).
     */
    int Scout();

    /**
     * @brief Re-pick a random wander direction. FAITHFUL:
     *        EnemyAI07__RunReflection @ game_full.c:677896.
     *
     * Draws two rg_random.Range(-1f, 1f) floats (lines 677935, 677940), builds a
     * Vector2 and normalizes it (FUN_00fa16ec build + FUN_00fa1e04 normalize,
     * lines 677941-677942), then stores it as move_direction (0x74, line 677943).
     * This method is NOT dead/dizzy gated -- the decomp draws unconditionally.
     * The preceding target_obj position read (lines 677922-677929) feeds only the
     * owner's facing and is not part of the pure draw logic. Owner:
     * anim.SetBool("walk", true) at the tail (line 677949).
     * @return the new normalized move_direction (zero if both draws were zero).
     */
    glm::vec2 RunReflection();

    /**
     * @brief Attack-selection roll. FAITHFUL: EnemyAI07__ShootReflection @
     *        game_full.c:677954.
     *
     * Gated: does nothing while dead (0x38) or dizzy (0xA1) (lines 677964-677969).
     * When active, advances the deterministic stream with the single
     * rg_random.Range(0, 100) attack-selection roll (line 677972). The branch
     * that maps the roll to Atk1/Atk2/Atk3 is tail-call-truncated in the decomp
     * (FUN_010b7dcc, line 677975) and is NOT modelled.
     * @return the rg_random.Range(0, 100) roll, or -1 when gated (no draw taken).
     */
    int ShootReflection();

    /**
     * @brief Begin attack 1: latch in_atk1 and stop in place. FAITHFUL:
     *        EnemyAI07__Atk1 @ game_full.c:678028.
     *
     * in_atk1 = true (0xAD, line 678038) and move_direction = Vector2.zero (0x74,
     * lines 678044-678045). No gate, no RNG draws.
     */
    void Atk1();

    /**
     * @brief Pure gate for incoming knockback. FAITHFUL: EnemyAI07__GetForce @
     *        game_full.c:678017.
     *
     * The decomp forwards to base RGEController.GetForce ONLY when in_atk1 (0xAD)
     * is false (line 678020). Modelled as a predicate so the owner can decide
     * whether to apply (and base-clamp to 28) the impulse. No RNG.
     * @return true if the base GetForce should run (i.e. !in_atk1).
     */
    bool GetForceGate() const { return !m_InAtk1; }

    /**
     * @brief Pure gate for Atk2. FAITHFUL: EnemyAI07__Atk2 @ game_full.c:678053.
     *
     * Atk2 early-returns while dead (0x38) or dizzy (0xA1) (lines 678065-678072);
     * when active its body is all Unity (ResourcesUtil.Load + Object.Instantiate
     * of bullet01, lines 678073-678084) and is NOT modelled. No RNG.
     * @return true if Atk2's body would run (i.e. !dead && !dizzy).
     */
    bool Atk2Active() const { return !m_Dead && !m_Dizzy; }

    /**
     * @brief One FixedUpdate physics step. FAITHFUL: EnemyAI07__FixedUpdate @
     *        game_full.c:677722.
     *
     * Models only the pure scalar/branch logic, not the Rigidbody2D writes. The
     * whole body is gated on awake (0x18, line 677751): not awake -> complete
     * no-op (NO friction decay, NO velocity write). When awake:
     *   - inertial_vel (0x44) <= 1.0 (line 677752):
     *       - if dead (0x38, line 677753): awake is cleared (awake = 0, line
     *         677754), velocity zeroed, function returns -> StepResult::Dead.
     *       - else: normal steering (677767-677791) -> StepResult::Steer (no
     *         friction decay).
     *   - inertial_vel > 1.0 (else, line 677793): knockback replaces steering and
     *     inertial_vel *= friction (0x50, line 677823) -> StepResult::Knockback.
     * @param friction the friction field (0x50), inertia decay multiplier.
     * @return the StepResult naming which of the four outcomes occurred.
     */
    StepResult FixedUpdateStep(float friction);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};

    bool m_Dead = false;     // 0x38
    bool m_Awake = false;    // 0x18
    bool m_Dizzy = false;    // 0xA1
    bool m_NeedTap = false;  // 0xAC (ctor sets true)
    bool m_InAtk1 = false;   // 0xAD
    bool m_HasTarget = false; // target_obj != null (0x7C)

    float m_InertialVel = 0.0F;            // 0x44
    glm::vec2 m_MoveDirection{0.0F, 0.0F}; // 0x74
};

} // namespace Game

#endif /* GAME_ENEMY_AI07_HPP */
