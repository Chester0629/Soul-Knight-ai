#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include "sim/SkillDash.hpp"
#include "sim/WorldCollision.hpp"

using Game::Sim::ApplyDash;
using Game::Sim::kSkillDashPx;
using Game::Sim::NullWorldCollision;
using Game::Sim::WorldCollision;

// NOLINTBEGIN(readability-magic-numbers)

// =============================================================================
// W1 (B1-P4a): the SHARED owner-side dash-apply (Game::Sim::ApplyDash) that the
// GameScene owner calls on the sim's skill_dash event -- and the RealBodyDriver test
// mirror calls the SAME function. Two behaviours are pinned here: (a) open-ground the
// dash advances the body by ~the dash distance; (b) toward a near wall the dash is
// CLAMPED by the collision gate and never tunnels through it (the air-wall scar).
// =============================================================================

namespace {
constexpr float kRadius = 16.0F; // player body radius (kPlayerRadius), supplied by the owner.

// A vertical wall occupying the half-plane x >= wallX: a body circle Blocks once its
// right edge (pos.x + radius) reaches the wall. Models "a wall directly ahead".
class WallAtX : public WorldCollision {
public:
    explicit WallAtX(float wallX) : m_WallX(wallX) {}
    bool Blocks(glm::vec2 pos, float radius) const override {
        return pos.x + radius >= m_WallX;
    }

private:
    float m_WallX;
};
} // namespace

// (a) Open ground -> the dash advances the body by ~the dash distance along the aim.
TEST(SkillDashTest, OpenGroundDashMovesBodyByDashDistance) {
    NullWorldCollision open; // never blocks
    glm::vec2 body{0.0F, 0.0F};
    ApplyDash(open, body, glm::vec2{1.0F, 0.0F}, kSkillDashPx, kRadius);
    EXPECT_NEAR(body.x, kSkillDashPx, 0.001F) << "open-ground dash advances ~80px along aim";
    EXPECT_NEAR(body.y, 0.0F, 0.001F);
}

// (a') A non-unit aim is normalised: the dash still moves exactly `dist` px.
TEST(SkillDashTest, DashNormalizesAimDirection) {
    NullWorldCollision open;
    glm::vec2 body{0.0F, 0.0F};
    ApplyDash(open, body, glm::vec2{0.0F, 5.0F}, kSkillDashPx, kRadius); // +y, length 5
    EXPECT_NEAR(body.x, 0.0F, 0.001F);
    EXPECT_NEAR(body.y, kSkillDashPx, 0.001F);
}

// (b) SCAR-CLOSING: a dash toward a near wall must not tunnel through it (WallAtX is a half-plane wall). A wall sits 40px ahead;
// an 80px dash would otherwise land the body at x=80, deep PAST the wall. P2.18 (T-62): the dash is
// no longer refused outright (the old all-or-nothing clamp left the body where it stood); it is
// swept and ends FLUSH against the wall, like the original's Continuous Rigidbody2D (Box2D stops
// the impulse at the wall). The no-tunnel property over a whole sweep of start distances lives in
// BodyMoveTest.DashTowardAOneCellWallFromAnyDistanceEndsOnTheNearSide.
TEST(SkillDashTest, DashIntoNearWallStopsFlushAgainstItNoTunnel) {
    WallAtX wall(40.0F); // wall half-plane at x>=40; body starts at the origin
    glm::vec2 body{0.0F, 0.0F};
    ApplyDash(wall, body, glm::vec2{1.0F, 0.0F}, kSkillDashPx, kRadius);
    EXPECT_LT(body.x + kRadius, 40.0F) << "dashed body must not cross the wall (no tunnel)";
    EXPECT_NEAR(body.x + kRadius, 40.0F, 0.1F) << "the dash ends flush against the wall, not cancelled";
}

// NOLINTEND(readability-magic-numbers)
