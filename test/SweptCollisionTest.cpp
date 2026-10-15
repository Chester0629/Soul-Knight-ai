// T-57 (P3.0 follow-up, Codex R30 + R31): swept (continuous) bullet collision. Since P3.0
// (1 unit = 32 px) a weapon bullet flies up to 64 units/s = ~41 px per 20 ms fixed step,
// which is more than a hurtbox is wide, so the old end-of-step overlap test let a bullet jump
// clean over its target (even head-on) or over a thin wall. Pinned here:
//   (a) Codex R30's exact counterexample (weapon_033 vs a head-on, approaching e_alien01, relative
//       position -22.08 -> +22.08 px, neither end overlapping) now HITS -- at primitive level
//       and through the whole Simulation for EVERY start alignment;
//   (b) a bullet passing outside the grown hurtbox (box edge and rounded corner) still MISSES;
//   (c) with several targets in one step the EARLIEST hit wins, not the lowest index;
//   (d) a fast bullet cannot tunnel through a wall, thin or one cell thick, and a target in
//       front of the wall is still hit first while one behind it is not;
//   (e) slow bullets keep the old discrete behaviour (sweep == the end-of-step test);
//   (f) R31: wall contact and target hit are ordered on ONE time scale (the bullet keeps its
//       FULL step path; a target hit counts only if t_hit <= tWall, ties are TARGET-FIRST), and
//       the wall query + DamageObstacle run per bullet in order, so a box broken by bullet A is
//       gone for bullet B of the same step;
//   (g) R31: the oracle is INDEPENDENT of production collision code (test/SweptOracle.hpp:
//       brute-force sub-stepping + a separately written discrete overlap function).
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "SweptOracle.hpp"
#include "data/GameData.hpp"
#include "sim/BodyGeom.hpp"
#include "sim/SimConfig.hpp"
#include "sim/Simulation.hpp"
#include "sim/SweptCollision.hpp"
#include "sim/WorldCollision.hpp"
#include "sim/WorldInputs.hpp"

using Game::Sim::BodyGeom;
using Game::Sim::MakeBodyGeom;
namespace oracle = sktest::oracle;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
const char *kResourceRoot = RESOURCE_DIR; // CMake-injected, as in GameDataTest.
Game::Sim::NullWorldCollision g_NullWorld;

constexpr float kStepPx = 64.0F * 32.0F * Game::Sim::kFixedStepSeconds; // 40.96 px per step @64 u/s

// Fire along +x from the origin; `room` != 0 keeps every room-0 enemy asleep (stationary).
Game::Sim::WorldInputs FireX(int room, bool firing = true) {
    Game::Sim::WorldInputs in;
    in.playerPos = glm::vec2{0.0F, 0.0F};
    in.aimDir = glm::vec2{1.0F, 0.0F};
    in.firing = firing;
    in.playerRoomId = room;
    return in;
}

// A vertical slab wall x in [x0, x1] (any y), recording DamageObstacle calls. With
// `bulletsOnly` only bullet-sized circles (radius <= 10) are blocked, so walking enemy BODIES
// (radius > 10) pass through it and stay free to move; `hits` records only the PLAYER bullets'
// obstacle damage (radius > 5: the enemies' own bullets use the 4 px fallback radius).
class SlabWorld : public Game::Sim::WorldCollision {
public:
    SlabWorld(float x0, float x1, bool bulletsOnly = false)
        : m_X0(x0), m_X1(x1), m_BulletsOnly(bulletsOnly) {}
    bool Blocks(glm::vec2 pos, float r) const override {
        if (m_BulletsOnly && r > 10.0F) {
            return false;
        }
        return pos.x + r >= m_X0 && pos.x - r <= m_X1;
    }
    void DamageObstacle(glm::vec2 pos, float radius) override {
        if (!m_BulletsOnly || radius > 5.0F) {
            hits.push_back(pos);
        }
    }
    std::vector<glm::vec2> hits;

private:
    float m_X0;
    float m_X1;
    bool m_BulletsOnly;
};

// A destructible slab: blocks while alive, loses 1 hp per DamageObstacle, stops blocking at 0 hp.
class BreakableSlabWorld : public Game::Sim::WorldCollision {
public:
    BreakableSlabWorld(float x0, float x1, int hp) : m_X0(x0), m_X1(x1), m_Hp(hp) {}
    bool Blocks(glm::vec2 pos, float r) const override {
        return m_Alive && pos.x + r >= m_X0 && pos.x - r <= m_X1;
    }
    void DamageObstacle(glm::vec2 /*pos*/, float /*radius*/) override {
        ++damageCalls;
        if (m_Alive && --m_Hp <= 0) {
            m_Alive = false;
        }
    }
    bool Alive() const { return m_Alive; }
    int damageCalls = 0;

private:
    float m_X0;
    float m_X1;
    int m_Hp;
    bool m_Alive = true;
};

// The weapon_033 geometry/enemy used by Codex's counterexample, straight from the JSON.
struct Codex {
    Game::GameData gd;
    const Game::WeaponDef *w = nullptr;
    const Game::EnemyDef *e = nullptr;
    BodyGeom geom;
    float bulletR = 0.0F;
    /// P3.0c: the bullet's prefab shape for a heading of 0 (the tests fire along +x), for the
    /// shape-aware oracle. Since P3.0c the Simulation sweeps THIS shape (an oriented box at its
    /// prefab offset) instead of the inscribed circle at the origin (bulletR).
    oracle::ShapeSpec shape;
    bool Load() {
        if (!gd.LoadAll(kResourceRoot)) {
            return false;
        }
        w = gd.FindWeapon("weapon_033");
        e = gd.FindEnemy("e_alien01");
        if (w == nullptr || e == nullptr) {
            return false;
        }
        const Game::BulletDef *bd = gd.BulletForWeapon(*w);
        if (bd == nullptr) {
            return false;
        }
        bulletR = Game::Sim::EquivalentRadiusPx(bd->collider);
        const Game::Sim::BulletShape bs = Game::Sim::MakeBulletShape(bd->collider);
        shape.box = bs.kind == Game::Sim::BulletShape::Kind::Box;
        shape.r = bs.radiusPx;
        shape.hx = bs.halfPx.x;
        shape.hy = bs.halfPx.y;
        shape.ox = bs.offsetPx.x;
        shape.oy = bs.offsetPx.y;
        geom = MakeBodyGeom(e->body, e->hurtbox, 16.0F);
        return true;
    }
    // weapon_033 as a bullet that WALLS stop (the data one has can_through 1, which skips walls).
    Game::WeaponDef Walled() const {
        Game::WeaponDef d = *w;
        d.canThrough = 0;
        d.throughCount = 0;
        return d;
    }
};

// Tiny deterministic LCG (no <random>: the sequence must be identical everywhere).
struct Lcg {
    std::uint32_t s;
    float Next() { // uniform in [0, 1)
        s = s * 1664525U + 1013904223U;
        return static_cast<float>(s >> 8) / 16777216.0F;
    }
    float Range(float lo, float hi) { return lo + (hi - lo) * Next(); }
};

glm::vec2 PosFromHurtCentre(const BodyGeom &g, glm::vec2 hurtCentre) {
    return hurtCentre - g.hurtOffsetPx;
}
} // namespace

// ---- primitives ------------------------------------------------------------------------
TEST(SweptPrimitiveTest, SegmentCircleEntry) {
    using Game::Sim::Swept::SegmentCircleEntry;
    float t = -1.0F;
    // Through the middle of a radius-10 disc: enters at x = -10 -> t = (30-10)/60.
    EXPECT_TRUE(SegmentCircleEntry({-30.0F, 0.0F}, {30.0F, 0.0F}, 10.0F, t));
    EXPECT_NEAR(t, 20.0F / 60.0F, 1e-5F);
    // Starts inside: t = 0.
    EXPECT_TRUE(SegmentCircleEntry({1.0F, 1.0F}, {50.0F, 0.0F}, 10.0F, t));
    EXPECT_FLOAT_EQ(t, 0.0F);
    // Tangent touch counts (inclusive); 0.01 outside does not.
    EXPECT_TRUE(SegmentCircleEntry({-30.0F, 10.0F}, {30.0F, 10.0F}, 10.0F, t));
    EXPECT_FALSE(SegmentCircleEntry({-30.0F, 10.01F}, {30.0F, 10.01F}, 10.0F, t));
    // Heading away / stopping short: no entry within [0,1].
    EXPECT_FALSE(SegmentCircleEntry({-30.0F, 0.0F}, {-20.0F, 0.0F}, 10.0F, t));
    EXPECT_FALSE(SegmentCircleEntry({-30.0F, 0.0F}, {-40.0F, 0.0F}, 10.0F, t));
    // Degenerate (zero-length) segment outside.
    EXPECT_FALSE(SegmentCircleEntry({-30.0F, 0.0F}, {-30.0F, 0.0F}, 10.0F, t));
}

TEST(SweptPrimitiveTest, SegmentRoundedBoxEntryIsTheExactMinkowskiSum) {
    using Game::Sim::Swept::SegmentRoundedBoxEntry;
    const glm::vec2 half{12.8F, 16.0F};
    const float r = 8.0F;
    float t = -1.0F;
    // Straight through the flat face: enters at x = -(12.8 + 8).
    EXPECT_TRUE(SegmentRoundedBoxEntry({-60.0F, 0.0F}, {60.0F, 0.0F}, half, r, t));
    EXPECT_NEAR(t, (60.0F - 20.8F) / 120.0F, 1e-5F);
    // Just inside / outside the top face.
    EXPECT_TRUE(SegmentRoundedBoxEntry({-60.0F, 23.9F}, {60.0F, 23.9F}, half, r, t));
    EXPECT_FALSE(SegmentRoundedBoxEntry({-60.0F, 24.1F}, {60.0F, 24.1F}, half, r, t));
    // CORNER: the corner point is (12.8, 16). A segment perpendicular to the box diagonal at
    // distance d from that corner touches the rounded box iff d <= r. The bounding box grown
    // by r would report a hit for d = 8.5 (the segment crosses its square corner), so this pins
    // the exact rounded corner rather than an expanded-AABB shortcut.
    auto cornerPass = [&](float d) {
        const float k = 0.70710678F;
        const glm::vec2 p{12.8F + d * k, 16.0F + d * k};
        const glm::vec2 u{k, -k};
        float tt = 0.0F;
        return SegmentRoundedBoxEntry(p - 20.0F * u, p + 20.0F * u, half, r, tt);
    };
    EXPECT_FALSE(cornerPass(8.5F));
    EXPECT_FALSE(cornerPass(8.05F));
    EXPECT_TRUE(cornerPass(7.95F));
    EXPECT_TRUE(cornerPass(7.4F));
    // Starts inside the grown box: t = 0.
    EXPECT_TRUE(SegmentRoundedBoxEntry({0.0F, 0.0F}, {50.0F, 0.0F}, half, r, t));
    EXPECT_FLOAT_EQ(t, 0.0F);
}

// ---- (g) the oracle checks the production primitive ---------------------------------------
// 24000 deterministic random configurations (4 hurtbox shapes x 3 bullet radii, bullet and target
// both moving linearly) are compared with the brute-force sub-step oracle (2000 sub-steps, see
// SweptOracle.hpp). Contract checked per configuration:
//   * oracle sees an overlap first at sub-step j  =>  HurtSweep hits, with t in
//     [(j - 1) / 2000, j / 2000] (the true entry lies between the last free and first
//     overlapping sub-step; 1e-4 slack for float rounding), and j == 0 <=> t == 0;
//   * oracle sees NO overlap at any sub-step  =>  HurtSweep may still hit, but only as a GRAZE
//     narrower than one sub-step: the discrete oracle at the reported t with the bullet radius
//     inflated by 0.01 px must overlap (so the production claim is verified, not trusted).
TEST(SweptOracleTest, HurtSweepAgreesWithBruteForceSubstepping) {
    Codex c;
    ASSERT_TRUE(c.Load());
    const Game::EnemyDef *slime = c.gd.FindEnemy("e_slime01");
    ASSERT_NE(slime, nullptr);
    BodyGeom circle;
    circle.hurtIsBox = false;
    circle.hurtRadiusPx = 10.0F;
    circle.hurtOffsetPx = {3.0F, -5.0F};
    const BodyGeom geoms[4] = {c.geom, MakeBodyGeom(slime->body, slime->hurtbox, 16.0F),
                               BodyGeom::Fallback(16.0F), circle};
    const float radii[3] = {4.0F, 6.4F, 8.0F};
    Lcg rng{12345U};
    int cases = 0;
    int oracleHits = 0;
    int startOverlaps = 0;
    int grazeOnly = 0;
    int bad = 0;
    std::string firstBad;
    for (int i = 0; i < 24000; ++i) {
        const BodyGeom &g = geoms[i % 4];
        const float r = radii[(i / 4) % 3];
        const glm::vec2 p0{rng.Range(-50.0F, 50.0F), rng.Range(-50.0F, 50.0F)};
        glm::vec2 p1 = p0;
        if (rng.Next() < 0.7F) { // 30% stationary targets
            p1 += glm::vec2{rng.Range(-8.0F, 8.0F), rng.Range(-8.0F, 8.0F)};
        }
        // The bullet starts 25..100 px from the hurtbox centre and heads roughly toward it
        // (+-0.7 rad), so a good share of the draws really cross the box.
        const float a0 = rng.Range(0.0F, 6.2831853F);
        const glm::vec2 centre = p0 + g.hurtOffsetPx;
        const glm::vec2 c0 = centre + glm::vec2{std::cos(a0), std::sin(a0)} * rng.Range(25.0F, 100.0F);
        const float ang = a0 + 3.14159265F + rng.Range(-0.7F, 0.7F);
        const float len = rng.Next() < 0.2F ? rng.Range(0.0F, 5.0F) : rng.Range(0.0F, 120.0F);
        const glm::vec2 c1 = c0 + glm::vec2{std::cos(ang), std::sin(ang)} * len;
        ++cases;
        const int j = oracle::FirstTouchSub(g, p0, p1, c0, c1, r);
        float t = -1.0F;
        const bool hit = g.HurtSweep(p0, p1, c0, c1, r, t);
        bool ok = true;
        if (j >= 0) {
            ++oracleHits;
            const double lo = (j - 1) / static_cast<double>(oracle::kSubSteps) - 1e-4;
            const double hi = j / static_cast<double>(oracle::kSubSteps) + 1e-4;
            ok = hit && t >= lo && t <= hi && ((j == 0) == (t == 0.0F));
            if (j == 0) {
                ++startOverlaps;
            }
        } else if (hit) {
            ++grazeOnly;
            ok = oracle::Touches(g, oracle::Lerp(c0, c1, t), static_cast<double>(r) + 0.01,
                                 oracle::Lerp(p0, p1, t));
        }
        if (!ok) {
            ++bad;
            if (firstBad.empty()) {
                firstBad = "case " + std::to_string(i) + " j=" + std::to_string(j) +
                           " hit=" + std::to_string(hit) + " t=" + std::to_string(t);
            }
        }
    }
    RecordProperty("cases", cases);
    RecordProperty("oracleHits", oracleHits);
    RecordProperty("startOverlaps", startOverlaps);
    RecordProperty("grazeOnly", grazeOnly);
    EXPECT_EQ(bad, 0) << firstBad;
    EXPECT_EQ(cases, 24000);
    // Pinned (deterministic LCG, fixed geometry): the mix must keep exercising every branch.
    EXPECT_EQ(oracleHits, 7967);
    EXPECT_EQ(startOverlaps, 106);
    EXPECT_LE(grazeOnly, 10);
}

// 24000 more draws (20667 have a free path start and are used) with a vertical slab wall
// (production default SweepBlocked) in the path: HurtSweepBeforeWall must equal the oracle rule "target hit at sub-step jh counts iff
// jh <= js" (js = first sub-step whose bullet circle is blocked, found with plain Blocks()).
// Hit and wall times within 8 sub-steps (0.4% of the step: the bisection error of the sampled
// contact plus the oracle resolution) of each other are a deliberate near-tie and are skipped,
// but COUNTED (they must stay a tiny fraction). The wall time itself is also checked against the
// oracle: tw in [(js - 1) / 2000, js / 2000 + 1/256].
TEST(SweptOracleTest, HitBeforeWallRuleAgreesWithBruteForceSubstepping) {
    Codex c;
    ASSERT_TRUE(c.Load());
    const float radii[3] = {4.0F, 6.4F, 8.0F};
    Lcg rng{777U};
    int cases = 0;
    int wallCases = 0;
    int accepted = 0;       // production hit a target before the wall (or no wall)
    int vetoedByWall = 0;   // target would be hit on the full path, but the wall came first
    int nearTies = 0;
    int bad = 0;
    std::string firstBad;
    for (int i = 0; i < 24000; ++i) {
        const BodyGeom &g = c.geom;
        const float r = radii[i % 3];
        const glm::vec2 p0{rng.Range(-30.0F, 30.0F), rng.Range(-30.0F, 30.0F)};
        const glm::vec2 p1 = p0 + glm::vec2{rng.Range(-6.0F, 6.0F), rng.Range(-6.0F, 6.0F)};
        const glm::vec2 c0{rng.Range(-90.0F, 20.0F), rng.Range(-40.0F, 40.0F)};
        const float ang = rng.Range(-1.0F, 1.0F);
        const float len = rng.Range(20.0F, 70.0F);
        const glm::vec2 c1 = c0 + glm::vec2{std::cos(ang), std::sin(ang)} * len;
        const float x0 = rng.Range(-40.0F, 60.0F);
        SlabWorld world(x0, x0 + rng.Range(2.0F, 40.0F));
        if (world.Blocks(c0, r)) {
            continue; // the path start is assumed free (previous step's end): skip such draws.
        }
        ++cases;
        const float tw = world.SweepBlocked(c0, c1, r);
        const int js = oracle::FirstBlockedSub(
            c0, c1, [&](glm::vec2 p) { return world.Blocks(p, r); });
        const int jh = oracle::FirstTouchSub(g, p0, p1, c0, c1, r);
        std::string why;
        if ((tw >= 0.0F) != (js >= 0)) {
            why = "wall presence differs";
        } else if (js >= 0) {
            ++wallCases;
            const double lo = (js - 1) / static_cast<double>(oracle::kSubSteps) - 1e-4;
            const double hi = js / static_cast<double>(oracle::kSubSteps) + 1.0 / 256.0 + 1e-4;
            if (tw < lo || tw > hi) {
                why = "wall time off";
            }
        }
        float t = 0.0F;
        const bool prod = g.HurtSweepBeforeWall(p0, p1, c0, c1, r, tw, t);
        float tFull = 0.0F;
        const bool fullHit = g.HurtSweep(p0, p1, c0, c1, r, tFull);
        if (why.empty() && !(jh < 0 && fullHit)) { // skip pure grazes (see the test above)
            if (jh >= 0 && js >= 0 && std::abs(jh - js) <= 8) {
                ++nearTies;
            } else {
                const bool expected = jh >= 0 && (js < 0 || jh < js);
                if (prod != expected) {
                    why = "decision differs";
                } else if (prod) {
                    ++accepted;
                    if (std::abs(t - tFull) > 1e-6F) {
                        why = "accepted hit time changed";
                    }
                } else if (jh >= 0) {
                    ++vetoedByWall;
                }
            }
        }
        if (!why.empty()) {
            ++bad;
            if (firstBad.empty()) {
                firstBad = "case " + std::to_string(i) + ": " + why + " jh=" + std::to_string(jh) +
                           " js=" + std::to_string(js) + " tw=" + std::to_string(tw);
            }
        }
    }
    RecordProperty("cases", cases);
    RecordProperty("wallCases", wallCases);
    RecordProperty("accepted", accepted);
    RecordProperty("vetoedByWall", vetoedByWall);
    RecordProperty("nearTies", nearTies);
    EXPECT_EQ(bad, 0) << firstBad;
    EXPECT_GT(accepted, 100);
    EXPECT_GT(vetoedByWall, 100);
    EXPECT_LT(nearTies * 100, cases) << "near-ties must stay under 1% of the cases";
}

// ---- (a) Codex R30's counterexample ------------------------------------------------------
TEST(SweptCollisionTest, CodexCounterexampleHitsAtPrimitiveLevel) {
    Codex c;
    ASSERT_TRUE(c.Load());
    ASSERT_FLOAT_EQ(c.w->bulletSpeed, 64.0F);
    ASSERT_FLOAT_EQ(c.bulletR, 8.0F);
    ASSERT_TRUE(c.geom.hurtIsBox);
    ASSERT_NEAR(c.geom.hurtHalfPx.x, 12.8F, 1e-4F);
    ASSERT_FLOAT_EQ(c.e->speed, 5.0F);
    const float enemyStep = 5.0F * 32.0F * Game::Sim::kFixedStepSeconds; // 3.2 px toward the bullet
    // Relative position (bullet - hurtbox centre) goes -22.08 -> +22.08 (= 40.96 + 3.2 = 44.16
    // px of closing motion, over the 2 * (12.8 + 8) = 41.6 px hit interval).
    const glm::vec2 e0{100.0F, 0.0F};
    const glm::vec2 e1 = e0 - glm::vec2{enemyStep, 0.0F};
    const glm::vec2 hc = c.geom.hurtOffsetPx; // hurtbox centre = pos + hurtOffset
    const glm::vec2 b0 = e0 + hc + glm::vec2{-22.08F, 0.0F};
    const glm::vec2 b1 = b0 + glm::vec2{kStepPx, 0.0F};
    ASSERT_NEAR((b1 - (e1 + hc)).x, 22.08F, 1e-3F);
    // Neither endpoint overlaps (this is what the old discrete test saw; checked with the
    // independent overlap function, not the production one)...
    EXPECT_FALSE(oracle::Touches(c.geom, oracle::Lerp(b0, b0, 0.0), c.bulletR, oracle::Lerp(e0, e0, 0.0)));
    EXPECT_FALSE(oracle::Touches(c.geom, oracle::Lerp(b1, b1, 0.0), c.bulletR, oracle::Lerp(e1, e1, 0.0)));
    // ...yet the swept test hits, at t = (22.08 - 20.8) / 44.16.
    float t = -1.0F;
    ASSERT_TRUE(c.geom.HurtSweep(e0, e1, b0, b1, c.bulletR, t));
    EXPECT_NEAR(t, (22.08F - 20.8F) / 44.16F, 1e-4F);
    // ...and the brute-force oracle agrees on the sub-step: ceil(0.028985 * 2000) = 58.
    EXPECT_EQ(oracle::FirstTouchSub(c.geom, e0, e1, b0, b1, c.bulletR), 58);
    // Control: with the enemy frozen at e0 the bullet still reaches 18.88 px past its centre at
    // the end of the step (< 20.8), so even the old end-of-step test sees that variant; only the
    // approaching-enemy case above is a pure tunnel.
    float t2 = -1.0F;
    EXPECT_TRUE(c.geom.HurtSweep(e0, e0, b0, b1, c.bulletR, t2));
    EXPECT_TRUE(oracle::Touches(c.geom, oracle::Lerp(b1, b1, 0.0), c.bulletR, oracle::Lerp(e0, e0, 0.0)));
}

// Whole-Simulation version. The enemy's wander direction is seed-driven (EnemyAI01 scouts every
// 0.5 s), so the bullet meets it at an arbitrary relative velocity. For 16 seeds x every
// alignment within one bullet step, the sim must damage the enemy on EXACTLY the step where
// the INDEPENDENT brute-force oracle (2000 sub-steps of bullet and enemy over the recorded
// start/end positions, discrete overlap function from SweptOracle.hpp -- no production
// collision code) first sees an overlap -- and never otherwise. A run is a pure TUNNEL when the
// overlap exists on neither end of the hit step (sub-steps 0 and 2000): the old end-of-step rule
// would have missed it.
//
// The counts are pinned EXACTLY. They are deterministic: the seeds, the 0.25 px alignment grid,
// the fixed-step sim (no wall clock) and the integer RNG streams are all fixed, and the oracle's
// overlap decisions are pure double arithmetic on the recorded float positions. 226 of the 2816
// runs are tunnels; 2774 of the runs are hits at all (in the other 42 the enemy has walked off
// the bullet line before the bullet gets there).
TEST(SweptCollisionTest, CodexCounterexampleThroughTheSimulationMatchesTheBruteForceOracle) {
    Codex c;
    ASSERT_TRUE(c.Load());
    float bulletY = 0.0F;
    float x1 = 0.0F;
    {
        Game::Sim::Simulation probe(20240607, &g_NullWorld);
        probe.EquipWeapon(*c.w, "Gun001", 5, &c.gd);
        probe.Advance(20.0F, FireX(1));
        ASSERT_EQ(probe.Bullets().size(), 1U);
        bulletY = probe.Bullets()[0].pos.y;
        x1 = probe.Bullets()[0].pos.x;
        ASSERT_NEAR(probe.Bullets()[0].vel.x, 64.0F * 32.0F, 1e-2F);
    }
    int runs = 0;
    int hits = 0;
    int tunnelHits = 0; // oracle hits with no overlap at sub-step 0 or 2000 of the hit step
    for (int seed = 21000; seed < 21016; ++seed) {
        for (float off = 0.0F; off < 44.0F; off += 0.25F) {
            Game::Sim::Simulation sim(20240607, &g_NullWorld);
            const float ex = x1 + 30.0F * kStepPx + off;
            sim.AddEnemy(*c.e, glm::vec2{ex, bulletY - c.geom.hurtOffsetPx.y}, /*roomId=*/0, seed);
            sim.EquipWeapon(*c.w, "Gun001", 5, &c.gd);
            const int hp0 = sim.EnemyViews()[0].hp;
            glm::vec2 prevEnemy = sim.EnemyViews()[0].pos;
            glm::vec2 prevBullet{x1 - kStepPx, bulletY}; // bullet 1 spawns here at the start of step 1
            int oracleStep = -1;
            int simStep = -1;
            bool oracleTunnel = false;
            for (int step = 0; step < 60 && simStep < 0; ++step) {
                sim.Advance(20.0F, FireX(0));
                const glm::vec2 enemy = sim.EnemyViews()[0].pos;
                const glm::vec2 endBullet = prevBullet + glm::vec2{kStepPx, 0.0F};
                if (oracleStep < 0) {
                    // P3.0c: the shape-aware oracle (the bullet is weapon_033's prefab box).
                    const int j =
                        oracle::FirstTouchSubShape(c.geom, c.shape, prevEnemy, enemy, prevBullet, endBullet);
                    if (j >= 0) {
                        oracleStep = step;
                        oracleTunnel =
                            !oracle::TouchesShape(c.geom, c.shape, oracle::Lerp(prevBullet, endBullet, 0.0),
                                                  oracle::Lerp(prevEnemy, enemy, 0.0)) &&
                            !oracle::TouchesShape(c.geom, c.shape, oracle::Lerp(prevBullet, endBullet, 1.0),
                                                  oracle::Lerp(prevEnemy, enemy, 1.0));
                    }
                }
                if (sim.EnemyViews()[0].hp < hp0) {
                    simStep = step;
                }
                prevEnemy = enemy;
                prevBullet = endBullet;
            }
            ++runs;
            ASSERT_EQ(simStep, oracleStep) << "seed " << seed << " offset " << off;
            if (simStep >= 0) {
                ++hits;
                if (oracleTunnel) {
                    ++tunnelHits;
                }
            }
        }
    }
    RecordProperty("runs", runs);
    RecordProperty("hits", hits);
    RecordProperty("tunnelHits", tunnelHits);
    EXPECT_EQ(runs, 2816);
    // P3.0c ITEMISED CHANGE (docs/MASTER_PLAN.md P3.0c, golden list): hits 2774 -> 2816 and
    // tunnelHits 226 -> 0. Old: the sim swept weapon_033's bullet as its inscribed circle (r 8 px)
    // at the origin: 42 of the 2816 alignments produced no hit at all and 226 of the hits were pure
    // tunnels (neither step end overlapped). New: the sim sweeps the real prefab box (bullet_8, 48 x 16 px at
    // +9.6 px), longer than one 40.96 px step, so every alignment overlaps the box at some step end
    // (no tunnelling left for this bullet; the circle tunnel cases live in the primitive tests, the
    // 4 px fallback wall test and BulletShapeTest). simStep == oracleStep still holds for all 2816
    // runs against the shape-aware oracle.
    EXPECT_EQ(hits, 2816);
    EXPECT_EQ(tunnelHits, 0);
}

// ---- (b) near misses stay misses ---------------------------------------------------------
TEST(SweptCollisionTest, PassingOutsideTheGrownHurtboxStillMissesAtAnySpeed) {
    Codex c;
    ASSERT_TRUE(c.Load());
    const float reach = c.geom.hurtHalfPx.y + c.bulletR; // vertical contact distance (24 px)
    auto run = [&](float dy, float off) {
        Game::Sim::Simulation probe(20240607, &g_NullWorld);
        probe.EquipWeapon(*c.w, "Gun001", 5, &c.gd);
        probe.Advance(20.0F, FireX(1));
        const float by = probe.Bullets()[0].pos.y;
        const float x1 = probe.Bullets()[0].pos.x;
        Game::Sim::Simulation sim(20240607, &g_NullWorld);
        // asleep enemy (room 0, player in room 1): stationary; hurtbox centre dy above the line.
        sim.AddEnemy(*c.e,
                     glm::vec2{x1 + 1.5F * kStepPx + off, by + dy - c.geom.hurtOffsetPx.y}, 0,
                     21000);
        sim.EquipWeapon(*c.w, "Gun001", 5, &c.gd);
        const int hp0 = sim.EnemyViews()[0].hp;
        for (int i = 0; i < 12; ++i) {
            sim.Advance(20.0F, FireX(1));
        }
        return sim.EnemyViews()[0].hp < hp0;
    };
    for (float off = 0.0F; off < 42.0F; off += 3.0F) {
        EXPECT_TRUE(run(reach - 0.5F, off)) << "0.5 px inside the top contact line, off " << off;
        EXPECT_FALSE(run(reach + 0.5F, off)) << "0.5 px outside the top contact line, off " << off;
        EXPECT_TRUE(run(-(reach - 0.5F), off));
        EXPECT_FALSE(run(-(reach + 0.5F), off));
    }
}

// ---- (c) earliest hit wins ---------------------------------------------------------------
TEST(SweptCollisionTest, EarliestHitWinsWhenTwoEnemiesAreCrossedInOneStep) {
    Codex c;
    ASSERT_TRUE(c.Load());
    Game::WeaponDef w = *c.gd.FindWeapon("weapon_000"); // can_through 0: consumed by the first hit
    w.deviation = 0;
    w.bulletSpeed = 64.0F;
    const Game::EnemyDef *slime = c.gd.FindEnemy("e_slime01");
    ASSERT_NE(slime, nullptr);
    Game::Sim::Simulation probe(20240607, &g_NullWorld);
    probe.EquipWeapon(w, "Gun001", 5, &c.gd);
    probe.Advance(20.0F, FireX(1));
    ASSERT_EQ(probe.Bullets().size(), 1U);
    const float x1 = probe.Bullets()[0].pos.x; // end of step 1; step 2 covers [x1, x1 + 40.96]
    const float by = probe.Bullets()[0].pos.y;

    Game::Sim::Simulation sim(20240607, &g_NullWorld);
    const BodyGeom g = MakeBodyGeom(slime->body, slime->hurtbox, 16.0F);
    // index 0 = FAR enemy, index 1 = NEAR enemy. Both grown hurtboxes are touched by the step-2
    // segment and both contain its end point, so the old rule (lowest index) hits the far one.
    // P3.0c: the bullet is its prefab box (reach Front() ahead of the origin), so the hurtbox
    // centres are derived from that reach: the NEAR centre leaves an 8 px gap beyond the box front
    // at the END of step 1 (not touched yet), the FAR one a further 25 px. Step 2's segment then
    // grows into both (near first), and its end position overlaps both.
    const oracle::ShapeSpec shp = [&] {
        oracle::ShapeSpec sp;
        const Game::BulletDef *bd = c.gd.BulletForWeapon(w);
        const Game::Sim::BulletShape bs = Game::Sim::MakeBulletShape(bd->collider);
        sp.box = bs.kind == Game::Sim::BulletShape::Kind::Box;
        sp.r = bs.radiusPx;
        sp.hx = bs.halfPx.x;
        sp.hy = bs.halfPx.y;
        sp.ox = bs.offsetPx.x;
        sp.oy = bs.offsetPx.y;
        return sp;
    }();
    const float nearX = x1 + static_cast<float>(shp.Front()) + g.hurtHalfPx.x + 8.0F;
    sim.AddEnemy(*slime, glm::vec2{nearX + 25.0F, by - g.hurtOffsetPx.y}, 0, 21000);
    sim.AddEnemy(*slime, glm::vec2{nearX, by - g.hurtOffsetPx.y}, 0, 21001);
    sim.EquipWeapon(w, "Gun001", 5, &c.gd);
    const int farHp = sim.EnemyViews()[0].hp;
    const int nearHp = sim.EnemyViews()[1].hp;
    sim.Advance(20.0F, FireX(1));
    EXPECT_EQ(sim.EnemyViews()[0].hp, farHp);
    EXPECT_EQ(sim.EnemyViews()[1].hp, nearHp);
    sim.Advance(20.0F, FireX(1)); // the bullet crosses the near enemy first, then ends inside the far one
    EXPECT_LT(sim.EnemyViews()[1].hp, nearHp) << "the NEAR enemy (index 1) is hit first";
    EXPECT_EQ(sim.EnemyViews()[0].hp, farHp) << "the far enemy (index 0) is not";
}

// ---- (d) walls ---------------------------------------------------------------------------
// A fast bullet scans a range of wall positions: it never passes the wall, and the enemy
// standing behind the wall is never hit. With the fallback 4 px bullet radius a ONE-CELL wall
// (32 px) leaves a 40 px blocked interval for the bullet centre, shorter than the 40.96 px
// step, so for some alignments the old end-of-step test jumped over it (counted below).
TEST(SweptCollisionTest, FastBulletCannotTunnelThroughAOneCellWall) {
    Codex c;
    ASSERT_TRUE(c.Load());
    Game::WeaponDef w = *c.gd.FindWeapon("weapon_000"); // can_through 0
    w.deviation = 0;
    w.bulletSpeed = 64.0F;
    w.bulletPrefab.clear(); // fallback radius 4 px
    const Game::EnemyDef *slime = c.gd.FindEnemy("e_slime01");
    ASSERT_NE(slime, nullptr);
    Game::Sim::Simulation probe(20240607, &g_NullWorld);
    probe.EquipWeapon(w, "Gun001", 5, &c.gd);
    probe.Advance(20.0F, FireX(1));
    ASSERT_EQ(probe.Bullets().size(), 1U);
    const float x1 = probe.Bullets()[0].pos.x;
    const float by = probe.Bullets()[0].pos.y;
    ASSERT_NEAR(probe.Bullets()[0].radiusPx, 0.0F, 1e-6F); // fallback 4 px

    int discreteSkipAlignments = 0;
    for (float wallOff = 5.0F; wallOff < 5.0F + kStepPx; wallOff += 0.5F) {
        const float wx0 = x1 + wallOff;
        SlabWorld world(wx0, wx0 + 32.0F);
        // Would the OLD end-of-step test have seen the wall? Bullet centres are x1 + k*step.
        bool discreteSees = false;
        for (int k = 0; k < 6; ++k) {
            discreteSees = discreteSees || world.Blocks({x1 + k * kStepPx, by}, 4.0F);
        }
        if (!discreteSees) {
            ++discreteSkipAlignments;
        }
        Game::Sim::Simulation sim(20240607, &world);
        const BodyGeom g = MakeBodyGeom(slime->body, slime->hurtbox, 16.0F);
        // a target right behind the wall (asleep): must stay untouched.
        sim.AddEnemy(*slime, glm::vec2{wx0 + 32.0F + 24.0F, by - g.hurtOffsetPx.y}, 0, 21000);
        sim.EquipWeapon(w, "Gun001", 5, &c.gd);
        const int hp0 = sim.EnemyViews()[0].hp;
        float maxX = -1.0e9F;
        for (int i = 0; i < 12; ++i) {
            sim.Advance(20.0F, FireX(1));
            for (const auto &b : sim.Bullets()) {
                maxX = std::max(maxX, b.pos.x);
            }
        }
        EXPECT_EQ(sim.EnemyViews()[0].hp, hp0) << "bullet tunnelled; wall offset " << wallOff;
        EXPECT_LT(maxX, wx0 + 32.0F + 4.0F) << "bullet centre got through; wall offset " << wallOff;
        EXPECT_FALSE(world.hits.empty()) << "the wall hit was never reported";
        for (const glm::vec2 &p : world.hits) { // reported at the CONTACT point, not past the wall
            EXPECT_LE(p.x, wx0 + 4.0F + 0.2F);
            EXPECT_GE(p.x, wx0 - 4.0F - 0.2F);
        }
    }
    EXPECT_GT(discreteSkipAlignments, 0) << "no alignment exercised the old tunnelling case";
}

TEST(SweptCollisionTest, ThinWallAndTargetInFrontOfTheWall) {
    Codex c;
    ASSERT_TRUE(c.Load());
    Game::WeaponDef w = *c.gd.FindWeapon("weapon_000");
    w.deviation = 0;
    w.bulletSpeed = 64.0F;
    const Game::EnemyDef *slime = c.gd.FindEnemy("e_slime01");
    ASSERT_NE(slime, nullptr);
    Game::Sim::Simulation probe(20240607, &g_NullWorld);
    probe.EquipWeapon(w, "Gun001", 5, &c.gd);
    probe.Advance(20.0F, FireX(1));
    const float x1 = probe.Bullets()[0].pos.x;
    const float by = probe.Bullets()[0].pos.y;
    const BodyGeom g = MakeBodyGeom(slime->body, slime->hurtbox, 16.0F);
    // A 2 px sliver (door seal / thin collider): blocked interval 2 + 2*6.4 = 14.8 px << 41 px.
    {
        SlabWorld world(x1 + 60.0F, x1 + 62.0F);
        Game::Sim::Simulation sim(20240607, &world);
        sim.AddEnemy(*slime, glm::vec2{x1 + 62.0F + 40.0F, by - g.hurtOffsetPx.y}, 0, 21000);
        sim.EquipWeapon(w, "Gun001", 5, &c.gd);
        const int hp0 = sim.EnemyViews()[0].hp;
        for (int i = 0; i < 12; ++i) {
            sim.Advance(20.0F, FireX(1));
        }
        EXPECT_EQ(sim.EnemyViews()[0].hp, hp0);
        EXPECT_FALSE(world.hits.empty());
    }
    // Enemy IN FRONT of the wall (touched earlier on the same path): it is hit, the wall is NOT
    // damaged (ONE shot, so no later bullet can reach the wall once the enemy is dead).
    {
        SlabWorld world(x1 + 120.0F, x1 + 152.0F);
        Game::Sim::Simulation sim(20240607, &world);
        sim.AddEnemy(*slime, glm::vec2{x1 + 60.0F, by - g.hurtOffsetPx.y}, 0, 21000);
        sim.EquipWeapon(w, "Gun001", 5, &c.gd);
        const int hp0 = sim.EnemyViews()[0].hp;
        sim.Advance(20.0F, FireX(1, /*firing=*/true));
        for (int i = 0; i < 11; ++i) {
            sim.Advance(20.0F, FireX(1, /*firing=*/false));
        }
        EXPECT_LT(sim.EnemyViews()[0].hp, hp0);
        EXPECT_TRUE(world.hits.empty()) << "the target was struck first; the wall must not be damaged";
    }
}

// ---- (f) R31: wall contact and target hit share ONE time scale -----------------------------
// Codex R31's analytic counterexample. Bullet radius 6.4 (bullet_0), x 0 -> 40.96; a wall whose
// face is at x = 26.4 is touched when the centre reaches x = 20, i.e. tw = 20 / 40.96 = 0.48828125.
// The alien hurtbox centre goes (10, 25) -> (10, 21.8). The vertical hit distance is
// 16 + 6.4 = 22.4. At the wall contact the centre is at y = 25 - 0.48828125 * 3.2 = 23.4375 > 22.4,
// so the bullet is stopped BEFORE it could touch the target: MISS. (The pre-fix code shortened the
// bullet to the wall contact point but still swept the target over its FULL step, mixing two time
// scales; it reported a hit at parameter 0.8125 -- reproduced below with the shortened segment.)
// On the full path the bullet is already past the hurtbox in x when y finally closes (x range
// of contact is [-9.2, 29.2], reached up to t = 0.713 < 0.8125), so the hit would not exist
// even without the wall; the VETO variant below makes the wall the only reason for the miss.
TEST(SweptCollisionTest, WallContactBeforeTheTargetHitMissesOnOneTimeScale) {
    Codex c;
    ASSERT_TRUE(c.Load());
    const float r = 6.4F;
    ASSERT_NEAR(c.geom.hurtHalfPx.y, 16.0F, 1e-4F);
    ASSERT_NEAR(c.geom.hurtHalfPx.x, 12.8F, 1e-4F);
    const glm::vec2 b0{0.0F, 0.0F};
    const glm::vec2 b1{kStepPx, 0.0F};
    SlabWorld world(26.4F, 400.0F);
    const float tw = world.SweepBlocked(b0, b1, r);
    EXPECT_NEAR(tw, 0.48828125F, 0.001F);
    EXPECT_GE(tw, 0.48828125F - 1e-6F) << "the bisection never reports a fraction BEFORE the contact";
    // y of the target hurtbox centre at the wall contact (double arithmetic, no production code).
    const double yAtWall = 25.0 - 0.48828125 * 3.2;
    EXPECT_NEAR(yAtWall, 23.4375, 1e-9);
    EXPECT_GT(yAtWall, 22.4);

    const glm::vec2 e0 = PosFromHurtCentre(c.geom, {10.0F, 25.0F});
    const glm::vec2 e1 = PosFromHurtCentre(c.geom, {10.0F, 21.8F});
    float t = -1.0F;
    // Fixed behaviour: full bullet path, hit only if t_hit <= tw  ->  MISS.
    EXPECT_FALSE(c.geom.HurtSweepBeforeWall(e0, e1, b0, b1, r, tw, t));
    // Brute force agrees: the bullet is blocked at sub-step ceil(0.48828125 * 2000) = 977, and the
    // oracle never sees an overlap on the full path either.
    EXPECT_EQ(oracle::FirstBlockedSub(b0, b1, [&](glm::vec2 p) { return world.Blocks(p, r); }), 977);
    EXPECT_EQ(oracle::FirstTouchSub(c.geom, e0, e1, b0, b1, r), -1);
    // The pre-fix failure mode, reproduced: the bullet cut at the wall contact point, the target
    // on its full step -> a bogus hit at parameter 0.8125 (of the SHORTENED segment).
    const glm::vec2 bCut = b0 + (b1 - b0) * tw;
    ASSERT_TRUE(c.geom.HurtSweep(e0, e1, b0, bCut, r, t));
    EXPECT_NEAR(t, 0.8125F, 2e-3F);

    // VETO variant: target (10, 25) -> (10, 20). Full path alone: hit at t = (25 - 22.4) / 5 = 0.52
    // (inside the x range too). The wall comes first (tw = 0.488 < 0.52): MISS with the wall.
    {
        const glm::vec2 v1 = PosFromHurtCentre(c.geom, {10.0F, 20.0F});
        EXPECT_TRUE(c.geom.HurtSweep(e0, v1, b0, b1, r, t));
        EXPECT_NEAR(t, 0.52F, 1e-4F);
        EXPECT_FALSE(c.geom.HurtSweepBeforeWall(e0, v1, b0, b1, r, tw, t));
        const int jv = oracle::FirstTouchSub(c.geom, e0, v1, b0, b1, r);
        EXPECT_GE(jv, 1040); // 0.52 * 2000, the exact boundary sub-step (+1 for float rounding)
        EXPECT_LE(jv, 1041);
        EXPECT_TRUE(c.geom.HurtSweepBeforeWall(e0, v1, b0, b1, r, /*no wall*/ -1.0F, t));
        EXPECT_NEAR(t, 0.52F, 1e-4F);
    }
    // IN-FRONT variant: target (10, 25) -> (10, 18): hit at t = 2.6 / 7 = 0.3714 < tw: HIT, and the
    // hit time is reported unchanged.
    {
        const glm::vec2 f1 = PosFromHurtCentre(c.geom, {10.0F, 18.0F});
        ASSERT_TRUE(c.geom.HurtSweepBeforeWall(e0, f1, b0, b1, r, tw, t));
        EXPECT_NEAR(t, 2.6F / 7.0F, 1e-4F);
        EXPECT_LT(t, tw);
    }
}

// The tie rule, exactly: a target hit at the SAME fraction as the wall contact is TARGET-FIRST.
// Circle hurtbox radius 16 + bullet 4 = 20; bullet (-52, 0) -> (12, 0) enters at x = -20, i.e.
// t = 32 / 64 = 0.5 exactly (all values are exact binary fractions, no rounding).
TEST(SweptCollisionTest, WallAndTargetAtTheSameFractionIsATargetFirstTie) {
    const BodyGeom g = BodyGeom::Fallback(16.0F);
    const glm::vec2 zero{0.0F, 0.0F};
    const glm::vec2 b0{-52.0F, 0.0F};
    const glm::vec2 b1{12.0F, 0.0F};
    float t = -1.0F;
    ASSERT_TRUE(g.HurtSweep(zero, zero, b0, b1, 4.0F, t));
    ASSERT_EQ(t, 0.5F);
    EXPECT_TRUE(g.HurtSweepBeforeWall(zero, zero, b0, b1, 4.0F, 0.5F, t)) << "tie: target first";
    EXPECT_EQ(t, 0.5F);
    EXPECT_FALSE(g.HurtSweepBeforeWall(zero, zero, b0, b1, 4.0F, 0.4999F, t)) << "wall strictly first";
    EXPECT_TRUE(g.HurtSweepBeforeWall(zero, zero, b0, b1, 4.0F, 0.5001F, t));
    EXPECT_TRUE(g.HurtSweepBeforeWall(zero, zero, b0, b1, 4.0F, -1.0F, t)) << "no wall";
    // A wall at the very end of the step (tw = 1, e.g. blocked only at the end point) never hides
    // a target touched at any time up to and including the end; a touch only by the discrete
    // end-of-step fallback is reported at t = 1 and ties target-first as well.
    EXPECT_TRUE(g.HurtSweepBeforeWall(zero, zero, b0, b1, 4.0F, 1.0F, t));
    EXPECT_TRUE(g.HurtSweepBeforeWall(zero, zero, b0, glm::vec2{-20.0F, 0.0F}, 4.0F, 1.0F, t));
    EXPECT_EQ(t, 1.0F);
    EXPECT_FALSE(g.HurtSweepBeforeWall(zero, zero, b0, glm::vec2{-20.0F, 0.0F}, 4.0F, 0.99F, t));

    // Same rule through the Simulation: the production SweepBlocked is replaced by a fixed
    // answer for step 2 so the wall fraction is exact. weapon_000 at 64 u/s with the fallback radius 4 and a stationary
    // slime: step 2 covers [x1, x1 + 40.96]; put the slime so its grown hurtbox starts at
    // fraction exactly 0.5 of that step.
    Codex c;
    ASSERT_TRUE(c.Load());
    Game::WeaponDef w = *c.gd.FindWeapon("weapon_000");
    w.deviation = 0;
    w.bulletSpeed = 64.0F;
    w.bulletPrefab.clear(); // fallback radius 4
    const Game::EnemyDef *slime = c.gd.FindEnemy("e_slime01");
    ASSERT_NE(slime, nullptr);
    const BodyGeom sg = MakeBodyGeom(slime->body, slime->hurtbox, 16.0F);
    ASSERT_TRUE(sg.hurtIsBox);
    Game::Sim::Simulation probe(20240607, &g_NullWorld);
    probe.EquipWeapon(w, "Gun001", 5, &c.gd);
    probe.Advance(20.0F, FireX(1));
    const float x1 = probe.Bullets()[0].pos.x;
    const float by = probe.Bullets()[0].pos.y;
    const float entryX = x1 + 0.5F * kStepPx; // bullet centre x at fraction 0.5 of step 2
    // hurtbox left edge grown by 4 touches the centre at entryX  =>  hurtbox centre x:
    const float hcx = entryX + sg.hurtHalfPx.x + 4.0F;
    // The world answers the wall query per call: call 1 is step 1 (bullet far from everything),
    // call 2 the contested step 2.
    class StepWorld : public Game::Sim::WorldCollision {
    public:
        explicit StepWorld(float tw2) : m_Tw2(tw2) {}
        bool Blocks(glm::vec2, float) const override { return false; }
        float SweepBlocked(glm::vec2, glm::vec2, float) const override {
            return ++m_Calls == 2 ? m_Tw2 : -1.0F; // call 1 = step 1, call 2 = the contested step 2
        }

    private:
        float m_Tw2;
        mutable int m_Calls = 0;
    };
    auto hitWithStepWall = [&](float tw2) {
        StepWorld world(tw2);
        Game::Sim::Simulation sim(20240607, &world);
        sim.AddEnemy(*slime, glm::vec2{hcx - sg.hurtOffsetPx.x, by - sg.hurtOffsetPx.y}, 0, 21000);
        sim.EquipWeapon(w, "Gun001", 5, &c.gd);
        const int hp0 = sim.EnemyViews()[0].hp;
        sim.Advance(20.0F, FireX(1, true));
        sim.Advance(20.0F, FireX(1, false));
        return sim.EnemyViews()[0].hp < hp0;
    };
    const float tHit = (entryX - x1) / kStepPx; // ~0.5 up to float rounding of x1
    EXPECT_TRUE(hitWithStepWall(tHit + 0.01F)) << "wall after the target: hit";
    EXPECT_TRUE(hitWithStepWall(1.0F)) << "wall at the very end of the step: target first";
    EXPECT_FALSE(hitWithStepWall(tHit - 0.01F)) << "wall before the target: miss";
    EXPECT_TRUE(hitWithStepWall(-1.0F)) << "no wall: hit";
}

// A STATIONARY target next to a wall, scanned over wall positions with 0.25 px resolution: the
// bullet hits it iff the target contact comes before the wall contact, an ANALYTIC criterion (no
// production collision code): the bullet centre first touches the grown hurtbox at
// x = hurtCentreX - (halfX + r) and the wall at x = wallFace - r; target first iff the first is
// smaller. Wall positions within 0.3 px of the tie are skipped (the sampled wall contact is
// rounded up by the bisection, <= 1/256 of one sample gap < 0.1 px); the hit case also asserts
// the wall was NOT damaged and the miss case that it WAS (once).
TEST(SweptCollisionTest, StationaryTargetNextToAWallFollowsTheAnalyticContactOrder) {
    Codex c;
    ASSERT_TRUE(c.Load());
    const Game::WeaponDef w = c.Walled();
    Game::Sim::Simulation probe(20240607, &g_NullWorld);
    probe.EquipWeapon(w, "Gun001", 5, &c.gd);
    probe.Advance(20.0F, FireX(1));
    ASSERT_EQ(probe.Bullets().size(), 1U);
    const float x1 = probe.Bullets()[0].pos.x;
    const float bulletY = probe.Bullets()[0].pos.y;
    const float hcx = x1 + 10.0F * kStepPx + 7.3F; // hurtbox centre x, not step aligned
    int hitRuns = 0;
    int missRuns = 0;
    int skipped = 0;
    for (float wallRel = -40.0F; wallRel <= 10.0F; wallRel += 0.25F) {
        const float face = hcx + wallRel;
        const double targetEnter = static_cast<double>(hcx) - (c.geom.hurtHalfPx.x + c.bulletR);
        const double wallEnter = static_cast<double>(face) - c.bulletR;
        if (std::abs(targetEnter - wallEnter) < 0.3) {
            ++skipped;
            continue;
        }
        const bool expectHit = targetEnter < wallEnter;
        SlabWorld world(face, face + 64.0F, /*bulletsOnly=*/true);
        Game::Sim::Simulation sim(20240607, &world);
        sim.AddEnemy(*c.e, glm::vec2{hcx - c.geom.hurtOffsetPx.x, bulletY - c.geom.hurtOffsetPx.y}, 0, 21000);
        sim.EquipWeapon(w, "Gun001", 5, &c.gd);
        const int hp0 = sim.EnemyViews()[0].hp;
        sim.Advance(20.0F, FireX(1, true));
        for (int i = 0; i < 30; ++i) {
            sim.Advance(20.0F, FireX(1, false));
        }
        const bool hit = sim.EnemyViews()[0].hp < hp0;
        EXPECT_EQ(hit, expectHit) << "wallRel " << wallRel;
        if (expectHit) {
            ++hitRuns;
            EXPECT_TRUE(world.hits.empty()) << "target first: the wall must not be damaged, wallRel " << wallRel;
        } else {
            ++missRuns;
            EXPECT_EQ(world.hits.size(), 1U) << "wall first: damaged exactly once, wallRel " << wallRel;
        }
    }
    RecordProperty("hitRuns", hitRuns);
    RecordProperty("missRuns", missRuns);
    EXPECT_GT(hitRuns, 40);
    EXPECT_GT(missRuns, 40);
    EXPECT_LT(skipped, 6);
}

// MOVING targets next to a wall, whole Simulation: 16 seeds x 44 alignments x 6 wall positions.
// The enemy (e_alien01, awake, wandering at 3.2 px/step) walks while weapon_033-as-a-walled-bullet
// flies; the INDEPENDENT oracle (brute-force sub-steps of bullet and enemy, plain Blocks() of the
// test world for the wall) decides, per step, whether the target is hit before the wall (hit
// sub-step <= wall sub-step), the wall comes first, or neither; the sim must agree on the hit
// step, and an obstacle must be damaged (exactly once) iff the wall won. Runs whose hit and wall
// sub-steps are within 8 of each other are near-ties and are counted, not asserted.
TEST(SweptCollisionTest, MovingTargetAndWallThroughTheSimulationMatchTheBruteForceOracle) {
    Codex c;
    ASSERT_TRUE(c.Load());
    ASSERT_GT(c.geom.bodyRadiusPx, 10.0F) << "the bullets-only slab must not block the walking body";
    const Game::WeaponDef w = c.Walled();
    float bulletY = 0.0F;
    float x1 = 0.0F;
    {
        Game::Sim::Simulation probe(20240607, &g_NullWorld);
        probe.EquipWeapon(w, "Gun001", 5, &c.gd);
        probe.Advance(20.0F, FireX(1));
        ASSERT_EQ(probe.Bullets().size(), 1U);
        bulletY = probe.Bullets()[0].pos.y;
        x1 = probe.Bullets()[0].pos.x;
        ASSERT_FALSE(probe.Bullets()[0].canThrough);
    }
    const float wallRels[6] = {-30.0F, -22.0F, -16.0F, -12.0F, -8.0F, 0.0F};
    int runs = 0;
    int targetFirst = 0;  // oracle: target hit, wall NOT damaged
    int targetFirstWithWallOnPath = 0; // ...and the full path really did reach the wall later
    int wallFirst = 0;    // oracle: wall stopped the bullet
    int vetoedHits = 0;   // wall first although the target is touched on the full path afterwards
    int nearTies = 0;
    for (int seed = 21000; seed < 21016; ++seed) {
        for (float off = 0.0F; off < 44.0F; off += 1.0F) {
            for (const float wallRel : wallRels) {
                const float ex = x1 + 30.0F * kStepPx + off;
                const float face = ex + c.geom.hurtOffsetPx.x + wallRel;
                SlabWorld world(face, face + 64.0F, /*bulletsOnly=*/true);
                Game::Sim::Simulation sim(20240607, &world);
                sim.AddEnemy(*c.e, glm::vec2{ex, bulletY - c.geom.hurtOffsetPx.y}, /*roomId=*/0, seed);
                sim.EquipWeapon(w, "Gun001", 5, &c.gd);
                const int hp0 = sim.EnemyViews()[0].hp;
                glm::vec2 prevEnemy = sim.EnemyViews()[0].pos;
                glm::vec2 prevBullet{x1 - kStepPx, bulletY};
                int oracleStep = -1;
                int oracleKind = 0; // 1 = target first, 2 = wall first
                bool oracleNearTie = false;
                bool oracleVeto = false;
                bool oracleWallOnPath = false;
                int simStep = -1;
                for (int step = 0; step < 60 && simStep < 0; ++step) {
                    sim.Advance(20.0F, FireX(0, step == 0));
                    ASSERT_LE(sim.Bullets().size(), 1U);
                    const glm::vec2 enemy = sim.EnemyViews()[0].pos;
                    const glm::vec2 endBullet = prevBullet + glm::vec2{kStepPx, 0.0F};
                    if (oracleKind == 0) {
                        // P3.0c: the bullet is weapon_033's prefab box; against this y-spanning slab the
                        // box is blocked exactly when its x extent [x + rear, x + front] meets the slab.
                        const int jh = oracle::FirstTouchSubShape(c.geom, c.shape, prevEnemy, enemy,
                                                                  prevBullet, endBullet);
                        const float front = static_cast<float>(c.shape.Front());
                        const float rear = static_cast<float>(c.shape.Rear());
                        // pre-filter: a path that stays left of the wall face cannot be blocked.
                        const int js = std::max(prevBullet.x, endBullet.x) + front < face
                                           ? -1
                                           : oracle::FirstBlockedSub(prevBullet, endBullet, [&](glm::vec2 p) {
                                                 return p.x + front >= face && p.x + rear <= face + 64.0F;
                                             });
                        if (jh >= 0 || js >= 0) {
                            oracleStep = step;
                            oracleKind = (jh >= 0 && (js < 0 || jh <= js)) ? 1 : 2;
                            oracleNearTie = jh >= 0 && js >= 0 && std::abs(jh - js) <= 8;
                            oracleVeto = oracleKind == 2 && jh >= 0;
                            oracleWallOnPath = js >= 0;
                        }
                    }
                    if (sim.EnemyViews()[0].hp < hp0) {
                        simStep = step;
                    }
                    prevEnemy = enemy;
                    prevBullet = endBullet;
                    if (oracleKind == 2 && step >= oracleStep + 3) {
                        break; // wall won: three more steps are enough to see no late hit.
                    }
                }
                ++runs;
                if (oracleNearTie) {
                    ++nearTies;
                    continue;
                }
                ASSERT_NE(oracleKind, 0) << "neither wall nor target reached: seed " << seed << " off " << off;
                if (oracleKind == 1) {
                    ++targetFirst;
                    if (oracleWallOnPath) {
                        ++targetFirstWithWallOnPath;
                    }
                    EXPECT_EQ(simStep, oracleStep) << "seed " << seed << " off " << off << " wall " << wallRel;
                    EXPECT_TRUE(world.hits.empty())
                        << "target hit first: the wall must not be damaged; seed " << seed << " off " << off
                        << " wall " << wallRel;
                } else {
                    ++wallFirst;
                    if (oracleVeto) {
                        ++vetoedHits;
                    }
                    EXPECT_EQ(simStep, -1) << "wall first, the target must not be hit; seed " << seed << " off "
                                           << off << " wall " << wallRel << " (oracle step " << oracleStep << ")";
                    EXPECT_EQ(world.hits.size(), 1U) << "seed " << seed << " off " << off << " wall " << wallRel;
                }
            }
        }
    }
    RecordProperty("runs", runs);
    RecordProperty("targetFirst", targetFirst);
    RecordProperty("targetFirstWithWallOnPath", targetFirstWithWallOnPath);
    RecordProperty("wallFirst", wallFirst);
    RecordProperty("vetoedHits", vetoedHits);
    RecordProperty("nearTies", nearTies);
    EXPECT_EQ(runs, 4224);
    EXPECT_GT(targetFirst, 100);
    EXPECT_GT(targetFirstWithWallOnPath, 50);
    EXPECT_GT(wallFirst, 100);
    EXPECT_GT(vetoedHits, 0) << "no run had the wall cancel an otherwise valid hit";
    EXPECT_LT(nearTies * 50, runs);
}

// ---- (f) R31: same-step box destruction ------------------------------------------------------
// Two bullets stacked on one line (count 2, fan angle 0) reach a 1-hp destructible box in the
// SAME step. Bullet A (lower id) is consumed by the box and breaks it; bullet B queries the wall
// AFTER that (wall query + DamageObstacle are per bullet, in id order), finds the cell empty and
// flies on to the enemy standing behind the box. The pre-fix code queried every bullet's wall
// before any DamageObstacle, so B was still consumed by the stale wall: 2 obstacle hits and an
// untouched enemy.
TEST(SweptCollisionTest, BulletBehindAJustBrokenBoxPassesThroughInTheSameStep) {
    Codex c;
    ASSERT_TRUE(c.Load());
    Game::WeaponDef w = *c.gd.FindWeapon("weapon_000"); // can_through 0
    w.deviation = 0;
    w.bulletSpeed = 64.0F;
    w.weaponClass = "Gun002"; // the gun brain is chosen by the def's class
    w.count = 2;
    w.angle = 0.0F; // Gun002 fan with step 0 and deviation 0: a stacked volley on one line
    const Game::EnemyDef *slime = c.gd.FindEnemy("e_slime01");
    ASSERT_NE(slime, nullptr);
    const BodyGeom g = MakeBodyGeom(slime->body, slime->hurtbox, 16.0F);
    Game::Sim::Simulation probe(20240607, &g_NullWorld);
    probe.EquipWeapon(w, "Gun002", 5, &c.gd);
    probe.Advance(20.0F, FireX(1));
    ASSERT_EQ(probe.Bullets().size(), 2U);
    ASSERT_LT(probe.Bullets()[0].id, probe.Bullets()[1].id);
    ASSERT_EQ(probe.Bullets()[0].pos.x, probe.Bullets()[1].pos.x);
    const float x1 = probe.Bullets()[0].pos.x; // end of step 1
    const float by = probe.Bullets()[0].pos.y;
    // 32 px box 60 px past x1: both bullets contact it during step 3 (path [x1 + 40.96, x1 + 81.92],
    // contact at the centre x1 + 60 - 6.4); the enemy stands 40 px behind the box.
    auto run = [&](int boxHp) {
        BreakableSlabWorld world(x1 + 60.0F, x1 + 92.0F, boxHp);
        Game::Sim::Simulation sim(20240607, &world);
        sim.AddEnemy(*slime, glm::vec2{x1 + 92.0F + 40.0F, by - g.hurtOffsetPx.y}, 0, 21000);
        sim.EquipWeapon(w, "Gun002", 5, &c.gd);
        const int hp0 = sim.EnemyViews()[0].hp;
        sim.Advance(20.0F, FireX(1, true));
        for (int i = 0; i < 9; ++i) {
            sim.Advance(20.0F, FireX(1, false));
        }
        struct R {
            int damageCalls;
            bool alive;
            bool enemyHit;
        };
        return R{world.damageCalls, world.Alive(), sim.EnemyViews()[0].hp < hp0};
    };
    const auto one = run(/*boxHp=*/1);
    EXPECT_EQ(one.damageCalls, 1) << "only bullet A may touch the box; B must see it already broken";
    EXPECT_FALSE(one.alive);
    EXPECT_TRUE(one.enemyHit) << "bullet B flew through the empty cell and hit the enemy behind it";
    // A 2-hp box survives A, so B (same step) is consumed by it too: order preserved, box breaks on B.
    const auto two = run(/*boxHp=*/2);
    EXPECT_EQ(two.damageCalls, 2);
    EXPECT_FALSE(two.alive);
    EXPECT_FALSE(two.enemyHit) << "both bullets were stopped by the box";
}

// ---- (e) slow bullets: sweep == the old discrete test --------------------------------------
TEST(SweptCollisionTest, SlowBulletSweepAgreesWithTheDiscreteBoundary) {
    Codex c;
    ASSERT_TRUE(c.Load());
    // 5 u/s = 3.2 px/step bullet marching along +x past a still alien hurtbox, many steps: it
    // is "ever hit" by the sweep exactly when a discrete overlap (independent function) sees it
    // at some step end.
    const glm::vec2 zero{0.0F, 0.0F};
    // (the grid is offset by 0.125 px so no sample sits exactly on a contact line: there the two
    // float/double evaluations may legitimately differ in the last bit)
    for (float dy = -30.125F; dy <= 30.125F; dy += 0.25F) {
        bool discrete = false;
        bool swept = false;
        glm::vec2 b{-80.0F, dy};
        for (int i = 0; i < 80 && !swept; ++i) {
            const glm::vec2 nb = b + glm::vec2{3.2F, 0.0F};
            float t = 0.0F;
            swept = c.geom.HurtSweep(zero, zero, b + c.geom.hurtOffsetPx, nb + c.geom.hurtOffsetPx, c.bulletR, t);
            const glm::vec2 nbWorld = nb + c.geom.hurtOffsetPx; // body at the origin: hurtbox centre = offset
            discrete = discrete || oracle::Touches(c.geom, oracle::Lerp(nbWorld, nbWorld, 0.0), c.bulletR,
                                                   oracle::Lerp(zero, zero, 0.0));
            b = nb;
        }
        EXPECT_EQ(swept, discrete) << "dy " << dy;
    }
}

TEST(SweptCollisionTest, MovingTargetRelativeMotion) {
    Codex c;
    ASSERT_TRUE(c.Load());
    // The TARGET walks across a stationary bullet (relative motion is all that matters).
    const glm::vec2 hc = c.geom.hurtOffsetPx;
    const glm::vec2 bullet{0.0F, 0.0F};
    float t = -1.0F;
    // Hurtbox centre moves from x=+60 to x=-60 (120 px/step) across the bullet at x=0.
    EXPECT_TRUE(c.geom.HurtSweep(glm::vec2{60.0F, 0.0F} - hc, glm::vec2{-60.0F, 0.0F} - hc, bullet, bullet,
                                 c.bulletR, t));
    EXPECT_NEAR(t, (60.0F - 20.8F) / 120.0F, 1e-4F);
    // ...and misses when it passes 0.5 px above the contact line.
    const float reach = c.geom.hurtHalfPx.y + c.bulletR;
    EXPECT_FALSE(c.geom.HurtSweep(glm::vec2{60.0F, reach + 0.5F} - hc, glm::vec2{-60.0F, reach + 0.5F} - hc,
                                  bullet, bullet, c.bulletR, t));
}

// NOLINTEND(readability-magic-numbers)
