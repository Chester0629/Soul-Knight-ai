#ifndef GAME_SIM_FIXEDCLOCK_HPP
#define GAME_SIM_FIXEDCLOCK_HPP

namespace Game::Sim {

/// Turns a variable real-time dtMs stream into a count of whole fixed steps,
/// carrying the sub-step remainder across frames so cadence stays exact.
class FixedClock {
public:
    /// Accumulate @p dtMs and return how many whole fixed steps elapsed (capped at
    /// kMaxStepsPerAdvance). Non-positive dt contributes nothing.
    int Advance(float dtMs);

    /// The unconsumed sub-step time (ms) carried to the next Advance. Note: if
    /// Advance() hit the runaway cap, the excess is dropped and this returns 0
    /// (not the mathematically exact remainder), so the clock is not left in debt.
    float RemainderMs() const { return m_AccumMs; }

private:
    float m_AccumMs = 0.0F;
};

} // namespace Game::Sim

#endif /* GAME_SIM_FIXEDCLOCK_HPP */
