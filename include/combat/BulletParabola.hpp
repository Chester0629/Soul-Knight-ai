#ifndef GAME_BULLET_PARABOLA_HPP
#define GAME_BULLET_PARABOLA_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class BulletParabola
 * @brief Faithful, engine-free timing brain for the "BulletParabola" projectile
 *        (a thrown / lobbed bullet whose visual position follows an arc each
 *        FixedUpdate).
 *
 * Per-content port. Models ONLY the pure, unit-testable scalar/timer/state math
 * recoverable from the three decompiled bodies; every Unity side effect
 * (Component.get_gameObject, Component.get_transform, Transform position write,
 * GetSourceObject lookup, Time.fixedDeltaTime read) is left to the owning entity
 * and referenced in comments only.
 *
 * WHAT THE DECOMP SHOWS (no reconstruction of unseen logic):
 *
 *  - Awake (BulletParabola__Awake @ game_full.c:963473): a static-init guard
 *    followed by Component.get_gameObject (subroutine does not return). PURE
 *    OWNER: caches the bullet's GameObject. NO recoverable scalar, NO RGRandom
 *    draw. Not modelled.
 *
 *  - SetTargetPosition (BulletParabola__SetTargetPosition @ game_full.c:960637):
 *    GetSourceObject(this) -> get_transform on that object (subroutine does not
 *    return). PURE OWNER: reads the source object's Transform to seed the throw
 *    target. NO recoverable scalar, NO RGRandom draw. Not modelled.
 *
 *  - FixedUpdate (BulletParabola__FixedUpdate @ game_full.c:963502): the only
 *    body with recoverable math. Structure (fields by offset):
 *        active = flag(0x20) != 0;             // a "moving/awake" bool
 *        counter = active ? int(0x1c) : 0;     // an int read only when active
 *        if (!active || counter == 0) {        // GATE
 *            rate = float(0x4c);               // per-second progress rate
 *            dt   = Time.fixedDeltaTime;        // owner-driven step
 *            accum(0x54) += rate * dt;          // <-- the recoverable accumulator
 *            get_transform(this);               // owner: arc position write
 *        }
 *        get_transform(this);                   // owner: unconditional tail
 *    The accumulator `progress(0x54) += rate(0x4c) * fixedDeltaTime` is the
 *    deterministic timer that the owner's arc/parabola position read consumes.
 *    The two get_transform calls (and the Transform write the arc would perform)
 *    are OWNER-side. Per the module note we recover the deterministic
 *    accumulator, not the transform write.
 *
 * The recoverable pure brain is therefore exactly one thing: the GATED
 * deltaTime progress accumulator. The arc-shaping math itself lives entirely
 * inside the owner's get_transform consumer and is not present as scalar code
 * in any of the three bodies, so it is not reconstructed here (doing so would
 * be fabrication).
 *
 * Determinism: none of the three recovered bodies calls rg_random, so this unit
 * makes ZERO RNG draws (the RGRandom member is carried only for interface parity
 * and to let a caller confirm the seed without ever advancing the stream).
 *
 * @see FAITHFUL: BulletParabola__FixedUpdate @ game_full.c:963502.
 */
class BulletParabola {
public:
    BulletParabola() = default;

    /// Seed the deterministic stream (kept untouched; no draw is ever made).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // --- FixedUpdate: the gated progress accumulator -----------------------

    /**
     * @brief The FixedUpdate gate predicate.
     *
     * FAITHFUL: BulletParabola__FixedUpdate @ game_full.c:963514-963519. The
     * accumulator block runs when `!active || counter == 0`, where @p active is
     * the bool at 0x20 and @p counter is the int at 0x1c (the decomp reads 0x1c
     * only when @p active is set; when @p active is false `counter` is forced to
     * 0, but the `!active` term already short-circuits the gate true). Returns
     * true when the accumulator should advance this fixed step.
     *
     * @param active  the bullet's "moving/awake" flag (0x20).
     * @param counter the int at 0x1c (only meaningful when @p active).
     * @return true if this fixed step should advance the progress accumulator.
     */
    static bool ShouldAccumulate(bool active, int counter);

    /**
     * @brief One FixedUpdate progress step: progress += rate * fixedDeltaTime.
     *
     * FAITHFUL: BulletParabola__FixedUpdate @ game_full.c:963520-963522, the
     * `*(0x54) = *(0x54) + *(0x4c) * Time.fixedDeltaTime` accumulator. This is a
     * pure scalar: @p dt is the owner-driven Time.fixedDeltaTime (modelled as the
     * Tick input). The trailing get_transform that turns this progress into an
     * arc position is OWNER-side and not performed here.
     *
     * Respects the gate: when @p active && counter != 0 the decomp does NOT run
     * the accumulator, so this method leaves the progress unchanged in that case.
     *
     * @param dt the fixed delta time for this step (Time.fixedDeltaTime, owner).
     * @return the live progress value (0x54) after this step.
     */
    float Tick(float dt);

    // --- live state (mirrors the BulletParabola instance fields) -----------

    /// Set the "moving/awake" flag (field 0x20) read by the FixedUpdate gate.
    void SetActive(bool active) { m_Active = active; }
    /// True when the bullet is flagged active (field 0x20).
    bool Active() const { return m_Active; }

    /// Set the int at 0x1c that the gate consults while the bullet is active.
    void SetCounter(int counter) { m_Counter = counter; }
    /// The int at field 0x1c.
    int Counter() const { return m_Counter; }

    /// Set the per-second progress rate (field 0x4c).
    void SetRate(float rate) { m_Rate = rate; }
    /// The per-second progress rate (field 0x4c).
    float Rate() const { return m_Rate; }

    /// The accumulated progress (field 0x54) the owner's arc read consumes.
    float Progress() const { return m_Progress; }
    /// Reset the accumulated progress (field 0x54) to zero.
    void ResetProgress() { m_Progress = 0.0F; }

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};         ///< deterministic stream; never advanced here.
    bool m_Active = false;    ///< "moving/awake" flag (0x20).
    int m_Counter = 0;        ///< int gate field (0x1c).
    float m_Rate = 0.0F;      ///< per-second progress rate (0x4c).
    float m_Progress = 0.0F;  ///< progress accumulator (0x54).
};

} // namespace Game

#endif /* GAME_BULLET_PARABOLA_HPP */
