#ifndef GAME_GUNMULTIBULLET_HPP
#define GAME_GUNMULTIBULLET_HPP

#include <cstddef>
#include <vector>

namespace Game {

/**
 * @class GunMultiBullet
 * @brief Faithful per-bullet stat-override selectors for the "GunMultiBullet"
 *        weapon (Soul Knight 1.7.10, an RGWeapon subclass).
 *
 * Per-content port. GunMultiBullet is a weapon whose shots can carry a DIFFERENT
 * attack / speed / pierce / crit per bullet index: it owns four optional override
 * arrays plus four scalar fallbacks. Each of the four virtual getters
 * (GetAttack / GetSpeed / GetCanThrough / GetCritics) makes the SAME decision:
 *
 *   if the override array has the same element count as the bullet-count array
 *   AND the requested index is in range  ->  return the indexed override element;
 *   otherwise  ->  return the single scalar fallback.
 *
 * Field-offset map recovered from the decomp:
 *   - bullet-count array          owner+0x6c   (its length is the per-shot count)
 *   - attack override array       owner+0x70   ; attack scalar fallback   owner+0x20
 *   - speed  override array       owner+0x74   ; speed  scalar fallback   owner+0x28
 *   - crit   override array       owner+0x78   ; crit   scalar fallback   owner+0x2c
 *   - pierce override array       owner+0x80   ; pierce scalar fallback   owner+0x38
 * Every IL2CPP array stores its element count at +0xc and its first element at
 * +0x10; the equal-count test compares overrideArray[0xc] == bulletCount[0xc].
 *
 * This brain models ONLY the recoverable decision + index formula. The arrays'
 * CONTENTS are owner-populated (they are filled elsewhere from weapon data), so
 * we take them as inputs: the override array as a std::vector and the bullet-
 * count array's length as a plain size. The getters are PURE selectors -- none
 * of the four draws an RGRandom value (only the truncated CreateBullet spawn
 * path does, and that scatter draw is the OWNER's, see the note below).
 *
 * NOT modeled (OWNER-side): GunMultiBullet__CreateBullet @ game_full.c:967491 is
 * the actual fire path. Its recoverable scalar (the scatter half-angle
 * angle + angle*recoil and the single Range(-spread, +spread) draw) is the SAME
 * shape already ported in Gun019::SpreadHalfAngle / Gun019::ScatterAngle; the
 * body's tail (the GetComponent recoil fetch at vtable+0xf4, the PrefabPool
 * Singleton spawn) is truncated ("Subroutine does not return"), and the spawn /
 * muzzle Transform are the owner's. We do not re-port that shared shape here;
 * this module is the four override selectors the completeness sweep flagged.
 *
 * @see include/combat/Gun019.hpp (the shared scatter shape of CreateBullet);
 *      recreation Weapon/RGWeapon.cs (field-offset reference);
 *      FAITHFUL: GunMultiBullet @ game_full.c:967458-967675.
 */
class GunMultiBullet {
public:
    /// ctor (owner+0x88 = 0xf): an int config field the ctor seeds to 15. It is
    /// NOT one of the four scalar fallbacks (0x20/0x28/0x2c/0x38) and does not
    /// enter any selector; recorded for completeness only.
    /// FAITHFUL: GunMultiBullet___ctor @ game_full.c:967466 (*(owner+0x88) = 0xf).
    static constexpr int kCtorField0x88 = 15;

    /// ctor (owner+0x85 = 1): a byte config field the ctor seeds to 1. Likewise
    /// not a selector input; recorded for completeness only.
    /// FAITHFUL: GunMultiBullet___ctor @ game_full.c:967465 (*(owner+0x85) = 1).
    static constexpr bool kCtorField0x85 = true;

    GunMultiBullet() = default;

    /**
     * @brief Whether the per-index override path (vs the scalar fallback) is
     *        taken, for an override array of @p overrideCount elements against a
     *        bullet-count array of @p bulletCount elements at @p index.
     *
     * FAITHFUL: the shared head of all four getters, e.g.
     * GunMultiBullet__GetAttack @ game_full.c:967555 --
     *   if (*(overrideArray + 0xc) == *(bulletCount + 0xc)) { ...indexed... }
     *   else { ...scalar... }
     * with the equal-count branch's IL2CPP bounds check at 967561
     * (*(overrideArray + 0xc) <= index -> throw). When the counts are equal the
     * override array is exactly as long as the bullet-count array, so a valid
     * bullet @p index (< bulletCount) is always in range; we fold the equal-count
     * test and the in-range test into one predicate (the original would throw an
     * IndexOutOfRange before reaching the scalar branch, never silently fall
     * back). NO RGRandom draw.
     * @param overrideCount the override array's element count (its +0xc).
     * @param bulletCount   the bullet-count array's element count (its +0xc).
     * @param index         the requested bullet index (param_2).
     * @return true -> return overrideArray[index]; false -> return the scalar.
     */
    static bool UsesOverride(std::size_t overrideCount, std::size_t bulletCount,
                             std::size_t index) {
        return overrideCount == bulletCount && index < overrideCount;
    }

    /**
     * @brief Per-bullet ATTACK: indexed override element, else the scalar.
     *
     * FAITHFUL: GunMultiBullet__GetAttack @ game_full.c:967541-967570.
     * Override array owner+0x70, scalar fallback owner+0x20. The indexed element
     * is read raw (*(array + index*4 + 0x10), an int attack value); the scalar
     * fallback is *(owner+0x20). Selector only -- NO RGRandom draw.
     * @param attackOverride the per-index attack override array (owner+0x70).
     * @param bulletCount    the bullet-count array's length (owner+0x6c, +0xc).
     * @param index          the bullet index.
     * @param attackScalar   the scalar attack fallback (owner+0x20).
     */
    static int GetAttack(const std::vector<int> &attackOverride,
                         std::size_t bulletCount, std::size_t index,
                         int attackScalar);

    /**
     * @brief Per-bullet SPEED: indexed override element, else the scalar.
     *
     * FAITHFUL: GunMultiBullet__GetSpeed @ game_full.c:967575-967606.
     * Override array owner+0x74, scalar fallback owner+0x28. NOTE the asymmetry:
     * the indexed element is read through VectorSignedToFloat (an int->float
     * widening at 967600), while the scalar fallback (967604) is returned raw.
     * We take both as float here (the caller supplies the owner's already-widened
     * representation); the int->float conversion of the array element is an owner
     * storage detail, not a computation we can re-derive. Selector only -- NO
     * RGRandom draw.
     * @param speedOverride the per-index speed override array (owner+0x74).
     * @param bulletCount   the bullet-count array's length (owner+0x6c, +0xc).
     * @param index         the bullet index.
     * @param speedScalar   the scalar speed fallback (owner+0x28).
     */
    static float GetSpeed(const std::vector<float> &speedOverride,
                          std::size_t bulletCount, std::size_t index,
                          float speedScalar);

    /**
     * @brief Per-bullet CAN-THROUGH (pierce): indexed override element, else the
     *        scalar.
     *
     * FAITHFUL: GunMultiBullet__GetCanThrough @ game_full.c:967611-967640.
     * Override array owner+0x80, scalar fallback owner+0x38. These are BYTE
     * (bool) values, so the index formula is *(array + index + 0x10) -- stride 1,
     * NOT index*4 (967635) -- and the scalar is the byte *(owner+0x38).
     * Selector only -- NO RGRandom draw.
     * @param canThroughOverride the per-index pierce override array (owner+0x80).
     * @param bulletCount        the bullet-count array's length (owner+0x6c, +0xc).
     * @param index              the bullet index.
     * @param canThroughScalar   the scalar pierce fallback (owner+0x38).
     */
    static bool GetCanThrough(const std::vector<bool> &canThroughOverride,
                              std::size_t bulletCount, std::size_t index,
                              bool canThroughScalar);

    /**
     * @brief Per-bullet CRITICS: indexed override element, else the scalar.
     *
     * FAITHFUL: GunMultiBullet__GetCritics @ game_full.c:967645-967674.
     * Override array owner+0x78, scalar fallback owner+0x2c. The indexed element
     * is read raw (*(array + index*4 + 0x10), an int crit value) and the scalar
     * fallback is *(owner+0x2c). Selector only -- NO RGRandom draw.
     * @param critOverride the per-index crit override array (owner+0x78).
     * @param bulletCount  the bullet-count array's length (owner+0x6c, +0xc).
     * @param index        the bullet index.
     * @param critScalar   the scalar crit fallback (owner+0x2c).
     */
    static int GetCritics(const std::vector<int> &critOverride,
                          std::size_t bulletCount, std::size_t index,
                          int critScalar);
};

} // namespace Game

#endif /* GAME_GUNMULTIBULLET_HPP */
