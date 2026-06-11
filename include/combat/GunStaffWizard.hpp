#ifndef GAME_GUNSTAFFWIZARD_HPP
#define GAME_GUNSTAFFWIZARD_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class GunStaffWizard
 * @brief Faithful 4-state barrel/phase counter + sign-gated scatter math for
 *        the "GunStaffWizard" weapon (Soul Knight 1.7.10, an RGWeapon subclass).
 *
 * Per-content port. A single recovered body (GunStaffWizard__Attack) holds a
 * written-back 4-state phase counter, a sign-gated burst-exhausted early-out,
 * and -- on the firing path -- the single-shot scatter (base + base*deviation,
 * then one symmetric RGRandom draw). It is the canonical single-shot scatter
 * gun shape (cf. Gun019) with two extra recoverable pieces of state on top:
 *
 *   - PHASE COUNTER (owner+0x78, param_1[0x1e]): a cyclic 4-state machine
 *     advanced FIRST every Attack via a switch 0->1->2->3->0, written back to
 *     the same field. A default (out-of-range) value skips the write-back. This
 *     is the barrel/phase rotation; the actual per-phase muzzle/animation use is
 *     in the truncated owner tail, but the counter itself is pure logic.
 *   - SIGN-GATE EARLY-OUT (owner+0x70, param_1[0x1c]): `x < -x` is true exactly
 *     when x < 0 (signed int) -- the burst-exhausted idiom. A NEGATIVE field
 *     selects the early-out path (a virtual owner call + RGMusicManager SFX, then
 *     return, NO scatter draw); a non-negative field selects the firing path.
 *   - FIRING PATH (param_1[0x1c] >= 0): widen the base angle (owner+0x30) to
 *     float, scale by the bullet-component deviation (component+0x20) to get
 *     spread = base + base*deviation, then draw ONE RGRandom float
 *     Range(-spread, +spread) for the shot's scatter, and spawn at the muzzle
 *     (get_transform, OWNER).
 *
 * This brain models ONLY the recoverable scalar/state math that FEEDS those
 * side-effects. The vtable+0x134 owner call, the RGMusicManager.GetInstance /
 * PlayEffect SFX, the vtable+0xf4 bullet/component fetch (which supplies the
 * deviation factor), and the Component.get_transform muzzle spawn are all OWNER
 * concerns (the firing tail is truncated -- "Subroutine does not return").
 *
 * Determinism: the firing path draws EXACTLY ONE RGRandom float per shot (the
 * symmetric scatter Range(-spread, +spread)); the sign-gated early-out path
 * draws ZERO (it only advances the phase counter, calls the owner and plays a
 * sound). Preserve the single draw per firing shot and never advance the stream
 * on the early-out path.
 *
 * Field offsets are read straight off the decomp's own int-array indices on the
 * (int *) owner: phase 0x78 (param_1[0x1e]), sign-gate 0x70 (param_1[0x1c]),
 * base angle 0x30 (param_1[0xc]), rng 0x60 (param_1[0x18]), component 0x50
 * (param_1[0x14]), sfx 0x10 (param_1[4]). The ctor pre-loads sign-gate 0x70 = 1
 * and a second field 0x74 = 15.
 *
 * @see recreation Weapon/RGWeapon.cs (field-offset reference);
 *      FAITHFUL: GunStaffWizard__Attack @ game_full.c:968615,
 *      GunStaffWizard___ctor @ game_full.c:968595.
 */
class GunStaffWizard {
public:
    /// Number of phases in the cyclic barrel/phase counter (states 0,1,2,3).
    /// FAITHFUL: GunStaffWizard__Attack @ game_full.c:968628-968644 (the switch
    /// cycles param_1[0x1e] over 0->1->2->3->0).
    static constexpr int kPhaseCount = 4;

    /// Idle / out-of-cycle sentinel for the phase counter: any value the switch
    /// does NOT cover (i.e. not 0..3) hits the default and is left unchanged.
    /// FAITHFUL: GunStaffWizard__Attack @ game_full.c:968641 (default: goto skips
    /// the write-back of param_1[0x1e]).
    static constexpr int kPhaseUnchanged = -1;

    /// ctor pre-load for the sign-gate field (owner+0x70 = 1): a non-negative
    /// start so the FIRST Attack takes the firing path, not the early-out.
    /// FAITHFUL: GunStaffWizard___ctor @ game_full.c:968602 (*(owner+0x70) = 1).
    static constexpr int kInitGateValue = 1;

    /// ctor pre-load for the second config field (owner+0x74 = 0xf == 15). This
    /// field is NOT read in Attack (it is consumed in the truncated owner tail);
    /// recorded for completeness from the ctor. FAITHFUL: GunStaffWizard___ctor @
    /// game_full.c:968603 (*(owner+0x74) = 0xf).
    static constexpr int kInitField74 = 15;

    GunStaffWizard() = default;

    /// Seed the weapon's deterministic stream (owner+0x60). One float is drawn
    /// per firing shot (see ScatterAngle); keep it in lockstep with the owner.
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief Advance the cyclic 4-state phase counter (the switch at Attack top).
     *
     * FAITHFUL: GunStaffWizard__Attack @ game_full.c:968628-968644.
     *   switch (param_1[0x1e]) { 0->1; 1->2; 2->3; 3->0; default: skip; }
     *   param_1[0x1e] = next;   // write-back ONLY on a covered case
     * A value outside 0..3 hits the default and is returned unchanged (the
     * write-back is skipped via the goto). This runs FIRST every Attack, before
     * the sign-gate. NO RGRandom draw.
     * @param phase the current phase (owner+0x78) BEFORE the advance.
     * @return the next phase (0->1->2->3->0), or @p phase unchanged if not 0..3.
     */
    static int NextPhase(int phase);

    /**
     * @brief The sign-gated burst-exhausted early-out is taken iff gate < 0.
     *
     * FAITHFUL: GunStaffWizard__Attack @ game_full.c:968646 --
     * `if (param_1[0x1c] < -param_1[0x1c])`. For a signed int x, `x < -x` is
     * equivalent to `2x < 0`, i.e. x < 0; so a NEGATIVE gate selects the
     * early-out path (owner virtual call + RGMusicManager SFX + return, no
     * scatter) and a non-negative gate selects the firing path (scatter + spawn).
     * @return true -> early-out (no draw); false -> firing path (one draw).
     */
    static bool IsEarlyOut(int gate) { return gate < 0; }

    /**
     * @brief The scatter half-angle (the magnitude fed to the scatter draw) for
     *        a firing shot: spread = base + base*deviation = base*(1 + deviation).
     *
     * FAITHFUL: GunStaffWizard__Attack @ game_full.c:968667-968668.
     *   fVar4 = (float)VectorSignedToFloat(param_1[0xc]);    // base angle 0x30
     *   fVar4 = fVar4 + fVar4 * *(float *)(iVar1 + 0x20);    // deviation
     * `base` is the owner's base scatter field (owner+0x30, an int the decomp
     * widens to float via VectorSignedToFloat); `deviation` is a bullet-component
     * float (component+0x20) supplied by the owner's vtable+0xf4 fetch. NO
     * RGRandom draw here.
     * @param base      the base scatter angle (owner+0x30), in degrees.
     * @param deviation the bullet deviation/spread factor (component+0x20).
     * @return the scatter half-angle magnitude used as the +/- bound below.
     */
    static float SpreadHalfAngle(float base, float deviation);

    /**
     * @brief One firing shot's scatter angle: Range(-spread, +spread).
     *
     * FAITHFUL: GunStaffWizard__Attack @ game_full.c:968670
     * (`RGRandom__Range(param_1[0x18], -fVar4, fVar4, 0)`). Draws EXACTLY ONE
     * float from this weapon's stream (max-INCLUSIVE float Range), the symmetric
     * single-shot scatter. `spread` is the value from SpreadHalfAngle. The
     * subsequent muzzle Component.get_transform bullet spawn is the OWNER's
     * concern (truncated tail in the decomp).
     * @return the bullet's deterministic scatter angle within [-spread, spread].
     */
    float ScatterAngle(float spread);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{}; ///< owner+0x60; advanced once per firing shot only.
};

} // namespace Game

#endif /* GAME_GUNSTAFFWIZARD_HPP */
