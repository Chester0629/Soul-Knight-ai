#ifndef GAME_GUNHEROBOW_HPP
#define GAME_GUNHEROBOW_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class GunHeroBow
 * @brief Faithful charge-scaled speed + arrow-count gate math for the
 *        "GunHeroBow" weapon (Soul Knight 1.7.10, an RGWeapon subclass).
 *
 * Per-content port (the "G6" charge family -- same shape as the ported
 * GunMagicBow / Gun007 charge guns, but with GunHeroBow's own field offsets).
 * GunHeroBow is a CHARGE bow that, on release, fires up to `arrow_count` arrows
 * whose speed is scaled by the accrued charge ratio: a half-drawn bow shoots a
 * slower arrow than a fully-drawn one, and an empty quiver (arrow_count < 1)
 * plays the dry-release SFX and fires nothing.
 *
 * This brain models ONLY the recoverable scalar/predicate math that FEEDS the
 * spawn. Everything touching Unity is OWNER:
 *   - the four VectorSignedToFloat reads of the base-velocity components
 *     (Attack @ 967049-967052) and the GetComponent<RGBullet> velocity apply at
 *     the truncated "does not return" tail (Attack @ 967067-967076);
 *   - the List<GameObject>.get_Item(index) arrow fetch (Attack @ 967067, and the
 *     per-arrow loop continuation FUN_00b138f0 @ 967104) -- the spawn is owner;
 *   - the RGMusicManager.PlayEffect dry-release SFX (Attack @ 967055-967060);
 *   - SetAttack's Animator.GetBool/SetBool charge-toggle gated on an owner
 *     component resource read (SetAttack @ 966983-967024);
 *   - StopWeapon's Animator stop, the owner-component cost write
 *     (*(component+0x14) += cost(owner+0x44) @ 967149), the Object.Destroy of the
 *     charge effect (owner+0xa4 @ 967156), the charge-field reset (owner+0x98 = 0
 *     @ 967157) and the component cost subtraction (@ 967173) -- all owner-side.
 *
 * Recoverable here:
 *   - the charge ratio (Attack @ 967052): ratio = charge(0x98) / max_charge(0x9c);
 *   - the charge-scaled SPEED component (Attack @ 967052): the base-velocity
 *     component at owner+0x94 is multiplied by the charge ratio --
 *     speed = ratio * base(0x94). The other two components (x 0x8c, y 0x90) are
 *     read but passed through UNCHANGED (owner direction), so ONLY 0x94 scales;
 *   - the arrow-count fire gate (Attack @ 967054): the bow fires only when
 *     arrow_count(0xac) >= 1; arrow_count < 1 plays the dry SFX and fires nothing;
 *   - the multi-arrow loop bound (FUN_00b138f0 @ 967091): after firing the arrow
 *     at index i, another fires only while i+1 < arrow_count, so the bow fires
 *     exactly arrow_count arrows (indices 0 .. arrow_count-1).
 *
 * Determinism: NO GunHeroBow body draws from rg_random across ctor, SetAttack,
 * Attack or StopWeapon -- the charge-to-speed map and the arrow-count cadence are
 * fully deterministic, not random scatter. This unit therefore makes ZERO RNG
 * draws. The RGRandom member is carried only for owner-side parity/lockstep and
 * is never advanced; Seeded() lets a caller confirm the seed without a draw.
 *
 * @see recreation Weapon/RGWeapon.cs (field-offset reference);
 *      FAITHFUL: GunHeroBow @ game_full.c:966960-967177.
 */
class GunHeroBow {
public:
    /// Minimum arrow_count (owner+0xac) for the bow to fire; below it the dry
    /// SFX plays and nothing spawns. FAITHFUL: GunHeroBow__Attack @
    /// game_full.c:967054 (`if (*(int *)(param_1 + 0xac) < 1)`).
    static constexpr int kMinArrowsToFire = 1;

    GunHeroBow() = default;

    /// Seed the deterministic stream (kept untouched; no draw is ever made on any
    /// GunHeroBow path). Carried purely for owner-side lockstep parity.
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief The charge ratio: charge / max_charge.
     *
     * FAITHFUL: GunHeroBow__Attack @ game_full.c:967052 --
     *   *(float *)(param_1 + 0x98) / *(float *)(param_1 + 0x9c).
     * charge (owner+0x98) over max_charge (owner+0x9c). The decomp applies NO
     * clamp; an over-charge yields a ratio > 1 exactly as the division produces.
     * A non-positive divisor is impossible in the original (the cap is set by the
     * owner and the charge only ticks toward it), so we return 0 in that
     * degenerate case to keep the brain total rather than fabricate a ratio.
     * NO RGRandom draw.
     * @param charge    the accrued charge (owner+0x98).
     * @param maxCharge the charge cap / divisor (owner+0x9c).
     */
    static float ChargeRatio(float charge, float maxCharge);

    /**
     * @brief The charge-scaled arrow speed: chargeRatio * baseSpeed.
     *
     * FAITHFUL: GunHeroBow__Attack @ game_full.c:967052. The decomp reads the
     * base-velocity speed component via VectorSignedToFloat:
     *   fVar2 = base(owner+0x94)            (967049),
     * then scales it by the charge ratio and hands the result to the owner's
     * GetComponent<RGBullet> velocity apply:
     *   (int)((charge/max_charge) * fVar2)  (967052).
     * The (int) truncation is the owner component-store concern; the float product
     * is the recoverable scalar. The other two base-velocity components read at
     * 967050 (owner+0x90) and 967051 (owner+0x8c) are passed through UNCHANGED
     * (owner direction), so ONLY the 0x94 speed component is charge-scaled. The
     * Instantiate/GetComponent<RGBullet> apply (967067-967076, truncated "does not
     * return" tail) is OWNER. NO RGRandom draw.
     * @param baseSpeed   the base speed component (owner+0x94).
     * @param chargeRatio the charge ratio (see ChargeRatio; uncapped, matching the
     *        decomp's raw quotient).
     * @return the charge-scaled speed (the float product before the owner's int
     *         store).
     */
    static float ScaledSpeed(float baseSpeed, float chargeRatio);

    /**
     * @brief Whether the bow fires this release (vs. the dry-release SFX).
     *
     * FAITHFUL: GunHeroBow__Attack @ game_full.c:967054 --
     *   if (*(int *)(param_1 + 0xac) < 1) { PlayEffect(dry); return; }
     * The bow fires only when arrow_count (owner+0xac) >= kMinArrowsToFire (1);
     * an empty quiver plays the RGMusicManager dry-release effect (owner-side) and
     * fires nothing. NO RGRandom draw.
     * @param arrowCount the remaining arrow count (owner+0xac).
     * @return true if the bow fires at least one arrow; false on a dry release.
     */
    static bool HasArrows(int arrowCount);

    /**
     * @brief Whether the bow fires another arrow after the one at @p firedIndex.
     *
     * FAITHFUL: FUN_00b138f0 @ game_full.c:967091 (the per-arrow loop
     * continuation of Attack) --
     *   if (*(int *)(unaff_r4 + 0xac) <= unaff_r7 + 1) { PlayEffect(dry); return; }
     * where unaff_r7 is the index just fired. So after firing the arrow at index
     * `firedIndex`, another fires only while firedIndex + 1 < arrow_count; the bow
     * thus fires exactly arrow_count arrows (indices 0 .. arrow_count-1) and then
     * plays the dry SFX. The arrow get_Item(index)/GetComponent<RGBullet> spawn is
     * OWNER. NO RGRandom draw.
     * @param firedIndex the 0-based index of the arrow just fired.
     * @param arrowCount the arrow count (owner+0xac).
     * @return true if another arrow (at firedIndex + 1) should fire.
     */
    static bool ShouldFireNext(int firedIndex, int arrowCount);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{}; ///< deterministic stream; never advanced on any path.
};

} // namespace Game

#endif /* GAME_GUNHEROBOW_HPP */
