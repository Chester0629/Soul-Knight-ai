#include "sim/FireSystem.hpp"

#include <cmath>

#include "sim/SimMath.hpp"

namespace Game::Sim {
namespace {

BulletState MakeBullet(const FireIntent &in, glm::vec2 dir, std::uint32_t &nextId) {
    BulletState b;
    b.id = nextId++;
    b.pos = in.origin;
    b.vel = dir * in.speedPxPerSec;
    b.lifeMs = in.lifeMs;
    b.damage = in.damage;
    b.camp = in.camp;
    b.repel = in.repel;
    b.critical = in.critical;
    b.canThrough = in.canThrough;
    b.pierce = in.pierce;
    b.active = true;
    return b;
}

} // namespace

void FireSystem::Expand(const FireIntent &intent, std::uint32_t &nextId,
                        std::vector<BulletState> &out) const {
    if (intent.pattern == FirePattern::Fan) {
        if (intent.count <= 0) {
            return; // a zero-or-negative-count fan emits nothing (no misfire).
        }
        if (intent.count >= 2) {
            // Evenly spread `count` bullets across [-spread/2, +spread/2] about
            // dir. spreadDeg == 0 is valid: step and start both collapse to 0, so
            // all bullets fire along dir (a stacked volley).
            const float step =
                intent.spreadDeg / static_cast<float>(intent.count - 1);
            const float start = -intent.spreadDeg * 0.5F;
            for (int i = 0; i < intent.count; ++i) {
                const float ang = start + step * static_cast<float>(i);
                out.push_back(
                    MakeBullet(intent, RotateDeg(intent.dir, ang), nextId));
            }
            return;
        }
        // count == 1: a one-bullet fan is just a shot along dir.
        out.push_back(MakeBullet(intent, intent.dir, nextId));
        return;
    }
    if (intent.pattern == FirePattern::Charge) {
        // B1-P3 Charge primitive: a single charge-release bullet whose speed is the
        // base speed scaled by the accrued chargeRatio. Pure stateless geometry --
        // NO RNG draw (the charge-gun brain, e.g. Gun005, takes zero draws; the
        // ratio is baked into the intent by the driver). The charge ACCUMULATION
        // and release detection live in the driver/adapter, not here. (Charge-scaled
        // bullet COUNT/SIZE -- Gun007 -- is a later 3.2 extension; Charge fires one
        // bullet here.) Burst is NOT a FireSystem branch: a burst is the driver
        // emitting one Single sub-shot per tick across ticks (decision B), so it
        // renders through the Single path below -- a same-tick Expand burst would be
        // unfaithful to the guns' time-spaced salvo.
        BulletState b = MakeBullet(intent, intent.dir, nextId);
        b.vel = intent.dir * (intent.speedPxPerSec * intent.chargeRatio);
        out.push_back(b);
        return;
    }
    // Single -- and Burst/Parabola, which currently emit one bullet along dir.
    // (Burst is driver-multi-tick: each sub-shot arrives here as a Single. Parabola
    // is deferred -- 0 guns need it.)
    out.push_back(MakeBullet(intent, intent.dir, nextId));
}

} // namespace Game::Sim
