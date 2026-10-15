#ifndef GAME_SIM_WORLDCOLLISION_HPP
#define GAME_SIM_WORLDCOLLISION_HPP

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "sim/Depenetration.hpp"

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

    /// SWEPT wall query (PORT CHOICE (T-57), sampled not exact): the first point of the straight
    /// path @p from -> @p to at which a circle of @p radius is blocked. Returns the path fraction
    /// t in (0,1] of that first contact (t = 0 is the step start, t = 1 the step end -- the SAME
    /// scale the target sweep uses, so the caller can order a wall contact against a target hit),
    /// or a negative value when the whole path is free. The START point is assumed free (it was
    /// the previous step's end, already tested) and is never sampled: a path that starts blocked
    /// is reported as stopped within its first sample gap (about 1/256 of it). The END (t = 1) is
    /// always tested, so a path that is blocked only at its end behaves exactly like the old
    /// discrete Blocks(to, radius) test.
    ///
    /// Default implementation: sample Blocks() along the path every <= max(radius, 1) px (so for
    /// radius >= 1 the sampled discs overlap and sweep a gap-free capsule except for ~0.134 *
    /// radius deep scallops between samples), then bisect the first blocked interval to the
    /// contact point (the returned fraction is the blocked side of the final bracket, i.e. at most
    /// 1/256 of a sample gap AFTER the true contact). A fast bullet therefore cannot tunnel through
    /// a 1-cell wall. Worlds with exact geometry may override this.
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

    /// SWEPT MOVE TEST (P2.18, PORT CHOICE T-62): does a circle of @p radius moving along the straight
    /// segment @p from -> @p to touch a blocker anywhere on the way (the END included)? MoveBody
    /// asks this instead of Blocks(to), so a short step can no longer graze a blocker that both end
    /// points miss. Worlds with geometry override it EXACTLY (GameScene: segment vs the Minkowski sum
    /// of every blocker, sim/Depenetration.hpp SegmentBlockedByAny). Default implementation, for
    /// worlds that only answer circle queries: Blocks() at the end point plus samples every
    /// max(radius / 4, 1) px along the segment; it can miss a graze whose depth is up to the
    /// circular sagitta R - sqrt(R^2 - s^2 / 4) (s = the sample spacing, R = body + blocker radius;
    /// roughly s^2 / (8 R) for small s): 0.04 px for the 12.8 px hero against a 16 px circle,
    /// 0.10 px at the smallest R = 12.8. It is an approximation, not a strict bound for other
    /// blocker shapes, and only test doubles use it.
    virtual bool MoveBlocked(glm::vec2 from, glm::vec2 to, float radius) const {
        const glm::vec2 d = to - from;
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        const float spacing = std::max(radius * 0.25F, 1.0F);
        const int n = std::max(1, static_cast<int>(std::ceil(len / spacing)));
        for (int i = 1; i < n; ++i) {
            if (Blocks(from + d * (static_cast<float>(i) / static_cast<float>(n)), radius)) {
                return true;
            }
        }
        return Blocks(to, radius);
    }

    /// DEPENETRATION (P2.18, PORT CHOICE T-61): the position nearest to @p pos at which a body
    /// circle of @p radius no longer overlaps any blocker. Returns @p pos unchanged when it is
    /// already free (the common case: one Blocks() call). INFERRED: Dynamic Rigidbody2Ds (13 heroes,
    /// 134 of 147 enemy/boss prefabs; the rest are Kinematic or have none) are pushed back out of a
    /// static collider by Box2D, so spawn, knockback, teleport or a closing door would not pin them
    /// inside a blocker for good; the exit POSITION here is an approximation. Even a swept move
    /// gate (MoveBlocked) refuses every step of an actor that STARTS overlapping, so without this
    /// such an actor would be stuck.
    ///
    /// Default implementation: the sampled nearest-free search over Blocks() (works for any
    /// world; deterministic; ~0.01 px is only the radial bisection precision along the ray found,
    /// only 32 directions are tried so the point can be a few px farther than the true nearest one).
    /// GameScene overrides it with the exact
    /// circle-vs-AABB / circle minimum-translation solve (sim/Depenetration.hpp).
    virtual glm::vec2 Depenetrate(glm::vec2 pos, float radius) const {
        return SearchNearestFree(pos, kDepenSearchMaxPx,
                                 [&](glm::vec2 p) { return !Blocks(p, radius); });
    }
};

/// A WorldCollision that never blocks (open arena), for headless tests.
class NullWorldCollision : public WorldCollision {
public:
    bool Blocks(glm::vec2 /*pos*/, float /*radius*/) const override { return false; }
    float SweepBlocked(glm::vec2, glm::vec2, float) const override { return -1.0F; } // open arena.
    bool MoveBlocked(glm::vec2, glm::vec2, float) const override { return false; } // open arena.
    glm::vec2 Depenetrate(glm::vec2 pos, float /*radius*/) const override { return pos; }
};

} // namespace Game::Sim

#endif /* GAME_SIM_WORLDCOLLISION_HPP */
