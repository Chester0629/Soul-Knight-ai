#ifndef GAME_SIM_SWEPTCOLLISION_HPP
#define GAME_SIM_SWEPTCOLLISION_HPP

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace Game::Sim::Swept {

/// Continuous (swept) collision primitives for the fixed-step bullet sim (T-57).
///
/// Every function works in a frame where the TARGET shape is centred on the origin and
/// the moving point is the bullet centre's RELATIVE position (bullet - target) at the start
/// (@p p0, t = 0) and end (@p p1, t = 1) of one fixed step; the relative position moves
/// linearly p(t) = p0 + t (p1 - p0). The bullet radius is folded into the target shape
/// (Minkowski sum), so "the point is inside the shape" == "the bullet circle touches the
/// target". All boundaries are INCLUSIVE (touching counts, like CirclesOverlap).
/// On a hit @p t is the EARLIEST time in [0,1] at which the point is inside (0 when it
/// starts inside). Quadratics are solved in double to keep the entry time stable.

/// Earliest entry of the segment into the disc |p| <= radius.
inline bool SegmentCircleEntry(glm::vec2 p0, glm::vec2 p1, float radius, float &t) {
    const double px = p0.x, py = p0.y;
    const double dx = static_cast<double>(p1.x) - px, dy = static_cast<double>(p1.y) - py;
    const double r = radius;
    const double c = px * px + py * py - r * r;
    if (c <= 0.0) { // starts inside (or exactly on the boundary)
        t = 0.0F;
        return true;
    }
    const double a = dx * dx + dy * dy;
    if (a <= 0.0) {
        return false;
    }
    const double b = px * dx + py * dy; // half the usual linear coefficient
    const double disc = b * b - a * c;
    if (disc < 0.0) {
        return false;
    }
    const double te = (-b - std::sqrt(disc)) / a; // smaller root = entry time
    if (te < 0.0 || te > 1.0) {
        return false;
    }
    t = static_cast<float>(te);
    return true;
}

/// Earliest entry of the segment into the axis-aligned box |x| <= half.x, |y| <= half.y
/// (slab method).
inline bool SegmentAabbEntry(glm::vec2 p0, glm::vec2 p1, glm::vec2 half, float &t) {
    double tmin = 0.0;
    double tmax = 1.0;
    for (int axis = 0; axis < 2; ++axis) {
        const double o = axis == 0 ? p0.x : p0.y;
        const double d = (axis == 0 ? p1.x : p1.y) - o;
        const double h = axis == 0 ? half.x : half.y;
        if (d == 0.0) {
            if (std::abs(o) > h) {
                return false;
            }
            continue;
        }
        double ta = (-h - o) / d;
        double tb = (h - o) / d;
        if (ta > tb) {
            std::swap(ta, tb);
        }
        tmin = std::max(tmin, ta);
        tmax = std::min(tmax, tb);
        if (tmin > tmax) {
            return false;
        }
    }
    t = static_cast<float>(tmin);
    return true;
}

/// Earliest entry into the ROUNDED box (box of half extents @p half grown by @p r with
/// circular corners = the exact Minkowski sum of the box and a disc of radius @p r).
/// The rounded box is the union of two edge-grown rectangles and four corner discs, each
/// convex, so the earliest entry is the minimum over those six exact tests.
inline bool SegmentRoundedBoxEntry(glm::vec2 p0, glm::vec2 p1, glm::vec2 half, float r,
                                   float &t) {
    bool hit = false;
    float best = 2.0F;
    float cand = 0.0F;
    if (SegmentAabbEntry(p0, p1, glm::vec2{half.x + r, half.y}, cand) && cand < best) {
        best = cand;
        hit = true;
    }
    if (SegmentAabbEntry(p0, p1, glm::vec2{half.x, half.y + r}, cand) && cand < best) {
        best = cand;
        hit = true;
    }
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sy = -1; sy <= 1; sy += 2) {
            const glm::vec2 corner{half.x * static_cast<float>(sx), half.y * static_cast<float>(sy)};
            if (SegmentCircleEntry(p0 - corner, p1 - corner, r, cand) && cand < best) {
                best = cand;
                hit = true;
            }
        }
    }
    if (hit) {
        t = best;
    }
    return hit;
}

} // namespace Game::Sim::Swept

#endif /* GAME_SIM_SWEPTCOLLISION_HPP */
