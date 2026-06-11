#ifndef GAME_BULLET_ROUNDABOUT_HPP
#define GAME_BULLET_ROUNDABOUT_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class BulletRoundabout
 * @brief Faithful, engine-free angle/coroutine brain for the "BulletRoundabout"
 *        projectile (a Bullet02 subclass: an orbiting / homing-arc bullet).
 *
 * Per-content port. Models ONLY the pure, unit-testable scalar/state math in the
 * two recovered bodies; every Unity side effect (Rigidbody2D velocity get/set,
 * Vector2 multiply, GetComponentInChildren, WaitForSeconds, Transform rotation,
 * bullet spawn) is left to the owning entity and referenced in comments only.
 *
 * WHAT THE DECOMP SHOWS (no reconstruction of unseen logic):
 *
 *  - AdjustmentAngle (the orbit-angle update; the named stub @ game_full.c:963631
 *    is only a static-init guard, the real ARM body is the unnamed FUN_00afe978
 *    @ game_full.c:963816). Pure angle math over the bullet's move_angle (0x64)
 *    and angle_speed (0x60):
 *        delta = target - move_angle;                 // angular difference
 *        delta = Mathf.DeltaAngle-style wrap(delta);  // into (-180, 180]
 *        if (angle_speed <= |delta|)                  // farther than one step
 *            move_angle += (delta <= 0 ? +angle_speed : -angle_speed); // step
 *        else
 *            move_angle -= delta;                     // snap: close the gap
 *    The preceding `if (sign < 0) target = -target` reads its sign from a register
 *    (unaff_r4) that the decomp does not pin to a field, and the trailing
 *    get_transform(move_angle * PI/180) rotation is the owner's job (both flagged).
 *    NO RGRandom draw.
 *
 *  - MoveNext (BulletRoundabout_<>c__Iterator0__MoveNext @ game_full.c:963646):
 *    a Gun008-shaped coroutine state machine. Iterator fields: state 0x20,
 *    `this` 0x14, loop-counter 0x8. Dispatch maps state s (<3) -> s+3:
 *        state 0 -> entry block (3), state 1 -> spin block (4), state 2 -> tail (5).
 *    Entry (state 0): clears the bullet's active flag (this+0x68 = 0), copies the
 *    spin repeat-count (this+0xc) into the iterator counter (0x8); if delay
 *    (this+0x48) > 0 it yields WaitForSeconds (owner). The spin block then runs a
 *    countdown loop: counter starts at this+0xc and decrements by 1.0 each
 *    iteration (each iteration applies a velocity multiply -- owner) until <= 0,
 *    after which the bullet's active flag is set (comp+0x68 = 1). NO RGRandom draw.
 *
 * The recoverable pure brain is therefore: (1) the per-step orbit-angle update,
 * and (2) the coroutine's spin-counter countdown + 3-state dispatch. Everything
 * else is an owner concern. This is a deliberately small, honest brain.
 *
 * Determinism: neither recovered body calls rg_random, so this unit makes ZERO
 * RNG draws (the RGRandom member is carried only for interface parity and to let
 * a caller confirm the seed without ever advancing the stream).
 *
 * @see IL2CPP Bullet02.cs (field names: angle_speed, move_angle, delay_time,
 *      limit_time/_limit_time); FAITHFUL: BulletRoundabout @ game_full.c:963631+.
 */
class BulletRoundabout {
public:
    /// Half-turn used by the DeltaAngle wrap (degrees). Engine intrinsic constant.
    static constexpr float kHalfTurnDegrees = 180.0F;
    /// Full turn used by the DeltaAngle wrap (degrees). Engine intrinsic constant.
    static constexpr float kFullTurnDegrees = 360.0F;

    /// MoveNext coroutine dispatch states (iterator field 0x20).
    enum class State : int {
        Entry = 0, ///< state 0 -> dispatch 3: clear flag, seed counter, optional delay.
        Spin = 1,  ///< state 1 -> dispatch 4: the velocity-multiply countdown loop.
        Tail = 2,  ///< state 2 -> dispatch 5: post-spin tail.
        Done = -1, ///< 0xffffffff: coroutine finished / no further yield.
    };

    BulletRoundabout() = default;

    /// Seed the deterministic stream (kept untouched; no draw is ever made).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // --- AdjustmentAngle: the orbit-angle update math ----------------------

    /**
     * @brief Mathf.DeltaAngle-style wrap of an angular difference into (-180, 180].
     *
     * FAITHFUL: BulletRoundabout AdjustmentAngle (FUN_00afe978 @ game_full.c:963816)
     * wrap block. `if (|delta| > 180) delta += (delta < 0 ? +360 : -360);`
     * (|delta| == 180 is left unchanged, matching the `< 180` compare).
     * @param delta raw `target - move_angle` (degrees).
     * @return the wrapped delta in (-180, 180].
     */
    static float WrapDeltaAngle(float delta);

    /**
     * @brief Advance move_angle one frame toward a target angle (orbit step).
     *
     * FAITHFUL: BulletRoundabout AdjustmentAngle step/snap branch. Computes
     * delta = WrapDeltaAngle(targetAngle - currentMoveAngle); if angle_speed
     * (degrees/step) is <= |delta| it steps move_angle by angle_speed in the
     * direction -sign(delta) (delta <= 0 -> +, delta > 0 -> -, exactly the
     * recovered Z||N branch); otherwise it snaps via `move_angle -= delta`.
     *
     * NOTE: the decomp's preceding `if (sign < 0) target = -target` reads its sign
     * from an un-pinned register (unaff_r4); this method takes the already
     * sign-resolved target so that source stays an owner input (see fabrication
     * flags). The trailing get_transform(move_angle * PI/180) rotation is owner.
     *
     * @param currentMoveAngle the bullet's move_angle (0x64), degrees.
     * @param targetAngle      the (sign-resolved) target angle, degrees.
     * @param angleSpeed       angle_speed (0x60), max degrees moved per step.
     * @return the updated move_angle (degrees) to store back into 0x64.
     */
    static float StepMoveAngle(float currentMoveAngle, float targetAngle,
                               float angleSpeed);

    // --- MoveNext: the orbit coroutine state machine -----------------------

    /**
     * @brief Map the stored iterator state (0x20) to its dispatch case.
     *
     * FAITHFUL: MoveNext header -- `s = state; state = -1; disp = (s < 3) ? s + 3
     * : 0;` then disp 3 = Entry block, 4 = Spin block, 5 = Tail, anything else
     * (the `if (disp != 5) return 0;` guard) = no-op. Returns the matching State.
     */
    static State Dispatch(State stored);

    /**
     * @brief Enter the coroutine (dispatch 3): seed the spin counter.
     *
     * FAITHFUL: MoveNext entry block -- clears the bullet active flag (this+0x68=0,
     * owner) and copies the spin repeat-count (this+0xc) into the iterator counter
     * (0x8). The optional `if (delay > 0) yield WaitForSeconds` is the owner's.
     * Loads m_Counter from @p spinCount and leaves the machine in the Spin state.
     * @param spinCount the bullet's spin repeat-count (this+0xc).
     */
    void BeginSpin(float spinCount);

    /**
     * @brief One iteration of the spin countdown loop (inside dispatch 4).
     *
     * FAITHFUL: MoveNext spin `while (counter > 0) { <velocity multiply -- owner>;
     * counter -= 1.0; }`. Returns true and decrements the counter by 1.0 when the
     * loop body should run this iteration (counter > 0); returns false when the
     * loop is exhausted (counter <= 0), at which point the bullet active flag is
     * set (comp+0x68 = 1, owner) and the machine advances to Done.
     * @return true if a spin step ran this call; false when the loop has ended.
     */
    bool SpinStep();

    /// Live coroutine state (mirrors iterator field 0x20).
    State CurrentState() const { return m_State; }
    /// Live spin counter (mirrors iterator field 0x8), in remaining iterations.
    float Counter() const { return m_Counter; }
    /// True once the spin loop has run to completion (active flag would be set).
    bool SpinComplete() const { return m_State == State::Done; }

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};                ///< deterministic stream; never advanced here.
    State m_State = State::Entry;    ///< iterator dispatch state (0x20).
    float m_Counter = 0.0F;          ///< spin loop counter (0x8); counts down by 1.
};

} // namespace Game

#endif /* GAME_BULLET_ROUNDABOUT_HPP */
