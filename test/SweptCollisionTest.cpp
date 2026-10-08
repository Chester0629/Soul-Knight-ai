// T-57 (P3.0 follow-up, Codex R30): swept (continuous) bullet collision. Since P3.0
// (1 unit = 32 px) a weapon bullet flies up to 64 units/s = ~41 px per 20 ms fixed step,
// which is more than a hurtbox is wide, so the old end-of-step overlap test let a bullet jump
// clean over its target (even head-on) or over a thin wall. Pinned here:
//   (a) Codex's exact counterexample (weapon_033 vs a head-on, approaching e_alien01, relative
//       position -22.08 -> +22.08 px, neither end overlapping) now HITS -- at primitive level
//       and through the whole Simulation for EVERY start alignment;
//   (b) a bullet passing outside the grown hurtbox (box edge and rounded corner) still MISSES;
//   (c) with several targets in one step the EARLIEST hit wins, not the lowest index;
//   (d) a fast bullet cannot tunnel through a wall, thin or one cell thick, and a target in
//       front of the wall is still hit first while one behind it is not;
//   (e) slow bullets keep the old discrete behaviour (sweep == the end-of-step test).
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include <glm/glm.hpp>

#include "data/GameData.hpp"
#include "sim/BodyGeom.hpp"
#include "sim/SimConfig.hpp"
#include "sim/Simulation.hpp"
#include "sim/SweptCollision.hpp"
#include "sim/WorldCollision.hpp"
#include "sim/WorldInputs.hpp"

using Game::Sim::BodyGeom;
using Game::Sim::MakeBodyGeom;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
const char *kResourceRoot = RESOURCE_DIR; // CMake-injected, as in GameDataTest.
Game::Sim::NullWorldCollision g_NullWorld;

constexpr float kStepPx = 64.0F * 32.0F * Game::Sim::kFixedStepSeconds; // 40.96 px per step @64 u/s

// Fire along +x from the origin; `room` != 0 keeps every room-0 enemy asleep (stationary).
Game::Sim::WorldInputs FireX(int room) {
    Game::Sim::WorldInputs in;
    in.playerPos = glm::vec2{0.0F, 0.0F};
    in.aimDir = glm::vec2{1.0F, 0.0F};
    in.firing = true;
    in.playerRoomId = room;
    return in;
}

// A vertical slab wall x in [x0, x1] (any y), recording DamageObstacle calls.
class SlabWorld : public Game::Sim::WorldCollision {
public:
    SlabWorld(float x0, float x1) : m_X0(x0), m_X1(x1) {}
    bool Blocks(glm::vec2 pos, float r) const override {
        return pos.x + r >= m_X0 && pos.x - r <= m_X1;
    }
    void DamageObstacle(glm::vec2 pos, float /*radius*/) override { hits.push_back(pos); }
    std::vector<glm::vec2> hits;

private:
    float m_X0;
    float m_X1;
};

// The weapon_033 geometry/enemy used by Codex's counterexample, straight from the JSON.
struct Codex {
    Game::GameData gd;
    const Game::WeaponDef *w = nullptr;
    const Game::EnemyDef *e = nullptr;
    BodyGeom geom;
    float bulletR = 0.0F;
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
        geom = MakeBodyGeom(e->body, e->hurtbox, 16.0F);
        return true;
    }
};
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
    // Neither endpoint overlaps (this is what the old discrete test saw)...
    EXPECT_FALSE(c.geom.HurtOverlaps(e0, b0, c.bulletR));
    EXPECT_FALSE(c.geom.HurtOverlaps(e1, b1, c.bulletR));
    // ...yet the swept test hits, at t = (22.08 - 20.8) / 44.16.
    float t = -1.0F;
    ASSERT_TRUE(c.geom.HurtSweep(e0, e1, b0, b1, c.bulletR, t));
    EXPECT_NEAR(t, (22.08F - 20.8F) / 44.16F, 1e-4F);
    // Control: with the enemy frozen at e0 the bullet still reaches 18.88 px past its centre at
    // the end of the step (< 20.8), so even the old end-of-step test sees that variant; only the
    // approaching-enemy case above is a pure tunnel.
    float t2 = -1.0F;
    EXPECT_TRUE(c.geom.HurtSweep(e0, e0, b0, b1, c.bulletR, t2));
    EXPECT_TRUE(c.geom.HurtOverlaps(e0, b1, c.bulletR));
}

// Whole-Simulation version. The enemy's wander direction is seed-driven (EnemyAI01 scouts every
// 0.5 s), so the bullet meets it at an arbitrary relative velocity. For 16 seeds x every
// alignment within one bullet step, the sim must damage the enemy on EXACTLY the step where
// the independently re-derived relative-motion oracle (BodyGeom::HurtSweep over the recorded
// start/end positions of bullet and enemy) first touches it -- and never otherwise. The old
// end-of-step test is evaluated alongside, to prove that some runs are genuine tunnels (neither
// end of the hit step overlaps) that the old rule would have missed.
TEST(SweptCollisionTest, CodexCounterexampleThroughTheSimulationMatchesTheRelativeMotionOracle) {
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
    int tunnelHits = 0; // swept hits that neither end-of-step overlap would have produced
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
            bool oracleDiscrete = false;
            for (int step = 0; step < 60 && simStep < 0; ++step) {
                sim.Advance(20.0F, FireX(0));
                const glm::vec2 enemy = sim.EnemyViews()[0].pos;
                const glm::vec2 endBullet = prevBullet + glm::vec2{kStepPx, 0.0F};
                float t = 0.0F;
                if (oracleStep < 0 &&
                    c.geom.HurtSweep(prevEnemy, enemy, prevBullet, endBullet, c.bulletR, t)) {
                    oracleStep = step;
                    oracleDiscrete = c.geom.HurtOverlaps(enemy, endBullet, c.bulletR) ||
                                     c.geom.HurtOverlaps(prevEnemy, prevBullet, c.bulletR);
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
                if (!oracleDiscrete) {
                    ++tunnelHits;
                }
            }
        }
    }
    RecordProperty("runs", runs);
    RecordProperty("hits", hits);
    RecordProperty("tunnelHits", tunnelHits);
    EXPECT_GT(hits, 100);
    EXPECT_GT(tunnelHits, 0) << "no run exercised the pure-tunnel case (" << hits << " hits in "
                             << runs << " runs)";
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
    sim.AddEnemy(*slime, glm::vec2{x1 + 60.0F, by - g.hurtOffsetPx.y}, 0, 21000);
    sim.AddEnemy(*slime, glm::vec2{x1 + 25.0F, by - g.hurtOffsetPx.y}, 0, 21001);
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
    // Enemy IN FRONT of the wall (touched earlier on the same path): it is hit, the wall is not.
    {
        SlabWorld world(x1 + 120.0F, x1 + 152.0F);
        Game::Sim::Simulation sim(20240607, &world);
        sim.AddEnemy(*slime, glm::vec2{x1 + 60.0F, by - g.hurtOffsetPx.y}, 0, 21000);
        sim.EquipWeapon(w, "Gun001", 5, &c.gd);
        const int hp0 = sim.EnemyViews()[0].hp;
        for (int i = 0; i < 12; ++i) {
            sim.Advance(20.0F, FireX(1));
        }
        EXPECT_LT(sim.EnemyViews()[0].hp, hp0);
    }
}

// ---- (e) slow bullets: sweep == the old discrete test --------------------------------------
TEST(SweptCollisionTest, SlowBulletSweepAgreesWithTheDiscreteBoundary) {
    Codex c;
    ASSERT_TRUE(c.Load());
    // 5 u/s = 3.2 px/step bullet marching along +x past a still alien hurtbox, many steps: it
    // is "ever hit" by the sweep exactly when the discrete test sees it at some step end.
    for (float dy = -30.0F; dy <= 30.0F; dy += 0.25F) {
        bool discrete = false;
        bool swept = false;
        glm::vec2 b{-80.0F, dy};
        for (int i = 0; i < 80 && !swept; ++i) {
            const glm::vec2 nb = b + glm::vec2{3.2F, 0.0F};
            float t = 0.0F;
            swept = c.geom.HurtSweep({0.0F, 0.0F}, {0.0F, 0.0F}, b + c.geom.hurtOffsetPx, nb + c.geom.hurtOffsetPx, c.bulletR, t);
            discrete = discrete || c.geom.HurtOverlaps({0.0F, 0.0F}, nb + c.geom.hurtOffsetPx, c.bulletR);
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
