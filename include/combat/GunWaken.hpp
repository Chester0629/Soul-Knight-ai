#ifndef GAME_GUNWAKEN_HPP
#define GAME_GUNWAKEN_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class GunWaken
 * @brief Faithful awakened-accuracy gate + per-shot scatter math for the
 *        "GunWaken" weapon (Soul Knight 1.7.10, an RGWeapon subclass).
 *
 * Per-content port. GunWaken's recovered Attack body
 * (GunWaken__Attack @ game_full.c:970426-970465) is the canonical single-shot
 * scatter shape (spread = angle + angle*recoil; cf. Gun019::SpreadHalfAngle)
 * wrapped in ONE extra gate: a "waken" / awakened flag (owner+0x84).
 *
 *   - NORMAL mode (wakenFlag == 0, the gate at 970452 is taken): widen the base
 *     scatter angle (owner+0x30) to float, fold in the bullet-component recoil
 *     (component+0x20) to get spread = angle + angle*recoil, then draw ONE
 *     RGRandom float Range(-spread, +spread) (970458) for the shot's scatter.
 *   - AWAKENED mode (wakenFlag != 0): the ENTIRE spread+draw block is SKIPPED.
 *     The shot is perfectly accurate AND -- critically for determinism -- the
 *     weapon's RGRandom stream (owner+0x60) is NOT advanced. No draw is taken on
 *     this path; the stream stays in lockstep with the original.
 *
 * This brain models ONLY the recoverable scalar/state math that FEEDS the
 * weapon's side-effects. The bullet/component virtual fetch (the vtable+0xf4
 * call at 970446 that supplies the recoil multiplier), the muzzle
 * Transform.get_position spawn (owner+0x4c at 970460-970462) and the bullet
 * Instantiate tail are all OWNER concerns (the body's spawn tail is truncated --
 * "Subroutine does not return").
 *
 * Determinism (the load-bearing fact for this module): the normal path draws
 * EXACTLY ONE RGRandom float per shot; the awakened path draws ZERO. Preserve
 * both -- never advance the stream when wakenFlag != 0.
 *
 * The three ctor-configured scalars (GunWaken___ctor @ game_full.c:970412-970414:
 * owner+0x70 = 12, owner+0x74 = 50, owner+0x78 = 45.0f) are RGWeapon base config
 * written before RGWeapon___ctor(param_1, 0). They do NOT enter the Attack math;
 * their exact values are recovered and recorded below, but their semantic meaning
 * (cooldown / range / damage / etc.) is not determinable from the decomp, so it
 * is left // TODO[verify] rather than guessed.
 *
 * @see recreation Weapon/RGWeapon.cs (field-offset reference; no GunWaken C#);
 *      FAITHFUL: GunWaken @ game_full.c:970405-970465.
 */
class GunWaken {
public:
    /// Idle / not-awakened sentinel for the waken flag (owner+0x84 == 0). When
    /// the flag is 0 the gun is in NORMAL mode and the scatter draw is taken.
    /// FAITHFUL: GunWaken__Attack @ game_full.c:970452 (`if (owner+0x84 == 0)`).
    static constexpr int kWakenFlagNormal = 0;

    /// owner+0x70: int immediate 0xc == 12, written by the ctor before the base
    /// RGWeapon ctor. RGWeapon base config; does NOT enter the Attack math, and
    /// the decomp does not reveal its semantic role.
    /// FAITHFUL: GunWaken___ctor @ game_full.c:970412 (*(owner+0x70) = 0xc).
    static constexpr int kCtorField70 = 12; // TODO[verify] meaning (cooldown/count?)

    /// owner+0x74: int immediate 0x32 == 50, written by the ctor. RGWeapon base
    /// config; does NOT enter the Attack math.
    /// FAITHFUL: GunWaken___ctor @ game_full.c:970413 (*(owner+0x74) = 0x32).
    static constexpr int kCtorField74 = 50; // TODO[verify] meaning

    /// owner+0x78: float immediate 0x42340000 == 45.0f, written by the ctor.
    /// RGWeapon base config; does NOT enter the Attack math.
    /// FAITHFUL: GunWaken___ctor @ game_full.c:970414 (*(owner+0x78) = 0x42340000).
    static constexpr float kCtorField78 = 45.0F; // TODO[verify] meaning (range?)

    GunWaken() = default;

    /// Seed the weapon's deterministic stream (owner+0x60). Exactly ONE float is
    /// drawn per NORMAL-mode ScatterAngle call (zero in awakened mode); keep it in
    /// lockstep with the original.
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief Whether the gun is in awakened (perfect-accuracy) mode.
     *
     * FAITHFUL: GunWaken__Attack @ game_full.c:970452 -- `if (owner+0x84 == 0)`
     * gates the spread+draw block: the block runs ONLY when the flag is 0
     * (NORMAL mode). A non-zero flag means AWAKENED: the block (and its RGRandom
     * draw) is skipped entirely.
     * @param wakenFlag the waken flag (owner+0x84).
     * @return true when awakened (flag != 0); false in normal mode (flag == 0).
     */
    static bool IsAwakened(int wakenFlag) { return wakenFlag != kWakenFlagNormal; }

    /**
     * @brief The scatter half-angle for a NORMAL-mode shot:
     *        spread = angle + angle*recoil = angle*(1 + recoil).
     *
     * FAITHFUL: GunWaken__Attack @ game_full.c:970451+970453 --
     *   fVar4 = (float)VectorSignedToFloat(owner+0x30);   // base angle
     *   fVar4 = fVar4 + fVar4 * *(float *)(component+0x20); // + angle*recoil
     * `angle` is the owner's base scatter field (owner+0x30, an int the decomp
     * widens to float via the ARM vcvt Ghidra surfaces as VectorSignedToFloat);
     * `recoil` is a bullet-component float (component+0x20) supplied by the
     * owner's vtable+0xf4 fetch. This term is computed ONLY on the normal path
     * (inside the wakenFlag == 0 gate). NO RGRandom draw here.
     * @param angle  the base scatter angle (owner+0x30), in degrees.
     * @param recoil the bullet recoil/spread factor (component+0x20).
     * @return the scatter half-angle magnitude used as the +/- bound below.
     */
    static float SpreadHalfAngle(float angle, float recoil);

    /**
     * @brief One NORMAL-mode shot's scatter angle: Range(-spread, +spread).
     *
     * FAITHFUL: GunWaken__Attack @ game_full.c:970458
     * (`RGRandom__Range(owner+0x60, -fVar4, fVar4, 0)`). Draws EXACTLY ONE float
     * from this weapon's stream (max-INCLUSIVE float Range), the symmetric
     * single-shot scatter. Call this ONLY when IsAwakened() is false -- in
     * awakened mode the decomp skips this draw and the stream must NOT advance.
     * `spread` is the value from SpreadHalfAngle. The subsequent muzzle
     * Transform.get_position bullet spawn (owner+0x4c) is the OWNER's concern.
     * @return the bullet's deterministic scatter angle within [-spread, spread].
     */
    float ScatterAngle(float spread);

    /**
     * @brief Convenience: resolve one shot's scatter, honouring the awakened gate.
     *
     * FAITHFUL composition of GunWaken__Attack @ game_full.c:970452-970459:
     *   - awakened (wakenFlag != 0): return 0 and take NO RGRandom draw (the gate
     *     skips the whole spread+draw block -> perfect accuracy, stream unmoved);
     *   - normal (wakenFlag == 0): spread = SpreadHalfAngle(angle, recoil), then
     *     ONE Range(-spread, +spread) draw.
     * This makes the zero-draw awakened path explicit at the call site so the
     * RGRandom stream stays in lockstep with the original.
     * @param wakenFlag the waken flag (owner+0x84).
     * @param angle     the base scatter angle (owner+0x30).
     * @param recoil    the bullet recoil/spread factor (component+0x20).
     * @return the scatter angle: 0 in awakened mode, else within [-spread, spread].
     */
    float ResolveScatter(int wakenFlag, float angle, float recoil);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{}; ///< owner+0x60; advanced once per NORMAL shot, never when awakened.
};

} // namespace Game

#endif /* GAME_GUNWAKEN_HPP */
