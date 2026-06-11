#ifndef GAME_BULLET_LATER_FIXED_TARGET_HPP
#define GAME_BULLET_LATER_FIXED_TARGET_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class BulletLaterFixedTarget
 * @brief Faithful, engine-free brain for the "BulletLaterFixedTarget" projectile
 *        -- a delayed-seek bullet that, on FindTarget, brakes to a stop and
 *        re-arms its own target lock after a randomized delay.
 *
 * Per-content port. The decompile exposes exactly two bodies:
 *
 *  - BulletLaterFixedTarget__Start (game_full.c:963396): an il2cpp static-init
 *    guard followed by a single UnityEngine.Component.get_transform(this) read.
 *    No scalar/timer/state math -- entirely owner-side, nothing to port.
 *
 *  - BulletLaterFixedTarget__FindTarget (game_full.c:963420):
 *        delay = *(float*)(this + 0x44);                       // delay field
 *        d     = UnityEngine.Random.Range(delay, delay + delay); // engine RNG
 *        this.Invoke("<method>", d);                           // owner: schedule
 *        rb = *(int*)(this + 0x10);                            // rigidbody ref
 *        rb.velocity = Vector2.zero;                           // owner: brake
 *        *(byte*)(this + 0x50) = 0;                            // owner: clear flag
 *    The ONLY recoverable, deterministic, pure-scalar fragment is the bounds
 *    handed to the (engine) RNG: the re-seek is scheduled at a uniformly random
 *    time in [delay, 2*delay]. That window is a pure function of the delay field;
 *    we model it here. Everything else -- the actual draw, the Invoke scheduling,
 *    the velocity-zero brake, and the 0x50 flag clear -- is owner-side and is
 *    referenced in comments only.
 *
 * RNG NOTE (faithfulness-critical): FindTarget draws from UnityEngine.Random
 * (Unity's GLOBAL engine RNG), NOT from the game's deterministic per-instance
 * RGRandom. These are different streams. Modelling that draw through this
 * module's RGRandom would be fabrication (wrong stream, invented draw), so this
 * unit makes ZERO RGRandom draws: it recovers only the [min, max] window the
 * engine RNG is fed. The RGRandom member is carried solely for interface parity
 * (and to let a caller confirm the seed) and is never advanced here.
 *
 * @see IL2CPP BulletLaterFixedTarget; FAITHFUL: BulletLaterFixedTarget @
 *      game_full.c:963396 (Start), :963420 (FindTarget).
 */
class BulletLaterFixedTarget {
public:
    BulletLaterFixedTarget() = default;

    /// Seed the deterministic stream (kept untouched; no draw is ever made here).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // --- FindTarget: the re-seek delay window ------------------------------

    /**
     * @brief Lower bound of the re-seek delay window (seconds).
     *
     * FAITHFUL: BulletLaterFixedTarget__FindTarget @ game_full.c:963434. The
     * decomp passes `min = *(float*)(this + 0x44)` (the delay field) as the first
     * argument to UnityEngine.Random.Range, so the minimum scheduled delay is
     * exactly the field value.
     * @param delay the bullet's delay field (this + 0x44), seconds.
     * @return @p delay unchanged (the Range min).
     */
    static float InvokeDelayMin(float delay);

    /**
     * @brief Upper bound of the re-seek delay window (seconds).
     *
     * FAITHFUL: BulletLaterFixedTarget__FindTarget @ game_full.c:963434. The
     * decomp passes `max = fVar3 + fVar3` (i.e. delay + delay) as the second
     * argument to UnityEngine.Random.Range, so the maximum scheduled delay is
     * twice the field value. (This is the literal `fVar3 + fVar3`, not a `2 *`
     * multiply, and is reproduced as an add to stay byte-faithful.)
     * @param delay the bullet's delay field (this + 0x44), seconds.
     * @return @p delay + @p delay (the Range max).
     */
    static float InvokeDelayMax(float delay);

    /**
     * @brief Map a normalized RNG sample to the scheduled re-seek delay.
     *
     * FAITHFUL: BulletLaterFixedTarget__FindTarget @ game_full.c:963434,:963435.
     * Given the engine RNG's [0,1] sample @p t, the Invoke delay is the linear
     * interpolation across the [min, max] window: min + t*(max - min). The actual
     * sample is drawn from UnityEngine.Random (engine global stream) -- this
     * module does NOT draw it; @p t is supplied by the owner so no RGRandom draw
     * is invented here. With @p t in [0,1] the result lies in [delay, 2*delay].
     * @param delay the bullet's delay field (this + 0x44), seconds.
     * @param t     the engine RNG sample in [0,1] (owner-supplied; no draw made).
     * @return the scheduled Invoke delay, seconds.
     */
    static float ScheduledDelay(float delay, float t);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{}; ///< deterministic stream; never advanced here (engine RNG draws the delay).
};

} // namespace Game

#endif /* GAME_BULLET_LATER_FIXED_TARGET_HPP */
