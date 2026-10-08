#ifndef GAME_SIM_WORLDCOLLISION_HPP
#define GAME_SIM_WORLDCOLLISION_HPP

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace Game::Sim {

/// Wall-blocking query the Simulation calls before committing a move. GameScene
/// implements it over the room AABBs + sealed doors; tests use NullWorldCollision.
class WorldCollision {
public:
    virtual ~WorldCollision() = default;
    virtual bool Blocks(glm::vec2 pos, float radius) const = 0;

    /// Notify the world that something (a bullet) struck a blocker at @p pos --
    /// lets a destructible design-room box (Phase 3) take damage and, once broken,
    /// stop blocking. Default no-op: only the live GameScene world reacts; tests
    /// (NullWorldCollision) never block, so this is never reached in combat goldens
    /// -- the deterministic combat hash is unaffected. Non-const (mutates boxes).
    virtual void DamageObstacle(glm::vec2 /*pos*/, float /*radius*/) {}

    /// SWEPT wall query (PORT CHOICE (T-57), sampled not exact): the first point of the straight path @p from -> @p to at
    /// which a circle of @p radius is blocked. Returns the path fraction t in (0,1] of that
    /// first contact, or a negative value when the whole path is free. The START point is
    /// assumed free (it was the previous step's end, already tested); the END (t = 1) is
    /// always tested, so a path that is blocked only at its end behaves exactly like the
    /// old discrete Blocks(to, radius) test.
    ///
    /// Default implementation: sample Blocks() along the path every <= radius px (so the
    /// sampled discs overlap and sweep a gap-free capsule except for ~0.134 * radius deep
    /// scallops between samples), then bisect the first blocked interval to the contact
    /// point. A fast bullet therefore cannot tunnel through a 1-cell wall. Worlds with exact
    /// geometry may override this.
    virtual float SweepBlocked(glm::vec2 from, glm::vec2 to, float radius) const {
        const glm::vec2 d = to - from;
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        const float spacing = std::max(radius, 1.0F);
        const int n = std::max(1, static_cast<int>(std::ceil(len / spacing)));
        for (int i = 1; i <= n; ++i) {
            const float ti = (i == n) ? 1.0F : static_cast<float>(i) / static_cast<float>(n);
            if (!Blocks(from + d * ti, radius)) {
                continue;
            }
            float lo = static_cast<float>(i - 1) / static_cast<float>(n); // last free sample
            float hi = ti;                                                 // first blocked sample
            for (int k = 0; k < kSweepBisectSteps; ++k) {
                const float mid = 0.5F * (lo + hi);
                if (Blocks(from + d * mid, radius)) {
                    hi = mid;
                } else {
                    lo = mid;
                }
            }
            return hi;
        }
        return -1.0F;
    }

    static constexpr int kSweepBisectSteps = 8; ///< contact refinement: 1/256 of a sample gap.
};

/// A WorldCollision that never blocks (open arena), for headless tests.
class NullWorldCollision : public WorldCollision {
public:
    bool Blocks(glm::vec2 /*pos*/, float /*radius*/) const override { return false; }
    float SweepBlocked(glm::vec2, glm::vec2, float) const override { return -1.0F; } // open arena.
};

} // namespace Game::Sim

#endif /* GAME_SIM_WORLDCOLLISION_HPP */
