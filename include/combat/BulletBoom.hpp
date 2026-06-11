#ifndef GAME_BULLET_BOOM_HPP
#define GAME_BULLET_BOOM_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class BulletBoom
 * @brief Faithful, engine-free explosion-timing brain for the "BulletBoom" box
 *        (an RGBox subclass: a delayed-detonation hazard that flashes a warning
 *        one second before it explodes).
 *
 * Per-content port. Models ONLY the pure, deterministic timing cadence -- two
 * independent timed events (warning, then detonation 1.0s later) recovered from
 * the three BulletBoom bodies. Every Unity side effect (MonoBehaviour.Invoke
 * scheduling, Animator.SetTrigger, CancelInvoke, Object.Instantiate<RGWeapon>,
 * get_transform) is left to the owning entity and referenced in comments only.
 *
 * FIELD MAP (IL2CPP dump TypeDefIndex 4775, class BulletBoom : RGBox):
 *   damage         int      @ 0x18
 *   boom_time      float    @ 0x1C   <- the detonation delay this brain consumes
 *   boom_at_start  bool     @ 0x20
 *   anim           Animator @ 0x24   <- SoonExplode's SetTrigger target (owner)
 *   source_objcet  GameObject@ 0x28
 *
 * WHAT THE DECOMP SHOWS (no reconstruction of unseen logic):
 *
 *  - StartBoom (BulletBoom__StartBoom @ game_full.c:962698) schedules two
 *    MonoBehaviour.Invoke calls off the single boom_time field (0x1C):
 *        Invoke(<earlier method>, boom_time - 1.0);   // game_full.c:962706
 *        Invoke(<later   method>, boom_time);          // game_full.c:962705
 *    i.e. two timed events with a FIXED 1.0-second lead between them. There is
 *    NO Mathf.Max/clamp on `boom_time - 1.0`: a boom_time below 1.0 yields a
 *    non-positive (immediate) warning delay, faithfully preserved here.
 *
 *  - SoonExplode (BulletBoom__SoonExplode @ game_full.c:962712) is the EARLIER
 *    (warning) event: its whole body is Animator.SetTrigger(anim, ...) -- an
 *    owner write, no recoverable scalar. It is the named target scheduled at
 *    boom_time - 1.0.
 *
 *  - ExplodeStart (BulletBoom__ExplodeStart @ game_full.c:962731) is the LATER
 *    (detonation) event, scheduled at boom_time. Its body is:
 *        CancelInvoke();                       // no-arg: cancels FUTURE pendings
 *        Instantiate<RGWeapon>(boom prefab);   // owner spawn
 *        get_transform(...);                   // owner
 *    The no-arg CancelInvoke cancels only invokes still PENDING in the future; it
 *    does not suppress the warning, which is scheduled 1.0s earlier and has
 *    already fired by the time ExplodeStart runs. The two timers are independent.
 *    The spawn and the transform read are owner concerns.
 *
 * The mapping of the two scheduled methods to SoonExplode (earlier, the
 * warning) / ExplodeStart (later, the detonation) follows the method anchors and
 * the dump's method order; the raw string-literal-to-method binding is not
 * byte-recoverable from the export and is flagged. The TIMING SCALARS -- two
 * scheduled times and the 1.0s lead -- are fully recoverable and are what this
 * unit ports.
 *
 * deltaTime accumulation is modelled as Tick(dt): Arm() seeds the schedule from
 * boom_time, then each Tick(dt) advances an accumulator and fires the warning
 * (SoonExplode) and detonation (ExplodeStart) events when their scheduled times
 * are crossed, mirroring Unity's Invoke timers.
 *
 * Determinism: no BulletBoom body calls rg_random, so this unit makes ZERO RNG
 * draws (the RGRandom member is carried only for interface parity and to let a
 * caller confirm the seed without ever advancing the stream).
 *
 * @see IL2CPP BulletBoom (TypeDefIndex 4775); FAITHFUL: BulletBoom @
 *      game_full.c:962698+.
 */
class BulletBoom {
public:
    /// Fixed lead (seconds) of the warning event before detonation: the decomp's
    /// `boom_time + -1.0` literal in StartBoom @ game_full.c:962706.
    static constexpr float kSoonExplodeLead = 1.0F;

    /// Explosion-timing state machine (mirrors the two scheduled invokes plus
    /// the ExplodeStart CancelInvoke transition).
    enum class State : int {
        IDLE = 0,          ///< not yet armed; StartBoom has not run.
        ARMED = 1,         ///< StartBoom ran; both events scheduled, none fired.
        SOON_EXPLODE = 2,  ///< warning fired (boom_time - 1.0 reached).
        EXPLODE_START = 3, ///< detonation fired (boom_time reached); terminal.
    };

    BulletBoom() = default;

    /// Seed the deterministic stream (kept untouched; no draw is ever made).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // --- StartBoom: the explosion-timing cadence ---------------------------

    /**
     * @brief Scheduled delay of the SoonExplode warning, in seconds.
     *
     * FAITHFUL: BulletBoom__StartBoom @ game_full.c:962706 -- the second Invoke
     * uses delay `boom_time + -1.0`. No clamp: may be non-positive when
     * @p boomTime < 1.0 (an immediate warning), exactly as the decomp.
     * @param boomTime the box's boom_time (0x1C), seconds.
     */
    static float SoonExplodeDelay(float boomTime);

    /**
     * @brief Scheduled delay of the ExplodeStart detonation, in seconds.
     *
     * FAITHFUL: BulletBoom__StartBoom @ game_full.c:962705 -- the first Invoke
     * uses delay `boom_time` unchanged.
     * @param boomTime the box's boom_time (0x1C), seconds.
     */
    static float ExplodeStartDelay(float boomTime);

    // --- Tick-driven schedule (Unity Invoke timers, modelled) --------------

    /**
     * @brief Arm the schedule (run StartBoom): stash boom_time and reset timers.
     *
     * FAITHFUL: BulletBoom__StartBoom @ game_full.c:962698 -- schedules the two
     * Invoke timers off boom_time. The Invoke calls themselves are owner; this
     * records their scheduled delays (SoonExplodeDelay/ExplodeStartDelay) and
     * moves the machine to ARMED.
     * @param boomTime the box's boom_time (0x1C), seconds.
     */
    void Arm(float boomTime);

    /**
     * @brief Advance the Invoke timers by @p dt seconds.
     *
     * Mirrors Unity's Invoke firing: the warning (SoonExplodeDelay) and the
     * detonation (ExplodeStartDelay) are two INDEPENDENT absolute-time invokes,
     * scheduled 1.0s apart. When elapsed crosses SoonExplodeDelay the warning
     * fires (state -> SOON_EXPLODE, SoonExplodeFired() true); when it crosses
     * ExplodeStartDelay the detonation fires (state -> EXPLODE_START). The no-arg
     * CancelInvoke @ game_full.c:962740 cancels only FUTURE pending invokes, so it
     * never suppresses the earlier warning. A single large @p dt that crosses both
     * thresholds fires the warning AND the detonation -- the warning is not
     * cancelled, matching the engine (firing order is owner/engine-side and not
     * recoverable; only the two delays and the 1.0s lead are faithful).
     * No-op while IDLE or once EXPLODE_START is reached. Takes NO RNG draw.
     * @param dt elapsed seconds since the previous Tick.
     */
    void Tick(float dt);

    /// Live timing state.
    State CurrentState() const { return m_State; }
    /// Seconds elapsed since Arm() (the modelled Invoke clock).
    float Elapsed() const { return m_Elapsed; }
    /// True once the warning (SoonExplode) event has fired (independent timer).
    bool SoonExplodeFired() const { return m_SoonExplodeFired; }
    /// True once the detonation (ExplodeStart) event has fired.
    bool ExplodeStarted() const { return m_State == State::EXPLODE_START; }

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};                  ///< deterministic stream; never advanced.
    State m_State = State::IDLE;       ///< explosion-timing state machine.
    float m_BoomTime = 0.0F;           ///< stashed boom_time (0x1C) at Arm().
    float m_Elapsed = 0.0F;            ///< modelled Invoke clock since Arm().
    bool m_SoonExplodeFired = false;   ///< warning fired (and not cancelled).
};

} // namespace Game

#endif /* GAME_BULLET_BOOM_HPP */
