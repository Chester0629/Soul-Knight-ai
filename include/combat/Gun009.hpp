#ifndef GAME_GUN009_HPP
#define GAME_GUN009_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class Gun009
 * @brief Faithful scatter-angle scalar math for the "Gun009" single-shot spread
 *        gun (Soul Knight 1.7.10, an RGWeapon subclass).
 *
 * Per-content port. Gun009 is the canonical single-shot scatter gun, structurally
 * identical to Gun001/Gun005's scatter path: every pull of the trigger spawns one
 * bullet whose firing angle is jittered by a random spread that grows with the
 * weapon's recoil. The whole body is heavily truncated -- the bullet
 * PrefabPool.Inst spawn, the bullet-data virtual fetch and the Singleton
 * accessors are all OWNER concerns. This brain models ONLY the recoverable
 * scalar/RNG math that FEEDS that spawn:
 *
 *   - Attack (Gun009__Attack @ game_full.c:963998): the spread half-angle
 *       fVar4 = baseAngle(this+0x30)                       (VectorSignedToFloat)
 *       fVar4 = fVar4 + fVar4 * recoil(bulletData+0x20)    (== baseAngle*(1+recoil))
 *     then a single symmetric scatter draw
 *       RGRandom.Range(-fVar4, +fVar4)                     (this+0x60, float, max INCLUSIVE)
 *     whose result is the per-shot angle offset handed to the spawned bullet. The
 *     baseAngle (this+0x30) and recoil (bulletData+0x20) are live fields supplied
 *     by the owner before Attack runs (the owner aims/configures the weapon); we
 *     accept them as arguments so the brain is unit-testable.
 *
 *   - _ctor (Gun009___ctor @ game_full.c:963969): carries NO recoverable scalar
 *     state -- it is a guarded RGWeapon___ctor(this, 0) chain with no field
 *     initialisers (unlike Gun010's ctor, which seeds this+0x6c). Nothing to
 *     model.
 *
 * NOT modelled (distinct class, different shape): EGun009__Attack
 * (game_full.c:201470) is the enemy "EGun009", a SEPARATE class -- it loops over a
 * bullet count, draws from UnityEngine.Random (NOT this weapon's RGRandom stream)
 * and plays a music effect. It is not part of the Gun009 weapon and is
 * intentionally excluded.
 *
 * Determinism: Gun009__Attack draws EXACTLY ONE float from its RGRandom stream
 * (this+0x60) per shot -- the symmetric scatter Range(-spread, +spread). This
 * brain preserves that single draw, in that order, and nothing else. ComputeSpread
 * is the pure pre-roll scalar (ZERO draws); RollScatter performs the one draw.
 *
 * @see recreation Enemy/RGEController.cs (field-offset reference);
 *      already-ported scatter shape: Gun005.cpp / GunThrow.cpp;
 *      FAITHFUL: Gun009 @ game_full.c:963969-964030.
 */
class Gun009 {
public:
    Gun009() = default;

    /// Seed this weapon's deterministic scatter stream (this+0x60). Call once at
    /// spawn so RollScatter's single draw stays in lockstep with the original.
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief The scatter half-angle (the magnitude fed to the symmetric draw).
     *
     * FAITHFUL: Gun009__Attack @ 963998 --
     *   fVar4 = baseAngle(this+0x30);
     *   fVar4 = fVar4 + fVar4 * recoil(bulletData+0x20);   // baseAngle*(1+recoil)
     * No clamp exists in the decomp: a negative baseAngle or recoil < -1 flips the
     * sign exactly as the original arithmetic does (RollScatter still draws
     * symmetrically about it). NO RGRandom draw on this path.
     *
     * @param baseAngle the configured spread angle (this+0x30, signed -> float).
     * @param recoil    the bullet-data recoil factor (bulletData+0x20), fetched by
     *                  the owner via the virtual call at *(*this+0x50)+0xf4.
     * @return the scatter half-angle (decomp fVar4).
     */
    static float ComputeSpread(float baseAngle, float recoil);

    /**
     * @brief Draw one per-shot scatter angle offset in [-spread, +spread].
     *
     * FAITHFUL: Gun009__Attack @ 964021 --
     *   RGRandom.Range(this+0x60, -fVar4, fVar4);
     * Exactly ONE RGRandom::Range(float) draw (max INCLUSIVE), symmetric about 0.
     * The returned offset is the per-shot angle handed to the spawned bullet; the
     * PrefabPool.Inst spawn / bullet GetComponent that consume it are OWNER.
     *
     * @param baseAngle the configured spread angle (this+0x30).
     * @param recoil    the bullet-data recoil factor (bulletData+0x20).
     * @return the random scatter angle offset for this shot.
     */
    float RollScatter(float baseAngle, float recoil);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{}; ///< this+0x60: the per-shot scatter stream.
};

} // namespace Game

#endif /* GAME_GUN009_HPP */
