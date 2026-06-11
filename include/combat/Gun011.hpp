#ifndef GAME_GUN011_HPP
#define GAME_GUN011_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class Gun011
 * @brief Faithful scatter-angle math for the "Gun011" spread weapon
 *        (Soul Knight 1.7.10, an RGWeapon subclass).
 *
 * Per-content port. Gun011 is a single-shot SPREAD weapon: each pull fires one
 * bullet whose direction is the aim angle perturbed by a random scatter, where
 * the scatter half-angle is the configured base spread amplified by the weapon's
 * recoil factor. It additionally fires an extra pellet on release
 * (CreateEndShootBullet) using the IDENTICAL scatter formula and the SAME single
 * RGRandom draw. The bullet PrefabPool/Instantiate spawn, the muzzle
 * Transform.get_position and the virtual owner-component fetch are all OWNER
 * concerns; this brain models ONLY the recoverable scalar math that FEEDS the
 * spawn.
 *
 * Both modelled bodies are byte-for-byte identical in the decomp:
 *
 *   Gun011__Attack             @ game_full.c:964176
 *   Gun011__CreateEndShootBullet @ game_full.c:964134
 *
 * each does, in order:
 *   1. piVar2 = *(this+0x50)          // owner weapon ref (vtable holder)
 *   2. uVar3  = *(this+0x30)          // base spread angle (raw signed -> float)
 *   3. iVar1  = piVar2->vtbl[0xf4](.) // owner component fetch (OWNER)
 *   4. fVar4  = VectorSignedToFloat(uVar3)              // base angle, as float
 *   5. fVar4  = fVar4 + fVar4 * *(iVar1+0x20)           // base + base*recoil
 *   6. RGRandom__Range(*(this+0x60), -fVar4, fVar4, 0)  // ONE max-incl. float draw
 *   7. get_position(*(this+0x4c))     // muzzle transform -> spawn (OWNER, tail)
 *
 * The canonical single-shot gun scatter (cf. Gun008/Gun012/Gun013/Gun014, same
 * idiom): spread = baseAngle(+0x30) + baseAngle*recoil(+0x20), then
 * RGRandom::Range(-spread, +spread) (symmetric scatter). Gun011 uses the '+'
 * variant of that formula (NOT '*'): fVar4 + fVar4 * recoil.
 *
 * Step 4's VectorSignedToFloat is the ARM vcvt that Ghidra surfaces when the
 * raw field at this+0x30 is an integer/fixed value converted to float; the
 * owner sets this+0x30 to the live base angle before Attack runs, so we take the
 * already-decoded float base angle as a parameter rather than re-deriving it.
 *
 * Determinism: each fired body makes EXACTLY ONE RGRandom float draw
 * (Range(-half,+half), max INCLUSIVE). The scatter half-angle (step 5) makes
 * ZERO draws. We preserve that single draw exactly per shot; the recoil read,
 * the owner component fetch, and the muzzle spawn are owner-side.
 *
 * @see recreation Enemy/RGEController.cs (field-offset reference);
 *      FAITHFUL: Gun011 @ game_full.c:964134-964254;
 *      cf. Gun008.cpp (same base+base*scale scatter, max-incl. float draw).
 */
class Gun011 {
public:
    Gun011() = default;

    /// Seed this weapon's deterministic scatter stream (this+0x60).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief The scatter HALF-angle (the +-bound of the symmetric draw).
     *
     * FAITHFUL: Gun011__Attack @ 964211 / Gun011__CreateEndShootBullet @ 964160:
     *   fVar4 = (base) ; fVar4 = fVar4 + fVar4 * *(iVar1+0x20)
     *         = base + base*recoil = base * (1 + recoil).
     * `baseAngle` is this+0x30 (the VectorSignedToFloat-decoded base spread);
     * `recoil` is the owner component's field at +0x20. NO RGRandom draw.
     *
     * Pure static math; identical for Attack and CreateEndShootBullet.
     */
    static float ScatterHalfAngle(float baseAngle, float recoil);

    /**
     * @brief Draw the random scatter angle in [-half, +half] (ONE float draw).
     *
     * FAITHFUL: Gun011__Attack @ 964217: RGRandom__Range(this+0x60, -fVar4,
     * fVar4, 0) -- the max-INCLUSIVE float overload (Range(float,float)). Exactly
     * one draw per shot. The resulting angle is the deviation the owner adds to
     * the aim direction before spawning the bullet at the muzzle transform.
     */
    float ScatterAngle(float half);

    /**
     * @brief Primary shot scatter angle: ScatterAngle(ScatterHalfAngle(...)).
     *
     * FAITHFUL: Gun011__Attack @ game_full.c:964176. Convenience that chains the
     * (zero-draw) half-angle and the (one-draw) symmetric scatter, mirroring the
     * decomp body. The owner fetches `recoil` from its component (+0x20), passes
     * the decoded base angle (+0x30), then spawns one bullet at the muzzle
     * (+0x4c). Makes EXACTLY ONE RGRandom draw.
     */
    float Attack(float baseAngle, float recoil);

    /**
     * @brief End-shoot extra-pellet scatter angle (same formula + same one draw).
     *
     * FAITHFUL: Gun011__CreateEndShootBullet @ game_full.c:964134. Byte-for-byte
     * identical to Attack in the decomp: same base+base*recoil half-angle and the
     * same single Range(-half,+half) float draw. Modeled as its own entry point
     * (the FOCUS asks for both methods) although it shares the math. Makes
     * EXACTLY ONE RGRandom draw.
     */
    float CreateEndShootBullet(float baseAngle, float recoil);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{}; ///< this+0x60: per-weapon deterministic scatter stream.
};

} // namespace Game

#endif /* GAME_GUN011_HPP */
