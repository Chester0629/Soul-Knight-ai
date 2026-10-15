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

/// Earliest entry of the segment into the CONVEX polygon with counter-clockwise vertices
/// @p v[0..n) (Cyrus-Beck clipping, inclusive boundary, doubles). P3.0c: the Minkowski sum of
/// an axis-aligned hurtbox and an ORIENTED box bullet is such a polygon (at most 8 vertices).
inline bool SegmentConvexEntry(glm::vec2 p0, glm::vec2 p1, const glm::vec2 *v, int n, float &t) {
    if (n < 3) {
        return false;
    }
    double tmin = 0.0;
    double tmax = 1.0;
    const double dx = static_cast<double>(p1.x) - p0.x;
    const double dy = static_cast<double>(p1.y) - p0.y;
    for (int i = 0; i < n; ++i) {
        const glm::vec2 a = v[i];
        const glm::vec2 b = v[(i + 1) % n];
        const double ex = static_cast<double>(b.x) - a.x;
        const double ey = static_cast<double>(b.y) - a.y;
        const double nx = ey; // outward normal of a CCW edge
        const double ny = -ex;
        const double dist0 = nx * (static_cast<double>(p0.x) - a.x) + ny * (static_cast<double>(p0.y) - a.y);
        const double den = nx * dx + ny * dy;
        if (den == 0.0) {
            if (dist0 > 0.0) {
                return false; // parallel and outside this half-plane
            }
            continue;
        }
        const double te = -dist0 / den; // inside when dist0 + t * den <= 0
        if (den < 0.0) {
            tmin = std::max(tmin, te);
        } else {
            tmax = std::min(tmax, te);
        }
        if (tmin > tmax) {
            return false;
        }
    }
    t = static_cast<float>(tmin);
    return true;
}

/// Convex hull (counter-clockwise, collinear points dropped) of up to 16 points; returns the
/// vertex count written to @p out (capacity 16). Andrew's monotone chain.
inline int ConvexHull16(const glm::vec2 *pts, int count, glm::vec2 *out) {
    glm::vec2 p[16];
    int n = std::min(count, 16);
    for (int i = 0; i < n; ++i) {
        p[i] = pts[i];
    }
    std::sort(p, p + n, [](const glm::vec2 &a, const glm::vec2 &b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    auto cross = [](const glm::vec2 &o, const glm::vec2 &a, const glm::vec2 &b) {
        return (static_cast<double>(a.x) - o.x) * (static_cast<double>(b.y) - o.y) -
               (static_cast<double>(a.y) - o.y) * (static_cast<double>(b.x) - o.x);
    };
    glm::vec2 h[34];
    int k = 0;
    for (int i = 0; i < n; ++i) {
        while (k >= 2 && cross(h[k - 2], h[k - 1], p[i]) <= 0.0) {
            --k;
        }
        h[k++] = p[i];
    }
    const int lower = k + 1;
    for (int i = n - 2; i >= 0; --i) {
        while (k >= lower && cross(h[k - 2], h[k - 1], p[i]) <= 0.0) {
            --k;
        }
        h[k++] = p[i];
    }
    const int cnt = std::max(0, k - 1);
    for (int i = 0; i < cnt; ++i) {
        out[i] = h[i];
    }
    return cnt;
}

/// Rotate @p v by @p angle radians (counter-clockwise).
inline glm::vec2 Rotated(glm::vec2 v, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

/// Earliest entry of the segment into the ROUNDED ORIENTED box: a box of half extents @p half
/// whose x axis points at @p angle, centred on the origin, grown by @p r (= Minkowski sum of the
/// oriented box and a disc). The oriented bullet box against a CIRCLE hurtbox reduces to this.
inline bool SegmentOrientedRoundedBoxEntry(glm::vec2 p0, glm::vec2 p1, glm::vec2 half, float angle,
                                           float r, float &t) {
    return SegmentRoundedBoxEntry(Rotated(p0, -angle), Rotated(p1, -angle), half, r, t);
}

/// Earliest entry of the segment into the Minkowski sum of an axis-aligned box (half extents
/// @p aabbHalf) and an ORIENTED box (half extents @p obbHalf, x axis at @p angle), both centred
/// on the origin: the octagon of the 16 corner sums. The oriented bullet box against a BOX
/// hurtbox reduces to this.
inline bool SegmentBoxSumEntry(glm::vec2 p0, glm::vec2 p1, glm::vec2 aabbHalf, glm::vec2 obbHalf,
                               float angle, float &t) {
    glm::vec2 pts[16];
    int n = 0;
    for (int ax = -1; ax <= 1; ax += 2) {
        for (int ay = -1; ay <= 1; ay += 2) {
            for (int bx = -1; bx <= 1; bx += 2) {
                for (int by = -1; by <= 1; by += 2) {
                    const glm::vec2 corner = Rotated({obbHalf.x * static_cast<float>(bx),
                                                      obbHalf.y * static_cast<float>(by)}, angle);
                    pts[n++] = glm::vec2{aabbHalf.x * static_cast<float>(ax),
                                         aabbHalf.y * static_cast<float>(ay)} + corner;
                }
            }
        }
    }
    glm::vec2 hull[16];
    const int hn = ConvexHull16(pts, n, hull);
    return SegmentConvexEntry(p0, p1, hull, hn, t);
}

} // namespace Game::Sim::Swept

#endif /* GAME_SIM_SWEPTCOLLISION_HPP */
