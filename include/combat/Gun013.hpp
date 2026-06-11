#ifndef GAME_GUN013_HPP
#define GAME_GUN013_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class Gun013
 * @brief Faithful single-shot spread/scatter math for the "Gun013" weapon
 *        (Soul Knight 1.7.10, an RGWeapon subclass).
 *
 * Per-content port. Gun013 is a MINIMAL single-shot gun: one pull fires one
 * bullet whose euler-Z muzzle angle is jittered by a random scatter. The whole
 * Attack body is heavily truncated -- the init guard (FUN_010877e8), the
 * component / attribute null checks (FUN_010b7dcc), the bullet spawn and the
 * get_transform tail are all OWNER concerns. This brain models ONLY the
 * recoverable scalar math that FEEDS the scatter draw:
 *
 *   - Attack (Gun013__Attack @ game_full.c:964313): the scatter half-span
 *       spread = baseAngle(this+0x30) + baseAngle(this+0x30) * recoil(iVar1+0x20)
 *     where recoil is read at +0x20 of the object returned by the virtual call
 *     `(*(this+0x50))->vtable[0xf4](...)` (the weapon's attribute/recoil holder),
 *     followed by the single random draw
 *       RGRandom::Range(this+0x60, -spread, +spread)
 *     (a symmetric scatter about the aim). The bullet Instantiate / muzzle
 *     Transform walk surfaced as the truncated UnityEngine_Component__get_transform
 *     tail at 964339 is the OWNER's job.
 *
 * Determinism: Gun013__Attack draws EXACTLY ONE float from rg_random
 * (the symmetric scatter), so this unit makes exactly one draw per Scatter()
 * call -- count and order matching the decomp. The draw is RGRandom::Range(float)
 * (max INCLUSIVE). SpreadHalfSpan() is the pure pre-draw math and advances
 * nothing, so callers can verify the angle math independently of the stream.
 *
 * @see recreation Enemy/RGEController.cs (field-offset reference);
 *      FAITHFUL: Gun013 @ game_full.c:964313-964341.
 */
class Gun013 {
public:
    Gun013() = default;

    /// Seed the weapon's deterministic stream (call once at spawn). The scatter
    /// draw advances this stream; seeding it identically reproduces the original
    /// frame-for-frame.
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief The scatter half-span (degrees) -- the pre-draw spread magnitude.
     *
     * FAITHFUL: Gun013__Attack @ game_full.c:964334 --
     *   fVar4 = baseAngle;                          // (float)VectorSignedToFloat(this+0x30)
     *   fVar4 = fVar4 + fVar4 * recoil;             // recoil = *(iVar1 + 0x20)
     * i.e. spread = baseAngle * (1 + recoil). `baseAngle` is the weapon's
     * configured aim/scatter angle (this+0x30, read through the ARM int->float
     * vcvt Ghidra surfaces as VectorSignedToFloat). `recoil` is the float at
     * +0x20 of the attribute holder returned by the virtual call at 964328
     * (`(*(this+0x50))->vtable[0xf4]`); the owner supplies it here. NO clamp
     * exists in the decomp -- a negative product is passed through exactly as the
     * original computes (it would simply flip the [-spread,+spread] interval, and
     * RGRandom::Range tolerates min>max). NO RGRandom draw.
     *
     * @param baseAngle the weapon's configured scatter angle (this+0x30).
     * @param recoil    the recoil multiplier read from the attribute holder
     *                  (iVar1+0x20).
     */
    static float SpreadHalfSpan(float baseAngle, float recoil);

    /**
     * @brief Draw one random scatter angle in [-spread, +spread].
     *
     * FAITHFUL: Gun013__Attack @ game_full.c:964337 --
     *   RGRandom__Range(this+0x60, -fVar4, fVar4, 0)
     * a single RGRandom::Range(float) draw (max INCLUSIVE) symmetric about the
     * aim, where fVar4 == SpreadHalfSpan(baseAngle, recoil). The returned value is
     * the euler-Z scatter the owner adds to the muzzle direction before spawning
     * the bullet (the spawn / get_transform tail at 964339 is OWNER). Advances the
     * stream by exactly one float draw -- count and order matching the decomp.
     *
     * @param baseAngle the weapon's configured scatter angle (this+0x30).
     * @param recoil    the recoil multiplier (iVar1+0x20).
     * @return the scattered euler-Z offset (degrees) for this shot.
     */
    float Scatter(float baseAngle, float recoil);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{}; ///< this+0x60: the per-weapon deterministic scatter stream.
};

} // namespace Game

#endif /* GAME_GUN013_HPP */
