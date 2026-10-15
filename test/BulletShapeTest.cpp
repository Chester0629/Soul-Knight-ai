// P3.0c: bullet visuals + collision volume. Pinned here:
//   (a) a bullet's collision shape is its PREFAB collider (shape, size, offset x scale, box rotation)
//       oriented with the bullet heading -- BulletShape / MakeBulletShape / PlaceShape;
//   (b) the swept hurtbox test of an oriented box / offset circle (BodyGeom::HurtSweepShape) is exact:
//       checked against an INDEPENDENT brute-force oracle (SAT overlap, sub-stepping) on 24000
//       random configurations, plus the AC2 counterexamples (hits with the oriented box but misses with
//       the old inscribed circle, and the other way round) built from the real weapon_000 prefab data;
//   (c) bullets carry their heading (atan2 of the aim at spawn, plus the Bullet01 / RGSBullet01 spin per
//       fixed step), their prefab row and their shape; enemy / boss bullets resolve their OWN prefab
//       (bullet_e_1 for e_orc01 / boss01), the generic bullet_e_1 stands in for species with no
//       RGBullet-family bullet (T-66), and nothing changes when the sim has no GameData (legacy 4 px);
//   (d) wall / obstacle contact uses the same shape (weapon_000's box front reaches 16 px, the old
//       inscribed circle 6.4 px) and records a BulletImpact for the hit-effect view;
//   (e) the view pose math (heading + child offset / rotation / flip) and the corpse state choice.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "SweptOracle.hpp"
#include "Util/Collider.hpp"
#include "data/GameData.hpp"
#include "entities/BulletPose.hpp"
#include "sim/BodyGeom.hpp"
#include "sim/BulletShape.hpp"
#include "sim/CoverCircles.hpp"
#include "sim/Depenetration.hpp"
#include "sim/SimConfig.hpp"
#include "sim/Simulation.hpp"
#include "sim/SweptCollision.hpp"
#include "sim/WorldCollision.hpp"
#include "sim/WorldInputs.hpp"

using Game::Sim::BodyGeom;
using Game::Sim::BulletShape;
using Game::Sim::BulletShapeWorld;
using Game::Sim::MakeBodyGeom;
using Game::Sim::MakeBulletShape;
using Game::Sim::PlaceShape;
namespace oracle = sktest::oracle;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
const char *kResourceRoot = RESOURCE_DIR;
constexpr float kPi = 3.14159265358979F;
Game::Sim::NullWorldCollision g_NullWorld;

struct Data {
    Game::GameData gd;
    bool Load() { return gd.LoadAll(kResourceRoot); }
};

// A vertical slab wall x in [x0, x1] (any y); records DamageObstacle calls.
class SlabWorld : public Game::Sim::WorldCollision {
public:
    SlabWorld(float x0, float x1) : m_X0(x0), m_X1(x1) {}
    bool Blocks(glm::vec2 pos, float r) const override { return pos.x + r >= m_X0 && pos.x - r <= m_X1; }
    void DamageObstacle(glm::vec2 pos, float radius) override { hits.emplace_back(pos, radius); }
    std::vector<std::pair<glm::vec2, float>> hits;

private:
    float m_X0;
    float m_X1;
};

Game::Sim::WorldInputs Fire(glm::vec2 aim, int room = 1) {
    Game::Sim::WorldInputs in;
    in.playerPos = glm::vec2{0.0F, 0.0F};
    in.aimDir = aim;
    in.firing = true;
    in.playerRoomId = room; // != 0 keeps every room-0 enemy asleep
    return in;
}

// weapon_000 with no scatter, 10 u/s so bullets crawl 6.4 px per step (easy to place targets).
Game::WeaponDef Starter(const Game::GameData &gd) {
    Game::WeaponDef d = *gd.FindWeapon("weapon_000");
    d.deviation = 0;
    d.bulletSpeed = 10.0F;
    return d;
}

struct Lcg {
    std::uint32_t s;
    float Next() {
        s = s * 1664525U + 1013904223U;
        return static_cast<float>(s >> 8) / 16777216.0F;
    }
    float Range(float lo, float hi) { return lo + (hi - lo) * Next(); }
};

// ---- an oracle for ORIENTED shapes, written separately from the production Minkowski code --------
using V = oracle::Vec;
V Rot(V v, double a) { return {v.x * std::cos(a) - v.y * std::sin(a), v.x * std::sin(a) + v.y * std::cos(a)}; }

// Discrete overlap (inclusive) of a bullet shape (heading h, oriented box or offset circle) whose
// origin is @p o with the hurtbox of a body at @p target.
bool TouchesOriented(const BodyGeom &g, const BulletShape &s, double heading, V o, V target) {
    const V off = Rot({s.offsetPx.x, s.offsetPx.y}, heading);
    const V c{o.x + off.x, o.y + off.y};
    const V t{target.x + g.hurtOffsetPx.x, target.y + g.hurtOffsetPx.y};
    if (s.kind == BulletShape::Kind::Circle) {
        return oracle::Touches(g, {c.x, c.y}, s.radiusPx, target);
    }
    const double ang = heading + s.boxRotRad;
    if (!g.hurtIsBox) {
        // circle centre in the box frame, distance outside the box
        const V d = Rot({t.x - c.x, t.y - c.y}, -ang);
        const double ox = std::max(std::fabs(d.x) - s.halfPx.x, 0.0);
        const double oy = std::max(std::fabs(d.y) - s.halfPx.y, 0.0);
        return ox * ox + oy * oy <= static_cast<double>(g.hurtRadiusPx) * g.hurtRadiusPx;
    }
    // box vs box: separating-axis test on the 4 face normals
    const double hx = g.hurtHalfPx.x;
    const double hy = g.hurtHalfPx.y;
    const V axes[4] = {{1, 0}, {0, 1}, {std::cos(ang), std::sin(ang)}, {-std::sin(ang), std::cos(ang)}};
    const V d{t.x - c.x, t.y - c.y};
    for (const V &a : axes) {
        const double rt = hx * std::fabs(a.x) + hy * std::fabs(a.y);
        const double rb = s.halfPx.x * std::fabs(a.x * std::cos(ang) + a.y * std::sin(ang)) +
                          s.halfPx.y * std::fabs(-a.x * std::sin(ang) + a.y * std::cos(ang));
        if (std::fabs(d.x * a.x + d.y * a.y) > rt + rb) {
            return false;
        }
    }
    return true;
}

int FirstTouchOriented(const BodyGeom &g, const BulletShape &s, double heading, glm::vec2 pos0,
                       glm::vec2 pos1, glm::vec2 b0, glm::vec2 b1) {
    for (int j = 0; j <= oracle::kSubSteps; ++j) {
        const double t = static_cast<double>(j) / static_cast<double>(oracle::kSubSteps);
        const oracle::Vec o = oracle::Lerp(b0, b1, t);
        const oracle::Vec tg = oracle::Lerp(pos0, pos1, t);
        if (TouchesOriented(g, s, heading, {o.x, o.y}, {tg.x, tg.y})) {
            return j;
        }
    }
    return -1;
}
} // namespace

// ---- (a) shape ---------------------------------------------------------------------------------
TEST(BulletShapeTest, Weapon000ShapeIsThePrefabBoxWithItsForwardOffset) {
    Data d;
    ASSERT_TRUE(d.Load());
    const Game::BulletDef *bd = d.gd.BulletForWeapon(*d.gd.FindWeapon("weapon_000"));
    ASSERT_NE(bd, nullptr);
    EXPECT_EQ(bd->id, "bullet_0");
    const BulletShape s = MakeBulletShape(bd->collider);
    ASSERT_EQ(s.kind, BulletShape::Kind::Box);
    EXPECT_FLOAT_EQ(s.halfPx.x, 8.0F);   // 0.5 u / 2 * 32
    EXPECT_FLOAT_EQ(s.halfPx.y, 6.4F);   // 0.4 u / 2 * 32
    EXPECT_FLOAT_EQ(s.offsetPx.x, 8.0F); // m_Offset (0.25, 0) * 32: the box starts AT the origin
    EXPECT_FLOAT_EQ(s.offsetPx.y, 0.0F);
    EXPECT_FLOAT_EQ(s.boxRotRad, 0.0F);
    EXPECT_FLOAT_EQ(s.InscribedRadius(), 6.4F); // what the old EquivalentRadiusPx kept
    EXPECT_FLOAT_EQ(Game::Sim::EquivalentRadiusPx(bd->collider), 6.4F);
    EXPECT_NEAR(s.BoundRadius(), 8.0F + std::sqrt(8.0F * 8.0F + 6.4F * 6.4F), 1e-4F);
}

TEST(BulletShapeTest, RotatedPrefabBoxKeepsItsAxesRotation) {
    Data d;
    ASSERT_TRUE(d.Load());
    const Game::BulletDef *b = d.gd.FindBullet("_bullet_4"); // child rotated 90 deg
    ASSERT_NE(b, nullptr);
    EXPECT_FLOAT_EQ(b->collider.rotDeg, 90.0F);
    const BulletShape s = MakeBulletShape(b->collider);
    EXPECT_NEAR(s.boxRotRad, kPi / 2.0F, 1e-6F);
}

TEST(BulletShapeTest, PlaceShapeTurnsTheOffsetAndTheBoxAxisWithTheHeading) {
    BulletShape s;
    s.kind = BulletShape::Kind::Box;
    s.halfPx = {8.0F, 6.4F};
    s.offsetPx = {8.0F, 0.0F};
    s.boxRotRad = 0.0F;
    for (float h : {0.0F, kPi / 2.0F, kPi, -kPi / 2.0F, 0.7F}) {
        const BulletShapeWorld w = PlaceShape(s, h);
        EXPECT_NEAR(w.centreOffset.x, 8.0F * std::cos(h), 1e-4F) << h;
        EXPECT_NEAR(w.centreOffset.y, 8.0F * std::sin(h), 1e-4F) << h;
        EXPECT_NEAR(w.boxAngle, h, 1e-6F);
    }
    s.boxRotRad = kPi / 2.0F;
    EXPECT_NEAR(PlaceShape(s, 0.3F).boxAngle, 0.3F + kPi / 2.0F, 1e-6F);
}

TEST(BulletShapeTest, BulletLaunchSpinIsOnlyForBullet01AndRGSBullet01) {
    Data d;
    ASSERT_TRUE(d.Load());
    const Game::BulletDef *e3 = d.gd.FindBullet("bullet_e_3"); // Bullet01, rotate_angle 6
    ASSERT_NE(e3, nullptr);
    EXPECT_EQ(e3->bulletClass, "Bullet01");
    EXPECT_FLOAT_EQ(Game::Sim::MakeBulletLaunch(*e3, 0).spinDegPerStep, 6.0F);
    const Game::BulletDef *b0 = d.gd.FindBullet("bullet_0");
    EXPECT_FLOAT_EQ(Game::Sim::MakeBulletLaunch(*b0, 0).spinDegPerStep, 0.0F);
    const Game::BulletDef *neg = d.gd.FindBullet("bullet_bottle_2"); // rotate_angle -17
    ASSERT_NE(neg, nullptr);
    EXPECT_FLOAT_EQ(Game::Sim::MakeBulletLaunch(*neg, 0).spinDegPerStep, -17.0F);
    // a curving class (Bullet05 / BulletFollow / BulletParabola) is NOT spun straight: not modelled
    for (const Game::BulletDef &b : d.gd.Bullets()) {
        if (b.bulletClass != "Bullet01" && b.bulletClass != "RGSBullet01") {
            EXPECT_FLOAT_EQ(Game::Sim::MakeBulletLaunch(b, 0).spinDegPerStep, 0.0F) << b.id;
        }
    }
}

// ---- primitives --------------------------------------------------------------------------------
TEST(SweptPrimitiveTest, SegmentConvexEntryClipsAgainstTheEdges) {
    using Game::Sim::Swept::SegmentConvexEntry;
    const glm::vec2 sq[4] = {{-10.0F, -10.0F}, {10.0F, -10.0F}, {10.0F, 10.0F}, {-10.0F, 10.0F}};
    float t = -1.0F;
    EXPECT_TRUE(SegmentConvexEntry({-30.0F, 0.0F}, {30.0F, 0.0F}, sq, 4, t));
    EXPECT_NEAR(t, 20.0F / 60.0F, 1e-6F);
    EXPECT_TRUE(SegmentConvexEntry({0.0F, 0.0F}, {50.0F, 0.0F}, sq, 4, t)); // starts inside
    EXPECT_FLOAT_EQ(t, 0.0F);
    EXPECT_TRUE(SegmentConvexEntry({-30.0F, 10.0F}, {30.0F, 10.0F}, sq, 4, t));  // grazing the top edge
    EXPECT_FALSE(SegmentConvexEntry({-30.0F, 10.01F}, {30.0F, 10.01F}, sq, 4, t));
    EXPECT_FALSE(SegmentConvexEntry({-30.0F, 0.0F}, {-20.0F, 0.0F}, sq, 4, t));  // stops short
    EXPECT_FALSE(SegmentConvexEntry({0.0F, 30.0F}, {0.0F, 30.0F}, sq, 4, t));    // degenerate, outside
    EXPECT_TRUE(SegmentConvexEntry({0.0F, 5.0F}, {0.0F, 5.0F}, sq, 4, t));       // degenerate, inside
}

TEST(SweptPrimitiveTest, BoxSumOfAlignedBoxesIsTheSummedRectangle) {
    using Game::Sim::Swept::SegmentBoxSumEntry;
    float t = -1.0F;
    // hurtbox half (16, 16) + bullet box half (8, 6.4) at angle 0: a (24, 22.4) rectangle.
    EXPECT_TRUE(SegmentBoxSumEntry({-60.0F, 0.0F}, {60.0F, 0.0F}, {16.0F, 16.0F}, {8.0F, 6.4F}, 0.0F, t));
    EXPECT_NEAR(t, 36.0F / 120.0F, 1e-5F);
    EXPECT_TRUE(SegmentBoxSumEntry({-60.0F, 22.3F}, {60.0F, 22.3F}, {16.0F, 16.0F}, {8.0F, 6.4F}, 0.0F, t));
    EXPECT_FALSE(SegmentBoxSumEntry({-60.0F, 22.5F}, {60.0F, 22.5F}, {16.0F, 16.0F}, {8.0F, 6.4F}, 0.0F, t));
    // turned 90 deg the bullet box is (6.4, 8) tall: the rectangle is (22.4, 24).
    EXPECT_TRUE(SegmentBoxSumEntry({-60.0F, 23.9F}, {60.0F, 23.9F}, {16.0F, 16.0F}, {8.0F, 6.4F}, kPi / 2.0F, t));
    EXPECT_FALSE(SegmentBoxSumEntry({-60.0F, 24.1F}, {60.0F, 24.1F}, {16.0F, 16.0F}, {8.0F, 6.4F}, kPi / 2.0F, t));
}

// ---- (b) AC2 counterexamples + the oracle -------------------------------------------------------
namespace {
BodyGeom CircleGeom(float r) {
    BodyGeom g;
    g.hurtIsBox = false;
    g.hurtRadiusPx = r;
    return g;
}
} // namespace

TEST(BulletShapeAC2Test, OrientedBoxHitsWhereTheOldInscribedCircleMisses) {
    Data d;
    ASSERT_TRUE(d.Load());
    const BulletShape s = MakeBulletShape(d.gd.BulletForWeapon(*d.gd.FindWeapon("weapon_000"))->collider);
    const BodyGeom g = CircleGeom(16.0F); // a 16 px circular hurtbox at (30, 0), the bullet flies +x
    const glm::vec2 target{30.0F, 0.0F};
    float t = 0.0F;
    // NEW: the box reaches 16 px ahead of the origin -> touches a hurtbox whose near edge is at 14.
    EXPECT_TRUE(g.HurtSweepShape(target, target, {0.0F, 0.0F}, {0.0F, 0.0F}, PlaceShape(s, 0.0F), t));
    // OLD: the inscribed circle (r 6.4) at the origin only reaches 6.4 px -> 16 + 6.4 = 22.4 < 30.
    EXPECT_FALSE(g.HurtSweep(target, target, {0.0F, 0.0F}, {0.0F, 0.0F}, s.InscribedRadius(), t));
}

TEST(BulletShapeAC2Test, OldInscribedCircleHitsWhereTheOrientedBoxMisses) {
    Data d;
    ASSERT_TRUE(d.Load());
    const BulletShape s = MakeBulletShape(d.gd.BulletForWeapon(*d.gd.FindWeapon("weapon_000"))->collider);
    const BodyGeom g = CircleGeom(16.0F);
    const glm::vec2 target{-20.0F, 0.0F}; // BEHIND the bullet (it flies +x)
    float t = 0.0F;
    // OLD: the circle sits on the origin, 6.4 + 16 = 22.4 >= 20 -> a hit on something the bullet has
    // already left. NEW: the box starts at the origin (x in [0, 16]): the nearest point is 20 away.
    EXPECT_TRUE(g.HurtSweep(target, target, {0.0F, 0.0F}, {0.0F, 0.0F}, s.InscribedRadius(), t));
    EXPECT_FALSE(g.HurtSweepShape(target, target, {0.0F, 0.0F}, {0.0F, 0.0F}, PlaceShape(s, 0.0F), t));
}

TEST(BulletShapeAC2Test, TheHeadingDecidesWhichSideTheLongAxisPoints) {
    Data d;
    ASSERT_TRUE(d.Load());
    const BulletShape s = MakeBulletShape(d.gd.BulletForWeapon(*d.gd.FindWeapon("weapon_000"))->collider);
    const BodyGeom g = CircleGeom(16.0F);
    const glm::vec2 above{0.0F, 30.0F};
    float t = 0.0F;
    // Flying UP the box reaches 16 px up (30 - 16 = 14 <= 16): hit. Flying along +x the same target
    // is only reachable laterally (6.4 + 16 = 22.4 < 30): miss.
    EXPECT_TRUE(g.HurtSweepShape(above, above, {0.0F, 0.0F}, {0.0F, 0.0F}, PlaceShape(s, kPi / 2.0F), t));
    EXPECT_FALSE(g.HurtSweepShape(above, above, {0.0F, 0.0F}, {0.0F, 0.0F}, PlaceShape(s, 0.0F), t));
}

TEST(BulletShapeAC2Test, SphereOnlyLegacyShapeIsBitIdenticalToHurtSweep) {
    // A circle with no offset must reproduce the legacy swept test exactly (no golden may move for
    // a bullet that has no prefab shape).
    const BodyGeom geoms[2] = {CircleGeom(16.0F), BodyGeom::Fallback(16.0F)};
    Lcg rng{99U};
    int checked = 0;
    for (int i = 0; i < 4000; ++i) {
        const BodyGeom &g = geoms[i % 2];
        const glm::vec2 p0{rng.Range(-40.0F, 40.0F), rng.Range(-40.0F, 40.0F)};
        const glm::vec2 p1 = p0 + glm::vec2{rng.Range(-6.0F, 6.0F), rng.Range(-6.0F, 6.0F)};
        const glm::vec2 c0{rng.Range(-100.0F, 100.0F), rng.Range(-100.0F, 100.0F)};
        const glm::vec2 c1 = c0 + glm::vec2{rng.Range(-60.0F, 60.0F), rng.Range(-60.0F, 60.0F)};
        BulletShapeWorld w;
        w.kind = BulletShape::Kind::Circle;
        w.radius = 6.4F;
        float ta = -1.0F;
        float tb = -1.0F;
        const bool a = g.HurtSweep(p0, p1, c0, c1, 6.4F, ta);
        const bool b = g.HurtSweepShape(p0, p1, c0, c1, w, tb);
        ASSERT_EQ(a, b) << i;
        if (a) {
            ASSERT_EQ(ta, tb) << i;
            ++checked;
        }
    }
    EXPECT_GT(checked, 100);
}

// 24000 random configurations: oriented box / offset circle x circle / box hurtbox, bullet and target
// both moving linearly. Contract (as in SweptCollisionTest's oracle test): the oracle's first
// overlapping sub-step j  =>  production hits with t in [(j-1)/2000, j/2000] (1e-4 slack); no oracle
// overlap => production may hit only as a graze (re-checked by the oracle at the reported t with a
// 0.02 px margin).
TEST(BulletShapeOracleTest, HurtSweepShapeAgreesWithBruteForceOnOrientedShapes) {
    Data d;
    ASSERT_TRUE(d.Load());
    const Game::EnemyDef *slime = d.gd.FindEnemy("e_slime01"); // box hurtbox
    const Game::EnemyDef *alien = d.gd.FindEnemy("e_alien01");
    ASSERT_NE(slime, nullptr);
    ASSERT_NE(alien, nullptr);
    BodyGeom circle = CircleGeom(10.0F);
    circle.hurtOffsetPx = {3.0F, -5.0F};
    const BodyGeom geoms[4] = {MakeBodyGeom(slime->body, slime->hurtbox, 16.0F),
                               MakeBodyGeom(alien->body, alien->hurtbox, 16.0F), BodyGeom::Fallback(16.0F),
                               circle};
    const Game::BulletDef *bd0 = d.gd.FindBullet("bullet_0");
    const Game::BulletDef *bd28 = d.gd.FindBullet("bullet_28");
    const Game::BulletDef *bd4 = d.gd.FindBullet("_bullet_4"); // rotated child
    const Game::BulletDef *bd80 = d.gd.FindBullet("bullet_80");
    ASSERT_TRUE(bd0 && bd28 && bd4 && bd80);
    const BulletShape shapes[5] = {MakeBulletShape(bd0->collider), MakeBulletShape(bd28->collider),
                                   MakeBulletShape(bd4->collider), MakeBulletShape(bd80->collider),
                                   [] {
                                       BulletShape s;
                                       s.kind = BulletShape::Kind::Circle;
                                       s.radiusPx = 6.0F;
                                       s.offsetPx = {10.0F, 4.0F};
                                       return s;
                                   }()};
    Lcg rng{424242U};
    int cases = 0;
    int oracleHits = 0;
    int boxHits = 0;
    int bad = 0;
    std::string firstBad;
    for (int i = 0; i < 24000; ++i) {
        const BodyGeom &g = geoms[i % 4];
        const BulletShape &s = shapes[(i / 4) % 5];
        const float heading = rng.Range(-kPi, kPi);
        const glm::vec2 p0{rng.Range(-30.0F, 30.0F), rng.Range(-30.0F, 30.0F)};
        glm::vec2 p1 = p0;
        if (rng.Next() < 0.7F) {
            p1 += glm::vec2{rng.Range(-8.0F, 8.0F), rng.Range(-8.0F, 8.0F)};
        }
        // the bullet travels along its heading (as in the game), 20..90 px away from the hurtbox
        const glm::vec2 centre = p0 + g.hurtOffsetPx;
        const glm::vec2 dir{std::cos(heading), std::sin(heading)};
        const glm::vec2 b0 = centre - dir * rng.Range(10.0F, 90.0F) + glm::vec2{-dir.y, dir.x} * rng.Range(-40.0F, 40.0F);
        const glm::vec2 b1 = b0 + dir * rng.Range(0.0F, 130.0F);
        ++cases;
        const int j = FirstTouchOriented(g, s, heading, p0, p1, b0, b1);
        float t = -1.0F;
        const bool hit = g.HurtSweepShape(p0, p1, b0, b1, PlaceShape(s, heading), t);
        bool ok = true;
        if (j >= 0) {
            ++oracleHits;
            if (s.kind == BulletShape::Kind::Box) {
                ++boxHits;
            }
            const double lo = (j - 1) / static_cast<double>(oracle::kSubSteps) - 1e-4;
            const double hi = j / static_cast<double>(oracle::kSubSteps) + 1e-4;
            ok = hit && t >= lo && t <= hi && ((j == 0) == (t == 0.0F));
        } else if (hit) {
            // a graze narrower than one sub-step: verify the claim with the oracle at t, bullet inflated
            BulletShape fat = s;
            fat.radiusPx += 0.02F;
            fat.halfPx += glm::vec2{0.02F, 0.02F};
            const oracle::Vec o = oracle::Lerp(b0, b1, t);
            const oracle::Vec tg = oracle::Lerp(p0, p1, t);
            ok = TouchesOriented(g, fat, heading, {o.x, o.y}, {tg.x, tg.y});
        }
        if (!ok) {
            ++bad;
            if (firstBad.empty()) {
                firstBad = "case " + std::to_string(i) + " j=" + std::to_string(j) +
                           " hit=" + std::to_string(hit) + " t=" + std::to_string(t);
            }
        }
    }
    RecordProperty("oracleHits", oracleHits);
    RecordProperty("boxHits", boxHits);
    EXPECT_EQ(bad, 0) << firstBad;
    EXPECT_EQ(cases, 24000);
    // Pinned (deterministic LCG, fixed geometry from the data tables): the mix must keep exercising
    // the box and circle branches against both hurtbox shapes.
    EXPECT_EQ(oracleHits, 14911);
    EXPECT_EQ(boxHits, 9011);
}

// ---- (c) sim: heading, spin, prefab ids, enemy / boss bullets ---------------------------------------
TEST(BulletShapeSimTest, PlayerBulletsCarryTheirHeadingPrefabAndShape) {
    Data d;
    ASSERT_TRUE(d.Load());
    const Game::WeaponDef w = Starter(d.gd);
    const int idx = d.gd.BulletIndex("bullet_0");
    ASSERT_GE(idx, 0);
    const float angles[] = {0.0F, 90.0F, 180.0F, -90.0F, 45.0F, 135.0F, -135.0F, -45.0F, 22.5F};
    for (const float deg : angles) {
        Game::Sim::Simulation sim(20240607, &g_NullWorld);
        sim.EquipWeapon(w, "Gun001", 5, &d.gd);
        const float a = deg * kPi / 180.0F;
        sim.Advance(20.0F, Fire(glm::vec2{std::cos(a), std::sin(a)}));
        ASSERT_EQ(sim.Bullets().size(), 1U) << deg;
        const Game::Sim::BulletState &b = sim.Bullets()[0];
        EXPECT_NEAR(std::cos(b.angleRad), std::cos(a), 1e-5F) << deg;
        EXPECT_NEAR(std::sin(b.angleRad), std::sin(a), 1e-5F) << deg;
        // the heading IS the travel direction
        EXPECT_NEAR(std::atan2(b.vel.y, b.vel.x), std::atan2(std::sin(a), std::cos(a)), 1e-4F) << deg;
        EXPECT_EQ(b.launch.bulletIdx, idx);
        EXPECT_EQ(b.launch.shape.kind, BulletShape::Kind::Box);
        EXPECT_FLOAT_EQ(b.launch.shape.offsetPx.x, 8.0F);
    }
}

TEST(BulletShapeSimTest, ChangingTheVelocityTurnsTheBulletAndReboundFlipsIt) {
    // FAITHFUL: RGBullet.SetBulletVelocity sets transform.right = dir; ReboundBullet negates both.
    Game::Sim::BulletState b;
    Game::Sim::SetBulletVelocity(b, {0.0F, 320.0F});
    EXPECT_NEAR(b.angleRad, kPi / 2.0F, 1e-6F);
    Game::Sim::SetBulletVelocity(b, {-100.0F, -100.0F});
    EXPECT_NEAR(b.angleRad, -0.75F * kPi, 1e-6F);
    Game::Sim::ReboundBullet(b);
    EXPECT_EQ(b.vel, glm::vec2(100.0F, 100.0F));
    EXPECT_NEAR(b.angleRad, 0.25F * kPi, 1e-6F);
    Game::Sim::SetBulletVelocity(b, {0.0F, 0.0F}); // stopped: keeps its heading
    EXPECT_NEAR(b.angleRad, 0.25F * kPi, 1e-6F);
    EXPECT_EQ(b.vel, glm::vec2(0.0F, 0.0F));
}

TEST(BulletShapeSimTest, ASpinningBulletTurnsPerFixedStepAndKeepsFlyingStraight) {
    Data d;
    ASSERT_TRUE(d.Load());
    // The first species whose bullet spins (Bullet01 with rotate_angle != 0): fire it and read the
    // bullet's heading each step.
    const Game::EnemyDef *spinner = nullptr;
    for (const Game::EnemyDef &e : d.gd.Enemies()) {
        const Game::BulletDef *bd = d.gd.FindBullet(e.bulletPrefab);
        if (bd != nullptr && bd->rotateAngle != 0 && bd->bulletClass == "Bullet01" && e.kinematic == 0) {
            spinner = &e;
            break;
        }
    }
    ASSERT_NE(spinner, nullptr);
    const Game::BulletDef *bd = d.gd.FindBullet(spinner->bulletPrefab);
    Game::Sim::Simulation sim(20240607, &g_NullWorld);
    sim.SetGameData(&d.gd);
    sim.AddEnemy(*spinner, glm::vec2{300.0F, 0.0F}, 0, 4242);
    Game::Sim::WorldInputs in;
    in.playerPos = {0.0F, 0.0F};
    in.playerRoomId = 0; // wake the enemy
    in.aimDir = {1.0F, 0.0F};
    const Game::Sim::BulletState *first = nullptr;
    for (int i = 0; i < 2000 && first == nullptr; ++i) {
        sim.Advance(20.0F, in);
        for (const Game::Sim::BulletState &b : sim.Bullets()) {
            if (b.camp == 1) {
                first = &b;
                break;
            }
        }
    }
    ASSERT_NE(first, nullptr) << spinner->id << " never fired";
    EXPECT_FLOAT_EQ(first->launch.spinDegPerStep, static_cast<float>(bd->rotateAngle));
    const std::uint32_t id = first->id;
    float prevAngle = first->angleRad;
    const glm::vec2 vel0 = first->vel;
    for (int i = 0; i < 5; ++i) {
        sim.Advance(20.0F, in);
        const Game::Sim::BulletState *cur = nullptr;
        for (const Game::Sim::BulletState &b : sim.Bullets()) {
            if (b.id == id) {
                cur = &b;
            }
        }
        if (cur == nullptr) {
            break;
        }
        EXPECT_NEAR(cur->angleRad - prevAngle, bd->rotateAngle * kPi / 180.0F, 1e-4F);
        EXPECT_EQ(cur->vel, vel0) << "spin must not bend the flight (Bullet01.FixedUpdate)";
        prevAngle = cur->angleRad;
    }
}

namespace {
const Game::Sim::BulletState *FirstEnemyBullet(Game::Sim::Simulation &sim, const Game::Sim::WorldInputs &in,
                                               int maxSteps = 3000) {
    for (int i = 0; i < maxSteps; ++i) {
        sim.Advance(20.0F, in);
        for (const Game::Sim::BulletState &b : sim.Bullets()) {
            if (b.camp == 1) {
                return &b;
            }
        }
    }
    return nullptr;
}
} // namespace

TEST(BulletShapeSimTest, EnemyBulletsUseTheirOwnPrefabSpriteRowAndCollider) {
    Data d;
    ASSERT_TRUE(d.Load());
    const Game::EnemyDef *orc = d.gd.FindEnemy("e_orc01");
    ASSERT_NE(orc, nullptr);
    EXPECT_EQ(orc->bulletPrefab, "bullet_e_1"); // FAITHFUL @ e_orc01.prefab `bullet:` -> bullet_29.png orb
    Game::Sim::Simulation sim(20240607, &g_NullWorld);
    sim.SetGameData(&d.gd);
    sim.AddEnemy(*orc, glm::vec2{300.0F, 0.0F}, 0, 4242);
    Game::Sim::WorldInputs in;
    in.playerPos = {0.0F, 0.0F};
    in.playerRoomId = 0;
    const Game::Sim::BulletState *b = FirstEnemyBullet(sim, in);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->launch.bulletIdx, d.gd.BulletIndex("bullet_e_1"));
    EXPECT_EQ(d.gd.Bullets()[static_cast<std::size_t>(b->launch.bulletIdx)].visual.sprite, "bullet_29.png");
    ASSERT_EQ(b->launch.shape.kind, BulletShape::Kind::Box);
    EXPECT_FLOAT_EQ(b->launch.shape.halfPx.x, 4.8F); // box 0.3 x 0.3 u
    EXPECT_FLOAT_EQ(b->launch.shape.halfPx.y, 4.8F);
    // heading = the travel direction (the enemy aims at the player at the origin: roughly -x)
    EXPECT_NEAR(std::cos(b->angleRad), b->vel.x / glm::length(b->vel), 1e-4F);
    EXPECT_NEAR(std::sin(b->angleRad), b->vel.y / glm::length(b->vel), 1e-4F);
    EXPECT_LT(b->vel.x, 0.0F);
}

TEST(BulletShapeSimTest, WithoutGameDataTheEnemyBulletKeepsTheLegacy4pxCircle) {
    Data d;
    ASSERT_TRUE(d.Load());
    const Game::EnemyDef *orc = d.gd.FindEnemy("e_orc01");
    Game::Sim::Simulation sim(20240607, &g_NullWorld); // no SetGameData
    sim.AddEnemy(*orc, glm::vec2{300.0F, 0.0F}, 0, 4242);
    Game::Sim::WorldInputs in;
    in.playerPos = {0.0F, 0.0F};
    in.playerRoomId = 0;
    const Game::Sim::BulletState *b = FirstEnemyBullet(sim, in);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->launch.bulletIdx, -1);
    EXPECT_FALSE(b->launch.shape.Present());
    EXPECT_FLOAT_EQ(b->radiusPx, 0.0F); // -> Sim::kBulletRadius
}

TEST(BulletShapeSimTest, ASpeciesWithNoRGBulletBulletGetsTheGenericBulletE1) {
    Data d;
    ASSERT_TRUE(d.Load());
    const Game::EnemyDef *miner = d.gd.FindEnemy("e_miner01"); // refs: sword_0 (melee prefab)
    ASSERT_NE(miner, nullptr);
    EXPECT_TRUE(miner->bulletPrefab.empty());
    ASSERT_EQ(miner->bulletRefs.size(), 1U);
    EXPECT_EQ(miner->bulletRefs[0], "sword_0");
    Game::Sim::Simulation sim(20240607, &g_NullWorld);
    sim.SetGameData(&d.gd);
    sim.AddEnemy(*miner, glm::vec2{300.0F, 0.0F}, 0, 4242);
    Game::Sim::WorldInputs in;
    in.playerPos = {0.0F, 0.0F};
    in.playerRoomId = 0;
    const Game::Sim::BulletState *b = FirstEnemyBullet(sim, in);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->launch.bulletIdx, d.gd.BulletIndex("bullet_e_1")) << "T-66 substitute";
}

TEST(BulletShapeSimTest, EveryResolvedEnemyBulletRowIsARealFlyingBullet) {
    Data d;
    ASSERT_TRUE(d.Load());
    int resolved = 0;
    for (const Game::EnemyDef &e : d.gd.Enemies()) {
        if (e.bulletPrefab.empty()) {
            continue;
        }
        ++resolved;
        const Game::BulletDef *b = d.gd.FindBullet(e.bulletPrefab);
        ASSERT_NE(b, nullptr) << e.id;
        EXPECT_TRUE(b->collider.Present()) << e.id << " -> " << b->id;
        EXPECT_TRUE(b->visual.Present()) << e.id << " -> " << b->id;
        EXPECT_FALSE(b->bulletClass.empty()) << e.id << " -> " << b->id;
    }
    EXPECT_EQ(resolved, 92);
}

TEST(BulletShapeSimTest, TheBossFiresItsPrimaryBullet) {
    Data d;
    ASSERT_TRUE(d.Load());
    const Game::EnemyDef *boss = d.gd.FindEnemy("boss01");
    ASSERT_NE(boss, nullptr);
    EXPECT_EQ(boss->bulletPrefab, "bullet_e_1"); // T-65: first resolvable of bullet01..06
    Game::Sim::Simulation sim(20240607, &g_NullWorld);
    sim.SetGameData(&d.gd);
    sim.SetBoss(2.0F, glm::vec2{300.0F, 0.0F}, 500, 0, 4242, "BossAI01", boss);
    Game::Sim::WorldInputs in;
    in.playerPos = {0.0F, 0.0F};
    in.playerRoomId = 0;
    const Game::Sim::BulletState *b = FirstEnemyBullet(sim, in);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->launch.bulletIdx, d.gd.BulletIndex("bullet_e_1"));
}

// ---- (d) walls --------------------------------------------------------------------------------------
TEST(BulletShapeWallTest, TheBoxFrontStopsTheBulletAt16pxNotAt6pt4px) {
    Data d;
    ASSERT_TRUE(d.Load());
    const Game::WeaponDef w = Starter(d.gd);
    const float x0 = 200.0F;
    SlabWorld world2(x0, x0 + 64.0F);
    Game::Sim::Simulation sim2(20240607, &world2);
    sim2.EquipWeapon(w, "Gun001", 5, &d.gd);
    std::vector<Game::Sim::Simulation::BulletImpact> impacts;
    for (int i = 0; i < 80 && impacts.empty(); ++i) {
        sim2.Advance(20.0F, Fire({1.0F, 0.0F}));
        impacts = sim2.DrainImpacts();
    }
    ASSERT_FALSE(impacts.empty());
    const auto &im = impacts[0];
    EXPECT_TRUE(im.wall);
    EXPECT_EQ(im.camp, 0);
    EXPECT_EQ(im.bulletIdx, d.gd.BulletIndex("bullet_0"));
    // the box front (origin + 16 px) meets the slab face at x0: the origin stops 16 px before it
    // (within the sampled contact accuracy), NOT 6.4 px before it as the inscribed circle did.
    EXPECT_NEAR(im.pos.x, x0 - 16.0F, 0.25F);
    EXPECT_LT(im.pos.x, x0 - 6.4F - 5.0F);
    EXPECT_NEAR(im.angleRad, 0.0F, 1e-6F);
    ASSERT_EQ(world2.hits.size(), 1U);
    // DamageObstacle is reported at the covering circle that touched first: the box (half 8 x 6.4,
    // centre +8) is covered by two r = 6.4 circles at +6.4 and +9.6; the front one (+9.6, front edge
    // +16) meets the slab.
    EXPECT_NEAR(world2.hits[0].first.x, x0 - 16.0F + 9.6F, 0.3F);
    EXPECT_FLOAT_EQ(world2.hits[0].second, 6.4F);
}

TEST(BulletShapeWallTest, WithoutGameDataTheLegacyCircleStopsAt4px) {
    Data d;
    ASSERT_TRUE(d.Load());
    Game::WeaponDef w = Starter(d.gd);
    const float x0 = 200.0F;
    SlabWorld world(x0, x0 + 64.0F);
    Game::Sim::Simulation sim(20240607, &world);
    sim.EquipWeapon(w, "Gun001", 5, nullptr); // no data: no shape, fallback 4 px circle at the origin
    std::vector<Game::Sim::Simulation::BulletImpact> impacts;
    for (int i = 0; i < 80 && impacts.empty(); ++i) {
        sim.Advance(20.0F, Fire({1.0F, 0.0F}));
        impacts = sim.DrainImpacts();
    }
    ASSERT_FALSE(impacts.empty());
    EXPECT_NEAR(impacts[0].pos.x, x0 - 4.0F, 0.25F);
    EXPECT_EQ(impacts[0].bulletIdx, -1);
}

TEST(BulletShapeWallTest, ALongBoxBulletIsStoppedByItsTipNotItsOrigin) {
    Data d;
    ASSERT_TRUE(d.Load());
    // weapon_043 -> bullet_28: box 2 x 1 u (64 x 32 px) starting at the origin, 64 px long.
    Game::WeaponDef w = *d.gd.FindWeapon("weapon_043");
    w.deviation = 0;
    w.bulletSpeed = 10.0F;
    w.canThrough = 0;
    w.throughCount = 0;
    ASSERT_EQ(w.bulletPrefab, "bullet_28");
    const float x0 = 300.0F;
    SlabWorld world(x0, x0 + 64.0F);
    Game::Sim::Simulation sim(20240607, &world);
    sim.EquipWeapon(w, "Gun001", 5, &d.gd);
    std::vector<Game::Sim::Simulation::BulletImpact> impacts;
    for (int i = 0; i < 120 && impacts.empty(); ++i) {
        sim.Advance(20.0F, Fire({1.0F, 0.0F}));
        impacts = sim.DrainImpacts();
    }
    ASSERT_FALSE(impacts.empty());
    EXPECT_NEAR(impacts[0].pos.x, x0 - 64.0F, 0.5F); // the 64 px tip meets the wall; the origin is 64 px short
}

TEST(BulletShapeWallTest, AnEnemyBulletWallImpactIsRecordedForTheRedHitEffect) {
    Data d;
    ASSERT_TRUE(d.Load());
    const Game::EnemyDef *orc = d.gd.FindEnemy("e_orc01");
    SlabWorld world(-200.0F, -150.0F); // the orc shoots toward the player at the origin: -x
    Game::Sim::Simulation sim(20240607, &world);
    sim.SetGameData(&d.gd);
    sim.AddEnemy(*orc, glm::vec2{300.0F, 0.0F}, 0, 4242);
    Game::Sim::WorldInputs in;
    in.playerPos = {-400.0F, 0.0F}; // behind the wall: enemy bullets must hit it first
    in.playerRoomId = 0;
    std::vector<Game::Sim::Simulation::BulletImpact> impacts;
    for (int i = 0; i < 3000 && impacts.empty(); ++i) {
        sim.Advance(20.0F, in);
        impacts = sim.DrainImpacts();
    }
    ASSERT_FALSE(impacts.empty());
    EXPECT_TRUE(impacts[0].wall);
    EXPECT_EQ(impacts[0].camp, 1);
    const Game::BulletDef &bd = d.gd.Bullets()[static_cast<std::size_t>(impacts[0].bulletIdx)];
    EXPECT_EQ(bd.id, "bullet_e_1");
    ASSERT_NE(d.gd.FindBulletEffect(bd.hitObject), nullptr);
    EXPECT_EQ(bd.hitObject, "hit_red");
}

// ---- through the whole sim: reach ---------------------------------------------------------------------
TEST(BulletShapeSimTest, ABoxBulletHitsATargetOnlyItsForwardReachTouches) {
    Data d;
    ASSERT_TRUE(d.Load());
    const Game::WeaponDef w = Starter(d.gd);
    const Game::EnemyDef *slime = d.gd.FindEnemy("e_slime01");
    ASSERT_NE(slime, nullptr);
    const BodyGeom g = MakeBodyGeom(slime->body, slime->hurtbox, 16.0F);
    ASSERT_TRUE(g.hurtIsBox);
    // The bullet is fired from the origin along +x and sits at ~(6.4, 0) after one step. A slime whose
    // hurtbox left edge is 12 px ahead of the bullet origin: the prefab box (front at +16) touches it,
    // the old 6.4 px circle at the origin does not.
    for (const bool withData : {true, false}) {
        Game::Sim::Simulation sim(20240607, &g_NullWorld);
        const float hurtLeft = 6.4F + 14.0F; // 14 px ahead of the end-of-step-1 origin
        sim.AddEnemy(*slime, glm::vec2{hurtLeft + g.hurtHalfPx.x - g.hurtOffsetPx.x, -g.hurtOffsetPx.y}, 0, 21000);
        sim.EquipWeapon(w, "Gun001", 5, withData ? &d.gd : nullptr);
        const int hp0 = sim.EnemyViews()[0].hp;
        for (int i = 0; i < 4; ++i) {
            sim.Advance(20.0F, Fire({1.0F, 0.0F}));
        }
        if (withData) {
            EXPECT_LT(sim.EnemyViews()[0].hp, hp0) << "the oriented box reaches 16 px ahead";
        } else {
            // legacy 4 px circle (no data): the bullet still crosses the box soon after, so only check
            // that the FIRST contact is later than the oriented one by comparing step counts below.
            SUCCEED();
        }
    }
    // First-contact step: oriented box strictly earlier than the legacy circle (same geometry).
    auto firstHitStep = [&](bool withData) {
        Game::Sim::Simulation sim(20240607, &g_NullWorld);
        sim.AddEnemy(*slime, glm::vec2{80.0F + g.hurtHalfPx.x - g.hurtOffsetPx.x, -g.hurtOffsetPx.y}, 0, 21000);
        sim.EquipWeapon(w, "Gun001", 5, withData ? &d.gd : nullptr);
        const int hp0 = sim.EnemyViews()[0].hp;
        for (int i = 0; i < 60; ++i) {
            sim.Advance(20.0F, Fire({1.0F, 0.0F}));
            if (sim.EnemyViews()[0].hp < hp0) {
                return i;
            }
        }
        return -1;
    };
    const int newStep = firstHitStep(true);
    const int oldStep = firstHitStep(false);
    ASSERT_GE(newStep, 0);
    ASSERT_GE(oldStep, 0);
    EXPECT_LT(newStep, oldStep) << "the box tip (16 px ahead) touches before a 4 px circle at the origin";
}

// ---- (e) view pose + corpse ----------------------------------------------------------------------------
TEST(BulletPoseTest, HeadingRotatesTheSpriteAndTheChildOffset) {
    // bullet_28's sprite child sits 1 unit (32 px) ahead of the root at scale 2 (2 x kSpriteScale = 4).
    const Game::BulletPose up = Game::ComputeBulletPose({100.0F, 50.0F}, kPi / 2.0F, {32.0F, 0.0F}, 0.0F,
                                                        {4.0F, 4.0F}, false);
    EXPECT_NEAR(up.translation.x, 100.0F, 1e-4F);
    EXPECT_NEAR(up.translation.y, 82.0F, 1e-4F);
    EXPECT_NEAR(up.rotation, kPi / 2.0F, 1e-6F);
    EXPECT_EQ(up.scale, glm::vec2(4.0F, 4.0F));
    // a child rotated 90 deg on a bullet flying -x, flipped
    const Game::BulletPose left = Game::ComputeBulletPose({0.0F, 0.0F}, kPi, {0.0F, 0.0F}, kPi / 2.0F,
                                                          {2.0F, 3.0F}, true);
    EXPECT_NEAR(left.rotation, 1.5F * kPi, 1e-5F);
    EXPECT_EQ(left.scale, glm::vec2(-2.0F, 3.0F));
}

TEST(BulletPoseTest, FourDirectionsPutTheWhiteTipAheadOfTheOrangeTail) {
    // bullet_5.png faces +x (white head right, orange tail left). With the heading as the rotation the
    // head's world direction is (cos h, sin h) for every aim.
    for (const float deg : {0.0F, 90.0F, 180.0F, 270.0F, 45.0F}) {
        const float h = deg * kPi / 180.0F;
        const Game::BulletPose p = Game::ComputeBulletPose({0.0F, 0.0F}, h, {0.0F, 0.0F}, 0.0F, {2.0F, 2.0F}, false);
        const glm::vec2 head{std::cos(p.rotation), std::sin(p.rotation)}; // image +x in world
        EXPECT_NEAR(head.x, std::cos(h), 1e-5F) << deg;
        EXPECT_NEAR(head.y, std::sin(h), 1e-5F) << deg;
    }
}

TEST(CorpseDataTest, DeadStateAndSkullFrameResolveFromTheManifest) {
    Data d;
    ASSERT_TRUE(d.Load());
    EXPECT_EQ(d.gd.DeadState("e_orc01"), "dead");
    EXPECT_EQ(d.gd.DeadState("e_miner03"), "enemy01_dead");
    EXPECT_EQ(d.gd.DeadState("e_chest1"), "e_chest_dead"); // the species' own prefix, not e_chest2_dead
    EXPECT_EQ(d.gd.DeadState("boss01"), "dead");
    EXPECT_EQ(d.gd.DeadState("not_a_species"), "");
    // the lying-corpse frame is not the idle frame, and the emoticon is biaoqing_11
    const std::vector<std::string> dead = d.gd.SpriteFrames("e_orc01", "R", "dead");
    ASSERT_EQ(dead.size(), 1U);
    EXPECT_EQ(dead[0], "R/sprites/enemy22_8.png");
    const std::vector<std::string> tap = d.gd.SpriteFrames("e_orc01", "R", "char_tap_dead");
    ASSERT_EQ(tap.size(), 1U);
    EXPECT_EQ(tap[0], "R/sprites/biaoqing_11.png");
    const Game::EnemyDef *orc = d.gd.FindEnemy("e_orc01");
    ASSERT_TRUE(orc->hasDeadTap);
    EXPECT_FLOAT_EQ(orc->deadTapX, 0.8F);
    EXPECT_FLOAT_EQ(orc->deadTapY, 1.2F);
    // the species that have no corpse clip are exactly the pinned ones (tools/validate_data.py)
    std::vector<std::string> none;
    for (const Game::EnemyDef &e : d.gd.Enemies()) {
        if (d.gd.FindSprites(e.id) != nullptr && d.gd.DeadState(e.id).empty()) {
            none.push_back(e.id);
        }
    }
    EXPECT_EQ(none, (std::vector<std::string>{"boss04", "boss06", "e_tentacle", "e_tentacle_temp"}));
}

TEST(BulletEffectDataTest, HitEffectsMatchThePrefabClip) {
    Data d;
    ASSERT_TRUE(d.Load());
    const Game::BulletDef *b0 = d.gd.FindBullet("bullet_0");
    ASSERT_NE(b0, nullptr);
    EXPECT_EQ(b0->hitObject, "hit_yellow");
    const Game::BulletEffectDef *y = d.gd.FindBulletEffect("hit_yellow");
    ASSERT_NE(y, nullptr);
    EXPECT_EQ(y->frames.size(), 3U);
    EXPECT_EQ(y->sampleRate, 12);
    EXPECT_NEAR(y->durationS, 0.25F, 1e-4F);
    EXPECT_FLOAT_EQ(y->color[0], 1.0F);
    EXPECT_NEAR(y->color[1], 0.6F, 1e-4F);
    EXPECT_FLOAT_EQ(y->color[2], 0.0F);
    const Game::BulletEffectDef *r = d.gd.FindBulletEffect("hit_red");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->frames, y->frames); // same sample_hit clip, different tint
    EXPECT_NEAR(r->color[0], 0.8309F, 1e-3F);
    EXPECT_EQ(d.gd.FindBullet("bullet_e_1")->hitObject, "hit_red");
}


// ---- R42: independent ShapeSpec numbers (read straight from the .prefab YAML, not via MakeBulletShape) ----
// bullet_8.prefab: BoxCollider2D m_Size (1.5, 0.5), m_Offset (0.3, 0), child b scale 1  -> 48 x 16 px, offset 9.6 px.
// _bullet_4.prefab: BoxCollider2D m_Size (0.8125, 1), m_Offset (0,0) on a child whose m_LocalRotation is
//   z 0.7071068 / w 0.7071068 (90 deg)  -> 26 x 32 px, axes turned 90 deg.
// bullet_80.prefab: CircleCollider2D m_Radius 0.55 on child `col` (scale 1.35) -> 0.7425 u = 23.76 px.
TEST(BulletShapeIndependentTest, DataConversionMatchesTheNumbersReadFromThePrefabYaml) {
    Data d;
    ASSERT_TRUE(d.Load());
    {
        const BulletShape s = MakeBulletShape(d.gd.FindBullet("bullet_8")->collider);
        ASSERT_EQ(s.kind, BulletShape::Kind::Box);
        EXPECT_NEAR(s.halfPx.x, 24.0F, 1e-4F);
        EXPECT_NEAR(s.halfPx.y, 8.0F, 1e-4F);
        EXPECT_NEAR(s.offsetPx.x, 9.6F, 1e-4F);
        EXPECT_NEAR(s.offsetPx.y, 0.0F, 1e-6F);
        EXPECT_NEAR(s.boxRotRad, 0.0F, 1e-6F);
    }
    {
        const BulletShape s = MakeBulletShape(d.gd.FindBullet("_bullet_4")->collider);
        ASSERT_EQ(s.kind, BulletShape::Kind::Box);
        EXPECT_NEAR(s.halfPx.x, 13.0F, 1e-4F);
        EXPECT_NEAR(s.halfPx.y, 16.0F, 1e-4F);
        EXPECT_NEAR(s.boxRotRad, kPi / 2.0F, 1e-5F);
        EXPECT_NEAR(s.offsetPx.x, 0.0F, 1e-6F);
    }
    {
        const BulletShape s = MakeBulletShape(d.gd.FindBullet("bullet_80")->collider);
        ASSERT_EQ(s.kind, BulletShape::Kind::Circle);
        EXPECT_NEAR(s.radiusPx, 23.76F, 1e-3F);
    }
}

// The shape-aware oracle fed with the LITERAL numbers above (no production conversion): the Simulation's
// first-hit step for weapon_033 (bullet_8) against a head-on e_alien01 equals the oracle's for every
// alignment of one seed.
TEST(BulletShapeIndependentTest, Weapon033SimulationAgreesWithAnOracleBuiltFromLiteralPrefabNumbers) {
    Data d;
    ASSERT_TRUE(d.Load());
    const Game::WeaponDef w = [&] {
        Game::WeaponDef x = *d.gd.FindWeapon("weapon_033");
        x.canThrough = 0;
        x.throughCount = 0;
        x.deviation = 0;
        x.bulletSpeed = 64.0F;
        return x;
    }();
    const Game::EnemyDef *e = d.gd.FindEnemy("e_alien01");
    ASSERT_NE(e, nullptr);
    const BodyGeom g = MakeBodyGeom(e->body, e->hurtbox, 16.0F);
    oracle::ShapeSpec lit;
    lit.box = true;
    lit.hx = 24.0;
    lit.hy = 8.0;
    lit.ox = 9.6;
    lit.oy = 0.0;
    constexpr float kStep = 64.0F * 32.0F * Game::Sim::kFixedStepSeconds;
    Game::Sim::Simulation probe(20240607, &g_NullWorld);
    probe.EquipWeapon(w, "Gun001", 5, &d.gd);
    probe.Advance(20.0F, Fire({1.0F, 0.0F}));
    ASSERT_EQ(probe.Bullets().size(), 1U);
    const float x1 = probe.Bullets()[0].pos.x;
    const float by = probe.Bullets()[0].pos.y;
    int runs = 0;
    int hits = 0;
    for (float off = 0.0F; off < 44.0F; off += 0.5F) {
        Game::Sim::Simulation sim(20240607, &g_NullWorld);
        sim.AddEnemy(*e, glm::vec2{x1 + 30.0F * kStep + off, by - g.hurtOffsetPx.y}, 0, 21000);
        sim.EquipWeapon(w, "Gun001", 5, &d.gd);
        const int hp0 = sim.EnemyViews()[0].hp;
        glm::vec2 prevEnemy = sim.EnemyViews()[0].pos;
        glm::vec2 prevBullet{x1 - kStep, by};
        int oracleStep = -1;
        int simStep = -1;
        for (int step = 0; step < 60 && simStep < 0; ++step) {
            sim.Advance(20.0F, Fire({1.0F, 0.0F}, 0));
            const glm::vec2 enemy = sim.EnemyViews()[0].pos;
            const glm::vec2 endBullet = prevBullet + glm::vec2{kStep, 0.0F};
            if (oracleStep < 0 && oracle::FirstTouchSubShape(g, lit, prevEnemy, enemy, prevBullet, endBullet) >= 0) {
                oracleStep = step;
            }
            if (sim.EnemyViews()[0].hp < hp0) {
                simStep = step;
            }
            prevEnemy = enemy;
            prevBullet = endBullet;
        }
        ASSERT_EQ(simStep, oracleStep) << off;
        ++runs;
        hits += simStep >= 0 ? 1 : 0;
    }
    EXPECT_EQ(runs, 88);
    EXPECT_EQ(hits, 88);
}

// ---- R42: the wall test of an oriented box ---------------------------------------------------------
namespace {
// A world of Util::Collider blockers. `exact` selects the box sweep: true = SweepBoxBlockedByAny (what
// GameScene does), false = the circle-only default (covering row).
class ColliderWorld : public Game::Sim::WorldCollision {
public:
    ColliderWorld(std::vector<Util::Collider> blockers, bool exact) : m_B(std::move(blockers)), m_Exact(exact) {}
    bool Blocks(glm::vec2 pos, float radius) const override {
        const Util::Collider c = Util::Collider::MakeCircle(pos, radius);
        for (const Util::Collider &b : m_B) {
            if (Util::Overlap(b, c)) {
                return true;
            }
        }
        return false;
    }
    float SweepBoxBlocked(glm::vec2 from, glm::vec2 to, glm::vec2 off, glm::vec2 half, float angle,
                          glm::vec2 &contactOffset, float &contactRadius) const override {
        if (m_Exact) {
            return Game::Sim::SweepBoxBlockedByAny(m_B, from, to, off, half, angle, contactOffset, contactRadius);
        }
        return WorldCollision::SweepBoxBlocked(from, to, off, half, angle, contactOffset, contactRadius);
    }
    std::vector<std::pair<glm::vec2, float>> hits;
    void DamageObstacle(glm::vec2 pos, float radius) override { hits.emplace_back(pos, radius); }

private:
    std::vector<Util::Collider> m_B;
    bool m_Exact;
};
} // namespace

// The cover row of circles is NOT the whole box. Registered, measured: along the whole perimeter the
// uncovered slivers are at most (sqrt(2) - 1) r deep (distance from an uncovered box point to the nearest
// circle boundary). Counterexample from the Codex R42 review: Bullet03 (0.1 x 1.2 u, half (1.6, 19.2), 12
// circles of r = 1.6): the point (1.5, 0) is INSIDE the box but 2.193 px from the nearest centre.
TEST(BulletShapeWallTest, CoverRowGapIsBoundedAndRegistered) {
    using Game::Sim::CoverCircles;
    using Game::Sim::MakeCoverCircles;
    const CoverCircles row = MakeCoverCircles({0.0F, 0.0F}, {1.6F, 19.2F}, 0.0F);
    ASSERT_EQ(row.n, 12);
    EXPECT_FLOAT_EQ(row.radius, 1.6F);
    float nearest = 1e9F;
    for (int i = 0; i < row.n; ++i) {
        nearest = std::min(nearest, glm::length(glm::vec2{1.5F, 0.0F} - row.centre[i]));
    }
    EXPECT_NEAR(nearest, 2.1932F, 1e-3F); // outside every circle (r 1.6) yet inside the box
    EXPECT_GT(nearest, row.radius);
    EXPECT_NEAR(nearest - row.radius, 0.5932F, 1e-3F); // this point's sliver depth

    Data d;
    ASSERT_TRUE(d.Load());
    float worstAll = 0.0F;
    std::string worstId;
    int boxes = 0;
    for (const Game::BulletDef &b : d.gd.Bullets()) {
        const BulletShape s = MakeBulletShape(b.collider);
        if (s.kind != BulletShape::Kind::Box) {
            continue;
        }
        ++boxes;
        const CoverCircles c = MakeCoverCircles({0.0F, 0.0F}, s.halfPx, 0.0F);
        float worst = 0.0F;
        for (int ix = 0; ix <= 48; ++ix) {
            for (int iy = 0; iy <= 48; ++iy) {
                const glm::vec2 p{s.halfPx.x * (2.0F * static_cast<float>(ix) / 48.0F - 1.0F),
                                  s.halfPx.y * (2.0F * static_cast<float>(iy) / 48.0F - 1.0F)};
                float dmin = 1e9F;
                for (int i = 0; i < c.n; ++i) {
                    dmin = std::min(dmin, glm::length(p - c.centre[i]));
                }
                worst = std::max(worst, dmin - c.radius);
            }
        }
        EXPECT_LE(worst, (std::sqrt(2.0F) - 1.0F) * c.radius + 1e-3F) << b.id;
        if (worst > worstAll) {
            worstAll = worst;
            worstId = b.id;
        }
    }
    RecordProperty("maxSliverDepthPx", std::to_string(worstAll));
    EXPECT_EQ(boxes, 177);
    // Pinned: the deepest sliver over every box collider in bullets.json (px) and where it is.
    EXPECT_EQ(worstId, "bullet_e_29");
    EXPECT_NEAR(worstAll, 13.2548F, 0.01F);
}

// The EXACT box sweep catches what the covering row misses: a 0.6 px-radius blocker touching the side of
// Bullet03's box midway between two row circles.
TEST(BulletShapeWallTest, ExactBoxSweepCatchesTheSliverTheCoverRowMisses) {
    const glm::vec2 half{1.6F, 19.2F};
    const std::vector<Util::Collider> blockers{Util::Collider::MakeCircle({1.7F, 0.0F}, 0.3F)};
    glm::vec2 off{0.0F, 0.0F};
    float rad = 0.0F;
    ColliderWorld exact(blockers, true);
    ColliderWorld row(blockers, false);
    // the box (x in [-1.6, 1.6]) touches the circle (x in [1.4, 2.0]) at the start already
    EXPECT_GE(exact.SweepBoxBlocked({0.0F, 0.0F}, {0.0F, 0.0F}, {0.0F, 0.0F}, half, 0.0F, off, rad), 0.0F);
    EXPECT_LT(row.SweepBoxBlocked({0.0F, 0.0F}, {0.0F, 0.0F}, {0.0F, 0.0F}, half, 0.0F, off, rad), 0.0F)
        << "registered gap: the circle-only fallback lets this one through";
}

// 12000 random oriented boxes vs circle / AABB blockers: the exact sweep (SweepBoxBlockedByAny) against a
// brute-force 2000-sub-step oracle written separately here (distance-outside-the-box for circles, SAT for
// boxes).
TEST(BulletShapeWallTest, ExactBoxSweepAgreesWithBruteForce) {
    Lcg rng{20261016U};
    int cases = 0;
    int hitsN = 0;
    int bad = 0;
    std::string firstBad;
    for (int i = 0; i < 12000; ++i) {
        const bool circle = (i % 2) == 0;
        const glm::vec2 half{rng.Range(2.0F, 30.0F), rng.Range(2.0F, 30.0F)};
        const float angle = rng.Range(-kPi, kPi);
        const glm::vec2 off{rng.Range(-20.0F, 20.0F), rng.Range(-20.0F, 20.0F)};
        const Util::Collider blk =
            circle ? Util::Collider::MakeCircle({0.0F, 0.0F}, rng.Range(3.0F, 25.0F))
                   : Util::Collider::MakeAABB({0.0F, 0.0F}, {rng.Range(8.0F, 64.0F), rng.Range(8.0F, 64.0F)});
        const glm::vec2 from{rng.Range(-120.0F, 120.0F), rng.Range(-120.0F, 120.0F)};
        const glm::vec2 to = from + glm::vec2{rng.Range(-150.0F, 150.0F), rng.Range(-150.0F, 150.0F)};
        glm::vec2 co{0.0F, 0.0F};
        float cr = 0.0F;
        const float t = Game::Sim::SweepBoxBlockedByAny({blk}, from, to, off, half, angle, co, cr);
        // overlap of the box (centre c, margin m added to the box) with the blocker, separately written
        auto overlaps = [&](V c, double m) {
            if (circle) {
                const V dd = Rot({-c.x, -c.y}, -static_cast<double>(angle)); // blocker centre in the box frame
                const double ox = std::max(std::fabs(dd.x) - half.x - m, 0.0);
                const double oy = std::max(std::fabs(dd.y) - half.y - m, 0.0);
                return ox * ox + oy * oy <= static_cast<double>(blk.radius) * blk.radius;
            }
            const double hx = blk.halfSize.x;
            const double hy = blk.halfSize.y;
            const V axes[4] = {{1, 0}, {0, 1}, {std::cos(angle), std::sin(angle)}, {-std::sin(angle), std::cos(angle)}};
            for (const V &a : axes) {
                const double rt = hx * std::fabs(a.x) + hy * std::fabs(a.y);
                const double rb = (half.x + m) * std::fabs(a.x * std::cos(angle) + a.y * std::sin(angle)) +
                                  (half.y + m) * std::fabs(-a.x * std::sin(angle) + a.y * std::cos(angle));
                if (std::fabs(-c.x * a.x - c.y * a.y) > rt + rb) {
                    return false;
                }
            }
            return true;
        };
        auto centreAt = [&](double u) {
            return V{from.x + u * (to.x - from.x) + off.x, from.y + u * (to.y - from.y) + off.y};
        };
        int j = -1;
        for (int k = 0; k <= 2000 && j < 0; ++k) {
            if (overlaps(centreAt(static_cast<double>(k) / 2000.0), 0.0)) {
                j = k;
            }
        }
        ++cases;
        bool ok = true;
        if (j >= 0) {
            ++hitsN;
            ok = t >= 0.0F && t >= (j - 1) / 2000.0 - 1e-4 && t <= j / 2000.0 + 1e-4;
            if (ok) { // the probe circle sits on the blocker
                const glm::vec2 origin = from + (to - from) * t;
                ok = Util::Overlap(blk, Util::Collider::MakeCircle(origin + co, cr));
            }
        } else if (t >= 0.0F) { // a graze narrower than one sub-step: confirm with a 0.02 px margin
            ok = overlaps(centreAt(t), 0.02);
        }
        if (!ok) {
            ++bad;
            if (firstBad.empty()) {
                firstBad = "case " + std::to_string(i) + " j=" + std::to_string(j) + " t=" + std::to_string(t);
            }
        }
    }
    EXPECT_EQ(cases, 12000);
    EXPECT_EQ(bad, 0) << firstBad;
    EXPECT_GT(hitsN, 2000);
}

// Through the Simulation: with the exact world the weapon_000 box stops with its FRONT on the wall face, to
// the float; the circle-only default stops within its sampling accuracy of the same spot.
TEST(BulletShapeWallTest, ExactWorldStopsTheBoxFrontOnTheWallFace) {
    Data d;
    ASSERT_TRUE(d.Load());
    const Game::WeaponDef w = Starter(d.gd);
    const float x0 = 200.0F;
    for (const bool exact : {true, false}) {
        const Util::Collider wall = Util::Collider::MakeAABB({x0 + 32.0F, 0.0F}, {64.0F, 4000.0F});
        ColliderWorld world({wall}, exact);
        Game::Sim::Simulation sim(20240607, &world);
        sim.EquipWeapon(w, "Gun001", 5, &d.gd);
        std::vector<Game::Sim::Simulation::BulletImpact> impacts;
        for (int i = 0; i < 80 && impacts.empty(); ++i) {
            sim.Advance(20.0F, Fire({1.0F, 0.0F}));
            impacts = sim.DrainImpacts();
        }
        ASSERT_FALSE(impacts.empty());
        EXPECT_NEAR(impacts[0].pos.x, x0 - 16.0F, exact ? 0.01F : 0.25F) << exact;
        ASSERT_EQ(world.hits.size(), 1U);
        // DamageObstacle's probe circle overlaps the wall
        EXPECT_TRUE(Util::Overlap(wall, Util::Collider::MakeCircle(world.hits[0].first, world.hits[0].second)))
            << exact;
    }
}

// NOLINTEND(readability-magic-numbers)
