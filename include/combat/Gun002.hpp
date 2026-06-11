#ifndef GAME_GUN002_HPP
#define GAME_GUN002_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class Gun002
 * @brief Faithful multi-barrel shotgun FAN math for the "Gun002" weapon
 *        (Soul Knight 1.7.10, an RGWeapon subclass).
 *
 * Per-content port. Gun002 fires `count` pellets per pull, fanned out around the
 * aim direction by a deterministic, geometric per-pellet index plus a random
 * per-pellet scatter. The bullet Instantiate / PrefabPool spawn, the muzzle
 * transform walks, the end-of-shoot virtual SFX call and RGMusicManager.PlayEffect
 * are all OWNER concerns; this brain models ONLY the recoverable scalar/state math
 * that FEEDS the spawn:
 *
 *   - _ctor (Gun002___ctor @ game_full.c:315797): count (this+0x70) = 1 and the
 *     per-pellet fan step angle (this+0x74) = 0xf == 15 (degrees). Modeled as the
 *     ctor defaults kDefaultCount / kDefaultStepAngle.
 *   - Attack (Gun002__Attack @ game_full.c:315830):
 *       * count = this+0x70 (decomp uVar1, param_1[0x1c]).
 *       * count < 1  -> end-shoot SFX (virtual *0x134 + RGMusicManager.PlayEffect):
 *         a pure no-fire predicate; the SFX itself is OWNER.
 *       * else the fan start index: (count & 1) == 0 -> start = -(count/2);
 *         odd -> start = -((count-1)/2). Pellet p in [0,count) sits at fan index
 *         start + p, and its geometric base angle is (start + p) * stepAngle.
 *       * the per-pellet scatter half-span:
 *           spread = baseAngle(this+0x30) + baseAngle(this+0x30) * recoil
 *         where recoil is *(float*)(obj+0x20) off the object returned by the
 *         owner's virtual GetShootAngle call (*piVar2 + 0xf4). recoil is supplied
 *         by the owner per shot; the canonical single-shot scatter shape is
 *         baseAngle + baseAngle*recoil.
 *       * ONE RGRandom float draw per pellet: Range(-spread, +spread) (this+0x60,
 *         max-INCLUSIVE), added to the geometric base angle by the owner before
 *         the bullet is rotated. Preserve the single draw per pellet exactly.
 *   - Awake (Gun002__Awake @ game_full.c:315817): a truncated get_gameObject tail
 *     -- no recoverable scalar logic, OWNER.
 *
 * Determinism: Gun002__Attack draws ONE float per fired pellet from RGRandom
 * (this+0x60). ScatterPellet() is the only path that advances the stream; the
 * geometric helpers (FanStartIndex / PelletFanIndex / PelletBaseAngle /
 * SpreadHalfSpan) make ZERO draws. The owner pumps one ScatterPellet() per pellet
 * index 0..count-1, in order, to stay frame-for-frame lockstep with the original.
 *
 * @see recreation Enemy/RGEController.cs (field-offset reference);
 *      FAITHFUL: Gun002 @ game_full.c:315797-315920.
 */
class Gun002 {
public:
    /// Default pellet count. FAITHFUL: Gun002___ctor @ 315804 -- this+0x70 = 1.
    static constexpr int kDefaultCount = 1;
    /// Default per-pellet fan step (degrees).
    /// FAITHFUL: Gun002___ctor @ 315805 -- this+0x74 = 0xf == 15.
    static constexpr float kDefaultStepAngle = 15.0F;

    Gun002() = default;

    /// Seed the weapon's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief Whether Attack actually fires the fan, or hits the no-shoot SFX path.
     *
     * FAITHFUL: Gun002__Attack @ 315843 -- `if ((int)count < 1)` takes the
     * end-shoot SFX branch (virtual *0x134 + RGMusicManager.PlayEffect, both
     * OWNER) and returns without spawning. We model only the predicate.
     * @return true iff count >= 1 (the fan-spawn branch runs).
     */
    static bool WillFire(int count) { return count >= 1; }

    /**
     * @brief The signed fan start index for `count` pellets.
     *
     * FAITHFUL: Gun002__Attack @ 315855-315862. Even count -> -(count/2);
     * odd count -> -((count-1)/2). (For count==1 -> 0, a single straight pellet.)
     * Integer division (truncation toward zero), matching the decomp's `/2`.
     * NO RGRandom draw.
     */
    static int FanStartIndex(int count);

    /**
     * @brief The signed fan index of pellet `pelletIndex` (0..count-1).
     *
     * FAITHFUL: Gun002__Attack -- pellet p occupies fan slot FanStartIndex(count)+p
     * (the per-pellet spawn loop walks the start index upward). NO RGRandom draw.
     */
    static int PelletFanIndex(int count, int pelletIndex);

    /**
     * @brief The geometric (pre-scatter) base angle of pellet `pelletIndex`.
     *
     * FAITHFUL: Gun002__Attack -- the fan slot times the configured step angle:
     * (FanStartIndex(count) + pelletIndex) * stepAngle. NO RGRandom draw.
     * @param stepAngle this+0x74 (defaults to kDefaultStepAngle == 15).
     */
    static float PelletBaseAngle(int count, int pelletIndex,
                                 float stepAngle = kDefaultStepAngle);

    /**
     * @brief The per-pellet random-scatter half-span (degrees).
     *
     * FAITHFUL: Gun002__Attack @ 315885-315886 --
     *   fVar5 = baseAngle(this+0x30);  fVar5 = fVar5 + fVar5 * recoil(obj+0x20).
     * i.e. spread = baseAngle * (1 + recoil). recoil is read off the object the
     * owner's virtual GetShootAngle call returns; the caller supplies it. This is
     * the canonical single-shot scatter shape (baseAngle + baseAngle*recoil).
     * NO RGRandom draw -- this only computes the half-span fed to Range().
     * @param baseAngle weapon spread field (this+0x30).
     * @param recoil    recoil multiplier (obj+0x20), owner-supplied per shot.
     */
    static float SpreadHalfSpan(float baseAngle, float recoil);

    /**
     * @brief Draw one pellet's random scatter offset (degrees).
     *
     * FAITHFUL: Gun002__Attack @ 315890 --
     *   RGRandom__Range(this+0x60, -fVar5, fVar5): ONE max-INCLUSIVE float draw
     * per fired pellet. The owner adds this to PelletBaseAngle before rotating the
     * spawned bullet. This is the ONLY path that advances the stream; pump it once
     * per pellet (index 0..count-1), in order, for frame-for-frame replay parity.
     * @param halfSpan the SpreadHalfSpan() result (fVar5).
     * @return Range(-halfSpan, +halfSpan), a symmetric scatter.
     */
    float ScatterPellet(float halfSpan);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{}; ///< this+0x60 deterministic stream; one draw per pellet.
};

} // namespace Game

#endif /* GAME_GUN002_HPP */
