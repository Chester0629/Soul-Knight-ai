#ifndef GAME_RGBTDIVISION_HPP
#define GAME_RGBTDIVISION_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class RGBTDivision
 * @brief Faithful, engine-free decision/state brain for the "RGBTDivision"
 *        bullet trigger (a RGBulletTrigger subclass: a projectile that, on
 *        impact, splits into a fan of `count` child bullets -- a "division").
 *
 * Per-content port. Models ONLY the pure, unit-testable decision/state math in
 * the two recovered bodies; every engine side effect (CompareTag, the base
 * RGBulletTrigger.OnTriggerEnter2D dispatch, RGMusicManager.PlayEffect, the
 * PrefabPool child-bullet spawn + transform/Vector fan placement) is left to
 * the owning entity and referenced in comments only.
 *
 * FIELD MAP (IL2CPP dump.cs class RGBTDivision : RGBulletTrigger):
 *   audio_clip     @ 0x48 (AudioClip)  -- impact SFX (owner: PlayEffect).
 *   count          @ 0x4C (int)        -- number of child bullets to spawn.
 *   angle          @ 0x50 (int)        -- fan spread (owner: spawn placement).
 *   atk            @ 0x54 (int)        -- child attack (owner: spawn SetInfo).
 *   child_critical @ 0x58 (int)        -- child crit (owner: spawn SetInfo).
 *   bullet         @ 0x5C (GameObject) -- child prefab (owner: spawn).
 *   bullet_speed   @ 0x60 (float)      -- child speed (owner: spawn).
 *   destroyed      @ 0x64 (bool)       -- the once-only hit gate flag.
 *
 * WHAT THE DECOMP SHOWS (no reconstruction of unseen logic):
 *
 *  - OnTriggerEnter2D (RGBTDivision__OnTriggerEnter2D @ game_full.c:468941):
 *    calls the base RGBulletTrigger.OnTriggerEnter2D (owner), then runs two
 *    Component.CompareTag checks (owner). When the collided object matches one
 *    of the two recognised tags it sets the bullet's `destroyed` flag
 *    (this+0x64 = 1). The recoverable PURE part is exactly that flag set: a hit
 *    that matches arms the once-only `destroyed` state. NO RGRandom draw.
 *
 *  - Division (RGBTDivision__Division @ game_full.c:469069): gated on the same
 *    flag -- `if (destroyed == 0) { PlayEffect(audio_clip);  // owner
 *    if (-count < count) { <spawn `count` child bullets via PrefabPool> } }`.
 *    The gate `-count < count` is, for a content count, exactly `count > 0`
 *    (translated literally below). The PlayEffect and the PrefabPool fan spawn
 *    (the angle/speed placement lives entirely behind UnityEngine.Vector2/
 *    Vector3/Transform calls in the truncated owner tail -- FUN_005b7364) are
 *    owner concerns. The recoverable PURE part is the two-stage decision:
 *    `ShouldDivide = !destroyed && count > 0`. NO RGRandom draw.
 *
 * The recoverable pure brain is therefore: (1) arming the `destroyed` hit flag,
 * and (2) the `!destroyed && count > 0` divide decision. The fan angle math, the
 * music, and the child-bullet spawn are all owner concerns. This is a
 * deliberately small, honest brain.
 *
 * Determinism: neither recovered body calls rg_random, so this unit makes ZERO
 * RNG draws (the RGRandom member is carried only for interface parity and to let
 * a caller confirm the seed without ever advancing the stream).
 *
 * @see IL2CPP dump.cs RGBTDivision (field names above);
 *      FAITHFUL: RGBTDivision @ game_full.c:468941, 469069.
 */
class RGBTDivision {
public:
    RGBTDivision() = default;

    /// Seed the deterministic stream (kept untouched; no draw is ever made).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // --- OnTriggerEnter2D: the once-only hit-flag arm ----------------------

    /**
     * @brief Arm (or leave armed) the `destroyed` hit flag for a collision.
     *
     * FAITHFUL: RGBTDivision__OnTriggerEnter2D @ game_full.c:468941. After the
     * base dispatch and the two CompareTag checks (both owner), the decomp sets
     * `*(this+0x64) = 1` only when the collided object matches a recognised tag
     * (this+0x66 in the field map: `destroyed`). This method models exactly that
     * conditional set: when @p tagMatched is true the flag latches to true; an
     * already-set flag stays set. The tags themselves and the base call are
     * owner concerns -- the caller passes the resolved match result.
     *
     * @param tagMatched true if the collided object matched a recognised tag
     *                   (the owner-side Component.CompareTag result).
     */
    void OnTriggerEnter2D(bool tagMatched);

    // --- Division: the divide decision -------------------------------------

    /**
     * @brief The `count > 0` spawn gate, written exactly as the decomp's
     *        `-count < count` test.
     *
     * FAITHFUL: RGBTDivision__Division @ game_full.c:469085. For a content count
     * the comparison `-count < count` is true iff `count > 0`; it is reproduced
     * literally here so the recovered branch is preserved byte-for-byte.
     * @param count the bullet's child-spawn count (this+0x4C).
     * @return true if the division should spawn child bullets.
     */
    static bool HasDivisionCount(int count);

    /**
     * @brief The full two-stage divide decision from Division's body.
     *
     * FAITHFUL: RGBTDivision__Division @ game_full.c:469078, 469085. The body
     * runs only when `destroyed == 0`, and the PrefabPool child spawn runs only
     * when `count > 0`. So the spawn happens iff `!destroyed && count > 0`. The
     * intervening RGMusicManager.PlayEffect(audio_clip) is an owner side effect
     * that fires whenever `!destroyed` (it is NOT modelled here -- this method
     * answers only "should the fan spawn?").
     * @param destroyed the bullet's `destroyed` flag (this+0x64).
     * @param count     the bullet's child-spawn count (this+0x4C).
     * @return true if the child-bullet fan should be spawned by the owner.
     */
    static bool ShouldDivide(bool destroyed, int count);

    /**
     * @brief Instance form of ShouldDivide using the live `destroyed` flag.
     *
     * FAITHFUL: RGBTDivision__Division @ game_full.c:469078, 469085. Combines the
     * latched hit state (set via OnTriggerEnter2D) with @p count.
     * @param count the bullet's child-spawn count (this+0x4C).
     */
    bool ShouldDivide(int count) const;

    /// Live `destroyed` flag (mirrors field 0x64): true once a matching hit armed it.
    bool Destroyed() const { return m_Destroyed; }

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};          ///< deterministic stream; never advanced here.
    bool m_Destroyed = false;  ///< `destroyed` flag (0x64): armed by a matching hit.
};

} // namespace Game

#endif /* GAME_RGBTDIVISION_HPP */
