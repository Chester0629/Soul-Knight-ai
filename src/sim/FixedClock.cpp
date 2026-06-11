#include "sim/FixedClock.hpp"

#include "sim/SimConfig.hpp"

namespace Game::Sim {

int FixedClock::Advance(float dtMs) {
    if (dtMs > 0.0F) {
        m_AccumMs += dtMs;
    }
    int steps = 0;
    while (m_AccumMs >= kFixedStepMs) {
        m_AccumMs -= kFixedStepMs;
        ++steps;
        if (steps >= kMaxStepsPerAdvance) {
            m_AccumMs = 0.0F; // drop the backlog rather than bank step-debt
            break;
        }
    }
    return steps;
}

} // namespace Game::Sim
