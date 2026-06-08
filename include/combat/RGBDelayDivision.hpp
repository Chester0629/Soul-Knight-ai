#ifndef GAME_RGB_DELAY_DIVISION_HPP
#define GAME_RGB_DELAY_DIVISION_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class RGBDelayDivision
 * @brief Faithful, engine-free brain for the "RGBDelayDivision" projectile -- a
 *        RGBullet subclass that, after an optional delay, splits into a fan of
 *        `c_count` child bullets spread `c_angle` degrees apart, centred on the
 *        bullet's own travel direction.
 *
 * Per-content port. Models ONLY the pure, unit-testable scalar/state math in the
 * four recovered bodies (AdjustAngle, OnTaken, FixedUpdate, the Division coroutine
 * MoveNext). Every Unity side effect -- Object.Instantiate of the child bullets,
 * RGMusicManager.PlayEffect, Transform.get_transform / rotation, StopCoroutine,
 * WaitForSeconds, the float-box of delay_time for the coroutine, and the
 * Singleton<PrefabPool> fetch -- is left to the owning entity and referenced in
 * comments only.
 *
 * FIELD MAP (from the il2cpp-asset RGBDelayDivision.cs; base RGBullet ends 0x44):
 *   delay_time     float  @ 0x44   (boxed into the Division(delay) coroutine)
 *   c_audio_clip   ref    @ 0x48   (PlayEffect arg -- owner)
 *   c_count        int    @ 0x4c   (number of child bullets in the fan)
 *   c_angle        int    @ 0x50   (degrees between adjacent child bullets)
 *   c_atk          int    @ 0x54   (child damage -- owner UpdateAttribute)
 *   c_critical     int    @ 0x58   (child crit -- owner)
 *   c_bullet       ref    @ 0x5c   (child prefab -- owner Instantiate)
 *   c_bullet_speed float  @ 0x60   (child speed -- owner)
 *   (base RGBullet) rotate_angle int @ 0x1c, awake bool @ 0x20.
 *
 * WHAT THE DECOMP SHOWS (no reconstruction of unseen logic):
 *
 *  - AdjustAngle (@ game_full.c:468170): a spread-angle clamp so the fan never
 *    overfills a circle:
 *        if (c_angle * c_count < 361) return;          // 0x169 == 361
 *        c_angle = 360 / c_count;                       // 0x168 == 360, int divide
 *    The divide is __aeabi_idiv(0x168, c_count) (FUN_001ceef4 @ 4881 is idiv; the
 *    decomp dropped the visible divisor, recovered from the sibling callers at
 *    game_full.c:256702 and 430743 which both pass `0x168, count`). NO RGRandom.
 *
 *  - OnTaken (@ game_full.c:468185): calls base RGBullet::OnTaken (owner), runs the
 *    SAME clamp written as `if (0x168 < c_angle * c_count) c_angle = 360 / c_count`,
 *    then StopCoroutine + boxes delay_time (0x44) to (re)start the Division coroutine
 *    -- both owner side. The recoverable part is the identical clamp. NO RGRandom.
 *
 *  - FixedUpdate (@ game_full.c:468263): a pure gate, no body work of its own here:
 *        bool go = awake;                               // 0x20
 *        int  r  = go ? rotate_angle : 0;               // 0x1c
 *        if (!go || r == 0) return;
 *        get_transform(...);                            // owner rotation
 *    i.e. it only proceeds when `awake && rotate_angle != 0`. The transform spin is
 *    the owner's; the GATE is recoverable. NO RGRandom.
 *
 *  - Division MoveNext (@ game_full.c:468300): a 3-state iterator. Dispatch:
 *        s = state(0x18); state = -1; disp = (s < 3) ? s + 3 : 0;
 *        disp 3 -> wait block (state 0), 4 -> spawn block (state 1), 5 -> finalise.
 *    The spawn block computes the leftmost child index from c_count (0x4c):
 *        startIndex = (count even) ? -(count / 2) : -((count - 1) / 2);
 *    each child is then offset by startIndex..(+) * c_angle and Instantiated (owner).
 *    The fan-index math is recoverable; Instantiate/PlayEffect/transform are owner.
 *    NO RGRandom.
 *
 * Determinism: none of the four bodies calls rg_random, so this unit makes ZERO RNG
 * draws. The RGRandom member is carried only for interface parity and to let a
 * caller confirm the seed without ever advancing the stream.
 *
 * @see il2cpp RGBDelayDivision.cs (field names above).
 * FAITHFUL: RGBDelayDivision @ game_full.c:468170+.
 */
class RGBDelayDivision {
public:
    /// Full circle in degrees; the idiv numerator (0x168) the clamp divides by count.
    static constexpr int kFullCircleDegrees = 360;
    /// Clamp threshold: the spread is re-fit only when `c_angle*c_count` reaches 361
    /// (the decomp's `< 0x169` / `0x168 <` boundary, i.e. strictly past a full turn).
    static constexpr int kOverfillThreshold = 361;

    /// Division coroutine dispatch states (iterator field 0x18).
    enum class State : int {
        Wait = 0,    ///< state 0 -> disp 3: optional WaitForSeconds(delay) block.
        Spawn = 1,   ///< state 1 -> disp 4: PlayEffect + Instantiate the fan.
        Finalise = 2,///< state 2 -> disp 5: PrefabPool finalise / recycle.
        Done = -1,   ///< 0xffffffff: coroutine finished / no further yield.
    };

    RGBDelayDivision() = default;

    /// Seed the deterministic stream (kept untouched; no draw is ever made).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // --- AdjustAngle / OnTaken: the spread-angle clamp ---------------------

    /**
     * @brief True when the fan would overfill a circle (spread * count >= 361).
     *
     * FAITHFUL: RGBDelayDivision__AdjustAngle @ game_full.c:468175 guard
     * (`c_angle * c_count < 0x169` returns) and RGBDelayDivision__OnTaken
     * @ game_full.c:468196 (`0x168 < c_angle * c_count`). Both gate on the product
     * reaching 361; this returns that predicate.
     * @param angle the spread angle c_angle (0x50), degrees.
     * @param count the child count c_count (0x4c).
     */
    static bool ShouldClampAngle(int angle, int count);

    /**
     * @brief Clamp the per-bullet spread angle so `count` bullets fit one circle.
     *
     * FAITHFUL: RGBDelayDivision__AdjustAngle @ game_full.c:468178-468179 and the
     * identical body in OnTaken @ 468197-468198. When ShouldClampAngle holds, the
     * spread becomes `360 / count` (integer divide, __aeabi_idiv); otherwise the
     * spread is left exactly as-is (the early `return`). Mirrors the decomp's "only
     * write c_angle when the product overflows" behaviour -- the field is NOT touched
     * on the pass-through path.
     * @param angle the current spread angle c_angle (0x50), degrees.
     * @param count the child count c_count (0x4c); must be non-zero to divide.
     * @return the (possibly clamped) spread angle to store back into 0x50.
     */
    static int ClampAngle(int angle, int count);

    // --- FixedUpdate: the spin gate ----------------------------------------

    /**
     * @brief The FixedUpdate predicate: proceed only when awake and rotating.
     *
     * FAITHFUL: RGBDelayDivision__FixedUpdate @ game_full.c:468269-468276.
     * `bool go = awake; int r = go ? rotate_angle : 0; if (!go || r == 0) return;`
     * i.e. the transform spin runs iff `awake && rotate_angle != 0`. The
     * get_transform rotation itself is the owner's; this returns the gate result.
     * @param awake       the bullet awake flag (0x20).
     * @param rotateAngle the base RGBullet rotate_angle (0x1c), degrees.
     * @return true when FixedUpdate would advance to the owner rotation.
     */
    static bool ShouldRotate(bool awake, int rotateAngle);

    // --- Division MoveNext: the fan layout + dispatch ----------------------

    /**
     * @brief Map the stored iterator state (0x18) to its dispatch block.
     *
     * FAITHFUL: Division MoveNext header @ game_full.c:468312-468318.
     * `s = state; state = -1; disp = (s < 3) ? s + 3 : 0;` then disp 3 = Wait,
     * 4 = Spawn, 5 = Finalise; anything else is a no-op (Done). The `s < 3` test is
     * the recovered UNSIGNED compare, so the -1 (Done) sentinel falls through to 0.
     */
    static State Dispatch(State stored);

    /**
     * @brief Leftmost child index of the symmetric fan, given the child count.
     *
     * FAITHFUL: Division MoveNext spawn block @ game_full.c:468348-468355.
     *   uVar2 = c_count;
     *   if ((uVar2 & 1) == 0) start = -(count / 2);        // even count
     *   else                  start = -((count - 1) / 2);  // odd count
     * The child at offset i (i = start..) is launched at angle base + i*c_angle.
     * This returns `start`; the per-bullet Instantiate and rotation are owner work.
     * @param count the child count c_count (0x4c).
     * @return the signed index of the first (leftmost) child in the fan.
     */
    static int FanStartIndex(int count);

    // --- Division coroutine state (mirrors the iterator object) -------------

    /**
     * @brief Enter the spawn block: latch the fan size for iteration.
     *
     * FAITHFUL: Division MoveNext spawn entry. Records the child count so SpawnStep
     * can walk the fan; the PlayEffect(c_audio_clip), Instantiate(c_bullet) and
     * Singleton<PrefabPool> fetch are owner side effects.
     * @param count the child count c_count (0x4c).
     */
    void BeginSpawn(int count);

    /**
     * @brief One step of the fan spawn walk: returns the next child's index.
     *
     * FAITHFUL: the fan iteration implied by the spawn block -- it walks `count`
     * children starting at FanStartIndex(count) and incrementing by one each step.
     * Each returned index multiplies c_angle to give that child's angular offset
     * (the Instantiate + rotation is owner). Returns false once the fan is exhausted,
     * at which point the machine advances to Done.
     * @param outIndex receives the next child index when the call returns true.
     * @return true while a child remains; false when the fan is complete.
     */
    bool SpawnStep(int &outIndex);

    /// Live coroutine state (mirrors iterator field 0x18).
    State CurrentState() const { return m_State; }
    /// Remaining children to spawn in the current fan walk.
    int Remaining() const { return m_Remaining; }
    /// Next child index the walk will emit.
    int NextIndex() const { return m_NextIndex; }
    /// True once the fan walk has completed.
    bool SpawnComplete() const { return m_State == State::Done; }

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};                 ///< deterministic stream; never advanced here.
    State m_State = State::Wait;      ///< iterator dispatch state (0x18).
    int m_Remaining = 0;              ///< children left to spawn in the fan.
    int m_NextIndex = 0;             ///< signed index of the next child to spawn.
};

} // namespace Game

#endif /* GAME_RGB_DELAY_DIVISION_HPP */
