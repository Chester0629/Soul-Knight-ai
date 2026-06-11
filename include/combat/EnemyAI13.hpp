#ifndef GAME_ENEMY_AI13_HPP
#define GAME_ENEMY_AI13_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class EnemyAI13
 * @brief Faithful decision/cadence brain for EnemyAI13 (an RGEController subclass).
 *
 * Per-content port: the pure, unit-testable logic layer over the deterministic
 * RGEController/RGRandom base. EnemyAI13 is a "wander-then-bounce" enemy that
 * carries two BulletBoom payloads it detonates on death (ChildDead). Modelled
 * here (all pure; no Unity types):
 *   - Scout(): clears the chase target (target_obj = null, 0x7C). NO RNG draw.
 *   - RunReflection(): GATED on can_hit (0xAC). When can_hit == 0 the body
 *     re-schedules itself (Invoke) and takes NO draw; when can_hit != 0 it draws
 *     two rg_random.Range(-1f, 1f) floats, normalizes them, and stores the result
 *     as move_direction (0x74). This gate decides whether two draws are taken.
 *   - ChildDead(): a once-only latch (boom_light, 0xAD). First call latches the
 *     flag and reports both BulletBoom payloads should detonate (boom 0xB0 always;
 *     secondBoom 0xB4 only when present). Re-entry is a no-op. NO RNG draws.
 *   - FixedUpdateStep(): the awake (0x18) gate + the inertial_vel (0x44) <= 1.0
 *     split. In the not-knockback branch a dead (0x38) enemy goes to sleep
 *     (awake = 0) and stops; otherwise it steers. In the knockback branch
 *     (inertial_vel > 1.0) the force term is composed and inertial_vel decays by
 *     friction (0x50). NO RNG draws.
 *
 * Owner concerns (NOT modelled, by design): Rigidbody2D.velocity writes,
 * Component.get_transform / Transform.get_position reads, RGRandom-driven facing,
 * Animator.SetBool("walk"/"reflect"), MonoBehaviour.Invoke("RunReflection"),
 * BulletBoom.StartBoom, and Component.get_gameObject in OnTriggerEnter2D. They are
 * referenced in comments at their decomp sites.
 *
 * NOT modelled (no recoverable pure logic / zero state writes / zero draws):
 *   - FixedRotation (game_full.c:680432): only builds a zero vector and tail-calls
 *     get_transform / get_position -- pure owner/transform concern, no field write.
 *   - OnTriggerEnter2D (game_full.c:680504): only a get_gameObject tail call.
 *
 * @see IL2CPP skeleton EnemyAI13.cs (can_hit @0xAC, boom_light @0xAD,
 *      boom @0xB0, secondBoom @0xB4);
 *      FAITHFUL: EnemyAI13 @ game_full.c:680238-680517.
 */
class EnemyAI13 {
public:
    /**
     * @brief Outcome of one FixedUpdateStep, mirroring the decomp's branch tree
     *        (EnemyAI13__FixedUpdate @ game_full.c:680238-680342).
     *
     * The whole body is gated on awake (0x18, line 680267). When awake, the split
     * is on inertial_vel (0x44) <= 1.0 (line 680268):
     *   - Asleep:   awake (0x18) == 0 -> entire body skipped; nothing happens
     *               (NO friction decay). Line 680267.
     *   - DeadStop: inertial_vel <= 1.0 AND dead (0x38) != 0 (line 680269): sets
     *               awake = 0 (line 680270), zeroes velocity, and returns (the
     *               steering below it does NOT run for a dead enemy). NO decay.
     *   - Steer:    inertial_vel <= 1.0 AND not dead: normal steering applies (the
     *               block at 680283-680307). NO decay.
     *   - Knockback: inertial_vel > 1.0 (line 680309): steering + force term are
     *               composed and inertial_vel *= friction (0x50) (line 680339).
     */
    enum class StepResult {
        Asleep,    ///< awake == 0: no-op (no decay, no velocity write).
        DeadStop,  ///< inertial_vel <= 1.0 && dead: sleep + zero velocity, no decay.
        Steer,     ///< inertial_vel <= 1.0 && not dead: normal steering, no decay.
        Knockback  ///< inertial_vel > 1.0: force composition + friction decay.
    };

    /// Wander component bounds (rg_random.Range(-1f, 1f) x2, max INCL).
    /// FAITHFUL: 0xbf800000 == -1.0f, 0x3f800000 == 1.0f (lines 680413, 680418).
    static constexpr float kWanderMin = -1.0F;
    static constexpr float kWanderMax = 1.0F;
    /// inertial_vel must NOT exceed this for the not-knockback branch (line 680268
    /// tests inertial_vel <= 1.0).
    static constexpr float kKnockbackThreshold = 1.0F;

    EnemyAI13() = default;

    /// Seed this enemy's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- mutable state (owning entity mirrors these into the real fields) ----
    bool Dead() const { return m_Dead; }       // 0x38
    void SetDead(bool dead) { m_Dead = dead; }

    bool Awake() const { return m_Awake; }      // 0x18
    void SetAwake(bool awake) { m_Awake = awake; }

    bool CanHit() const { return m_CanHit; }    // 0xAC
    void SetCanHit(bool canHit) { m_CanHit = canHit; }

    bool BoomLight() const { return m_BoomLight; } // 0xAD
    void SetBoomLight(bool v) { m_BoomLight = v; }

    /// Whether the enemy currently has a chase target (target_obj, 0x7C).
    bool HasTarget() const { return m_HasTarget; }

    /// Last wander direction chosen by RunReflection (move_direction, 0x74).
    glm::vec2 MoveDirection() const { return m_MoveDirection; }

    float InertialVel() const { return m_InertialVel; } // 0x44
    /// Seed/clear knockback (owner calls when GetForce lands; clamp is base 28).
    void SetInertialVel(float vel) { m_InertialVel = vel; }

    /**
     * @brief Result of a ChildDead call: which payloads the owner should detonate.
     *
     * The decomp always StartBooms boom (0xB0) on the first (un-latched) call, and
     * also StartBooms secondBoom (0xB4) only when it is non-null (op_Implicit == 1,
     * line 680490). A re-entrant call (boom_light already set, line 680476) detonates
     * nothing.
     */
    struct ChildDeadResult {
        bool latched = false;     ///< true on the first call (boom_light flipped 0->1).
        bool detonateBoom = false;       ///< StartBoom(boom, 0xB0).
        bool detonateSecondBoom = false; ///< StartBoom(secondBoom, 0xB4) (if present).
    };

    /**
     * @brief Target-acquisition tick. FAITHFUL: EnemyAI13__Scout @ game_full.c:680356.
     *
     * Clears the chase target (target_obj = null, 0x7C, line 680363). The tail is a
     * get_transform tail-call (Subroutine does not return, line 680365) and is NOT
     * modelled. NO RNG draws here. (Confirmed: no rg_random call in this body.)
     */
    void Scout();

    /**
     * @brief Re-pick a random wander direction. FAITHFUL: EnemyAI13__RunReflection
     *        @ game_full.c:680370.
     *
     * GATED on can_hit (0xAC, line 680391): when can_hit == 0 the decomp re-Invokes
     * "RunReflection" after scout_rate (0x98, line 680392) and returns WITHOUT any
     * RNG draw -- so the gated path takes ZERO draws and must skip both. When
     * can_hit != 0 it (optionally reads target_obj position for facing -- owner
     * concern, lines 680400-680408) then draws two rg_random.Range(-1f, 1f) floats
     * (lines 680413, 680418), builds a Vector2 and normalizes it (FUN_00fa16ec +
     * FUN_00fa1e04), stores it as move_direction (set_move_direction, line 680421),
     * and the owner SetBools the "reflect" anim (Animator.SetBool, line 680427).
     * @param outDir  filled with the new normalized move_direction when active.
     * @return true if the active (can_hit) path ran and the two draws were taken;
     *         false when gated (no draw taken -- keeps the stream lockstep).
     */
    bool RunReflection(glm::vec2 &outDir);

    /**
     * @brief Detonate-on-death latch. FAITHFUL: EnemyAI13__ChildDead @
     *        game_full.c:680467.
     *
     * Once-only (boom_light, 0xAD, line 680476): a re-entrant call (boom_light != 0)
     * returns immediately and detonates nothing. The first call latches boom_light = 1
     * (line 680479) and reports boom (0xB0) should detonate (StartBoom, line 680484);
     * it ALSO reports secondBoom (0xB4) when @p hasSecondBoom is true (op_Implicit == 1,
     * line 680490 -> StartBoom, line 680498). NO RNG draws.
     * @param hasSecondBoom whether secondBoom (0xB4) is present (non-null).
     * @return which payloads the owner should StartBoom (see ChildDeadResult).
     */
    ChildDeadResult ChildDead(bool hasSecondBoom);

    /**
     * @brief One FixedUpdate physics step. FAITHFUL: EnemyAI13__FixedUpdate @
     *        game_full.c:680238.
     *
     * Models only the pure scalar/branch logic, not the Rigidbody2D writes. The
     * whole body is gated on awake (0x18, line 680267): not awake -> complete no-op
     * (NO friction decay). When awake, the split is on inertial_vel (0x44) <= 1.0
     * (line 680268):
     *   - DeadStop  while inertial_vel <= 1.0 AND dead (0x38) != 0 (line 680269):
     *               the decomp sets awake = 0 (line 680270), zeroes velocity and
     *               returns; the steering below does NOT run. NO decay.
     *   - Steer     while inertial_vel <= 1.0 AND not dead: normal steering (the
     *               block at 680283-680307). NO decay.
     *   - Knockback while inertial_vel > 1.0 (line 680309): the force term is
     *               composed with steering and inertial_vel decays by friction
     *               (0x50): inertial_vel *= friction (line 680339). (No early
     *               return here -- the decay is the tail of this branch.)
     * @param friction the friction field (0x50), inertia decay multiplier.
     * @return the StepResult naming which of the four outcomes occurred.
     */
    StepResult FixedUpdateStep(float friction);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};

    bool m_Dead = false;      // 0x38
    bool m_Awake = false;     // 0x18
    bool m_CanHit = false;    // 0xAC
    bool m_BoomLight = false; // 0xAD
    bool m_HasTarget = false; // target_obj != null (0x7C)

    float m_InertialVel = 0.0F;            // 0x44
    glm::vec2 m_MoveDirection{0.0F, 0.0F}; // 0x74
};

} // namespace Game

#endif /* GAME_ENEMY_AI13_HPP */
