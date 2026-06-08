#ifndef GAME_SIM_SCHEDULER_HPP
#define GAME_SIM_SCHEDULER_HPP

#include <cstdint>
#include <functional>
#include <vector>

namespace Game::Sim {

/// Deterministic tick-based emulation of Unity Invoke / InvokeRepeating /
/// coroutine cadence. One Tick() == one fixed step; callbacks fire in insertion
/// order within a tick. No wall-clock time, no RNG -- fully replay-safe.
class Scheduler {
public:
    using Handle = std::uint32_t; ///< 0 == invalid.
    using Callback = std::function<void()>;

    /// Convert a real-second delay to a whole tick count (round-half-up).
    static int SecondsToTicks(float seconds);

    /// Fire @p cb once, @p delayTicks ticks from now. Zero or negative is clamped
    /// to 1 (fires next tick) -- it never fires on the current tick.
    Handle Invoke(int delayTicks, Callback cb);

    /// Fire @p cb after @p firstDelayTicks, then every @p intervalTicks. Both are
    /// clamped to >= 1, so a repeating cadence always advances by at least a tick.
    Handle InvokeRepeating(int firstDelayTicks, int intervalTicks, Callback cb);

    /// Suppress a pending callback. Safe to call on an already-fired, unknown, or
    /// zero handle, and safe to call from inside a firing callback.
    void Cancel(Handle handle);

    /// Advance one fixed step, firing all callbacks due at or before the new tick.
    void Tick();

    /// The current tick index (incremented by Tick()).
    int CurrentTick() const { return m_Tick; }

private:
    struct Entry {
        Handle id = 0;
        int dueTick = 0;
        int intervalTicks = 0;
        Callback cb;
        bool repeating = false;
        bool cancelled = false;
    };

    std::vector<Entry> m_Entries;
    int m_Tick = 0;
    // Wraps at UINT32_MAX back to 0 (the invalid sentinel) then 1; a collision
    // would need ~4 billion schedule calls in one session, so it is theoretical.
    Handle m_NextId = 1;
};

} // namespace Game::Sim

#endif /* GAME_SIM_SCHEDULER_HPP */
