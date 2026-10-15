#ifndef SKTEST_SWEPTORACLE_HPP
#define SKTEST_SWEPTORACLE_HPP

// T-57 (Codex R31): an INDEPENDENT oracle for the swept bullet-vs-hurtbox test.
//
// "Independent" means: nothing in here calls production collision code (no BodyGeom::HurtSweep /
// HurtOverlaps / HurtSweepBeforeWall, no Swept::*, no WorldCollision::SweepBlocked). The only
// production thing read is the DATA of a BodyGeom (hurtbox shape, half extents, radius, offset).
// The oracle is brute force: split one fixed step into kSubSteps equal sub-steps, move the
// bullet centre and the target LINEARLY, and test a plain DISCRETE overlap at every sub-step
// boundary with an overlap function written separately below (a different formulation from the
// production clamp-based one: a "distance outside the box" form for boxes). The first
// overlapping sub-step index j (time j / kSubSteps) is then compared with the production hit.
//
// Resolution: the true first-contact time t* lies in ((j - 1) / kSubSteps, j / kSubSteps]
// (no overlap at j - 1, overlap at j); a contact narrower than one sub-step (a graze) can be
// missed entirely, which the callers handle explicitly.

#include <algorithm>
#include <cmath>
#include <functional>

#include <glm/glm.hpp>

#include "sim/BodyGeom.hpp" // DATA fields only

namespace sktest::oracle {

inline constexpr int kSubSteps = 2000;

struct Vec {
    double x = 0.0;
    double y = 0.0;
};

/// a + t (b - a), in double.
inline Vec Lerp(glm::vec2 a, glm::vec2 b, double t) {
    return {static_cast<double>(a.x) + t * (static_cast<double>(b.x) - static_cast<double>(a.x)),
            static_cast<double>(a.y) + t * (static_cast<double>(b.y) - static_cast<double>(a.y))};
}

/// Discrete overlap (inclusive) of a bullet circle (@p bullet, radius @p r) with the hurtbox
/// of a body standing at @p target. Box: the distance from the circle centre to the box is
/// sqrt(ox^2 + oy^2) with o = how far outside the box the centre is per axis. Circle: centre
/// distance <= hurtRadius + r.
inline bool Touches(const Game::Sim::BodyGeom &g, Vec bullet, double r, Vec target) {
    const double dx = bullet.x - (target.x + static_cast<double>(g.hurtOffsetPx.x));
    const double dy = bullet.y - (target.y + static_cast<double>(g.hurtOffsetPx.y));
    if (g.hurtIsBox) {
        const double ox = std::max(std::fabs(dx) - static_cast<double>(g.hurtHalfPx.x), 0.0);
        const double oy = std::max(std::fabs(dy) - static_cast<double>(g.hurtHalfPx.y), 0.0);
        return ox * ox + oy * oy <= r * r;
    }
    const double reach = static_cast<double>(g.hurtRadiusPx) + r;
    return dx * dx + dy * dy <= reach * reach;
}

/// First sub-step index j in [0, kSubSteps] at which the bullet (c0 -> c1, radius @p r) touches
/// the body's hurtbox while the body moves pos0 -> pos1; -1 if no sub-step does. A conservative
/// bounding-box pre-check (does the bullet's AABB grown by r reach the hurtbox's AABB over the
/// whole step?) skips the 2001 evaluations for the many clear misses.
inline int FirstTouchSub(const Game::Sim::BodyGeom &g, glm::vec2 pos0, glm::vec2 pos1,
                         glm::vec2 c0, glm::vec2 c1, float r) {
    const double ext = g.hurtIsBox ? static_cast<double>(std::max(g.hurtHalfPx.x, g.hurtHalfPx.y))
                                   : static_cast<double>(g.hurtRadiusPx);
    const double reach = ext + static_cast<double>(r) + 1.0;
    const double bx0 = std::min(c0.x, c1.x) - reach;
    const double bx1 = std::max(c0.x, c1.x) + reach;
    const double by0 = std::min(c0.y, c1.y) - reach;
    const double by1 = std::max(c0.y, c1.y) + reach;
    const double tx0 = std::min(pos0.x, pos1.x) + static_cast<double>(g.hurtOffsetPx.x);
    const double tx1 = std::max(pos0.x, pos1.x) + static_cast<double>(g.hurtOffsetPx.x);
    const double ty0 = std::min(pos0.y, pos1.y) + static_cast<double>(g.hurtOffsetPx.y);
    const double ty1 = std::max(pos0.y, pos1.y) + static_cast<double>(g.hurtOffsetPx.y);
    if (tx1 < bx0 || tx0 > bx1 || ty1 < by0 || ty0 > by1) {
        return -1;
    }
    for (int j = 0; j <= kSubSteps; ++j) {
        const double t = static_cast<double>(j) / static_cast<double>(kSubSteps);
        if (Touches(g, Lerp(c0, c1, t), static_cast<double>(r), Lerp(pos0, pos1, t))) {
            return j;
        }
    }
    return -1;
}

// ---- P3.0c: a bullet with a prefab SHAPE (circle or box at an offset) heading along +x -------------
// The bullet root is rotated to its travel direction, so for a bullet flying along +x the local
// frame IS the world frame: an axis-aligned box (or circle) centred at origin + offset. These
// overlap tests are written separately from the production Minkowski-sum code (plain per-axis
// "distance outside" / interval-overlap forms).

/// A bullet's prefab shape for a heading of 0 (px): circle (r) or box (half extents), at an offset.
struct ShapeSpec {
    bool box = false;
    double r = 0.0;       ///< circle radius
    double hx = 0.0;      ///< box half extents
    double hy = 0.0;
    double ox = 0.0;      ///< centre offset from the bullet origin
    double oy = 0.0;
    /// Smallest x extent measured from the origin (rear edge) / largest (front edge).
    double Rear() const { return ox - (box ? hx : r); }
    double Front() const { return ox + (box ? hx : r); }
};

/// Discrete overlap (inclusive) of the shape of a bullet whose origin is at @p origin with the
/// hurtbox of a body standing at @p target.
inline bool TouchesShape(const Game::Sim::BodyGeom &g, const ShapeSpec &s, Vec origin, Vec target) {
    if (!s.box) {
        return Touches(g, Vec{origin.x + s.ox, origin.y + s.oy}, s.r, target);
    }
    const double cx = origin.x + s.ox;
    const double cy = origin.y + s.oy;
    const double tx = target.x + static_cast<double>(g.hurtOffsetPx.x);
    const double ty = target.y + static_cast<double>(g.hurtOffsetPx.y);
    const double dx = std::fabs(tx - cx);
    const double dy = std::fabs(ty - cy);
    if (g.hurtIsBox) {
        return dx <= s.hx + static_cast<double>(g.hurtHalfPx.x) &&
               dy <= s.hy + static_cast<double>(g.hurtHalfPx.y);
    }
    const double ox = std::max(dx - s.hx, 0.0);
    const double oy = std::max(dy - s.hy, 0.0);
    const double rr = static_cast<double>(g.hurtRadiusPx);
    return ox * ox + oy * oy <= rr * rr;
}

/// FirstTouchSub for a prefab shape (origin path c0 -> c1, heading 0).
inline int FirstTouchSubShape(const Game::Sim::BodyGeom &g, const ShapeSpec &s, glm::vec2 pos0,
                              glm::vec2 pos1, glm::vec2 c0, glm::vec2 c1) {
    const double reach = static_cast<double>(g.hurtIsBox ? std::max(g.hurtHalfPx.x, g.hurtHalfPx.y)
                                                         : g.hurtRadiusPx) +
                         std::max({std::fabs(s.ox) + std::max(s.hx, s.r), std::fabs(s.oy) + std::max(s.hy, s.r)}) +
                         1.0;
    const double bx0 = std::min(c0.x, c1.x) - reach;
    const double bx1 = std::max(c0.x, c1.x) + reach;
    const double by0 = std::min(c0.y, c1.y) - reach;
    const double by1 = std::max(c0.y, c1.y) + reach;
    const double tx0 = std::min(pos0.x, pos1.x) + static_cast<double>(g.hurtOffsetPx.x);
    const double tx1 = std::max(pos0.x, pos1.x) + static_cast<double>(g.hurtOffsetPx.x);
    const double ty0 = std::min(pos0.y, pos1.y) + static_cast<double>(g.hurtOffsetPx.y);
    const double ty1 = std::max(pos0.y, pos1.y) + static_cast<double>(g.hurtOffsetPx.y);
    if (tx1 < bx0 || tx0 > bx1 || ty1 < by0 || ty0 > by1) {
        return -1;
    }
    for (int j = 0; j <= kSubSteps; ++j) {
        const double t = static_cast<double>(j) / static_cast<double>(kSubSteps);
        if (TouchesShape(g, s, Lerp(c0, c1, t), Lerp(pos0, pos1, t))) {
            return j;
        }
    }
    return -1;
}

/// First sub-step index j in [1, kSubSteps] at which @p blocked(bulletCentre) is true while the
/// bullet moves c0 -> c1; -1 if never. (j = 0, the path start, is assumed free as in the sim.)
inline int FirstBlockedSub(glm::vec2 c0, glm::vec2 c1,
                           const std::function<bool(glm::vec2)> &blocked) {
    for (int j = 1; j <= kSubSteps; ++j) {
        const double t = static_cast<double>(j) / static_cast<double>(kSubSteps);
        const Vec p = Lerp(c0, c1, t);
        if (blocked(glm::vec2{static_cast<float>(p.x), static_cast<float>(p.y)})) {
            return j;
        }
    }
    return -1;
}

} // namespace sktest::oracle

#endif /* SKTEST_SWEPTORACLE_HPP */
