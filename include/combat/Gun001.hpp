#ifndef GAME_GUN001_HPP
#define GAME_GUN001_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class Gun001
 * @brief Faithful scatter-angle math for the "Gun001" single-shot gun
 *        (Soul Knight 1.7.10, an RGWeapon subclass).
 *
 * Per-content port. Gun001 is the canonical single-shot spread gun: on each
 * pull it computes a recoil-widened spread half-angle and draws ONE random
 * scatter offset inside that symmetric +-spread cone, then spawns a single
 * bullet at the scattered angle. The body is the same shape as Gun002 and the
 * already-ported Gun005, except Gun001 DOES advance the RNG (Gun005's spread is
 * a deterministic charge scalar; Gun001's is a random scatter draw).
 *
 * Recoverable from Gun001__Attack @ game_full.c:315755:
 *   - the base spread half-angle read from this+0x30 (decomp uVar3 -> fVar4 via
 *     the ARM vcvt Ghidra surfaces as VectorSignedToFloat);
 *   - the recoil widening: spread = base + base * recoil, where recoil is read
 *     from (holder-subobject + 0x20). The holder is reached through this+0x50
 *     and a virtual accessor (vtable+0xf4); only the resulting "+0x20 recoil
 *     float" is recoverable here -- the holder walk itself is OWNER.
 *   - the scatter draw: RGRandom::Range(rg[this+0x60], -spread, +spread). Float
 *     Range == max INCLUSIVE. EXACTLY ONE draw, in this order. The drawn value
 *     is the signed angular offset the owner applies to the muzzle/aim before
 *     spawning the bullet.
 *
 * NOT modeled (OWNER): the static-ctor guard (DAT_015e263a / FUN_010877e8), the
 * holder null-checks and virtual accessor walk (this+0x50, vtable+0xf4/0xf8),
 * and the entire bullet spawn tail -- PrefabPool singleton init + get_Inst
 * (Singleton<PrefabPool>__get_Inst, truncated "Subroutine does not return").
 * This brain models only the scalar that FEEDS the spawn: the spread half-angle
 * and the single scatter draw within it.
 *
 * Determinism: Gun001's modelled path makes EXACTLY ONE RGRandom::Range(float)
 * draw per shot, matching the decomp's single RGRandom__Range call. ScatterAngle
 * advances the stream once; SpreadHalfAngle is pure and draws zero.
 *
 * @see recreation Enemy/RGEController.cs (field-offset reference);
 *      FAITHFUL: Gun001 @ game_full.c:315755-315788. Same shape as Gun002__Attack
 *      @ 315827 and Gun005__Attack @ 316757.
 */
class Gun001 {
public:
    Gun001() = default;

    /// Seed the deterministic scatter stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief The recoil-widened spread half-angle (pure; no RNG draw).
     *
     * FAITHFUL: Gun001__Attack @ 315778 --
     *   fVar4 = (float)VectorSignedToFloat(*(param_1 + 0x30));     // base angle
     *   fVar4 = fVar4 + fVar4 * *(float *)(iVar1 + 0x20);          // + base*recoil
     * No clamp exists in the decomp; a negative base or recoil < -1 can flip the
     * sign exactly as the original multiply-add does. The returned value is the
     * +-bound handed to the scatter Range below.
     *
     * @param baseAngle the configured base spread half-angle (this+0x30).
     * @param recoil    the holder-subobject recoil multiplier (+0x20).
     */
    static float SpreadHalfAngle(float baseAngle, float recoil);

    /**
     * @brief Draw the scatter offset for one shot (advances the stream ONCE).
     *
     * FAITHFUL: Gun001__Attack @ 315784 --
     *   RGRandom__Range(*(param_1 + 0x60), -fVar4, fVar4, 0);
     * Float Range is max-INCLUSIVE. This is the weapon's single RNG draw; the
     * returned angle is the signed scatter the owner adds to the muzzle/aim
     * angle before spawning the bullet (the PrefabPool get_Inst spawn tail is
     * OWNER). Computes the half-angle from (baseAngle, recoil) first, then draws.
     *
     * @return the signed scatter angle in [-spread, +spread].
     */
    float ScatterAngle(float baseAngle, float recoil);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{}; ///< this+0x60: the per-weapon scatter stream.
};

} // namespace Game

#endif /* GAME_GUN001_HPP */
