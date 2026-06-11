#ifndef GAME_GUN014_HPP
#define GAME_GUN014_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class Gun014
 * @brief Faithful scatter/fan + rotating-reflect-angle math for the "Gun014"
 *        multi-barrel weapon (Soul Knight 1.7.10, an RGWeapon subclass).
 *
 * Per-content port. Gun014 is a MULTI-BARREL gun whose base aim angle is
 * periodically re-rolled to a random 0..360 heading (the "rotating" reflect
 * angle), and which fires a symmetric fan of `count` pellets, each pellet
 * jittered by a recoil-scaled random scatter. The whole firing path is heavily
 * truncated -- the bullet Instantiate / PrefabPool spawn (Singleton<PrefabPool>),
 * the muzzle-Transform read, RGMusicManager.PlayEffect and the MonoBehaviour
 * Invoke re-fire are all OWNER concerns. This brain models ONLY the recoverable
 * scalar/state math that FEEDS the spawn, preserving every RGRandom draw in
 * count and order:
 *
 *   - AdjustAngle (Gun014__AdjustAngle @ game_full.c:964350): the rotating base
 *     angle (this+0x7c) is re-rolled ONLY when angle(0x7c) * count(0x6c) >= 361
 *     (the decomp gates on `< 0x169` -> return). The re-roll is integer DIVISION,
 *     NOT RNG: FUN_001ceef4 is __aeabi_idiv (its body at game_full.c:4881), so
 *     angle = 360 / <divisor> (numerator 0x168 == 360; the divisor is owner-fed and
 *     was dropped by Ghidra). param_1+0x60 (the m_Rng field) is never accessed in
 *     this function body (964350-964361), so m_Rng is NOT advanced by ReadjustAngle().
 *     ShouldReadjust() models the gate; ReadjustAngle() returns the gate result.
 *   - Attack (Gun014__Attack @ game_full.c:964365): the fire/re-fire branch --
 *     in_atk(0x1c)==0 fires CreateBullet immediately, else schedules a delayed
 *     re-fire via Invoke(..., delay(0x1d)). FireOrInvoke() models that predicate;
 *     the actual CreateBullet call, the Invoke, the get_transform slot-call and
 *     RGMusicManager.PlayEffect tail are OWNER. NO RGRandom draw on this body.
 *   - CreateBullet (Gun014__CreateBullet @ game_full.c:964392): the per-pellet
 *     scatter math. Guards count(0x6c) < 1; the symmetric fan START index is
 *     -(count/2) for even counts and -((count-1)/2) for odd (FanStartIndex());
 *     the scatter half-width is base(0x30) + base*recoil(recoilObj+0x20)
 *     (SpreadWithRecoil()); and the per-pellet jitter is the single float draw
 *     RGRandom.Range(-spread, +spread) (ScatterDraw()). The rotating base angle
 *     (0x7c) carried to the spawn and the PrefabPool Instantiate are OWNER.
 *
 * Determinism: AdjustAngle does NOT advance m_Rng (FUN_001ceef4 is __aeabi_idiv
 * integer division -- angle = 360/divisor; param_1+0x60 is never touched). CreateBullet
 * draws ONE float (Range(-spread,+spread), max-inclusive) per pellet via
 * m_Rng. Attack draws none. Every modelled method preserves its draw count
 * and order against a parallel same-seeded stream for the float-only scatter.
 *
 * @see recreation Enemy/RGEController.cs (field-offset reference);
 *      FAITHFUL: Gun014 @ game_full.c:964350-964442.
 */
class Gun014 {
public:
    /// Rotating-angle division NUMERATOR: angle = 360 / <divisor> (NOT an RGRandom range --
    /// FUN_001ceef4 is __aeabi_idiv integer division, and the divisor is owner-fed/unrecovered).
    /// FAITHFUL: Gun014__AdjustAngle @ 964358 -- FUN_001ceef4 == __aeabi_idiv, 0x168 = 360.
    static constexpr int kAngleRange = 360;
    /// Readjust gate threshold: the decomp returns when angle*count < 0x169.
    /// FAITHFUL: Gun014__AdjustAngle @ 964354 -- 0x169 = 361.
    static constexpr int kReadjustThreshold = 361;
    /// Minimum bullet count to fire: CreateBullet returns when count < 1.
    /// FAITHFUL: Gun014__CreateBullet @ 964406 -- `if (count < 1) return`.
    static constexpr int kMinBulletCount = 1;

    Gun014() = default;

    /// Seed the deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /// Current rotating base angle (this+0x7c), in degrees.
    int BaseAngle() const { return m_BaseAngle; }
    /// Owner-set rotating base angle (the aim heading the fan is built around).
    void SetBaseAngle(int angle) { m_BaseAngle = angle; }

    /**
     * @brief AdjustAngle gate: should the base angle be re-rolled this call?
     *
     * FAITHFUL: Gun014__AdjustAngle @ 964354 -- the body returns (no draw) while
     * `angle(0x7c) * count(0x6c) < 0x169`; the re-roll happens on the complement
     * (product >= kReadjustThreshold). Pure predicate, NO RGRandom draw.
     */
    bool ShouldReadjust(int bulletCount) const;

    /**
     * @brief AdjustAngle re-roll: if the gate passes, signal that the base angle
     * should be re-rolled.
     *
     * FAITHFUL: Gun014__AdjustAngle @ 964350. FUN_001ceef4(0x168) is called with
     * NO rng-instance argument; param_1+0x60 (the RGRandom field) is never
     * accessed in Gun014__AdjustAngle (964350-964361). Therefore m_Rng is NOT
     * advanced here. The owner is responsible for obtaining the new angle from
     * the global helper FUN_001ceef4(0x168) and writing it to the base-angle
     * field (this+0x7c).
     * TODO[verify]: FUN_001ceef4(0x168) is not RGRandom::Range; do not advance m_Rng here.
     * @return true if the gate passed (owner must re-roll the base angle).
     */
    bool ReadjustAngle(int bulletCount);

    /**
     * @brief CreateBullet fire guard: count(0x6c) must be >= 1.
     * FAITHFUL: Gun014__CreateBullet @ 964406. Pure predicate, NO draw.
     */
    static bool CanCreateBullet(int bulletCount);

    /**
     * @brief Symmetric fan START index for `bulletCount` pellets.
     *
     * FAITHFUL: Gun014__CreateBullet @ 964422-964427 -- even count -> -(count/2);
     * odd count -> -((count-1)/2). This is the lowest (most-negative) pellet
     * offset; the owner walks index..index+count building the fan. Pure, NO draw.
     */
    static int FanStartIndex(int bulletCount);

    /**
     * @brief Scatter half-width: base spread plus its recoil contribution.
     *
     * FAITHFUL: Gun014__CreateBullet @ 964420/964428 --
     *   fVar5 = base(0x30); fVar5 = fVar5 + fVar5 * recoil(recoilObj+0x20).
     * i.e. base * (1 + recoil). This half-width is the +-bound of the scatter
     * draw below. Pure scalar, NO RGRandom draw.
     * @param baseSpread the configured base spread angle (this+0x30, degrees).
     * @param recoil     the recoil multiplier read from the weapon-config object
     *                   (recoilObj+0x20), set by the owner.
     */
    static float SpreadWithRecoil(float baseSpread, float recoil);

    /**
     * @brief Per-pellet scatter jitter: one symmetric float draw.
     *
     * FAITHFUL: Gun014__CreateBullet @ 964434 --
     *   RGRandom.Range(rng(0x60), -fVar5, fVar5) (max-inclusive). Draws exactly
     *   ONE float in [-spread, +spread]; the result is added to the pellet's
     *   heading by the owner before the PrefabPool Instantiate (OWNER tail).
     * @param spreadHalfWidth the half-width from SpreadWithRecoil().
     * @return the scatter offset (degrees) for this pellet.
     */
    float ScatterDraw(float spreadHalfWidth);

    /**
     * @brief Attack fire/re-fire predicate.
     *
     * FAITHFUL: Gun014__Attack @ 964373 -- `if ((char)in_atk(0x1c) == 0)` fire
     * CreateBullet now; else schedule a delayed re-fire via Invoke(..., delay).
     * @return true when the weapon should fire immediately (in_atk == 0); false
     *         when it should schedule the delayed re-fire instead. The actual
     *         CreateBullet / Invoke call, the get_transform slot-call and the
     *         RGMusicManager.PlayEffect tail are OWNER concerns. NO RGRandom draw.
     */
    static bool FireOrInvoke(bool inAtk);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{}; ///< deterministic stream (this+0x60); used by ScatterDraw only.
                     ///< NOT used by ReadjustAngle -- FUN_001ceef4(0x168) is global.
                     ///< TODO[verify]: FUN_001ceef4(0x168) is not RGRandom::Range; do not advance m_Rng here.
    int m_BaseAngle = 0; ///< this+0x7c: rotating base aim angle (degrees).
};

} // namespace Game

#endif /* GAME_GUN014_HPP */
