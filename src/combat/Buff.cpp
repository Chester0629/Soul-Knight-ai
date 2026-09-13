#include "combat/Buff.hpp"

namespace Game {
namespace Combat {

const char *BuffKindName(BuffKind kind) {
    switch (kind) {
        case BuffKind::Fire: return "Fire";
        case BuffKind::Poison: return "Poison";
        case BuffKind::Ice: return "Ice";
        case BuffKind::Electric: return "Electric";
        case BuffKind::Armor: return "Armor";
        case BuffKind::ChangeSpeed: return "ChangeSpeed";
    }
    return "Unknown";
}

void BuffList::Apply(const BuffParams &params) {
    // INFERRED (T-32): reapplying the same kind refreshes remainingSec (does not
    // stack a second concurrent instance).
    for (ActiveBuff &b : m_Active) {
        if (b.p.kind == params.kind) {
            b.p = params;
            b.remainingSec = params.durationSec;
            b.tickAccum = 0.0F;
            return;
        }
    }
    ActiveBuff fresh;
    fresh.p = params;
    fresh.remainingSec = params.durationSec;
    fresh.tickAccum = 0.0F;
    m_Active.push_back(fresh);
}

BuffList::TickResult BuffList::Tick(float dtSec) {
    TickResult r;
    for (auto it = m_Active.begin(); it != m_Active.end();) {
        ActiveBuff &b = *it;
        b.remainingSec -= dtSec;

        // Fire/Poison: accumulate and emit tickDamage once per full tickSec elapsed.
        // A small epsilon absorbs float accumulation error from repeated dtSec adds
        // (kFixedStepSeconds = 0.02F) so a boundary tick is not dropped by a hair.
        if (b.p.tickSec > 0.0F) {
            b.tickAccum += dtSec;
            constexpr float kTickEpsilon = 1e-4F;
            while (b.tickAccum + kTickEpsilon >= b.p.tickSec) {
                r.damage += b.p.tickDamage;
                b.tickAccum -= b.p.tickSec;
            }
        }

        if (b.p.armorDelta != 0) {
            r.armorDelta += b.p.armorDelta;
        }
        r.speedMul *= b.p.speedMul;

        // Epsilon absorbs float drift from repeated dtSec adds (kFixedStepSeconds =
        // 0.02F is not exactly representable), so an exact-multiple duration (e.g.
        // 2.0s / 0.02s = 100 steps) ends on step 100, not 101.
        constexpr float kEndEpsilon = 1e-4F;
        if (b.remainingSec <= kEndEpsilon) {
            r.ended.push_back(b.p.kind);
            it = m_Active.erase(it);
        } else {
            ++it;
        }
    }
    r.frozen = (r.speedMul <= 0.0F);
    return r;
}

bool BuffList::Has(BuffKind kind) const {
    for (const ActiveBuff &b : m_Active) {
        if (b.p.kind == kind) {
            return true;
        }
    }
    return false;
}

void BuffList::Clear() { m_Active.clear(); }

} // namespace Combat
} // namespace Game
