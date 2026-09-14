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

void BuffList::Apply(const BuffParams &params, bool fromPlayer) {
    // INFERRED (T-32): reapplying the same kind refreshes remainingSec (does not
    // stack a second concurrent instance).
    for (ActiveBuff &b : m_Active) {
        if (b.p.kind == params.kind) {
            b.p = params;
            b.remainingSec = params.durationSec;
            b.tickAccum = 0.0F;
            b.fromPlayer = fromPlayer; // item 5: a reapply's source replaces the old one.
            return;
        }
    }
    ActiveBuff fresh;
    fresh.p = params;
    fresh.remainingSec = params.durationSec;
    fresh.tickAccum = 0.0F;
    fresh.fromPlayer = fromPlayer;
    m_Active.push_back(fresh);
}

BuffList::TickResult BuffList::Tick(float dtSec) {
    TickResult r;
    for (auto it = m_Active.begin(); it != m_Active.end();) {
        ActiveBuff &b = *it;
        // Only the slice of dtSec that still falls INSIDE the buff's lifetime may be
        // credited to tickAccum. Without this clamp a buff whose durationSec is not an
        // exact multiple of the fixed step (e.g. 0.99s at 0.02s steps) would still
        // accumulate a full 1.00s of tick time on the step that ends it, emitting a
        // tick that logically happens after the buff was already over. With the clamp
        // the invariant holds exactly: ticks == floor(durationSec / tickSec).
        const float remainingBefore = b.remainingSec;
        const float effectiveDt = (remainingBefore > 0.0F)
                                      ? ((dtSec < remainingBefore) ? dtSec : remainingBefore)
                                      : 0.0F;
        b.remainingSec -= dtSec;

        // Fire/Poison: accumulate and emit tickDamage once per full tickSec elapsed.
        // A small epsilon absorbs float accumulation error from repeated dtSec adds
        // (kFixedStepSeconds = 0.02F) so a boundary tick is not dropped by a hair.
        if (b.p.tickSec > 0.0F) {
            b.tickAccum += effectiveDt;
            constexpr float kTickEpsilon = 1e-4F;
            while (b.tickAccum + kTickEpsilon >= b.p.tickSec) {
                r.damage += b.p.tickDamage;
                b.tickAccum -= b.p.tickSec;
                // item 2 (R19): split into the per-source totals too, so a caller
                // that needs kill-attribution can apply the two portions as SEPARATE
                // damage events (see TickResult::damageFromPlayer/damageFromOther).
                if (b.p.tickDamage > 0) {
                    if (b.fromPlayer) {
                        r.damageFromPlayer += b.p.tickDamage;
                    } else {
                        r.damageFromOther += b.p.tickDamage;
                    }
                }
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
