#ifndef GAME_BOSS_AI06_CHILD_HPP
#define GAME_BOSS_AI06_CHILD_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class BossAI06Child
 * @brief Faithful brain for the boss-06 "child" turret (a MonoBehaviour summon,
 *        NOT an RGEController subclass).
 *
 * Per-content port. BossAI06Child is a small satellite spawned by BossAI06: it
 * orbits a parent point, optionally breaks free to chase a target, and fires one
 * of three attacks (Atk1 / Atk02 / Atk03). Almost the whole class is owner-side
 * (Rigidbody2D move, Transform rotate, Animator, PrefabPool bullet spawns,
 * Object.Instantiate, RGMusicManager). The ONLY pure, deterministic, testable
 * decision in the class is the single attack-selector roll in GetToTarget.
 *
 * RNG faithfulness (THE load-bearing detail):
 *   - The class makes exactly ONE RNG draw of its own:
 *     BossAI06Child.GetToTarget -> UnityEngine.Random.Range(0, 2).
 *   - That is the GLOBAL Unity RNG (UnityEngine_Random__Range), NOT the
 *     per-instance RGRandom stream that RGEController bosses use. We model it
 *     with an injected RGRandom because RGRandom *is* Unity's Xorshift128
 *     generator (see RGRandom.hpp): same algorithm, same int max-EXCLUSIVE
 *     semantics, so a same-seeded stream replays the global draw bit-for-bit.
 *   - Range(0, 2) is max-EXCLUSIVE: it yields {0, 1} only. The selector therefore
 *     can only ever pick Atk1 (0) or Atk02 (1) here; Atk03 (2) is dispatched by
 *     the parent boss through Attack(2), never by this roll. We faithfully keep
 *     the ceiling at 2 so the draw count/range matches the original exactly.
 *   - The roll is GATED by free_move (field 0x28): when free_move is false,
 *     GetToTarget consumes NO draw (stream stays in lockstep).
 *
 * The parent-side BossAI06 bodies (ChildsCreateBullet*, Atk3CreateBullet's
 * RGRandom.Range(0,360), Dizzy on field 0xa1, ...) operate on the BOSS object,
 * not the child, and are out of scope for this brain.
 *
 * Field offsets (IL2CPP layout, header 0xC, decoded from BossAI06Child.cs order
 * cross-checked against every decomp access):
 *   0x14 parent_point (Transform)   0x18 target (Transform)
 *   0x28 free_move (bool)           0x29 get_target (bool)
 *   0x2a has_target (bool)          0x2b lock_target (bool)
 *   0x2c speed (float)              0x3c bullet03   0x40 the_aim   0x48 in_atk3
 *
 * @see recreation BossAI06Child.cs; offset table RGEController.cs (parent boss).
 *      FAITHFUL: BossAI06Child @ game_full.c:441486-441935.
 */
class BossAI06Child {
public:
    // ---- Attack dispatch codes (BossAI06Child.Attack switch on param_2) ----
    /// Attack(0) -> Atk1 (re-spawn aim crosshair, fire bullet01 fan).
    static constexpr int kAttackAtk1 = 0;
    /// Attack(1) -> Atk02 (PrefabPool bullet02 spread).
    static constexpr int kAttackAtk02 = 1;
    /// Attack(2) -> Atk03 (Instantiate bullet03 weapon). Reachable only via the
    /// parent boss; the GetToTarget roll (Range(0,2)) can never select it.
    static constexpr int kAttackAtk03 = 2;
    /// Value passed by GetToTarget that matches no case (>=3): Attack() no-ops.
    static constexpr int kAttackNone = -1;

    /// GetToTarget selector ceiling: UnityEngine.Random.Range(0, 2), max EXCLUSIVE.
    static constexpr int kSelectCeiling = 2;

    // ---- Constructor-initialised scalars (BossAI06Child..ctor) ----
    /// get_target (0x29) initial value = 1 (true).
    static constexpr bool kCtorGetTarget = true;
    /// lock_target (0x2b) initial value = 1 (true).
    static constexpr bool kCtorLockTarget = true;
    /// speed (0x2c) initial value = 10.0f (0x41200000).
    static constexpr float kCtorSpeed = 10.0F;

    BossAI06Child() = default;

    /// Seed the deterministic selector stream (models the global Unity RNG).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- Field accessors (only fields the decomp actually writes) ----
    bool FreeMove() const { return m_FreeMove; }
    bool GetTarget() const { return m_GetTarget; }
    bool HasTarget() const { return m_HasTarget; }
    bool LockTarget() const { return m_LockTarget; }
    float Speed() const { return m_Speed; }
    bool InAtk3() const { return m_InAtk3; }

    /// Test/owner hook: set free_move (the GetToTarget gate). The decomp gates on
    /// this field; SetFreeMove()/BackToParent() (owner-side) flip it.
    void SetFreeMove(bool v) { m_FreeMove = v; }

    /**
     * @brief FAITHFUL: BossAI06Child.GetToTarget -> the one decision in the class.
     *
     * Gate: free_move (0x28). When free_move is false the body's `if` is skipped
     * entirely, so NO draw is taken (the trailing get_transform is owner-side).
     * When free_move is true it draws UnityEngine.Random.Range(0, 2) and forwards
     * the result to Attack(value). Returns the value Attack() was dispatched with,
     * or kAttackNone when gated out (no draw, no attack).
     *
     * @return kAttackAtk1 / kAttackAtk02 when free_move; kAttackNone otherwise.
     */
    int GetToTarget();

    /**
     * @brief FAITHFUL: BossAI06Child.Attack(value) dispatch switch (no RNG).
     *
     * Pure jump table: 2 -> Atk03, 1 -> Atk02, 0 -> Atk1, anything else no-op.
     * Returns the dispatched attack code (== value for 0/1/2) or kAttackNone.
     * When value == 0 it runs the Atk1() field write (lock_target = 0); the rest
     * of every AtkNN body is owner-side (bullets / animator / PrefabPool). This
     * dispatch does NOT itself call into Atk02/Atk03 brain-side because those
     * bodies write no modelled field.
     */
    int Attack(int value);

    /**
     * @brief FAITHFUL: BossAI06Child.Atk1 -> lock_target (0x2b) = 0.
     *
     * The only field write inside any AtkNN body. The remainder (Destroy the_aim
     * @0x40, PrefabPool spawn of a fresh aim) is owner-side. Invoked by
     * Attack(kAttackAtk1); exposed directly for unit testing the write.
     */
    void Atk1();

    /**
     * @brief FAITHFUL: BossAI06Child.FindTarget -> has_target (0x2a) = 0.
     * The remainder (get_transform / detect cast) is owner-side.
     */
    void FindTarget();

    /**
     * @brief FAITHFUL: BossAI06Child.EndAtk03 -> in_atk3 (0x48) = 0.
     * The trailing get_transform is owner-side.
     */
    void EndAtk03();

    /**
     * @brief FAITHFUL: BossAI06Child..ctor -> get_target=1, lock_target=1,
     *        speed=10.0f. Resets the modelled scalars to their constructed state.
     * (The decomp ctor also runs a RaycastHit2D[] type-init; owner-side.)
     */
    void ResetToCtorState();

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};

    // Only fields BossAI06Child bodies actually write are modelled here.
    bool m_FreeMove = false;             ///< 0x28 free_move (GetToTarget gate)
    bool m_GetTarget = kCtorGetTarget;   ///< 0x29 get_target (ctor = 1)
    bool m_HasTarget = false;            ///< 0x2a has_target (FindTarget = 0)
    bool m_LockTarget = kCtorLockTarget; ///< 0x2b lock_target (ctor = 1)
    float m_Speed = kCtorSpeed;          ///< 0x2c speed (ctor = 10.0f)
    bool m_InAtk3 = false;               ///< 0x48 in_atk3 (EndAtk03 = 0)
};

} // namespace Game

#endif /* GAME_BOSS_AI06_CHILD_HPP */
