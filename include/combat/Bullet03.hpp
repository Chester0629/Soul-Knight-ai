#ifndef GAME_BULLET03_HPP
#define GAME_BULLET03_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class Bullet03
 * @brief Faithful, engine-free lifetime-progress / size-lerp accumulator for the
 *        "Bullet03" projectile (an RGBullet subclass: a growing/scaling bullet).
 *
 * Per-content port. Models ONLY the pure, unit-testable timer/scalar math in the
 * two recovered bodies. Every Unity side effect (Component.get_transform and the
 * localScale write it feeds, Time.deltaTime sampling) is left to the owning
 * entity and referenced in comments only.
 *
 * FIELD MAP (decompiled Bullet03.cs asset confirms the names behind the offsets):
 *   - a_time   (RGBullet timer, owner+0x50): the accumulating lifetime timer.
 *   - max_time (owner+0x4c): the lifetime duration / ratio divisor.
 *   - max_size (owner+0x48): the size the owner scales toward.
 *   - start_size: DECLARED on Bullet03 but NEVER read in Update -- so this brain
 *     deliberately does NOT model any start_size lerp (that would be fabrication).
 *   - awake    (RGBullet bool, owner+0x20): the active gate byte.
 *   - rotate_angle (RGBullet int, owner+0x1c): second gate; its body is an owner
 *     get_transform rotation, no recoverable scalar.
 *
 * WHAT THE DECOMP SHOWS (no reconstruction of unseen logic):
 *
 *  - Bullet03__Start @ game_full.c:962152: a static-init guard plus a bare
 *    Component.get_transform(owner). Pure owner side effect; NO recoverable scalar.
 *
 *  - Bullet03__Update @ game_full.c:962183:
 *        if (awake)                                  // byte 0x20 gate
 *          if (a_time < max_time) {                  // 0x50 < 0x4c
 *            a_time += Time.deltaTime;               // 0x50 += dt
 *            get_transform((a_time/max_time)*max_size, ...);   // OWNER localScale
 *          }
 *          if (rotate_angle != 0) get_transform(owner);        // OWNER rotation
 *    The recoverable deterministic math is the timer accumulation and the two
 *    scalars the owner consumes: progress = a_time/max_time and progress*max_size.
 *    The decomp accumulates and writes ONLY while a_time < max_time (strict), and
 *    applies NO clamp to the ratio -- a single step may push a_time past max_time,
 *    so the final progress can exceed 1; this brain reproduces that exactly.
 *    NO RGRandom draw in either body.
 *
 * Determinism: neither recovered body calls rg_random, so this unit makes ZERO
 * RNG draws (the RGRandom member is carried only for interface parity and to let
 * a caller confirm the seed without ever advancing the stream).
 *
 * @see decompiled Bullet03.cs (fields start_size, max_size, max_time, a_time);
 *      FAITHFUL: Bullet03__Update @ game_full.c:962183.
 */
class Bullet03 {
public:
    Bullet03() = default;

    /// Seed the deterministic stream (kept untouched; no draw is ever made).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // --- Configuration (the two fields the Update math reads) ---------------

    /**
     * @brief Set the lifetime duration (max_time, owner+0x4c) and target size
     *        (max_size, owner+0x48), and reset the timer to zero.
     *
     * FAITHFUL: Bullet03__Update reads max_time (0x4c) and max_size (0x48); they
     * are owner-configured per-bullet. a_time (0x50) starts at 0 for a fresh
     * bullet. start_size is intentionally absent (the Update body never reads it).
     * @param maxTime lifetime duration / ratio divisor (max_time, 0x4c).
     * @param maxSize size the owner scales toward (max_size, 0x48).
     */
    void Configure(float maxTime, float maxSize);

    // --- The recovered timer accumulator -----------------------------------

    /**
     * @brief Advance the lifetime timer by one frame (Time.deltaTime modelled as
     *        @p dt) and report whether the size-write branch ran.
     *
     * FAITHFUL: Bullet03__Update @ game_full.c:962183. Reproduces exactly:
     *   if (a_time < max_time) { a_time += dt; <owner scales by progress*max_size>; }
     * The accumulation and the owner write happen ONLY while a_time < max_time
     * (strict); once the cap is reached the timer freezes and this returns false.
     * The active (awake, 0x20) and rotate (0x1c) gates are owner state and are NOT
     * modelled here -- call this only when the owner's awake gate is set, exactly
     * as the original does. No clamp is applied to a_time (a final step may
     * overshoot max_time), matching the decomp.
     * @param dt the frame delta (Time.deltaTime), seconds.
     * @return true if the timer advanced and the owner size-write branch ran this
     *         frame; false if the timer was already at/over max_time (no write).
     */
    bool Tick(float dt);

    /**
     * @brief Lifetime progress = a_time / max_time (the ratio the owner lerps by).
     *
     * FAITHFUL: Bullet03__Update @ game_full.c:962198, the `a_time / max_time`
     * factor. NO clamp in the original. A non-positive max_time is impossible in a
     * real bullet (the owner configures a positive duration); this guards a
     * divide-by-zero by returning 0 in that degenerate case rather than NaN.
     * @return the current progress ratio (may exceed 1 after a final overshoot).
     */
    float Progress() const;

    /**
     * @brief The scaled size the owner writes: progress * max_size.
     *
     * FAITHFUL: Bullet03__Update @ game_full.c:962198, the
     * `(a_time/max_time) * max_size` value passed to the owner's transform write.
     * This is the literal recovered scalar -- the owner applies it to localScale.
     * @return Progress() * max_size.
     */
    float ScaledSize() const;

    /// Live lifetime timer (mirrors a_time, owner+0x50), seconds.
    float ATime() const { return m_ATime; }
    /// Configured lifetime duration (mirrors max_time, owner+0x4c).
    float MaxTime() const { return m_MaxTime; }
    /// Configured target size (mirrors max_size, owner+0x48).
    float MaxSize() const { return m_MaxSize; }
    /// True once the timer has reached/passed max_time (no further accumulation).
    bool Finished() const { return !(m_ATime < m_MaxTime); }

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};        ///< deterministic stream; never advanced here.
    float m_ATime = 0.0F;    ///< a_time (0x50): the accumulating lifetime timer.
    float m_MaxTime = 0.0F;  ///< max_time (0x4c): duration / ratio divisor.
    float m_MaxSize = 0.0F;  ///< max_size (0x48): size the owner scales toward.
};

} // namespace Game

#endif /* GAME_BULLET03_HPP */
