#include "world/SpikeTrap.hpp"

#include <algorithm>

namespace Game {

SpikeTrap::SpikeTrap(const StingDef &def) : m_Def(def) {}

void SpikeTrap::Invoke(Kind kind, double delayMs) {
    // Unity: Invoke(name, delay) is due at Time.time + delay; the due list is ordered by due time.
    m_Timers.push_back(Timer{kind, m_NowMs + delayMs, m_Seq++});
}

void SpikeTrap::OnRoomStarted() {
    m_Started = true;
    Invoke(Kind::ShowSting, static_cast<double>(m_Def.offsetTime) * 1000.0);
}

// FAITHFUL: ObjectSting$$ShowSting / HideSting / TurnCanHit (see the class comment for the addresses).
void SpikeTrap::Fire(Kind kind) {
    switch (kind) {
    case Kind::ShowSting:
        m_ShowSting = true;
        m_CanHit = true;                     // TurnCanHit()
        Invoke(Kind::TurnCanHit, kTickMs);   //   ... which re-invokes itself every 0.3 s
        Invoke(Kind::HideSting, static_cast<double>(m_Def.showTime) * 1000.0);
        break;
    case Kind::HideSting:
        m_ShowSting = false;
        m_Timers.clear();                    // CancelInvoke(): every pending Invoke of this component
        Invoke(Kind::ShowSting, static_cast<double>(m_Def.hideTime) * 1000.0);
        break;
    case Kind::TurnCanHit:
        m_CanHit = true;
        Invoke(Kind::TurnCanHit, kTickMs);
        break;
    }
}

bool SpikeTrap::Advance(double dtMs) {
    m_NowMs += dtMs;
    // The timers due at this frame, in due-time order (ties: scheduling order). Timers scheduled while
    // firing (and not yet due when the pass started) wait for the next frame; a timer cancelled by an
    // earlier one of this pass (HideSting's CancelInvoke) must not fire.
    std::vector<Timer> due;
    for (const Timer &t : m_Timers) {
        if (t.dueMs <= m_NowMs) {
            due.push_back(t);
        }
    }
    std::sort(due.begin(), due.end(), [](const Timer &a, const Timer &b) {
        return a.dueMs != b.dueMs ? a.dueMs < b.dueMs : a.seq < b.seq;
    });
    for (const Timer &t : due) {
        const auto it = std::find_if(m_Timers.begin(), m_Timers.end(),
                                     [&t](const Timer &o) { return o.seq == t.seq; });
        if (it == m_Timers.end()) {
            continue; // cancelled earlier in this pass
        }
        m_Timers.erase(it);
        Fire(t.kind);
    }
    // Update(): if (show_sting && can_hit) { can_hit = false; <hit test> }
    if (m_ShowSting && m_CanHit) {
        m_CanHit = false;
        return true;
    }
    return false;
}

} // namespace Game
