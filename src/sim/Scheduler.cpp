#include "sim/Scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "sim/SimConfig.hpp"

namespace Game::Sim {

int Scheduler::SecondsToTicks(float seconds) {
    if (seconds <= 0.0F) {
        return 0;
    }
    return static_cast<int>(std::lround(seconds / kFixedStepSeconds));
}

Scheduler::Handle Scheduler::Invoke(int delayTicks, Callback cb) {
    const int delay = delayTicks < 1 ? 1 : delayTicks;
    Entry e;
    e.id = m_NextId++;
    e.dueTick = m_Tick + delay;
    e.cb = std::move(cb);
    e.repeating = false;
    m_Entries.push_back(std::move(e));
    return m_Entries.back().id;
}

Scheduler::Handle Scheduler::InvokeRepeating(int firstDelayTicks, int intervalTicks,
                                             Callback cb) {
    const int first = firstDelayTicks < 1 ? 1 : firstDelayTicks;
    const int interval = intervalTicks < 1 ? 1 : intervalTicks;
    Entry e;
    e.id = m_NextId++;
    e.dueTick = m_Tick + first;
    e.intervalTicks = interval;
    e.cb = std::move(cb);
    e.repeating = true;
    m_Entries.push_back(std::move(e));
    return m_Entries.back().id;
}

void Scheduler::Cancel(Handle handle) {
    for (Entry &e : m_Entries) {
        if (e.id == handle) {
            e.cancelled = true;
            return;
        }
    }
}

void Scheduler::Tick() {
    ++m_Tick;
    // Index-based loop: a callback may push_back new entries (reallocating the
    // vector); we copy the callback out before invoking and never touch the entry
    // reference afterwards, so a realloc is safe. New entries are appended with a
    // future dueTick, so they will not fire this tick.
    for (std::size_t i = 0; i < m_Entries.size(); ++i) {
        if (m_Entries[i].cancelled || m_Entries[i].dueTick > m_Tick) {
            continue;
        }
        // Copy cb by value first: cb() may push_back into m_Entries (realloc) or
        // Cancel an entry. We update dueTick/cancelled BEFORE the call and make no
        // post-call access to m_Entries[i], so a realloc inside cb() is harmless.
        Callback cb = m_Entries[i].cb;
        if (m_Entries[i].repeating) {
            m_Entries[i].dueTick += m_Entries[i].intervalTicks;
        } else {
            m_Entries[i].cancelled = true;
        }
        cb();
    }
    m_Entries.erase(
        std::remove_if(m_Entries.begin(), m_Entries.end(),
                       [](const Entry &e) { return e.cancelled; }),
        m_Entries.end());
}

} // namespace Game::Sim
