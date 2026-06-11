#ifndef GAME_GUN012_HPP
#define GAME_GUN012_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class Gun012
 * @brief Faithful multi-barrel FAN scatter math for the "Gun012" weapon
 *        (Soul Knight 1.7.10, an RGWeapon subclass; sibling of Gun002).
 *
 * Per-content port. Gun012 is a MULTI-BARREL FAN gun: a single pull fires
 * `count` pellets fanned around the aim, each pellet given a small random
 * scatter on top of the geometric fan. The body recovered from the decomp
 * computes, per pull:
 *   - the signed-floor FAN START index (the leftmost fan slot), and
 *   - one symmetric random scatter angle drawn from the weapon's RGRandom.
 * The per-pellet PrefabPool/Instantiate spawn loop, the muzzle Transform walk,
 * the Animator trigger ((**)(*this+0x134)) and RGMusicManager.PlayEffect are
 * all OWNER concerns -- the decomp body is truncated ("Subroutine does not
 * return") at the PrefabPool.get_Inst spawn. This brain models ONLY the
 * recoverable scalar/state math that FEEDS the spawn:
 *
 *   - _ctor (Gun012___ctor @ game_full.c:964218): count (this+0x70) = 1 and the
 *     base scatter/recoil angle (this+0x74) = 0xf == 15. These match the sibling
 *     Gun002___ctor @ 315797 exactly. We expose them as the ctor defaults.
 *   - Attack count<1 guard (Gun012__Attack @ game_full.c:964258): a count < 1
 *     pull plays the end-of-fire effect and fires nothing -- modeled by
 *     CanFire() returning false (the Animator trigger + PlayEffect are OWNER).
 *   - Attack FAN START (964282): fanStart = -((count + ((count<<31)>>31)) / 2),
 *     i.e. the signed arithmetic floor of -count/2 with an odd-count bias of -1.
 *     This is the leftmost fan slot index fed through VectorSignedToFloat (the
 *     ARM int->float vcvt) -- modeled by FanStart().
 *   - Attack SCATTER half-angle (964295-964296): the base angle (this+0x30,
 *     decomp iVar3) is scaled by a recoil factor read from a component at
 *     this+0x50 (the virtual *(this+0xf4) call's result +0x20) as
 *     spread = baseAngle + baseAngle * recoil -- modeled by ScatterHalfAngle().
 *   - Attack RANDOM scatter (964301): RGRandom::Range(this+0x60, -spread,
 *     +spread) -- ONE float draw, symmetric, modeled by RollScatter().
 *
 * Determinism: the visible Attack body makes EXACTLY ONE RGRandom float draw
 * (the symmetric scatter); the count<1 guard path makes ZERO draws. The
 * per-pellet spawn loop that consumes FanStart() lives past the truncation point
 * and is the owner's concern, so this brain advances the stream exactly once per
 * RollScatter() call and never on the guard path.
 *
 * @see recreation Enemy/RGEController.cs (field-offset reference);
 *      sibling Gun002__Attack @ game_full.c:315830 (identical scatter shape);
 *      FAITHFUL: Gun012 @ game_full.c:964218-964345.
 */
class Gun012 {
public:
    /// Default pellet count (this+0x70). FAITHFUL: Gun012___ctor @ 964225 = 1.
    static constexpr int kDefaultCount = 1;
    /// Default base scatter/recoil angle (this+0x74).
    /// FAITHFUL: Gun012___ctor @ 964226 = 0xf == 15.
    static constexpr float kDefaultBaseAngle = 15.0F;

    /**
     * @brief Construct a fan gun.
     * @param count      pellet count (this+0x70); defaults to the ctor's 1.
     * @param baseAngle  base scatter/recoil angle (this+0x74); defaults to 15.
     * These mirror the ctor immediates; the owner's weapon config overwrites
     * them before Attack runs (a multi-barrel Gun012 has count > 1 in practice).
     */
    explicit Gun012(int count = kDefaultCount,
                    float baseAngle = kDefaultBaseAngle)
        : m_Count(count), m_BaseAngle(baseAngle) {}

    /// Seed the weapon's deterministic stream (this+0x60). Call once at spawn.
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /// Configured pellet count (this+0x70).
    int Count() const { return m_Count; }
    void SetCount(int count) { m_Count = count; }

    /// Configured base scatter/recoil angle (this+0x74 / read at this+0x30).
    float BaseAngle() const { return m_BaseAngle; }
    void SetBaseAngle(float baseAngle) { m_BaseAngle = baseAngle; }

    /**
     * @brief Whether a pull fires (count >= 1).
     *
     * FAITHFUL: Gun012__Attack @ game_full.c:964283 `if (count < 1)`. A sub-1
     * count plays only the end-of-fire effect (Animator trigger + PlayEffect,
     * both OWNER) and spawns nothing. NO RGRandom draw on this path.
     */
    bool CanFire() const { return m_Count >= 1; }

    /**
     * @brief Signed-floor FAN START index (the leftmost fan slot).
     *
     * FAITHFUL: Gun012__Attack @ 964282:
     *   fanStart = -((count + ((count << 0x1f) >> 0x1f)) / 2)
     * The `(count << 31) >> 31` is an arithmetic shift that sign-extends bit 0:
     * -1 for an odd count, 0 for an even count. So fanStart is the floor of
     * -count/2 with the odd-count bias: count 1->0, 2->-1, 3->-1, 4->-2, 5->-2.
     * The result is fed through VectorSignedToFloat (int->float vcvt) and used as
     * the starting fan offset for the owner's per-pellet spawn loop. NO draw.
     * @return the fan-start slot index for a `count`-pellet pull.
     */
    int FanStart() const;

    /**
     * @brief Static form of FanStart for an arbitrary count (owner convenience).
     * FAITHFUL: same 964282 arithmetic, parameterised by count.
     */
    static int FanStartFor(int count);

    /**
     * @brief Symmetric scatter half-angle (degrees) before the random draw.
     *
     * FAITHFUL: Gun012__Attack @ 964295-964296:
     *   fVar4 = (float)VectorSignedToFloat(iVar3, ...)  // baseAngle int->float
     *   fVar4 = fVar4 + fVar4 * *(float *)(iVar1 + 0x20)  // spread = base + base*recoil
     * where baseAngle is this+0x30 (decomp iVar3, fed through VectorSignedToFloat)
     * and recoil is *(component+0x20), the float read from the result of the
     * virtual *(*(this+0x50)+0xf4) call -- a recoil/accuracy provider owned by the
     * weapon. The owner supplies the live recoil factor; with recoil 0 the spread
     * is exactly the base angle. NO RGRandom draw.
     * @param recoil the recoil factor (component+0x20) the owner has computed.
     * @return the +-half-angle handed to RollScatter().
     */
    float ScatterHalfAngle(float recoil) const;

    /**
     * @brief Draw one symmetric random scatter angle in [-half, +half].
     *
     * FAITHFUL: Gun012__Attack @ 964301 -- RGRandom::Range(this+0x60, -spread,
     * +spread). Range(float) is max-INCLUSIVE. This is the ONLY RGRandom draw the
     * recovered body makes; it advances the stream exactly once. The scattered
     * angle is added to each pellet's fan slot by the owner's spawn loop.
     * @param halfAngle the +-bound from ScatterHalfAngle(recoil).
     * @return the drawn scatter angle (degrees) in [-halfAngle, +halfAngle].
     */
    float RollScatter(float halfAngle);

    /**
     * @brief Convenience: ScatterHalfAngle(recoil) then one RollScatter() draw.
     *
     * FAITHFUL ordering: the decomp computes the half-angle then immediately
     * makes the single Range draw. Exactly one RGRandom draw, matching the body.
     */
    float ScatterAngle(float recoil) { return RollScatter(ScatterHalfAngle(recoil)); }

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};                       ///< this+0x60 (scatter stream).
    int m_Count = kDefaultCount;            ///< this+0x70 (pellet count).
    float m_BaseAngle = kDefaultBaseAngle;  ///< this+0x74 / read at this+0x30.
};

} // namespace Game

#endif /* GAME_GUN012_HPP */
