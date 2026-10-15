#ifndef GAME_SIM_BODYMOVE_HPP
#define GAME_SIM_BODYMOVE_HPP

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "sim/WorldCollision.hpp"

namespace Game::Sim {

/// Longest single displacement (px) MoveBody will honour; anything longer is cut to this length.
/// A real frame never gets near it (walking 33.3 px per clamped frame, a dash 80 px); it only
/// bounds the sub-step loop against a NaN / absurd delta.
inline constexpr float kMaxMovePx = 4096.0F;

/// Sub-step length (px) for a body of @p radius: the body radius (never below 1 px). Each sub-step
/// is tested with WorldCollision::MoveBlocked (the swept segment, not just its end point), so
/// crossing a blocker is caught for ANY step length in a world whose MoveBlocked is exact
/// (GameScene); the sub-steps then only keep a long displacement sliding along walls the way a walk
/// does. In a world on the default sampled MoveBlocked (test doubles) the behaviour is the sampled
/// one documented there (a shallow graze, about the sagitta R - sqrt(R^2 - s^2/4) deep, can slip through); T-62.
inline float MoveStepCapPx(float radius) {
    return std::max(radius, 1.0F);
}

/// SWEPT, wall-gated body movement (P2.18, PORT CHOICE T-62). Moves a circle of @p radius from
/// @p pos by @p delta with the SAME axis-separated slide the walk always used (each axis is
/// committed only if the swept circle does not Block on the way to its destination), cut into
/// sub-steps of at most MoveStepCapPx(radius). The pre-P2.18 gate tested only the END point, so a
/// displacement longer than (wall + 2 radii) jumped a 1-cell wall by landing on its far side, and
/// even a short step could graze a blocker that both end points missed (R37 counterexample: circle
/// blocker r 16 at the origin, body 12.8 moving (-6,28.5) -> (6,28.5)). INFERRED: the original's
/// characters are Continuous Rigidbody2Ds (c01.prefab), whose contact solver does not tunnel.
///
/// A delta within one sub-step takes ONE call with target = pos + delta (bit-exact with the old
/// target); in an open world (NullWorldCollision) the result is bit-identical to the old code
/// for every delta, so goldens are unchanged. Longer deltas: while an axis has never been
/// blocked its target is pos + delta * t (no accumulated rounding); once blocked it advances from
/// the last committed value in one-step increments, so a blocked axis never skips the obstacle.
///
/// @p flushToContact: when an axis sub-step is blocked, bisect to the farthest free point on it
/// (8 halvings, ~1/256 of a sub-step) so the body ends against the blocker instead of up to a
/// sub-step short of it. The dash uses it (it ends ON the near side of the wall); plain walking
/// keeps the old stop-short behaviour.
///
/// The start is assumed free (callers run WorldCollision::Depenetrate first); a start that
/// overlaps a blocker cannot move on a blocked axis, as before.
inline glm::vec2 MoveBody(const WorldCollision &wc, glm::vec2 pos, glm::vec2 delta, float radius,
                          bool flushToContact = false) {
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y) || !std::isfinite(pos.x) ||
        !std::isfinite(pos.y)) {
        return pos;
    }
    float len = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (len > kMaxMovePx) {
        delta *= kMaxMovePx / len;
        len = kMaxMovePx;
    }
    const float cap = MoveStepCapPx(radius);
    const int n = len <= cap ? 1 : static_cast<int>(std::ceil(len / cap));
    const glm::vec2 stepVec = delta / static_cast<float>(n);

    glm::vec2 cur = pos;
    bool xBlocked = false;
    bool yBlocked = false;
    for (int i = 1; i <= n; ++i) {
        const float t = (i == n) ? 1.0F : static_cast<float>(i) / static_cast<float>(n);
        // X axis
        {
            const float target = xBlocked ? cur.x + stepVec.x : pos.x + delta.x * t;
            if (!wc.MoveBlocked(cur, glm::vec2{target, cur.y}, radius)) {
                cur.x = target;
            } else {
                xBlocked = true;
                if (flushToContact) {
                    float lo = cur.x; // free
                    float hi = target; // blocked
                    for (int k = 0; k < WorldCollision::kSweepBisectSteps; ++k) {
                        const float mid = 0.5F * (lo + hi);
                        if (wc.MoveBlocked(cur, glm::vec2{mid, cur.y}, radius)) {
                            hi = mid;
                        } else {
                            lo = mid;
                        }
                    }
                    cur.x = lo;
                }
            }
        }
        // Y axis
        {
            const float target = yBlocked ? cur.y + stepVec.y : pos.y + delta.y * t;
            if (!wc.MoveBlocked(cur, glm::vec2{cur.x, target}, radius)) {
                cur.y = target;
            } else {
                yBlocked = true;
                if (flushToContact) {
                    float lo = cur.y;
                    float hi = target;
                    for (int k = 0; k < WorldCollision::kSweepBisectSteps; ++k) {
                        const float mid = 0.5F * (lo + hi);
                        if (wc.MoveBlocked(cur, glm::vec2{cur.x, mid}, radius)) {
                            hi = mid;
                        } else {
                            lo = mid;
                        }
                    }
                    cur.y = lo;
                }
            }
        }
    }
    return cur;
}

} // namespace Game::Sim

#endif /* GAME_SIM_BODYMOVE_HPP */
