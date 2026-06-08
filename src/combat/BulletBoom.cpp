#include "combat/BulletBoom.hpp"

namespace Game {

// FAITHFUL: BulletBoom__StartBoom @ game_full.c:962706. The warning Invoke is
// scheduled at `boom_time + -1.0` -- a raw subtraction with NO Mathf.Max/clamp,
// so a boom_time below the 1.0s lead yields a non-positive (immediate) delay.
float BulletBoom::SoonExplodeDelay(float boomTime) {
    return boomTime - kSoonExplodeLead;
}

// FAITHFUL: BulletBoom__StartBoom @ game_full.c:962705. The detonation Invoke is
// scheduled at `boom_time` unchanged.
float BulletBoom::ExplodeStartDelay(float boomTime) {
    return boomTime;
}

// FAITHFUL: BulletBoom__StartBoom @ game_full.c:962698. Schedules the two Invoke
// timers off boom_time (0x1C) and arms the machine. The MonoBehaviour.Invoke
// calls themselves are owner; here we record their scheduled delays and reset
// the clock. NO RGRandom draw.
void BulletBoom::Arm(float boomTime) {
    m_BoomTime = boomTime;
    m_Elapsed = 0.0F;
    m_SoonExplodeFired = false;
    m_State = State::ARMED;
}

// FAITHFUL: explosion-timing cadence. Advances the modelled Invoke clock and
// fires the two INDEPENDENT scheduled events:
//   - SoonExplode (warning) at SoonExplodeDelay(boom_time)   -- game_full.c:962706
//   - ExplodeStart (detonation) at ExplodeStartDelay(boom_time) -- game_full.c:962705
// These are two separate absolute-time MonoBehaviour.Invoke callbacks; the
// warning is scheduled strictly 1.0s earlier than the detonation. The no-arg
// CancelInvoke() in BulletBoom__ExplodeStart @ game_full.c:962740 cancels only
// FUTURE pending invokes -- it does NOT and cannot suppress the warning, which
// (being scheduled earlier) has already fired by the time detonation runs.
// Firing order, and any same-frame handling of two due timers, is engine-owned
// and not recoverable from the decomp; the only faithful facts are the two
// scalar delays and the 1.0s lead. We therefore fire the warning whenever its
// threshold is crossed, independently of the detonation, even within one dt.
// The Animator.SetTrigger (SoonExplode body) and Instantiate/get_transform
// (ExplodeStart body) are owner side effects, not modelled here. NO RGRandom draw.
void BulletBoom::Tick(float dt) {
    if (m_State == State::IDLE || m_State == State::EXPLODE_START) {
        return; // not armed, or detonation already reached (terminal).
    }

    m_Elapsed += dt;

    // Fire the warning once its (earlier) scheduled delay is crossed. This is an
    // independent timer: it is never suppressed by the detonation.
    if (!m_SoonExplodeFired && m_Elapsed >= SoonExplodeDelay(m_BoomTime)) {
        m_SoonExplodeFired = true;
        m_State = State::SOON_EXPLODE;
    }

    // Fire the detonation once its (later) scheduled delay is crossed.
    if (m_Elapsed >= ExplodeStartDelay(m_BoomTime)) {
        m_State = State::EXPLODE_START;
    }
}

} // namespace Game
