#ifndef GAME_GUN008_HPP
#define GAME_GUN008_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class Gun008
 * @brief Faithful laser-sweep state/scatter math for the "Gun008" weapon
 *        (Soul Knight 1.7.10).
 *
 * Per-content port. Gun008 fires a burst ("sweep") of bullets through a
 * coroutine (the nested Gun008_<>c__Iterator0): on the first pump it yields a
 * WaitForSeconds, then on each subsequent pump it advances a counter and, while
 * the counter is below a limit, fires one sweep tick (effect + bullet) and
 * yields again. This brain models ONLY the recoverable scalar/state math that
 * feeds those side-effects; the WaitForSeconds, RGMusicManager.PlayEffect, the
 * PrefabPool bullet Instantiate, the transform/scale component walk, and the
 * Singleton<PrefabPool>.Inst spawn are all OWNER concerns.
 *
 * Recoverable here:
 *   - the ctor's two configured weapon fields (Gun008.ctor @ 317442): an int at
 *     param+0x6c (= 3) and a float at param+0x70 (= 0.1f). Their exact runtime
 *     roles (sweep-tick count / inter-tick delay) are inferred, so the VALUES
 *     are exposed as named constants and the role is documented, not asserted.
 *   - the coroutine counter/limit/state machine (MoveNext @ 317492): state at
 *     iterator+0x2c is read then set to -1 each call; state 0 -> first yield
 *     (WaitForSeconds); state 1 -> resume: counter (iterator+0xc)++; if
 *     counter < limit (iterator+0x10) fire one tick + re-yield, else end.
 *   - the per-tick scatter math (MoveNext @ 317560-317569): the sweep half-angle
 *     = base + base*scale = base*(1+scale), where base is the owner's spread
 *     field (owner+0x30) and scale is a transform/scale component value
 *     (component+0x20, an OWNER-supplied float); then ONE RGRandom float draw
 *     Range(-half, +half) (owner+0x60 stream) gives the bullet's scatter angle.
 *
 * @see recreation Enemy/RGEController.cs (field-offset reference);
 *      FAITHFUL: Gun008 @ game_full.c:317442-317640.
 */
class Gun008 {
public:
    /// Configured int field written by the ctor at param+0x6c (= 3). Inferred to
    /// be the sweep tick count (the iterator `limit` at +0x10); the ctor only
    /// proves the stored value, so the role is documented, not relied upon.
    /// FAITHFUL: Gun008.ctor @ game_full.c:317449 (*(param+0x6c) = 3).
    static constexpr int kCtorFieldI6c = 3;

    /// Configured float field written by the ctor at param+0x70 (= 0.1f, the
    /// bit pattern 0x3dcccccd). Inferred to be the inter-tick / windup delay fed
    /// to WaitForSeconds (owner). FAITHFUL: Gun008.ctor @ game_full.c:317450
    /// (*(param+0x70) = 0x3dcccccd).
    static constexpr float kCtorFieldF70 = 0.1F;

    Gun008() = default;

    /// Seed the weapon's deterministic stream (owner+0x60). One float is drawn
    /// per fired sweep tick (see SweepScatterAngle); keep it in lockstep.
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief Sweep half-angle (the magnitude fed to the scatter draw) for one
     *        tick: base*(1 + scale).
     *
     * FAITHFUL: Gun008_<>c__Iterator0__MoveNext @ game_full.c:317560-317566.
     * `base` is the owner's spread field (owner+0x30, decomp uVar4/uVar5);
     * `scale` is a transform/scale component float (component+0x20, decomp
     * *(iVar1+0x20)) supplied by the owner. The decomp computes
     * fVar7 = base + base*scale, stored at iterator+0x14. NO RGRandom draw here.
     * @return the half-angle magnitude (degrees) used as +/- bound below.
     */
    static float SweepHalfAngle(float base, float scale);

    /**
     * @brief One fired sweep tick's scatter angle: Range(-half, +half).
     *
     * FAITHFUL: MoveNext @ game_full.c:317569
     * (RGRandom__Range(owner+0x60, -fVar7, fVar7)), stored at iterator+0x18.
     * Draws EXACTLY ONE float from this weapon's stream (max-INCLUSIVE float
     * Range), matching the decomp's single draw per fired tick. `half` is the
     * value from SweepHalfAngle.
     * @return the bullet's deterministic scatter angle within [-half, +half].
     */
    float SweepScatterAngle(float half);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};
};

/**
 * @class Gun008SweepIterator
 * @brief Recoverable counter/limit/state machine of the Gun008_<>c__Iterator0
 *        laser-sweep coroutine.
 *
 * FAITHFUL: Gun008_<>c__Iterator0__MoveNext @ game_full.c:317492. Each call
 * reads state (iterator+0x2c), immediately writes -1, and dispatches:
 *   - state 0  -> the first yield: WaitForSeconds (OWNER); keeps the coroutine
 *                 alive (returns true) and stages state 1 for the next pump.
 *   - state 1  -> resume: counter (iterator+0xc)++; if counter < limit
 *                 (iterator+0x10) the owner fires one tick (PlayEffect +
 *                 scatter draw + bullet spawn) and the coroutine yields again
 *                 (returns true, restages state 1); otherwise it ends
 *                 (returns false).
 *   - any other (incl. -1 done) -> ends (returns false).
 *
 * The decomp's MoveNext sets state to -1 at entry and, on the fire branch,
 * writes -1 again at +0x2c (317574) before the yield return; the iterator's
 * resumed continuation is what re-enters at state 1. We model the observable
 * lifecycle (which step yields vs. ends, and whether THIS pump fires a tick).
 * The actual WaitForSeconds object, PlayEffect, the scatter draw and the bullet
 * Instantiate are OWNER concerns; the owner calls Gun008::SweepScatterAngle on
 * each Fired() pump to keep the RNG stream in lockstep.
 */
class Gun008SweepIterator {
public:
    /// state == 0: the not-yet-started branch (iVar2==0 -> first WaitForSeconds).
    static constexpr int kStart = 0;
    /// state == 1: the resume branch (iVar2==1 -> counter++/fire-or-end).
    static constexpr int kResume = 1;
    /// state == -1: terminal value MoveNext writes back (0xffffffff).
    static constexpr int kDone = -1;

    /**
     * @param limit the iterator's fire limit (iterator+0x10). The owner copies
     *        this from the weapon's configured tick count (see
     *        Gun008::kCtorFieldI6c); the iterator body only ever reads it.
     */
    explicit Gun008SweepIterator(int limit) : m_Limit(limit) {}

    int State() const { return m_State; }
    int Counter() const { return m_Counter; }
    int Limit() const { return m_Limit; }

    /**
     * @brief True iff the most recent MoveNext landed on the fire branch
     *        (state 1 AND counter < limit). On a Fired() pump the owner emits
     *        one sweep tick and must draw one scatter angle (lockstep).
     */
    bool Fired() const { return m_Fired; }

    /**
     * @brief Advance the coroutine one step (one MoveNext call).
     *
     * FAITHFUL: MoveNext @ 317492 dispatch + 317542-317576 fire/end logic.
     *   - from kStart: yields the WaitForSeconds (no fire), stages kResume.
     *   - from kResume: counter++; if counter < limit -> fire + re-yield (stays
     *     kResume); else -> end (kDone).
     *   - from kDone / any other: end (kDone).
     * @return true while the coroutine is still alive (yielded), false when it
     *         has ended -- mirroring MoveNext's IEnumerator.MoveNext contract.
     */
    bool MoveNext();

private:
    int m_State = kStart;
    int m_Counter = 0; ///< iterator+0xc (decomp *(param+0xc)).
    int m_Limit = 0;   ///< iterator+0x10 (decomp *(param+0x10)).
    bool m_Fired = false;
};

} // namespace Game

#endif /* GAME_GUN008_HPP */
