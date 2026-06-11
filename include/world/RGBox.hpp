#ifndef GAME_RGBOX_HPP
#define GAME_RGBOX_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class RGBox
 * @brief Pure, recoverable damage/destruction state of Soul Knight 1.7.10's
 *        @c RGBox (the destructible loot crate / barrel).
 *
 * Faithful port of the ONLY recoverable logic in the original @c RGBox class:
 * the @c Hit damage-resolution state machine (HP gate, HP decrement, hit-flag
 * return, and the below-zero "should destroy" signal) plus the trivial
 * @c SetSourceObject reference setter. Everything else the original @c RGBox
 * does - spawning the dropped item (@c CreateItem resolves the
 * @c RGGameProcess singleton then Instantiates), the @c BoxDestroy GameObject
 * name comparison + despawn, and the virtual on-death dispatch reached through
 * the object vtable - is owner-side (Unity Instantiate / PrefabPool / Object
 * name reflection) and is intentionally NOT modelled here.
 *
 * @par Owner-side methods (left to the orchestrator, with cited evidence)
 *  - @c RGBox__CreateItem @ game_full.c:468712 - resolves
 *    @c Singleton<RGGameProcess>.Inst and Instantiates the drop; the
 *    decompiled tail is a non-returning singleton/Instantiate call with no
 *    recoverable scalar or RNG draw. OWNER-side.
 *  - @c RGBox__BoxDestroy @ game_full.c:468688 - reads the bound GameObject
 *    (field 0x10), fetches its @c name, and string-compares it to a literal;
 *    the body is pure Unity Object reflection and despawn. OWNER-side.
 *  - The below-zero branch in @c Hit calls a virtual through
 *    @c (*this+0xd4)(this, source, *(this+0xd8)) - an owner vtable death
 *    dispatch. We expose its CONDITION (HP fell below 1) via @ref HitResult
 *    so the owner can fire that dispatch; we do not model the call itself.
 *
 * There is NO @c RGRandom draw anywhere in @c RGBox: @c Hit, @c SetSourceObject,
 * @c BoxDestroy and @c CreateItem advance no random stream. A same-seeded
 * parallel @ref RGRandom therefore stays bit-identical across any sequence of
 * @c RGBox calls (asserted in the test). The seeded stream is carried here only
 * to honour the project's brain-wrapper convention (@ref SetSeed); it is never
 * drawn from by this class.
 */
class RGBox {
public:
    /**
     * @struct HitResult
     * @brief Outcome of a single @ref Hit, recovered verbatim from
     *        @c RGBox__Hit's return value and branch structure.
     */
    struct HitResult {
        /// True iff the hit registered: the source was a live object AND the box
        /// still had HP (>0) when struck. Mirrors @c RGBox__Hit's @c uVar2 return
        /// (1 on a registered hit, 0 otherwise).
        bool registered = false;
        /// True iff this registered hit dropped HP below 1, i.e. the original
        /// would now fire its virtual on-death dispatch (owner-side BoxDestroy /
        /// CreateItem). False when @ref registered is false.
        bool shouldDestroy = false;
    };

    /// Construct a box with zero HP and no bound source. Call @ref SetHp before
    /// use; the original sets HP via its prefab/spawn path (owner-side).
    RGBox() = default;

    /**
     * @brief Brain-wrapper seeding hook (project convention).
     * @param seed Deterministic seed forwarded to the per-instance stream.
     *
     * @c RGBox itself never draws from this stream (no RNG appears in any
     * @c RGBox body); the seed is carried only so the class matches the
     * seeded-brain convention used across the port.
     */
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }

    /**
     * @brief Set the box's current durability/HP.
     * @param hp Remaining HP (the original's inline field at offset 0x0C).
     *
     * The original initialises this through its spawn/prefab path; we expose a
     * direct setter so @ref Hit can be exercised deterministically.
     */
    void SetHp(int hp) { m_Hp = hp; }

    /// @return Current remaining HP (field 0x0C in the original).
    int Hp() const { return m_Hp; }

    /// @return The bound source-object handle last set by @ref SetSourceObject
    ///         (field 0x14 in the original). 0 means "none".
    int SourceObject() const { return m_SourceObject; }

    /**
     * @brief Store the source-object reference.
     * @param sourceObject Opaque owner handle (a Unity Object reference in the
     *        original; modelled here as an integer handle).
     *
     * FAITHFUL to @c RGBox__SetSourceObject: a single field write
     * @c this[0x14] = sourceObject. No gate, no RNG.
     */
    void SetSourceObject(int sourceObject);

    /**
     * @brief Resolve a damage event against the box.
     * @param damage      Damage amount to subtract from HP.
     * @param sourceValid Whether the damage source is a live Unity Object - the
     *        original computes this via @c Object.op_Implicit(source) (a null
     *        check). The owner supplies the boolean; we faithfully gate on it.
     * @return The recovered @ref HitResult (registered + shouldDestroy).
     *
     * FAITHFUL to @c RGBox__Hit: gates on (source valid) AND (HP > 0); on a
     * registered hit, writes HP -= damage and returns registered=true; if the
     * new HP is below 1, signals shouldDestroy (the original's virtual on-death
     * dispatch). When the gate fails, NOTHING is written and the result is
     * {false, false}. No random draw is taken on either path.
     */
    HitResult Hit(int damage, bool sourceValid);

private:
    RGRandom m_Rng;          ///< Per-instance stream (never drawn from by RGBox).
    int m_Hp = 0;            ///< Remaining HP / durability (field 0x0C).
    int m_SourceObject = 0;  ///< Bound source-object handle (field 0x14).
};

} // namespace Game

#endif /* GAME_RGBOX_HPP */
