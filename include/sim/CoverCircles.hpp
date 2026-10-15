#ifndef GAME_SIM_COVERCIRCLES_HPP
#define GAME_SIM_COVERCIRCLES_HPP

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace Game::Sim {

/// A row of equal circles covering an oriented box (P3.0c, T-64). This is the FALLBACK wall test of
/// WorldCollision::SweepBoxBlocked for worlds that can only answer CIRCLE queries (test doubles); the
/// live GameScene world answers the exact oriented-box sweep instead (SweepBoxBlockedByAny).
///
/// Construction: radius r = the box's smaller half extent (at least 0.5 px); n = ceil(longHalf / r)
/// circles (1..kMax) centred on the box's long axis, evenly spaced from -(L - r) to +(L - r), so
/// neighbours are at most 2 r apart and the end circles reach the box ends.
///
/// TRUE COVERAGE GAP (measured, pinned by BulletShapeTest.CoverRowGapIsBoundedAndRegistered): the
/// union does NOT cover the whole box. Between two neighbours spaced s <= 2 r apart, a point on the
/// box's side edge midway between them is sqrt(r^2 + (s/2)^2) <= sqrt(2) r from the nearest centre,
/// and the same holds at the four corners, so the uncovered slivers run along the whole PERIMETER
/// (sides and corners) and are at most (sqrt(2) - 1) r deep, measured as the distance from an
/// uncovered box point to the nearest circle boundary. For the 177 box colliders in bullets.json the
/// largest sliver depth is reported by that test (< 0.6 px for the 0.1 x 1.2 u Bullet03, r = 1.6 px).
struct CoverCircles {
    static constexpr int kMax = 12;
    int n = 0;
    float radius = 0.0F;
    glm::vec2 centre[kMax]; ///< circle centres relative to the BULLET ORIGIN (world axes).
};

/// @p centreOffset the box centre relative to the bullet origin (world axes), @p half the box half
/// extents along its own axes, @p angle the world angle of the box's x axis.
inline CoverCircles MakeCoverCircles(glm::vec2 centreOffset, glm::vec2 half, float angle) {
    CoverCircles c;
    const bool longX = half.x >= half.y;
    const float longHalf = longX ? half.x : half.y;
    c.radius = std::max(longX ? half.y : half.x, 0.5F);
    c.n = std::clamp(static_cast<int>(std::ceil(longHalf / c.radius)), 1, CoverCircles::kMax);
    const float ca = std::cos(angle);
    const float sa = std::sin(angle);
    const glm::vec2 axisLocal = longX ? glm::vec2{1.0F, 0.0F} : glm::vec2{0.0F, 1.0F};
    const glm::vec2 axis{axisLocal.x * ca - axisLocal.y * sa, axisLocal.x * sa + axisLocal.y * ca};
    const float reach = std::max(0.0F, longHalf - c.radius);
    for (int i = 0; i < c.n; ++i) {
        const float s = c.n == 1 ? 0.0F
                                 : -reach + 2.0F * reach * static_cast<float>(i) / static_cast<float>(c.n - 1);
        c.centre[i] = centreOffset + axis * s;
    }
    return c;
}

} // namespace Game::Sim

#endif /* GAME_SIM_COVERCIRCLES_HPP */
