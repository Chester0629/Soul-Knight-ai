#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include <glm/glm.hpp>

#include "Util/Collider.hpp"
#include "sim/Depenetration.hpp"
#include "sim/WorldCollision.hpp"

using Game::Sim::DepenetrateAgainst;
using Game::Sim::kPushOutEpsPx;
using Game::Sim::NullWorldCollision;
using Game::Sim::WorldCollision;
using Util::Collider;

// NOLINTBEGIN(readability-magic-numbers)

// P2.18 AC2 (EARS): while any actor's body overlaps a blocker after any position change, the
// system shall resolve it within that same tick to a non-overlapping position at minimal
// displacement. These tests pin the geometric solver (Game::Sim::DepenetrateAgainst) and the
// generic WorldCollision::Depenetrate on shapes the real rooms produce.

namespace {
constexpr float kCell = 32.0F;
constexpr float kR = 12.8F; // the knight's body circle (c01 collider r 0.4 u)

bool Overlaps(const std::vector<Collider> &bs, glm::vec2 p, float r) {
    const Collider c = Collider::MakeCircle(p, r);
    for (const Collider &b : bs) {
        if (Util::Overlap(b, c)) {
            return true;
        }
    }
    return false;
}

float Dist(glm::vec2 a, glm::vec2 b) {
    const glm::vec2 d = a - b;
    return std::sqrt(d.x * d.x + d.y * d.y);
}

Collider Cell(int cx, int cy) { // one 32x32 blocker cell centred on integer cell coords
    return Collider::MakeAABB(glm::vec2{cx * kCell, cy * kCell}, glm::vec2{kCell, kCell});
}

// A world made of static colliders, with the exact solver wired in like GameScene does.
class StaticWorld : public WorldCollision {
public:
    explicit StaticWorld(std::vector<Collider> bs, bool exact = true)
        : m_Blockers(std::move(bs)), m_Exact(exact) {}
    bool Blocks(glm::vec2 pos, float radius) const override { return Overlaps(m_Blockers, pos, radius); }
    glm::vec2 Depenetrate(glm::vec2 pos, float radius) const override {
        if (!m_Exact) {
            return WorldCollision::Depenetrate(pos, radius); // the generic sampled search
        }
        return DepenetrateAgainst(m_Blockers, pos, radius);
    }

private:
    std::vector<Collider> m_Blockers;
    bool m_Exact;
};
} // namespace

TEST(DepenetrationTest, FreeBodyIsReturnedUnchanged) {
    const std::vector<Collider> bs{Cell(0, 0)};
    const glm::vec2 p{100.0F, 0.0F};
    EXPECT_EQ(DepenetrateAgainst(bs, p, kR), p);
}

TEST(DepenetrationTest, EdgeOverlapIsPushedAlongTheNormalByTheOverlapDepth) {
    // Box [-16,16]^2; the body centre is 10 px right of the box's right face (x=26): 12.8 - 10 = 2.8 deep.
    const std::vector<Collider> bs{Cell(0, 0)};
    const glm::vec2 p{26.0F, 0.0F};
    ASSERT_TRUE(Overlaps(bs, p, kR));
    const glm::vec2 out = DepenetrateAgainst(bs, p, kR);
    EXPECT_FALSE(Overlaps(bs, out, kR));
    EXPECT_NEAR(out.y, 0.0F, 1e-4F) << "pure normal push, no sideways drift";
    EXPECT_NEAR(Dist(out, p), 2.8F + kPushOutEpsPx, 0.01F) << "minimal translation = depth + epsilon";
}

TEST(DepenetrationTest, CornerOverlapIsPushedAlongTheDiagonalNormal) {
    const std::vector<Collider> bs{Cell(0, 0)};
    const glm::vec2 corner{16.0F, 16.0F};
    const glm::vec2 p = corner + glm::vec2{5.0F, 5.0F}; // 7.07 px from the corner point
    ASSERT_TRUE(Overlaps(bs, p, kR));
    const glm::vec2 out = DepenetrateAgainst(bs, p, kR);
    EXPECT_FALSE(Overlaps(bs, out, kR));
    // Along the corner->centre direction, ending exactly r + eps from the corner point.
    EXPECT_NEAR(Dist(out, corner), kR + kPushOutEpsPx, 0.01F);
    EXPECT_NEAR(out.x - corner.x, out.y - corner.y, 1e-3F);
}

TEST(DepenetrationTest, ActorFullyInsideABlockerExitsThroughTheNearestFace) {
    const std::vector<Collider> bs{Cell(0, 0)};
    const glm::vec2 p{-9.0F, 2.0F}; // inside the box, 7 px from the left face (x=-16)
    ASSERT_TRUE(Overlaps(bs, p, kR));
    const glm::vec2 out = DepenetrateAgainst(bs, p, kR);
    EXPECT_FALSE(Overlaps(bs, out, kR));
    EXPECT_NEAR(out.x, -16.0F - kR - kPushOutEpsPx, 1e-3F) << "left face is nearest";
    EXPECT_NEAR(out.y, 2.0F, 1e-4F);
    // exactly the minimal exit: 7 px to the face + r + eps
    EXPECT_NEAR(Dist(out, p), 7.0F + kR + kPushOutEpsPx, 1e-3F);
}

TEST(DepenetrationTest, ActorDeadCentreInsideABoxStillResolves) {
    const std::vector<Collider> bs{Cell(3, -2)};
    const glm::vec2 p = bs[0].center; // the worst case: tied faces
    const glm::vec2 out = DepenetrateAgainst(bs, p, kR);
    EXPECT_FALSE(Overlaps(bs, out, kR));
    EXPECT_NEAR(Dist(out, p), 16.0F + kR + kPushOutEpsPx, 1e-3F);
}

TEST(DepenetrationTest, TwoBlockersDoNotPushTheActorBackAndForth) {
    // A row of two boxes [A][B]: the body is inside A near the shared edge, so the NEAREST face
    // (right, into B) is blocked by B -- the exit must be a face that is free, i.e. top or bottom.
    const std::vector<Collider> bs{Cell(0, 0), Cell(1, 0)};
    const glm::vec2 p{12.0F, 3.0F}; // 4 px from A's right face (x=16), inside A, overlapping B too
    ASSERT_TRUE(Overlaps(bs, p, kR));
    const glm::vec2 out = DepenetrateAgainst(bs, p, kR);
    EXPECT_FALSE(Overlaps(bs, out, kR));
    // y exit: the top face of the row is y=16 -> y = 16 + r + eps = 28.85, a 25.85 px move.
    EXPECT_NEAR(out.x, 12.0F, 1e-4F);
    EXPECT_NEAR(out.y, 16.0F + kR + kPushOutEpsPx, 1e-3F);
}

TEST(DepenetrationTest, TwoBlockersOverlappingTheBodyAtOnceAreBothClearedByOneResult) {
    // Two boxes side by side with a 6 px slot between them (too narrow for the 25.6 px body):
    // the body sits in the slot and overlaps both; the nearest free spot is above or below.
    const std::vector<Collider> bs{
        Collider::MakeAABB({-19.0F, 0.0F}, {32.0F, 64.0F}), // spans x -35..-3
        Collider::MakeAABB({19.0F, 0.0F}, {32.0F, 64.0F})}; // spans x 3..35
    const glm::vec2 p{0.0F, 20.0F};
    ASSERT_TRUE(Overlaps(bs, p, kR));
    const glm::vec2 out = DepenetrateAgainst(bs, p, kR);
    EXPECT_FALSE(Overlaps(bs, out, kR));
    EXPECT_NEAR(out.x, 0.0F, 1e-3F);
    EXPECT_NEAR(out.y, 32.0F + kR + kPushOutEpsPx, 1e-3F) << "exits the slot through its nearer end (top)";
}

TEST(DepenetrationTest, OneCellCorridorActorOverlappingTheWallStepsBackIntoTheCorridor) {
    // A horizontal 1-cell corridor (y in [-16,16]) walled by a row of cells above and below.
    std::vector<Collider> bs;
    for (int x = -6; x <= 6; ++x) {
        bs.push_back(Cell(x, 1));  // north flank, spans y 16..48
        bs.push_back(Cell(x, -1)); // south flank, spans y -48..-16
    }
    const glm::vec2 p{40.0F, 22.0F}; // 6 px into the north flank
    ASSERT_TRUE(Overlaps(bs, p, kR));
    const glm::vec2 out = DepenetrateAgainst(bs, p, kR);
    EXPECT_FALSE(Overlaps(bs, out, kR));
    // The corridor is 32 wide, the body 25.6: the free band is y in (-3.2, 3.2) exclusive of eps.
    EXPECT_LT(std::abs(out.y), 16.0F - kR + 1e-3F);
    EXPECT_NEAR(out.x, 40.0F, 1e-3F) << "moves across the corridor, not along it";
    EXPECT_LT(Dist(out, p), 20.0F);
}

TEST(DepenetrationTest, ActorInsideAOneCellWallOfACorridorLandsInTheCorridorNotTheFarSide) {
    // Same corridor; the body is dead centre in the NORTH flank cell (y 32): the near free spot is
    // the corridor (south, ~y 3), not beyond the 2-cell-thick wall above (there is none: the row is
    // one cell thick, so the open void above is also free and is 16+r away, farther than the corridor).
    std::vector<Collider> bs;
    for (int x = -6; x <= 6; ++x) {
        bs.push_back(Cell(x, 1));
        bs.push_back(Cell(x, -1));
    }
    const glm::vec2 p{0.0F, 32.0F}; // dead centre of the cell spanning y 16..48
    const glm::vec2 out = DepenetrateAgainst(bs, p, kR);
    EXPECT_FALSE(Overlaps(bs, out, kR));
    // Exits: down to y = 16 - r - eps = 3.15 (28.85 away) or up to y = 48 + r + eps = 60.85
    // (28.85 away) -- a tie; either is a legal minimal exit. Both are free.
    EXPECT_NEAR(Dist(out, p), 16.0F + kR + kPushOutEpsPx, 1e-3F);
}

TEST(DepenetrationTest, CircleBlockerPushesRadially) {
    const std::vector<Collider> bs{Collider::MakeCircle({0.0F, 0.0F}, 16.0F)};
    const glm::vec2 p{0.0F, 20.0F}; // 20 from centre, needs 16 + 12.8 = 28.8
    ASSERT_TRUE(Overlaps(bs, p, kR));
    const glm::vec2 out = DepenetrateAgainst(bs, p, kR);
    EXPECT_FALSE(Overlaps(bs, out, kR));
    EXPECT_NEAR(out.x, 0.0F, 1e-4F);
    EXPECT_NEAR(out.y, 16.0F + kR + kPushOutEpsPx, 1e-3F);
}

TEST(DepenetrationTest, CircleBlockerDeadCentreResolves) {
    const std::vector<Collider> bs{Collider::MakeCircle({5.0F, 5.0F}, 16.0F)};
    const glm::vec2 out = DepenetrateAgainst(bs, {5.0F, 5.0F}, kR);
    EXPECT_FALSE(Overlaps(bs, out, kR));
    EXPECT_NEAR(Dist(out, {5.0F, 5.0F}), 16.0F + kR + kPushOutEpsPx, 1e-3F);
}

TEST(DepenetrationTest, BigBodyAgainstAStatueSizedBox) {
    // statue_01: a 2x2-unit box (64x64). A 32 px (boss11) body overlapping its side.
    const std::vector<Collider> bs{Collider::MakeAABB({0.0F, 0.0F}, {64.0F, 64.0F})};
    const glm::vec2 p{40.0F, 10.0F};
    ASSERT_TRUE(Overlaps(bs, p, 32.0F));
    const glm::vec2 out = DepenetrateAgainst(bs, p, 32.0F);
    EXPECT_FALSE(Overlaps(bs, out, 32.0F));
    EXPECT_NEAR(out.x, 32.0F + 32.0F + kPushOutEpsPx, 1e-3F);
}

TEST(DepenetrationTest, PocketNarrowerThanTheBodyIsResolvedBySearchNotLeftOverlapping) {
    // A 20 px wide vertical slot between two tall walls: nothing inside fits a 25.6 px body, and the
    // slot is 192 px long, so the exits-only solve cannot leave it nearby; the result must still be
    // free of every blocker (resolved) and deterministic.
    const std::vector<Collider> bs{
        Collider::MakeAABB({-26.0F, 0.0F}, {32.0F, 200.0F}), // x -42..-10
        Collider::MakeAABB({26.0F, 0.0F}, {32.0F, 200.0F})}; // x 10..42
    bool resolved = false;
    const glm::vec2 p{0.0F, 0.0F};
    const glm::vec2 out = DepenetrateAgainst(bs, p, kR, &resolved);
    EXPECT_TRUE(resolved);
    EXPECT_FALSE(Overlaps(bs, out, kR));
    const glm::vec2 out2 = DepenetrateAgainst(bs, p, kR, &resolved);
    EXPECT_EQ(out, out2) << "deterministic";
}

TEST(DepenetrationTest, PropertySweepAlwaysEndsFreeAndNeverMovesFartherThanTheNearestClearing) {
    // Deterministic scatter of blockers (a room-like cluster) and starting points; whenever the
    // solver reports resolved, the result is free, and an already-free start never moves.
    std::vector<Collider> bs;
    for (int x = -4; x <= 4; ++x) {
        for (int y = -4; y <= 4; ++y) {
            if ((x * 7 + y * 13 + 100) % 5 == 0) {
                bs.push_back(Cell(x, y));
            }
        }
    }
    int overlapping = 0;
    int resolvedCount = 0;
    unsigned lcg = 12345U;
    const auto next = [&lcg]() {
        lcg = lcg * 1664525U + 1013904223U;
        return static_cast<float>((lcg >> 8) & 0xFFFF) / 65535.0F;
    };
    for (int i = 0; i < 4000; ++i) {
        const glm::vec2 p{-160.0F + next() * 320.0F, -160.0F + next() * 320.0F};
        const float r = 8.0F + next() * 24.0F;
        bool resolved = false;
        const glm::vec2 out = DepenetrateAgainst(bs, p, r, &resolved);
        if (!Overlaps(bs, p, r)) {
            EXPECT_EQ(out, p);
            continue;
        }
        ++overlapping;
        if (resolved) {
            ++resolvedCount;
            EXPECT_FALSE(Overlaps(bs, out, r)) << "p=(" << p.x << "," << p.y << ") r=" << r;
            EXPECT_LT(Dist(out, p), 64.0F + r + 4.0F) << "in this scatter an exit is never farther than one cluster cell + body";
        }
    }
    EXPECT_GT(overlapping, 500) << "the scatter must actually exercise the solver";
    EXPECT_EQ(resolvedCount, overlapping) << "every pocket in this scatter has a way out";
}

TEST(DepenetrationTest, GenericWorldSearchResolvesAndMatchesTheExactSolverToAFewPixels) {
    const std::vector<Collider> bs{Cell(0, 0), Cell(1, 0), Cell(0, 1), Cell(1, 1)}; // a 2x2 statue
    const StaticWorld generic(bs, /*exact=*/false);
    const StaticWorld exact(bs, /*exact=*/true);
    for (const glm::vec2 p : {glm::vec2{0.0F, 0.0F}, glm::vec2{20.0F, 20.0F}, glm::vec2{-10.0F, 40.0F},
                              glm::vec2{40.0F, 16.0F}}) {
        ASSERT_TRUE(generic.Blocks(p, kR));
        const glm::vec2 g = generic.Depenetrate(p, kR);
        const glm::vec2 e = exact.Depenetrate(p, kR);
        EXPECT_FALSE(generic.Blocks(g, kR)) << "generic result must be free";
        EXPECT_FALSE(exact.Blocks(e, kR));
        // The sampled search finds a free point no farther than the exact minimum plus its ring step.
        EXPECT_LT(Dist(g, p), Dist(e, p) + 3.0F) << "p=(" << p.x << "," << p.y << ")";
    }
}

TEST(DepenetrationTest, NullWorldAndFreeBodiesNeverMove) {
    const NullWorldCollision open;
    EXPECT_EQ(open.Depenetrate({5.0F, 6.0F}, kR), (glm::vec2{5.0F, 6.0F}));
    const StaticWorld w({Cell(0, 0)});
    EXPECT_EQ(w.Depenetrate({200.0F, 200.0F}, kR), (glm::vec2{200.0F, 200.0F}));
}

// NOLINTEND(readability-magic-numbers)
