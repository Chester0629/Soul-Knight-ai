#ifndef GAME_GUNMAGICBOW_HPP
#define GAME_GUNMAGICBOW_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class GunMagicBow
 * @brief Faithful charge-gate + charge-scaled velocity math for the
 *        "GunMagicBow" weapon (Soul Knight 1.7.10, an RGWeapon subclass).
 *
 * Per-content port (the "G6" charge family -- same shape as the ported Gun007
 * and Gun016 charge guns). GunMagicBow is a CHARGE bow: while the firing
 * animation bool is held, a charge accumulator (charge, owner+0x8c) ramps up
 * toward a cap (max_charge, owner+0x90). On release the bow fires a bullet
 * whose base velocity is scaled by the accrued charge ratio, so a half-drawn
 * bow shoots a slower arrow than a fully-drawn one.
 *
 * This brain models ONLY the recoverable scalar/state math that FEEDS the
 * spawn. Everything touching Unity is OWNER:
 *   - the Animator.GetBool firing read (Update @ 967259, StopWeapon @ 967407);
 *   - the charge-accumulator INCREMENT itself, which lives in the truncated
 *     Singleton<RGGameProcess> tail (Update @ 967262-967268, "Subroutine does
 *     not return"); we model the GATE predicate, not the step;
 *   - the bullet Instantiate + GetComponent<RGBullet> velocity apply (Attack @
 *     967324, the truncated "does not return" tail), and the Animator.SetBool
 *     stop (StopWeapon @ 967414);
 *   - MakeConsume's owner-component vcall that subtracts the cost field
 *     (-(owner+0x34)) through the owner's stat component (MakeConsume @
 *     967380-967387) -- a virtual write to an owner field, not a recoverable
 *     scalar formula;
 *   - StopWeapon's component cost subtraction (component+0x14 -= owner+0x44 via
 *     the owner's vcall @ 967421-967426), an owner-component write.
 *
 * Recoverable here:
 *   - the charge-tick GATE (Update @ 967260-967261): the charge accumulator is
 *     advanced ONLY while firing && charge(0x8c) < max_charge(0x90). The
 *     increment amount is OWNER (truncated tail), so we model the predicate;
 *   - the charge ratio (Attack @ 967314): ratio = charge(0x8c) / max_charge(0x90);
 *   - the charge-scaled bullet velocity per axis (Attack @ 967315-967325):
 *     each base-velocity component (x 0x80, y 0x84, z 0x88) is multiplied by the
 *     charge ratio -- velocity = ratio * base. The decomp computes
 *     fVar4*fVar1 (x), fVar4*fVar2 (y) and (int)(fVar4*fVar3) (z) and hands them
 *     to the owner's GetComponent<RGBullet> apply.
 *
 * Determinism: NO GunMagicBow body draws from rg_random across Update, Attack,
 * MakeConsume or StopWeapon -- the charge-to-velocity map is a fully
 * deterministic scalar, not random scatter. This unit therefore makes ZERO RNG
 * draws. The RGRandom member is carried only for owner-side parity/lockstep and
 * is never advanced; Seeded() lets a caller confirm the seed without a draw.
 *
 * @see recreation Weapon/RGWeapon.cs (field-offset reference);
 *      FAITHFUL: GunMagicBow @ game_full.c:967246-967432.
 */
class GunMagicBow {
public:
    GunMagicBow() = default;

    /// Seed the deterministic stream (kept untouched; no draw is ever made on
    /// any GunMagicBow path). Carried purely for owner-side lockstep parity.
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief Whether the Update charge-tick advances the accumulator this frame.
     *
     * FAITHFUL: GunMagicBow__Update @ game_full.c:967260-967261. The decomp's
     * gate is:
     *   if (Animator.GetBool(firing) == 1)
     *     if (charge(0x8c) < max_charge(0x90)) { ...accumulate... }
     * i.e. the charge is ticked up ONLY while firing AND charge < max_charge.
     * The increment amount itself lives in the truncated
     * Singleton<RGGameProcess> tail (OWNER), so we model only this gate
     * predicate, not the step. NO RGRandom draw.
     * @param firing    the Animator firing bool (owner-read; GetBool @ 967259).
     * @param charge    the current charge accumulator (owner+0x8c).
     * @param maxCharge the charge cap (owner+0x90).
     * @return true if the owner should advance charge this frame.
     */
    static bool ShouldTickCharge(bool firing, float charge, float maxCharge);

    /**
     * @brief The charge ratio: charge / max_charge.
     *
     * FAITHFUL: GunMagicBow__Attack @ game_full.c:967314 --
     *   fVar4 = *(float *)(param_3 + 0x8c) / *(float *)(param_3 + 0x90).
     * charge (owner+0x8c) over max_charge (owner+0x90). The decomp applies NO
     * clamp; an over-charge yields a ratio > 1 exactly as the division produces.
     * A non-positive divisor is impossible in the original (the ctor sets the cap
     * and Update only ticks toward it, gated by charge < max_charge), so we
     * return 0 in that degenerate case to keep the brain total rather than
     * fabricate a ratio. NO RGRandom draw.
     * @param charge    the accrued charge (owner+0x8c).
     * @param maxCharge the charge cap / divisor (owner+0x90).
     */
    static float ChargeRatio(float charge, float maxCharge);

    /**
     * @brief One bullet base-velocity component scaled by the charge ratio.
     *
     * FAITHFUL: GunMagicBow__Attack @ game_full.c:967315-967325. The decomp reads
     * the three base-velocity components via VectorSignedToFloat:
     *   fVar1 = base x (owner+0x80), fVar2 = base y (owner+0x84),
     *   fVar3 = base z (owner+0x88),
     * then scales each by the charge ratio fVar4 and hands the results to the
     * owner's GetComponent<RGBullet> velocity apply:
     *   x -> fVar4 * fVar1 (967325), y -> fVar4 * fVar2 (967325),
     *   z -> (int)(fVar4 * fVar3) (967318, the z is truncated to int for the
     *   component store -- an owner storage concern; the float product is the
     *   recoverable scalar). Every axis is the same form: velocity = ratio*base.
     * The Instantiate/GetComponent<RGBullet> apply (967324, truncated "does not
     * return" tail) is OWNER. NO RGRandom draw.
     * @param baseComponent one axis of the bullet base velocity
     *        (owner+0x80 x / +0x84 y / +0x88 z).
     * @param chargeRatio   the charge ratio (see ChargeRatio; uncapped, matching
     *        the decomp's raw fVar4).
     * @return the charge-scaled velocity component for that axis.
     */
    static float ScaledVelocity(float baseComponent, float chargeRatio);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{}; ///< deterministic stream; never advanced on any path.
};

} // namespace Game

#endif /* GAME_GUNMAGICBOW_HPP */
