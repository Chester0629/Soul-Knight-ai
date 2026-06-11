#ifndef GAME_GUN007_HPP
#define GAME_GUN007_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class Gun007
 * @brief Faithful charge-cannon scalar/state math for the "Gun007" weapon
 *        (Soul Knight 1.7.10, an RGWeapon subclass).
 *
 * Per-content port. Gun007 is a CHARGE cannon, richer than the ported Gun005:
 * the longer the trigger is held, the more bullets it fires (a charge-scaled
 * burst), the further forward the muzzle pushes, and the larger the projectile
 * grows. On release, Attack reads the accrued charge ratio and kicks off a
 * coroutine (the nested Gun007_<>c__Iterator0, via StartCoroutine(CreateBullet))
 * that spawns the burst frame-by-frame.
 *
 * This brain models ONLY the recoverable scalar/state math that FEEDS the
 * spawn. Everything that touches Unity is OWNER:
 *   - Attack's AudioSource.Stop, StartCoroutine, Object.Destroy, the UICanvas
 *     full-charge UI hook and the recovered-jumptable RGWeapon vcall tail
 *     (Gun007__Attack @ 317096; jumptable @ 0x3f0db0 = OWNER);
 *   - the coroutine's WaitForSeconds yields, GameObject.get_transform /
 *     Destroy / SetActive walks, the muzzle-prefab GetComponent<RGBullet> spawn
 *     and the RGWeapon.TurnActivate at the end of the burst (MoveNext @ 317174);
 *   - CreateBullet itself is just the iterator allocation thunk (317134).
 *
 * Recoverable here:
 *   - the charge ratio (Attack @ 317110): charge = a_time(this[0x26]) /
 *     max_time(this[0x27]); and the full-charge gate 1.0 <= charge (317117);
 *   - the burst bullet count (MoveNext @ 317246-317253):
 *     bulletCount = Max(1, FloorToInt(maxCount * Min(1, charge)));
 *   - the charge-scaled muzzle offset (MoveNext @ 317274-317290):
 *     muzzle = base + charge * dir (per axis);
 *   - the size lerp (MoveNext @ 317320): size = start + (end-start)*charge;
 *   - the threshold flag (MoveNext @ 317314): when the owner's alt-spawn field is
 *     set, the flag is (0.6 < charge);
 *   - the coroutine counter/limit/state machine (MoveNext @ 317189): state at
 *     iterator+0x58 maps state 0/1/2 -> 3/4/5; 3 = setup (compute count/muzzle/
 *     size/flag, yield), 4 = burst tick (counter(0x28)++; counter < limit(0xc)?
 *     fire+yield : cleanup+yield), 5 = TurnActivate end.
 *
 * Determinism: NO Gun007 body draws from rg_random -- the burst is a fully
 * deterministic charge-scaled geometric expansion, not random scatter. This unit
 * therefore makes ZERO RNG draws. The RGRandom member is carried only for
 * owner-side parity/lockstep and is never advanced; Seeded() lets a caller
 * confirm the seed without a draw.
 *
 * @see recreation Enemy/RGEController.cs (field-offset reference);
 *      FAITHFUL: Gun007 @ game_full.c:317096-317429.
 */
class Gun007 {
public:
    /// Full-charge gate (Attack @ 317117: `1.0 <= charge`). At or above this the
    /// owner shows the full-charge UI hook (UICanvas.GetInstance, OWNER).
    static constexpr float kFullChargeRatio = 1.0F;

    /// Charge clamp applied before scaling the bullet count
    /// (MoveNext @ 317246: `Mathf.Min(1.0, charge)`). The bullet count never
    /// grows past maxCount even on an over-charge.
    static constexpr float kBulletCountChargeCap = 1.0F;

    /// Minimum burst size (MoveNext @ 317252: `Mathf.Max(1, floor)`). Even a
    /// near-zero charge fires at least one bullet.
    static constexpr int kMinBulletCount = 1;

    /// Threshold separating the "weak" and "strong" burst shapes in the owner's
    /// alt-spawn branch (MoveNext @ 317314: `0.6 < charge`). The owner uses this
    /// flag to pick which projectile/effect variant to spawn.
    static constexpr float kStrongShotThreshold = 0.6F;

    Gun007() = default;

    /// Seed the deterministic stream (kept untouched; no draw is ever made on any
    /// Gun007 path). Carried purely for owner-side lockstep parity.
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief The charge ratio: aTime / maxTime.
     *
     * FAITHFUL: Gun007__Attack @ game_full.c:317110 --
     *   (float)param_1[0x26] / (float)param_1[0x27]
     * a_time (this[0x26]) is the accrued hold time, max_time (this[0x27]) the
     * full-charge time. The decomp applies NO clamp here: an over-hold yields a
     * ratio > 1, exactly as the division produces (the Min(1,...) clamp only
     * happens later, inside the bullet-count formula). NO RGRandom draw.
     *
     * @param aTime   accrued charge/hold time (this[0x26]).
     * @param maxTime full-charge time (this[0x27]); a non-positive divisor is
     *        impossible in the original (would be a divide-by-zero), so we return
     *        0 to keep the brain total rather than fabricate a ratio.
     */
    static float ChargeRatio(float aTime, float maxTime);

    /**
     * @brief The full-charge gate predicate used by Attack.
     *
     * FAITHFUL: Gun007__Attack @ game_full.c:317119 -- `if (1.0 <= charge)` (only
     * reached when the owner's this[0x21] flag is 0). When true the owner pops the
     * full-charge UICanvas hook (OWNER); we model only the boolean.
     * NOTE: line 317117 is UnityEngine_Object__Destroy (OWNER); the gate itself is
     * at 317119.
     */
    static bool IsFullCharge(float charge);

    /**
     * @brief The burst bullet count for a given charge.
     *
     * FAITHFUL: Gun007_<>c__Iterator0__MoveNext @ game_full.c:317246-317264 --
     *
     * STEP 1 (317248-317252): compute the primary formula and store at iterator+0xc:
     *   fVar3       = Mathf.Min(1.0, charge);
     *   floor       = Mathf.FloorToInt(maxCount * fVar3);
     *   bulletCount = Mathf.Max(1, floor);   // iterator+0xc
     *
     * STEP 2 (317257-317264): unconditional second write that overrides the limit
     * field when owner+0x84 == 0 (i.e. maxCount == 0):
     *   if (*(int *)(*(int *)(param_1 + 0x4c) + 0x84) == 0) {
     *       // ARM FPSCR sign-bit expression: effectively (charge >= 1.0) ? 1 : 0
     *       *(param_1 + 0xc) = (charge < 1.0) ? 0 : 1;
     *   }
     * The override replaces the stored limit: when maxCount == 0 and charge < 1.0,
     * the decomp produces 0 (not 1 as the primary Max(1,...) formula yields).
     * When maxCount == 0 and charge >= 1.0, the limit is set to 1.
     *
     * OWNER NOTE: the condition is tested via the owner pointer (param_1 + 0x4c),
     * which is an owner-side concern. The condition is equivalent to maxCount == 0
     * (owner+0x84 holds the maxCount int field). This brain models the pure scalar
     * version: when maxCount == 0, returns (charge >= 1.0F ? 1 : 0) instead of the
     * primary Max(1,...) result.
     *
     * The charge is capped at 1.0 BEFORE scaling in the primary formula. NO RNG draw.
     *
     * @param maxCount the owner's configured maximum burst size (owner+0x84).
     * @param charge   the charge ratio (may exceed 1; it is clamped in step 1).
     */
    static int BulletCount(int maxCount, float charge);

    /**
     * @brief Charge-scaled muzzle offset along one axis: base + charge * dir.
     *
     * FAITHFUL: Gun007_<>c__Iterator0__MoveNext @ game_full.c:317274 (x: base
     * owner+0x20, dir owner+0x78), 317281 (y: base owner+0x2c, dir owner+0x7c).
     * The decomp computes `base + (int)(charge * dir)` per axis and stores it at
     * iterator+0x10 (x) / +0x14 (y); the larger the charge the further forward the
     * muzzle spawns. We return the pure scalar; the Transform placement is OWNER.
     * NO RGRandom draw.
     *
     * @param base   the un-charged muzzle base coordinate (owner+0x20 / +0x2c).
     * @param dir    the per-axis charge push direction (owner+0x78 / +0x7c).
     * @param charge the charge ratio (uncapped: matches the decomp's raw *(param+8)).
     */
    static float MuzzleOffset(float base, float dir, float charge);

    /**
     * @brief The projectile size for a given charge (start..end lerp).
     *
     * FAITHFUL: Gun007_<>c__Iterator0__MoveNext @ game_full.c:317320 --
     *   size = start(owner+0x90) + (end(owner+0x94) - start(owner+0x90)) * charge
     * stored at iterator+0x20. This is the un-clamped lerp form
     * (Mathf.LerpUnclamped semantics): an over-charge interpolates past `end`.
     * NO RGRandom draw.
     *
     * @param startSize size at zero charge (owner+0x90).
     * @param endSize   size at full charge (owner+0x94).
     * @param charge    the charge ratio (uncapped: matches *(param+8)).
     */
    static float SizeForCharge(float startSize, float endSize, float charge);

    /**
     * @brief The strong-shot flag in the owner's alt-spawn branch.
     *
     * FAITHFUL: Gun007_<>c__Iterator0__MoveNext @ game_full.c:317314 --
     * when the owner's alt-spawn field (owner+0x84 read as an int != 0) selects
     * this branch, the iterator's flag (iterator+0x1c) is set to `0.6 < charge`.
     * The OTHER branch (owner+0x84 == 0, 317305-317310) instead copies an owner
     * bool field (owner+0x38) and is OWNER-driven, so it is not modeled here. We
     * model only the recoverable `0.6 < charge` predicate. NO RGRandom draw.
     */
    static bool IsStrongShot(float charge);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{}; ///< deterministic stream; never advanced on any path.
};

/**
 * @class Gun007BurstIterator
 * @brief Recoverable counter/limit/state machine of the Gun007_<>c__Iterator0
 *        charge-cannon burst coroutine.
 *
 * FAITHFUL: Gun007_<>c__Iterator0__MoveNext @ game_full.c:317189. Each call
 * reads state (iterator+0x58), immediately writes -1, and remaps it:
 *   `iVar2 = (state < 3) ? state + 3 : 0;`  -- so state 0 -> 3, 1 -> 4, 2 -> 5.
 * Dispatch:
 *   - mapped 3 (state 0, SETUP): compute bulletCount/muzzle/size/flag, then yield
 *     (the decomp falls through to get_transform + WaitForSeconds, OWNER). Stages
 *     the burst branch (state 1) and stays alive.
 *   - mapped 4 (state 1, BURST TICK): counter(iterator+0x28)++; if counter < limit
 *     (iterator+0xc) the owner spawns one bullet (get_transform on the muzzle
 *     prefab) and the coroutine yields again (WaitForSeconds); otherwise it
 *     cleans up (clears owner+0x74/+0x98) and yields once more. Either way it
 *     stays alive and re-stages the burst branch.
 *   - mapped 5 (state 2, END): RGWeapon.TurnActivate(owner) (OWNER), then ends.
 *   - any other (incl. -1 done) -> ends.
 *
 * NOTE the decomp's MoveNext `return 0` on every yield path: Ghidra surfaces this
 * iterator's MoveNext as returning 0, but the staged state writes (and the
 * resumed continuation) drive the IEnumerator lifecycle. We model the observable
 * lifecycle: which step is setup vs. a fired tick vs. the end, and how many
 * bullets fire. The state transitions across yields (3 -> 4 -> ... -> 5) are the
 * coroutine's own resume sequence; the burst branch re-enters at state 1 until
 * the counter reaches the limit, after which the owner's continuation advances to
 * the TurnActivate end (state 2). The actual WaitForSeconds, transforms, bullet
 * spawn and TurnActivate are OWNER concerns.
 */
class Gun007BurstIterator {
public:
    /// state == 0: the SETUP branch (remapped to 3: count/muzzle/size/flag).
    static constexpr int kSetup = 0;
    /// state == 1: the BURST-TICK branch (remapped to 4: counter++/fire-or-clean).
    static constexpr int kBurst = 1;
    /// state == 2: the END branch (remapped to 5: RGWeapon.TurnActivate).
    static constexpr int kEnd = 2;
    /// terminal value MoveNext writes back to iterator+0x58 each call (0xffffffff).
    static constexpr int kDone = -1;

    /**
     * @param limit the burst size (iterator+0xc), i.e. Gun007::BulletCount. The
     *        SETUP branch is what writes this in the real coroutine; for a
     *        unit-testable machine we accept it up front. The burst branch only
     *        ever reads it.
     */
    explicit Gun007BurstIterator(int limit) : m_Limit(limit) {}

    int State() const { return m_State; }
    int Counter() const { return m_Counter; }
    int Limit() const { return m_Limit; }

    /**
     * @brief True iff the most recent MoveNext landed on a burst tick that
     *        spawned a bullet (state 1 AND the incremented counter < limit).
     *
     * FAITHFUL: MoveNext @ 317208-317216 -- `iVar2 = *(param+0x28) + 1;
     * *(param+0x28) = iVar2; if (iVar2 < *(param+0xc)) get_transform(prefab)`.
     * The bullet spawn (get_transform on the muzzle prefab) only happens on this
     * predicate; the counter==limit pump runs the cleanup instead.
     */
    bool Fired() const { return m_Fired; }

    /**
     * @brief Advance the coroutine one step (one MoveNext call).
     *
     * FAITHFUL: MoveNext @ 317189 dispatch + 317205-317234 burst logic.
     *   - from kSetup: stage the count/muzzle/size/flag, yield (no bullet), stage
     *     kBurst, stay alive.
     *   - from kBurst: counter++; if counter < limit -> spawn one bullet
     *     (Fired()), yield, stay kBurst (more bullets); else -> cleanup (clear
     *     owner+0x74/+0x98, OWNER), yield once more, advance to kEnd.
     *   - from kEnd: RGWeapon.TurnActivate (OWNER), end (kDone).
     *   - from kDone / any other: end (kDone).
     * @return true while the coroutine is still alive (yielded), false when it
     *         has ended -- mirroring the IEnumerator.MoveNext contract.
     */
    bool MoveNext();

private:
    int m_State = kSetup;
    int m_Counter = 0; ///< burst counter (iterator+0x28; decomp *(param+0x28)).
    int m_Limit = 0;   ///< burst size / limit (iterator+0xc; decomp *(param+0xc)).
    bool m_Fired = false;
};

} // namespace Game

#endif /* GAME_GUN007_HPP */
