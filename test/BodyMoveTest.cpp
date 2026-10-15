#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <glm/glm.hpp>

#include "Util/Collider.hpp"
#include "sim/BodyMove.hpp"
#include "sim/Depenetration.hpp"
#include "sim/SimConfig.hpp"
#include "sim/SkillDash.hpp"
#include "sim/WorldCollision.hpp"

using Game::Sim::ApplyDash;
using Game::Sim::ClampFrameDtMs;
using Game::Sim::kMaxFrameDtMs;
using Game::Sim::kSkillDashPx;
using Game::Sim::MoveBody;
using Game::Sim::NullWorldCollision;
using Game::Sim::WorldCollision;
using Util::Collider;

// NOLINTBEGIN(readability-magic-numbers)

// P2.18 AC4 / AC5 (RC3): swept movement + dash + dt clamp. The pre-P2.18 gate tested only the
// END point of a displacement, so anything longer than (wall + 2 radii) jumped a 1-cell wall.

namespace {
constexpr float kR = 12.8F;

class Walls : public WorldCollision {
public:
    explicit Walls(std::vector<Collider> w) : m_W(std::move(w)) {}
    bool Blocks(glm::vec2 pos, float radius) const override {
        const Collider c = Collider::MakeCircle(pos, radius);
        for (const Collider &w : m_W) {
            if (Util::Overlap(w, c)) {
                return true;
            }
        }
        return false;
    }

private:
    std::vector<Collider> m_W;
};

// The EXACT-geometry world (what GameScene is): MoveBlocked is the segment vs the Minkowski sum.
class ExactWorld : public WorldCollision {
public:
    explicit ExactWorld(std::vector<Collider> w) : m_W(std::move(w)) {}
    bool Blocks(glm::vec2 pos, float radius) const override {
        const Collider c = Collider::MakeCircle(pos, radius);
        for (const Collider &w : m_W) {
            if (Util::Overlap(w, c)) {
                return true;
            }
        }
        return false;
    }
    bool MoveBlocked(glm::vec2 from, glm::vec2 to, float radius) const override {
        return Game::Sim::SegmentBlockedByAny(m_W, from, to, radius);
    }

private:
    std::vector<Collider> m_W;
};

// A single 1-cell (32 px thick) wall, centred at x = wallCx, tall enough that no 3000 px step can go around.
Walls OneCellWallAt(float wallCx) {
    return Walls({Collider::MakeAABB({wallCx, 0.0F}, {32.0F, 200000.0F})});
}

// The PRE-P2.18 ApplyDash, kept here verbatim as the regression oracle: axis-separated, but each
// axis tested only at its END point.
void OldEndpointDash(const WorldCollision &wc, glm::vec2 &body, glm::vec2 dir, float dist, float radius) {
    const glm::vec2 step = dir * dist;
    if (!wc.Blocks(glm::vec2{body.x + step.x, body.y}, radius)) {
        body.x += step.x;
    }
    if (!wc.Blocks(glm::vec2{body.x, body.y + step.y}, radius)) {
        body.y += step.y;
    }
}
} // namespace

TEST(BodyMoveTest, OpenGroundMoveIsExact) {
    const NullWorldCollision open;
    const glm::vec2 out = MoveBody(open, {10.0F, -5.0F}, {300.0F, 40.0F}, kR);
    EXPECT_EQ(out, (glm::vec2{310.0F, 35.0F})) << "no blocker: pos + delta, bit-exact at the end";
}

TEST(BodyMoveTest, OpenWorldMoveIsBitIdenticalToPosPlusDeltaForEveryLength) {
    // Goldens rely on this: in an open world nothing changes, whatever the step length / sub-step count.
    const NullWorldCollision open;
    unsigned lcg = 7U;
    const auto next = [&lcg]() {
        lcg = lcg * 1664525U + 1013904223U;
        return static_cast<float>((lcg >> 8) & 0xFFFF) / 65535.0F;
    };
    for (int i = 0; i < 3000; ++i) {
        const glm::vec2 pos{next() * 4000.0F - 2000.0F, next() * 4000.0F - 2000.0F};
        const float scale = (i % 3 == 0) ? 3.0F : (i % 3 == 1) ? 60.0F : 900.0F; // within / over a sub-step
        const glm::vec2 delta{(next() - 0.5F) * scale, (next() - 0.5F) * scale};
        const glm::vec2 out = MoveBody(open, pos, delta, kR);
        EXPECT_EQ(out.x, pos.x + delta.x) << i;
        EXPECT_EQ(out.y, pos.y + delta.y) << i;
    }
}

TEST(BodyMoveTest, ShortStepFollowsTheSweptAxisSlide) {
    // A step within one sub-step is ONE swept test per axis: the same slide as the old destination
    // test, with the segment (not just its end) deciding.
    const Walls w({Collider::MakeAABB({60.0F, 0.0F}, {32.0F, 400.0F})}); // spans x 44..76
    unsigned lcg = 99U;
    const auto next = [&lcg]() {
        lcg = lcg * 1664525U + 1013904223U;
        return static_cast<float>((lcg >> 8) & 0xFFFF) / 65535.0F;
    };
    for (int i = 0; i < 2000; ++i) {
        const glm::vec2 pos{next() * 80.0F - 10.0F, next() * 100.0F - 50.0F};
        const glm::vec2 delta{(next() - 0.5F) * 12.0F, (next() - 0.5F) * 12.0F}; // |d| <= 8.5 < r
        if (w.Blocks(pos, kR)) {
            continue; // the gate assumes a free start
        }
        glm::vec2 expect = pos;
        if (!w.MoveBlocked(pos, {pos.x + delta.x, pos.y}, kR)) {
            expect.x = pos.x + delta.x;
        }
        if (!w.MoveBlocked(expect, {expect.x, pos.y + delta.y}, kR)) {
            expect.y = pos.y + delta.y;
        }
        EXPECT_EQ(MoveBody(w, pos, delta, kR), expect) << "i=" << i;
    }
}

// R37 counterexample: the old argument "step <= radius => cannot cross" was false. A circle blocker
// (r 16 at the origin) and the 12.8 px hero moving (-6,28.5) -> (6,28.5): step 12 <= 12.8, both ends
// are free (distance ~29.125 > 28.8) but the middle (28.5) overlaps. The end-only test let it through.
TEST(BodyMoveTest, R37ShallowGrazeOfACircleBlockerIsCaughtOnBothWorldKinds) {
    const std::vector<Collider> blockers{Collider::MakeCircle({0.0F, 0.0F}, 16.0F)};
    const ExactWorld exact(blockers);
    const Walls sampled(blockers); // default MoveBlocked: samples every max(r/4, 1) = 3.2 px
    const glm::vec2 a{-6.0F, 28.5F};
    const glm::vec2 b{6.0F, 28.5F};
    const WorldCollision *worlds[] = {&exact, &sampled};
    for (const WorldCollision *w : worlds) {
        ASSERT_FALSE(w->Blocks(a, kR));
        ASSERT_FALSE(w->Blocks(b, kR)) << "both end points are free: the old end-only test accepted this move";
        EXPECT_TRUE(w->MoveBlocked(a, b, kR));
        EXPECT_EQ(MoveBody(*w, a, {12.0F, 0.0F}, kR), a) << "the graze is refused, the body stays";
    }
    EXPECT_TRUE(Game::Sim::SegmentBlockedBy(blockers[0], a, b, kR));
    // just outside the grown circle (distance 28.9 > 28.8) is free
    EXPECT_FALSE(Game::Sim::SegmentBlockedBy(blockers[0], {-6.0F, 28.9F}, {6.0F, 28.9F}, kR));
}

TEST(BodyMoveTest, BoxCornerGrazeIsCaughtByTheExactSweep) {
    const std::vector<Collider> blockers{Collider::MakeAABB({0.0F, 0.0F}, {32.0F, 32.0F})}; // [-16,16]^2
    // diagonal past the corner (16,16): both ends are 12.9 px from a face -> free; the middle is 9.1 from the corner
    const glm::vec2 a{28.9F, 16.0F};
    const glm::vec2 b{16.0F, 28.9F};
    const ExactWorld w(blockers);
    ASSERT_FALSE(w.Blocks(a, kR));
    ASSERT_FALSE(w.Blocks(b, kR));
    EXPECT_TRUE(Game::Sim::SegmentBlockedBy(blockers[0], a, b, kR));
    // an axis-aligned pass over the top face with both ends free (corner distance 12.85 > 12.8)
    const glm::vec2 c{-19.0F, 28.5F};
    const glm::vec2 d{19.0F, 28.5F};
    ASSERT_FALSE(w.Blocks(c, kR));
    ASSERT_FALSE(w.Blocks(d, kR));
    EXPECT_TRUE(w.MoveBlocked(c, d, kR));
    EXPECT_EQ(MoveBody(w, c, {38.0F, 0.0F}, kR), c) << "refused from the very first sub-step";
    // a clear pass 0.2 px above the grown box is free
    EXPECT_FALSE(Game::Sim::SegmentBlockedBy(blockers[0], {-19.0F, 29.0F}, {19.0F, 29.0F}, kR));
}

TEST(BodyMoveTest, ExactSweepAgreesWithDenseSamplingOnRandomSegments) {
    const std::vector<Collider> blockers{Collider::MakeAABB({0.0F, 0.0F}, {32.0F, 32.0F}),
                                         Collider::MakeCircle({70.0F, 10.0F}, 16.0F),
                                         Collider::MakeAABB({-60.0F, -30.0F}, {48.0F, 24.0F})};
    const ExactWorld w(blockers);
    unsigned lcg = 4242U;
    const auto next = [&lcg]() {
        lcg = lcg * 1664525U + 1013904223U;
        return static_cast<float>((lcg >> 8) & 0xFFFF) / 65535.0F;
    };
    int hits = 0;
    for (int i = 0; i < 4000; ++i) {
        const glm::vec2 a{next() * 200.0F - 100.0F, next() * 120.0F - 60.0F};
        const glm::vec2 b = a + glm::vec2{(next() - 0.5F) * 40.0F, (next() - 0.5F) * 40.0F};
        const bool exact = w.MoveBlocked(a, b, kR);
        const float len = glm::length(b - a);
        const int n = std::max(1, static_cast<int>(len / 0.05F)); // dense sampling every ~0.05 px
        bool denseHit = false;
        bool denseHitShrunk = false;
        for (int k = 0; k <= n; ++k) {
            const glm::vec2 p = a + (b - a) * (static_cast<float>(k) / static_cast<float>(n));
            denseHit = denseHit || w.Blocks(p, kR);
            denseHitShrunk = denseHitShrunk || w.Blocks(p, kR + 0.06F);
        }
        if (denseHit) {
            EXPECT_TRUE(exact) << "sampling found an overlap the exact sweep missed, i=" << i;
        }
        if (!denseHitShrunk) {
            EXPECT_FALSE(exact) << "exact sweep reported a hit that sampling (r+0.06) cannot find, i=" << i;
        }
        hits += exact ? 1 : 0;
    }
    EXPECT_GT(hits, 300);
    EXPECT_LT(hits, 3700);
}

TEST(BodyMoveTest, OldEndpointDashTunnelsAOneCellWallButMoveBodyStopsAtIt) {
    // Wall x 44..76 (centre 60); body r 12.8 starts at x=30 (clear of the wall face by 1.2 px
    // after r): an 80 px dash ends at x=110, whose circle (97..123) is clear of the wall -> the
    // old end-point test happily tunnels.
    const Walls w = OneCellWallAt(60.0F);
    glm::vec2 oldBody{30.0F, 0.0F};
    OldEndpointDash(w, oldBody, {1.0F, 0.0F}, kSkillDashPx, kR);
    EXPECT_GT(oldBody.x, 76.0F) << "the oracle must show the bug: the old dash ends BEYOND the wall";

    glm::vec2 body{30.0F, 0.0F};
    ApplyDash(w, body, {1.0F, 0.0F}, kSkillDashPx, kR);
    EXPECT_LT(body.x + kR, 44.0F) << "the swept dash stays on the near side";
    EXPECT_FALSE(w.Blocks(body, kR));
}

// AC4 (GWT): Given the player dashing toward a 1-cell wall from any distance <= dash length,
// when the dash resolves, then the player ends on the near side of the wall.
TEST(BodyMoveTest, DashTowardAOneCellWallFromAnyDistanceEndsOnTheNearSide) {
    const Walls w = OneCellWallAt(200.0F); // faces at x = 184 and 216
    const float nearFace = 184.0F;
    int tunnelledByOldCode = 0;
    int checked = 0;
    for (float gap = 0.0F; gap <= kSkillDashPx + 0.5F; gap += 0.5F) {
        // start so the body's right edge is `gap` px from the wall face (gap 0 = touching + eps)
        const glm::vec2 start{nearFace - kR - gap - 0.06F, 0.0F};
        ASSERT_FALSE(w.Blocks(start, kR));
        glm::vec2 body = start;
        ApplyDash(w, body, {1.0F, 0.0F}, kSkillDashPx, kR);
        ++checked;
        EXPECT_LT(body.x + kR, nearFace) << "gap " << gap << ": ended past/inside the wall";
        EXPECT_FALSE(w.Blocks(body, kR));
        const float reachable = std::min(kSkillDashPx, gap + 0.06F);
        EXPECT_NEAR(body.x - start.x, reachable, 0.06F) << "gap " << gap << ": dash stops FLUSH at the wall";
        glm::vec2 old = start;
        OldEndpointDash(w, old, {1.0F, 0.0F}, kSkillDashPx, kR);
        if (old.x > 216.0F) {
            ++tunnelledByOldCode;
        }
    }
    EXPECT_EQ(checked, 162);
    // Old end-point test: tunnels whenever the dash end clears the wall (gap < 22.34 px: starts 0.0, 0.5, ... 22.0).
    EXPECT_EQ(tunnelledByOldCode, 45) << "the sweep must include the starts the old code tunnelled";
}

TEST(BodyMoveTest, DashAlongADiagonalSlidesAlongTheWallInsteadOfStopping) {
    const Walls w = OneCellWallAt(200.0F);
    glm::vec2 body{150.0F, 0.0F};
    const glm::vec2 dir = glm::normalize(glm::vec2{1.0F, 1.0F});
    ApplyDash(w, body, dir, kSkillDashPx, kR);
    EXPECT_LT(body.x + kR, 184.0F);
    EXPECT_GT(body.y, 40.0F) << "the y component keeps sliding along the wall face";
    EXPECT_FALSE(w.Blocks(body, kR));
}

TEST(BodyMoveTest, DashStartingInsideABlockerFirstSteppesOutThenDashes) {
    const Walls w({Collider::MakeAABB({0.0F, 0.0F}, {32.0F, 32.0F})});
    glm::vec2 body{4.0F, 0.0F}; // inside the box (a spawn / door seal left it overlapping)
    ASSERT_TRUE(w.Blocks(body, kR));
    ApplyDash(w, body, {0.0F, 1.0F}, kSkillDashPx, kR); // up
    EXPECT_FALSE(w.Blocks(body, kR));
    EXPECT_GT(body.y, 16.0F + kR) << "ends above the box";
}

// AC5 (EARS): if a frame's dt (hence displacement) exceeds the bound, no actor crosses a blocker.
TEST(BodyMoveTest, HugeDisplacementNeverCrossesAOneCellWall) {
    const Walls w = OneCellWallAt(100.0F);
    for (const float len : {50.0F, 500.0F, 5000.0F, 1.0e6F, 1.0e12F}) {
        const glm::vec2 out = MoveBody(w, {0.0F, 0.0F}, {len, 0.0F}, kR);
        EXPECT_LT(out.x + kR, 84.0F) << "len " << len;
        EXPECT_FALSE(w.Blocks(out, kR));
    }
    // Diagonal huge step, every direction of a circle.
    for (int k = 0; k < 64; ++k) {
        const float a = 6.2831853F * static_cast<float>(k) / 64.0F;
        const glm::vec2 d{std::cos(a) * 3000.0F, std::sin(a) * 3000.0F};
        const glm::vec2 out = MoveBody(w, {0.0F, 0.0F}, d, kR);
        EXPECT_FALSE(w.Blocks(out, kR)) << "k=" << k;
        EXPECT_LT(out.x + kR, 84.0F) << "k=" << k;
    }
}

TEST(BodyMoveTest, NonFiniteDeltaDoesNothing) {
    const NullWorldCollision open;
    const glm::vec2 p{3.0F, 4.0F};
    EXPECT_EQ(MoveBody(open, p, {std::numeric_limits<float>::quiet_NaN(), 0.0F}, kR), p);
    EXPECT_EQ(MoveBody(open, p, {0.0F, std::numeric_limits<float>::infinity()}, kR), p);
}

TEST(BodyMoveTest, WalkingIntoAWallStopsShortAndSlidesAlongIt) {
    const Walls w = OneCellWallAt(100.0F); // face at x=84
    // A walk long enough to need sub-steps (a late frame): ends against the wall, y still advances.
    const glm::vec2 out = MoveBody(w, {40.0F, 0.0F}, {200.0F, 60.0F}, kR);
    EXPECT_LT(out.x + kR, 84.0F);
    EXPECT_GT(out.x + kR, 84.0F - kR - 0.1F) << "stops within one sub-step (body radius) of the face";
    EXPECT_NEAR(out.y, 60.0F, 1e-3F) << "free axis keeps its full travel";
}

TEST(BodyMoveTest, ClampFrameDtMs) {
    EXPECT_EQ(kMaxFrameDtMs, 160.0F) << "= kMaxStepsPerAdvance (8) fixed steps of 20 ms";
    EXPECT_EQ(ClampFrameDtMs(16.7F), 16.7F);
    EXPECT_EQ(ClampFrameDtMs(160.0F), 160.0F);
    EXPECT_EQ(ClampFrameDtMs(161.0F), 160.0F);
    EXPECT_EQ(ClampFrameDtMs(5000.0F), 160.0F);
    EXPECT_EQ(ClampFrameDtMs(std::numeric_limits<float>::infinity()), 160.0F);
    EXPECT_EQ(ClampFrameDtMs(-5.0F), 0.0F);
    EXPECT_EQ(ClampFrameDtMs(0.0F), 0.0F);
    EXPECT_EQ(ClampFrameDtMs(std::numeric_limits<float>::quiet_NaN()), 0.0F);
}

TEST(BodyMoveTest, ClampedWalkPerFrameIsAboutOneCellAtEveryHeroSpeed) {
    // All 13 heroes walk 6.5 u/s (characters.json) = 208 px/s: a clamped 160 ms frame is 33.3 px,
    // below one cell + body (32 + 2 * 12.8 = 57.6), so even without sub-steps it could not skip a wall.
    const float heroPxPerSec = 6.5F * 32.0F;
    EXPECT_NEAR(heroPxPerSec * kMaxFrameDtMs / 1000.0F, 33.28F, 0.01F);
    EXPECT_LT(heroPxPerSec * kMaxFrameDtMs / 1000.0F, 32.0F + 2.0F * 12.8F);
}

// NOLINTEND(readability-magic-numbers)
