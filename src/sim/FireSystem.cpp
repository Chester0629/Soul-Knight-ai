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
    // Single -- and Burst/Charge/Parabola, which are reserved for later cycles and
    // currently emit one bullet along dir.
    // TODO(engine-port Plan 2+): implement Burst / Charge / Parabola spawn shapes.
    out.push_back(MakeBullet(intent, intent.dir, nextId));
}

} // namespace Game::Sim
