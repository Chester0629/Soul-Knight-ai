#ifndef GAME_GUN_THROW_HPP
#define GAME_GUN_THROW_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class GunThrow
 * @brief Faithful spread-angle / consume math for the "GunThrow" multi-shot
 *        weapon (Soul Knight 1.7.10).
 *
 * Per-content port. GunThrow fires several bullets per pull, fanned out around
 * the aim direction by a deterministic, geometric spread (NO RGRandom draw on
 * any modelled path -- the scatter is purely a function of bullet count and the
 * per-bullet index). The bullet Instantiate / GetComponent<RGBullet> / muzzle
 * transform walks / RGMusicManager.PlayEffect and the throw-weapon coroutine's
 * actual GameObject side-effects all belong to the owning weapon entity; this
 * brain models only the recoverable scalar math that FEEDS them:
 *
 *   - GetShootAngle (969528): the per-bullet spread fan-out euler-Z angle, by
 *     bullet count and bullet index. <2 -> 0; <4 -> 15deg; <6 -> 30deg;
 *     6..7 -> kSpread6to7; 8+ -> kSpread8plus (the last two magnitudes are
 *     UNRECOVERABLE -- see fabrication flags).
 *   - GetWeaponAngle (969374): the sibling weapon-angle euler-Z for the thrown
 *     weapon child (a different fan-out using a 60-degree default span).
 *   - ResetConsume (969427): consume_count = CeilToInt(damage*count / bulletCnt).
 *   - ResetDir (969505): the "do we have a muzzle child" guard (childCount > 0).
 *   - Throwing coroutine MoveNext (969650): the iterator counter/limit/state
 *     transitions (state 0 -> -1, gated on childCount > 0).
 *
 * The original packs each result into a Vector3 whose x=y=0 and z carries the
 * angle (an euler-Z rotation fed to Quaternion.Euler by the owner); we return
 * the scalar z directly. Truncated tails, Vector3/Mathf/Transform intrinsics,
 * bullet spawns and effects are OWNER concerns.
 *
 * @see recreation Enemy/RGEController.cs (field-offset reference);
 *      FAITHFUL: GunThrow @ game_full.c:969323-969720.
 */
class GunThrow {
public:
    /// Bullet counts below this fire a single straight shot (spread = 0).
    /// FAITHFUL: GetShootAngle/GetWeaponAngle early-out `if (count < 2)`.
    static constexpr int kMinSpreadCount = 2;

    /// Spread span (degrees) for 2..3 bullets. FAITHFUL: 969560 `fVar12 = 15`.
    static constexpr float kSpread2to3 = 15.0F;
    /// Spread span (degrees) for 4..5 bullets. FAITHFUL: 969563 `fVar12 = 30`.
    static constexpr float kSpread4to5 = 30.0F;

    /// Spread span (degrees) for 6..7 bullets. UNRECOVERABLE: this is
    /// *DAT_00b24804, declared `undefined4 DAT_00b24804;` (game_full.c:896807)
    /// with NO initialized value. Exposed but NOT guessed.
    // TODO[verify] real value of DAT_00b24804 (6..7 bullet spread magnitude).
    static constexpr float kSpread6to7 = 0.0F;
    /// Spread span (degrees) for 8+ bullets. UNRECOVERABLE: this is
    /// *DAT_00b24808, declared `undefined4 DAT_00b24808;` (game_full.c:896805)
    /// with NO initialized value. Exposed but NOT guessed.
    // TODO[verify] real value of DAT_00b24808 (8+ bullet spread magnitude).
    static constexpr float kSpread8plus = 0.0F;

    /// Default weapon-angle span (degrees) used by GetWeaponAngle when the
    /// thrown-child count field is 0. FAITHFUL: 969406 `fVar4 = 60`.
    static constexpr float kWeaponAngleDefaultSpan = 60.0F;

    GunThrow() = default;

    /// Seed the weapon's deterministic stream. GunThrow's modelled math takes no
    /// RGRandom draw, but the stream is carried for owner-side parity/lockstep.
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief Spread span (degrees) selected purely by bullet count.
     *
     * FAITHFUL: GunThrow__GetShootAngle @ game_full.c:969528 count cascade
     * (969559-969570). Returns 0 for count < 2 (the Vector3.zero early-out),
     * 15 for <4, 30 for <6, kSpread6to7 for 6..7, kSpread8plus for 8+.
     * NOTE: for counts >= 6 the returned span is the UNRECOVERABLE DAT_*
     * constant (still 0.0F here) -- do not assert its value.
     */
    static float SpreadSpanForCount(int bulletCount);

    /**
     * @brief Per-bullet spread euler-Z angle (degrees) for GetShootAngle.
     *
     * FAITHFUL: GunThrow__GetShootAngle @ game_full.c:969528. The owner passes
     * the per-bullet index in `bulletIndex` (decomp param_3) and the weapon's
     * configured bullet count in `bulletCount` (param_2+0x6c, decomp uVar7).
     * `baseAngleOffset` is the field at param_2+0x30 (decomp fVar9), added under
     * the abs to the span. Result is z = ABS(span + baseAngleOffset) *
     * (signedStep / Abs(halfSteps)); x=y=0 in the original Vector3.
     *
     * Returns 0 for bulletCount < 2 (Vector3.zero early-out). NO RGRandom draw.
     */
    static float GetShootAngle(int bulletCount, int bulletIndex,
                               float baseAngleOffset = 0.0F);

    /**
     * @brief Thrown-weapon child fan-out euler-Z angle (degrees).
     *
     * FAITHFUL: GunThrow__GetWeaponAngle @ game_full.c:969374. Uses count
     * (param_2+0x6c, decomp iVar2) and a sibling count field (param_2+0x7c,
     * decomp iVar3); when that field is 0 the span defaults to 60. The index
     * is param_3. Result z = span * (idxOffset / halfCount) * 0.5, with a
     * parity half-step bias on even counts. Returns 0 for count < 2.
     */
    static float GetWeaponAngle(int bulletCount, int siblingCount, int index);

    /**
     * @brief Consume count per pull = CeilToInt(damage * count / bulletCount).
     *
     * FAITHFUL: GunThrow__ResetConsume @ game_full.c:969427 (969445-969452).
     * damage is param_1+0x88, count is param_1+0x70, bulletCount is param_1+0x6c.
     * Stores into param_1+0x34. The UICanvas / local-player ammo-bar refresh
     * tail is the owner's concern. NO RGRandom draw.
     */
    static int ResetConsume(int damage, int count, int bulletCount);

    /**
     * @brief Whether ResetDir has a muzzle child to reset (childCount > 0).
     *
     * FAITHFUL: GunThrow__ResetDir @ game_full.c:969505: `if (childCount < 1)
     * return;` then Transform.GetChild(0)/get_transform (owner side-effect).
     * childCount is param_1+0x88. We model only the gate.
     */
    static bool ResetDirHasChild(int childCount);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};
};

/**
 * @class GunThrowThrowingIterator
 * @brief Recoverable counter/state of the <Throwing>c__Iterator0 coroutine.
 *
 * FAITHFUL: GunThrow_<Throwing>c__Iterator0__MoveNext @ game_full.c:969650.
 * The iterator's only recoverable scalar/state logic is the single-step
 * lifecycle: state (iterator field +0x14) is read, immediately set to -1
 * (done), and the body acts only while the state was 0 or 1 (`(state|1)==1`),
 * gated additionally on the owner's childCount > 0 (param+8 -> +0x88). It
 * always returns false (0): the coroutine yields nothing further. The actual
 * GetChild/get_gameObject throw side-effect is the OWNER's. We model the state
 * machine + the "fire?" predicate.
 */
class GunThrowThrowingIterator {
public:
    /// State values: kRunning(0)/kStarted(1) both satisfy `(state|1)==1`;
    /// kDone(-1) is the terminal value MoveNext always writes back.
    static constexpr int kRunning = 0;
    static constexpr int kStarted = 1;
    static constexpr int kDone = -1;

    GunThrowThrowingIterator() = default;

    int State() const { return m_State; }

    /**
     * @brief Advance the coroutine one step.
     *
     * FAITHFUL: MoveNext @ 969650. Reads state, writes -1 unconditionally, and
     * (only if the prior state was 0/1 AND childCount > 0) the owner performs
     * the throw. Always returns false. Sets fired=true on the throw step.
     * @param childCount the owner weapon's muzzle child count (param+0x88).
     * @return false (the iterator never asks to be pumped again).
     */
    bool MoveNext(int childCount);

    /// True iff the last MoveNext hit the throw step (state 0/1 && childCount>0).
    bool Fired() const { return m_Fired; }

private:
    int m_State = kRunning;
    bool m_Fired = false;
};

} // namespace Game

#endif /* GAME_GUN_THROW_HPP */
