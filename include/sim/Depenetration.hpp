#ifndef GAME_SIM_DEPENETRATION_HPP
#define GAME_SIM_DEPENETRATION_HPP

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/glm.hpp>

#include "Util/Collider.hpp"
#include "sim/SweptCollision.hpp"

namespace Game::Sim {

/// Depenetration: push a body circle that overlaps static blockers out to the nearest free
/// position (P2.18, slice 2 of the "characters stuck in boxes and walls" fix).
///
/// FAITHFUL (prefab data): the 13 hero prefabs and 134 of the 147 enemy/boss prefabs carry a Dynamic
/// (m_BodyType 0) Rigidbody2D (c01.prefab: Continuous detection); 6 are Kinematic (e_battery01,
/// ex_battery01, e_chest1, e_chest2, boss07, boss06_c) and 7 have no Rigidbody2D (mineral_crystal,
/// mineral_gold, e_succulent, ex_succulent, e_tentacle, e_tentacle_temp, boss12_parent). INFERRED
/// (physics outcome): for a Dynamic body Box2D's contact solver pushes it out of an overlapping static
/// Wall / Obstacle collider along the contact normal, so it would not stay pinned; the port applies
/// the same exit to every hostile, Kinematic ones included (a Kinematic body would NOT be pushed
/// by Box2D -- those 6 prefabs are where the port differs).
/// PORT CHOICE (T-61): Box2D is not ported; exit POSITIONS are an approximation. The port answers
/// the same question geometrically:
/// the minimum-translation exit of a circle from circle / AABB blockers. For each overlapping
/// blocker the candidates are the exact MTV exit (centre outside: along the closest-point
/// normal) and the four axis-aligned face exits (the only exits when the centre is inside); the
/// smallest candidate that is free of EVERY blocker wins (so an exit that would land in the neighbouring cell of a box row is
/// skipped). When no candidate is free the nearest candidate is taken and the solve repeats
/// (at most kDepenIterations times); if the pocket still cannot be left (a 1-cell corridor
/// walled in on all sides) a sampled nearest-free search runs. Exits stop kPushOutEpsPx beyond
/// the contact, because Util::Overlap treats touching as overlapping.

/// Extra clearance (px) added beyond the exact contact when pushing a body out.
inline constexpr float kPushOutEpsPx = 0.05F;
/// Candidate-resolution rounds before the sampled search takes over.
inline constexpr int kDepenIterations = 8;
/// Furthest the sampled nearest-free search looks (px): 6 cells covers a 2x2 statue plus any body.
inline constexpr float kDepenSearchMaxPx = 192.0F;

namespace DepenDetail {

inline bool Overlaps(const Util::Collider &blocker, glm::vec2 pos, float r) {
    return Util::Overlap(blocker, Util::Collider::MakeCircle(pos, r));
}

inline bool AnyOverlap(const std::vector<Util::Collider> &blockers, glm::vec2 pos, float r) {
    for (const Util::Collider &b : blockers) {
        if (Overlaps(b, pos, r)) {
            return true;
        }
    }
    return false;
}

/// Exit candidates of ONE overlapping blocker (appended to @p out).
inline void ExitCandidates(const Util::Collider &b, glm::vec2 pos, float r,
                           std::vector<glm::vec2> &out) {
    const float reach = r + kPushOutEpsPx;
    if (b.shape == Util::ColliderShape::Circle) {
        const glm::vec2 d = pos - b.center;
        const float dist = std::sqrt(d.x * d.x + d.y * d.y);
        if (dist > 1e-4F) {
            out.push_back(b.center + d / dist * (b.radius + reach));
        } else {
            const float k = b.radius + reach;
            out.push_back(b.center + glm::vec2{k, 0.0F});
            out.push_back(b.center + glm::vec2{-k, 0.0F});
            out.push_back(b.center + glm::vec2{0.0F, k});
            out.push_back(b.center + glm::vec2{0.0F, -k});
        }
        return;
    }
    const glm::vec2 lo = b.Min();
    const glm::vec2 hi = b.Max();
    const glm::vec2 cp{std::clamp(pos.x, lo.x, hi.x), std::clamp(pos.y, lo.y, hi.y)};
    const glm::vec2 d = pos - cp;
    const float dist = std::sqrt(d.x * d.x + d.y * d.y);
    const bool inside = dist <= 1e-4F; // centre in (or on the edge of) the box
    if (!inside) {
        out.push_back(cp + d / dist * reach); // the exact MTV exit
    }
    // Axis-aligned exits through all four faces. They are the fallback when the exact normal exit
    // lands in a neighbouring blocker (a slot between two boxes: the way out is along the slot).
    // A far-side exit is as long as the box is thick plus the body, so it is only chosen when no
    // nearer free candidate exists -- selection is by smallest translation.
    out.push_back({lo.x - reach, pos.y});
    out.push_back({hi.x + reach, pos.y});
    out.push_back({pos.x, lo.y - reach});
    out.push_back({pos.x, hi.y + reach});
}

} // namespace DepenDetail

/// EXACT swept test (P2.18 review R37): does a circle of radius @p r moving along the straight
/// segment @p from -> @p to touch blocker @p b at ANY point of the way (start and end included,
/// touching counts, like Util::Overlap)? Circle blocker: segment vs the disc grown by r. AABB
/// blocker: segment vs the exact Minkowski sum (the box grown by r with rounded corners).
/// Pure geometry, no sampling, so a shallow graze between two free end points is caught.
inline bool SegmentBlockedBy(const Util::Collider &b, glm::vec2 from, glm::vec2 to, float r) {
    float t = 0.0F;
    if (b.shape == Util::ColliderShape::Circle) {
        return Swept::SegmentCircleEntry(from - b.center, to - b.center, b.radius + r, t);
    }
    return Swept::SegmentRoundedBoxEntry(from - b.center, to - b.center, b.halfSize, r, t);
}

inline bool SegmentBlockedByAny(const std::vector<Util::Collider> &blockers, glm::vec2 from, glm::vec2 to,
                                float r) {
    for (const Util::Collider &b : blockers) {
        if (SegmentBlockedBy(b, from, to, r)) {
            return true;
        }
    }
    return false;
}

/// Sampled nearest-free search: the smallest-distance point around @p pos for which
/// @p isFree(point) holds, or @p pos itself when @p isFree(pos) already holds or nothing free is
/// found within @p maxPx. 2 px ring spacing x 32 directions, then the first free ring is refined
/// by bisection back toward the last blocked ring (radial accuracy about 0.01 px). Only 32 directions
/// are tried, so the result can be up to a few px (tests: < 3 px) farther than the true nearest
/// free point; the exact solver above is not subject to that. Deterministic, PORT CHOICE (T-61).
template <class IsFree>
glm::vec2 SearchNearestFree(glm::vec2 pos, float maxPx, IsFree &&isFree) {
    if (isFree(pos)) {
        return pos;
    }
    constexpr int kDirs = 32;
    constexpr float kRing = 2.0F;
    constexpr float kTwoPi = 6.28318530718F;
    for (float d = kRing; d <= maxPx; d += kRing) {
        for (int k = 0; k < kDirs; ++k) {
            const float a = kTwoPi * static_cast<float>(k) / static_cast<float>(kDirs);
            const glm::vec2 dir{std::cos(a), std::sin(a)};
            if (!isFree(pos + dir * d)) {
                continue;
            }
            float lo = d - kRing; // blocked on every ray (the previous ring found nothing)
            float hi = d;         // free on this ray
            for (int i = 0; i < 8; ++i) {
                const float mid = 0.5F * (lo + hi);
                if (isFree(pos + dir * mid)) {
                    hi = mid;
                } else {
                    lo = mid;
                }
            }
            return pos + dir * hi;
        }
    }
    return pos;
}

/// Push the circle (@p pos, @p r) out of @p blockers (see the file comment). Returns @p pos
/// unchanged when it overlaps none. @p resolved (optional) is set false when the pocket could
/// not be left (the result still overlaps) -- the caller may fall back to a wider search.
inline glm::vec2 DepenetrateAgainst(const std::vector<Util::Collider> &blockers, glm::vec2 pos,
                                    float r, bool *resolved = nullptr) {
    if (resolved != nullptr) {
        *resolved = true;
    }
    if (!DepenDetail::AnyOverlap(blockers, pos, r)) {
        return pos;
    }
    const glm::vec2 start = pos;
    std::vector<glm::vec2> cands;
    for (int iter = 0; iter < kDepenIterations; ++iter) {
        cands.clear();
        for (const Util::Collider &b : blockers) {
            if (DepenDetail::Overlaps(b, pos, r)) {
                DepenDetail::ExitCandidates(b, pos, r, cands);
            }
        }
        if (cands.empty()) {
            return pos; // nothing overlaps any more
        }
        bool haveFree = false;
        float bestFree = 0.0F;
        glm::vec2 bestFreePos = pos;
        float bestAny = 0.0F;
        glm::vec2 bestAnyPos = pos;
        bool haveAny = false;
        for (const glm::vec2 &c : cands) {
            const glm::vec2 t = c - pos;
            const float dsq = t.x * t.x + t.y * t.y;
            if (!haveAny || dsq < bestAny) {
                haveAny = true;
                bestAny = dsq;
                bestAnyPos = c;
            }
            if ((!haveFree || dsq < bestFree) && !DepenDetail::AnyOverlap(blockers, c, r)) {
                haveFree = true;
                bestFree = dsq;
                bestFreePos = c;
            }
        }
        if (haveFree) {
            return bestFreePos;
        }
        pos = bestAnyPos; // partial exit; the next round resolves the neighbour it ran into
    }
    // Could not leave the pocket by exits: sampled search around the ORIGINAL position.
    const glm::vec2 found = SearchNearestFree(start, kDepenSearchMaxPx, [&](glm::vec2 p) {
        return !DepenDetail::AnyOverlap(blockers, p, r);
    });
    if (resolved != nullptr) {
        *resolved = !DepenDetail::AnyOverlap(blockers, found, r);
    }
    return found;
}

} // namespace Game::Sim

#endif /* GAME_SIM_DEPENETRATION_HPP */
