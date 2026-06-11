#ifndef GAME_ITEMWISHINGWELL_HPP
#define GAME_ITEMWISHINGWELL_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class ItemWishingWell
 * @brief Faithful port of the RECOVERABLE decision / state / RNG math of Soul
 *        Knight 1.7.10's @c ItemWishingWell interactable (the "wishing well"
 *        chest the player charges up and then opens for a random reward).
 *
 * @par Scope (why this is a PARTIAL port)
 * The decompiled class is overwhelmingly owner / network plumbing:
 *  - @c OnItemTriggerSuccess, @c CreateObject and @c Reset are pure
 *    RGGameProcess-singleton / coroutine-spawn calls with no recoverable state.
 *  - @c MoveNext is a coroutine state machine whose yields (WaitForSeconds) and
 *    object instantiation are engine-side.
 * Only four things in the class are real, recoverable logic, and they are the
 * only things modelled here:
 *  1. @c Trigger() - the @c Triggerable counter: each trigger increments
 *     @c trigger_count (field @0x3c) by one; reaching exactly @b 2 is the gate
 *     that fires the success path. (ItemWishingWell__Triggerable @0x... )
 *  2. @c FailPhase() - the @c OnItemTriggerFail branch classification on that
 *     same counter (==2 vs ==1 vs other); the actions it selects are owner-side
 *     (UI canvas vs transform) but WHICH branch is taken is decidable here.
 *  3. @c NextStep() - the @c MoveNext step selector that maps the coroutine's
 *     incoming state (1 -> open-chest step, 0 -> wait step) onto its action.
 *  4. @c OpenChest() - the ONE deterministic RNG draw in the whole class:
 *     @c RGRandom.Range(0, pool_size) picking the reward index out of the
 *     chest's item pool (field @0x38).
 *
 * @par What is intentionally NOT modelled (owner-side, cited at the call sites)
 *  - The reward pool itself, instantiation of the reward, UI canvas, transforms,
 *    the RGGameProcess "is host / not-spectator" gate, and the coroutine timing.
 *  - The chest @b pool_size (field @0x38) and the inter-step @b wait delay are
 *    never written by any recovered body; they are owner-set prefab data. They
 *    are exposed as caller-supplied / TODO[verify] values, never invented here.
 *
 * All randomness flows through one seeded @ref RGRandom so the same seed
 * reproduces the same reward index - the determinism the original relies on for
 * networked loot.
 */
class ItemWishingWell {
public:
    /**
     * @brief Number of @ref Trigger calls that arms the success path.
     *
     * FAITHFUL: ItemWishingWell__Triggerable - the counter is compared @c ==2.
     */
    static constexpr int kTriggerThreshold = 2;

    /**
     * @enum FailPhase
     * @brief Classification of @c OnItemTriggerFail by the trigger counter.
     *
     * The decomp branches on the SAME counter @ref Trigger increments:
     *  - counter @c ==2 -> @ref Armed (owner shows the UI canvas);
     *  - counter @c ==1 -> @ref Charging (owner pokes the transform);
     *  - anything else  -> @ref Idle (no-op).
     * Only the branch SELECTION is ported; the owner actions are not.
     */
    enum class FailPhase {
        Idle,     ///< counter not in {1,2}: fail does nothing.
        Charging, ///< counter ==1: owner-side transform poke.
        Armed     ///< counter ==2: owner-side UI canvas.
    };

    /**
     * @enum Step
     * @brief The coroutine @c MoveNext step selector outcome.
     *
     * FAITHFUL: ItemWishingWell_<>c__Iterator0__MoveNext maps the iterator's
     * incoming state word to an action:
     *  - state 0 -> @ref Wait  (yield a WaitForSeconds; delay is owner data);
     *  - state 1 -> @ref Open  (call OpenChest);
     *  - else    -> @ref Done  (iterator finished).
     */
    enum class Step {
        Done, ///< incoming state not in {0,1}: coroutine is finished.
        Wait, ///< incoming state 0: yield the (owner-timed) wait.
        Open  ///< incoming state 1: perform the OpenChest draw.
    };

    /// Construct an unseeded well. Seed via @ref SetSeed before @ref OpenChest.
    ItemWishingWell() = default;

    /**
     * @brief Seed the deterministic reward stream.
     * @param seed Network-authoritative seed (as in the original).
     */
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }

    /// @return Whether the reward stream has been seeded.
    bool Seeded() const { return m_Rng.Seeded(); }

    /// @return Current trigger counter (field @0x3c), starts at 0.
    int TriggerCount() const { return m_TriggerCount; }

    /**
     * @brief Register one trigger; returns whether THIS trigger armed success.
     * @return true exactly when the counter reaches @ref kTriggerThreshold (2).
     *
     * Faithful to @c Triggerable: it does @c counter += 1 then tests
     * @c counter == 2. Only on that equality does the original run its (owner)
     * success path; this returns that boolean and never re-fires (a third
     * trigger pushes the counter to 3 and returns false). NO RNG.
     */
    bool Trigger();

    /**
     * @brief Classify an @c OnItemTriggerFail given the current counter.
     * @return The @ref FailPhase the original would branch into. NO RNG, no
     *         state change (read-only, exactly like the decomp's fail branch).
     */
    FailPhase ClassifyFail() const;

    /**
     * @brief Map a coroutine incoming-state word to its @ref Step.
     * @param incomingState The iterator's state field (@0x14) BEFORE this tick.
     * @return @ref Step::Wait for 0, @ref Step::Open for 1, else @ref Step::Done.
     *
     * Faithful to @c MoveNext's state-to-selector table. Pure; NO RNG. The
     * WaitForSeconds delay and the actual OpenChest call are owner-side.
     */
    static Step NextStep(int incomingState);

    /**
     * @brief Draw the reward index out of a pool of @p poolSize entries.
     * @param poolSize The chest's item-pool size (owner-set field @0x38). Must
     *                 be > 0; the original asserts (FUN_010b7dcc) on a 0 pool.
     * @return @c RGRandom.Range(0, poolSize) - a single int draw in
     *         [0, poolSize) (max EXCLUSIVE), or 0 when @p poolSize <= 0 (the
     *         degenerate guard; the original would assert instead).
     *
     * This is the ONE RNG draw of the class. Exactly one int draw, in order.
     * The pool itself and what the index maps to are owner data.
     */
    int OpenChest(int poolSize);

private:
    RGRandom m_Rng;        ///< rg_random @0x14 - the seeded reward stream.
    int m_TriggerCount = 0; ///< trigger_count @0x3c, starts at 0.
};

} // namespace Game

#endif /* GAME_ITEMWISHINGWELL_HPP */
