#ifndef GAME_ENEMY_AI06_HPP
#define GAME_ENEMY_AI06_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class EnemyAI06
 * @brief Faithful decision/cadence brain for EnemyAI06 (an RGEController subclass).
 *
 * EnemyAI06 is a multi-part / child enemy (a body part of a composite enemy: it
 * has a ChildDead hook that de-activates the part, and a DeadEvent that spawns a
 * weapon drop on death). Its "thinking" is a scout -> reflect-shoot cadence:
 * Scout() drops the chase target and re-detects (tail truncated); ShootReflection()
 * draws a single random aim/spread roll and schedules the next reflection shot.
 *
 * Modelled here (all pure; no Unity types):
 *   - Scout(): the dead/dizzy gate; clears the chase target (target_obj = null,
 *     0x7C). NO RNG draw in the recoverable head -- the re-detect/reflection
 *     continuation is an inlined/indirect (jump-table, unaff_ register) tail that
 *     is NOT faithfully recoverable and is NOT modelled (see fabricationFlags).
 *   - ShootReflection(): the dead/dizzy gate; when active, the single
 *     rg_random.Range(0, 10) roll (only drawn when rg_random != null). The tail
 *     (FUN_010b7dcc) is truncated and not modelled. This is the ONE clean RNG
 *     draw in the whole class.
 *   - ChildDead(): clears awake (0x18 = false) -- this part stops thinking.
 *   - EndCycle(): zeroes move_direction (0x74) and reports the not-dead gate
 *     (0x38) that guards the trailing virtual call. Owner: CancelInvoke of the
 *     reflection cadence. No RNG.
 *
 * Owner concerns (NOT modelled, by design): FixedRotation() (pure Unity
 * transform "face the target", no scalar logic, no draw); DeadEvent()
 * (Instantiate<RGWeapon> + GetComponent<OffensiveInterface>, pure Unity, no
 * draw); the Animator/Rigidbody2D writes; MonoBehaviour.Invoke/CancelInvoke
 * scheduling; the raycast detect loop. Those are referenced in comments at their
 * decomp sites.
 *
 * @see IL2CPP skeleton EnemyAI06.cs (dead_obj field, Scout/ShootReflection/Atk/
 *      FixedRotation/ChildDead/DeadEvent/EndCycle/Dizzy overrides).
 * @see FAITHFUL: EnemyAI06 @ game_full.c:677434-677667.
 */
class EnemyAI06 {
public:
    /// Re-roll ceiling drawn by ShootReflection (rg_random.Range(0, 10), max EXCL).
    static constexpr int kShootRerollCeiling = 10;

    EnemyAI06() = default;

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

    /// Whether the enemy currently has a chase target (target_obj, 0x7C).
    bool HasTarget() const { return m_HasTarget; }

    /// Last move_direction written (0x74); EndCycle zeroes it.
    glm::vec2 MoveDirection() const { return m_MoveDirection; }
    void SetMoveDirection(glm::vec2 dir) { m_MoveDirection = dir; }

    /// Whether this enemy owns an rg_random stream (0x0C != null). The decomp
    /// guards the ShootReflection draw on this; when false, NO draw is taken.
    bool HasRng() const { return m_HasRng; }
    void SetHasRng(bool has) { m_HasRng = has; }

    /**
     * @brief Target-acquisition tick. FAITHFUL: EnemyAI06__Scout @ game_full.c:677434.
     *
     * Gate (lines 677444-677451): returns while dead (0x38) OR dizzy (0xA1)
     * (proceed only when not-dead AND not-dizzy). When active, clears the chase
     * target (target_obj = null, 0x7C, line 677452). The decomp then tail-calls
     * get_transform (line 677454) and the re-detect/reflection continuation
     * (FUN_007ecec0) is an inlined/indirect jump-table tail that is NOT
     * recoverable -> NOT modelled. There is NO RNG draw in the recoverable head.
     * @return true if the scout body ran (was not gated); false if gated.
     */
    bool Scout();

    /**
     * @brief Reflection-shoot roll. FAITHFUL: EnemyAI06__ShootReflection @
     *        game_full.c:677523.
     *
     * Gate (lines 677533-677538): runs only when not-dead (0x38) AND not-dizzy
     * (0xA1). When active AND rg_random != null (0x0C, line 677539), draws a
     * single rg_random.Range(0, 10) (line 677541). When rg_random IS null, the
     * decomp skips the draw (the draw is inside the rg_random != null guard). The
     * tail (FUN_010b7dcc, line 677544) is truncated and not modelled.
     * @param outRoll filled with the Range(0,10) roll when a draw was taken.
     * @return true if a draw was taken (active AND rg_random != null).
     */
    bool ShootReflection(int &outRoll);

    /**
     * @brief Body-part death hook. FAITHFUL: EnemyAI06__ChildDead @
     *        game_full.c:677601.
     *
     * Clears awake (0x18 = false, line 677608): this part stops thinking. Owner:
     * get_transform tail (line 677610). No gate, no RNG.
     */
    void ChildDead();

    /**
     * @brief Stop the AI cycle. FAITHFUL: EnemyAI06__EndCycle @ game_full.c:677645.
     *
     * Owner: CancelInvoke of the reflection cadence (StringLiteral_6549, line
     * 677655). Pure: move_direction = Vector2.zero (0x74, lines 677661-677662).
     * Then, when NOT dead (0x38 == 0, line 677663), the decomp makes a trailing
     * virtual call (vtable +0x104, line 677664) -- modelled here only as the
     * not-dead gate report (the virtual target is an owner concern). No RNG.
     * @return true if the not-dead virtual call would fire (i.e. not dead).
     */
    bool EndCycle();

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};

    bool m_Dead = false;      // 0x38
    bool m_Dizzy = false;     // 0xA1
    bool m_Awake = false;     // 0x18
    bool m_HasTarget = false; // target_obj != null (0x7C)
    bool m_HasRng = true;     // rg_random != null (0x0C)

    glm::vec2 m_MoveDirection{0.0F, 0.0F}; // 0x74
};

} // namespace Game

#endif /* GAME_ENEMY_AI06_HPP */
