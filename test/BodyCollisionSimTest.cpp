#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include <glm/glm.hpp>

#include "Util/Collider.hpp"
#include "data/GameData.hpp"
#include "sim/Depenetration.hpp"
#include "sim/SimConfig.hpp"
#include "sim/Simulation.hpp"
#include "sim/WorldCollision.hpp"
#include "sim/WorldInputs.hpp"

using Game::Sim::Simulation;
using Game::Sim::WorldCollision;
using Game::Sim::WorldInputs;
using Util::Collider;

// NOLINTBEGIN(readability-magic-numbers)

// P2.18 AC1 / AC2 / AC5 at the SIMULATION level: an enemy or boss that starts (or is put)
// overlapping a blocker is pushed out within the first tick and then walks; and a frame with an
// absurd dt does not carry a hostile through a wall (tested on a closed pen).

namespace {
class StaticWorld : public WorldCollision {
public:
    explicit StaticWorld(std::vector<Collider> bs) : m_B(std::move(bs)) {}
    bool Blocks(glm::vec2 pos, float radius) const override {
        const Collider c = Collider::MakeCircle(pos, radius);
        for (const Collider &b : m_B) {
            if (Util::Overlap(b, c)) {
                return true;
            }
        }
        return false;
    }
    glm::vec2 Depenetrate(glm::vec2 pos, float radius) const override {
        return Game::Sim::DepenetrateAgainst(m_B, pos, radius);
    }

private:
    std::vector<Collider> m_B;
};

Game::EnemyDef WalkerDef() {
    Game::EnemyDef def{};
    def.shootCd = 0.5F;
    def.scoutRate = 0.1F;
    def.friction = 0.9F;
    return def;
}

WorldInputs InRoom(glm::vec2 playerPos, int room) {
    WorldInputs in;
    in.playerPos = playerPos;
    in.aimDir = glm::vec2{1.0F, 0.0F};
    in.playerRoomId = room;
    return in;
}
} // namespace

TEST(BodyCollisionSimTest, EnemySpawnedInsideABoxIsPushedOutOnTheFirstTickAndAsleepStaysFree) {
    // a 3x3 block of boxes with the enemy buried in its middle cell (kEnemyBodyRadius 16 fallback)
    std::vector<Collider> bs;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            bs.push_back(Collider::MakeAABB({x * 32.0F, y * 32.0F}, {32.0F, 32.0F}));
        }
    }
    StaticWorld world(bs);
    Simulation sim(777, &world);
    sim.AddEnemy(WalkerDef(), glm::vec2{0.0F, 0.0F}, /*roomId=*/2, /*seed=*/9);
    ASSERT_TRUE(world.Blocks(sim.EnemyViews()[0].pos, 16.0F)) << "starts overlapping";
    sim.Advance(20.0F, InRoom({400.0F, 0.0F}, /*room=*/1)); // different room: asleep
    const glm::vec2 p = sim.EnemyViews()[0].pos;
    EXPECT_FALSE(world.Blocks(p, 16.0F)) << "resolved within the first tick (AC2)";
    EXPECT_LT(glm::length(p), 48.0F + 16.0F + 1.0F) << "exits through the nearest face of the block";
    for (int i = 0; i < 30; ++i) {
        sim.Advance(20.0F, InRoom({400.0F, 0.0F}, 1));
    }
    EXPECT_EQ(sim.EnemyViews()[0].pos, p) << "asleep enemies stay where they were put";
}

TEST(BodyCollisionSimTest, EnemyThatSpawnedInsideABoxMovesAfterwardsWhenAwake) {
    std::vector<Collider> bs;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            bs.push_back(Collider::MakeAABB({x * 32.0F, y * 32.0F}, {32.0F, 32.0F}));
        }
    }
    StaticWorld world(bs);
    Simulation sim(777, &world);
    sim.AddEnemy(WalkerDef(), glm::vec2{0.0F, 0.0F}, /*roomId=*/2, /*seed=*/9);
    sim.Advance(20.0F, InRoom({200.0F, 0.0F}, 2)); // awake, one tick
    const glm::vec2 first = sim.EnemyViews()[0].pos;
    EXPECT_FALSE(world.Blocks(first, 16.0F));
    for (int i = 0; i < 250; ++i) {
        sim.Advance(20.0F, InRoom({200.0F, 0.0F}, 2));
        EXPECT_FALSE(world.Blocks(sim.EnemyViews()[0].pos, 16.0F)) << "tick " << i;
    }
    EXPECT_GT(glm::length(sim.EnemyViews()[0].pos - first), 1.0F) << "it walks off instead of staying pinned";
}

TEST(BodyCollisionSimTest, BossSpawnedInsideABlockerIsPushedOutToo) {
    StaticWorld world({Collider::MakeAABB({0.0F, 0.0F}, {64.0F, 64.0F})}); // a statue-sized 2x2
    Simulation sim(5, &world);
    sim.SetBoss(2.0F, glm::vec2{10.0F, 5.0F}, 500, /*roomId=*/3, /*seed=*/11);
    sim.Advance(20.0F, InRoom({300.0F, 0.0F}, 3));
    const glm::vec2 p = sim.BossView().pos;
    EXPECT_FALSE(world.Blocks(p, Game::Sim::kBossBodyRadius));
}

// AC5: a hostile in a closed pen never leaves it, whatever dt the shell passes.
TEST(BodyCollisionSimTest, HugeDtNeverCarriesAHostileThroughAWall) {
    // 200x200 interior pen with 32 px thick walls; the player stands outside, to the east.
    std::vector<Collider> bs{
        Collider::MakeAABB({0.0F, 116.0F}, {264.0F, 32.0F}),  // top    (y 100..132)
        Collider::MakeAABB({0.0F, -116.0F}, {264.0F, 32.0F}), // bottom (y -132..-100)
        Collider::MakeAABB({116.0F, 0.0F}, {32.0F, 264.0F}),  // right  (x 100..132)
        Collider::MakeAABB({-116.0F, 0.0F}, {32.0F, 264.0F})};// left   (x -132..-100)
    StaticWorld world(bs);
    Simulation sim(31337, &world);
    sim.AddEnemy(WalkerDef(), glm::vec2{50.0F, 0.0F}, /*roomId=*/2, /*seed=*/4);
    for (int i = 0; i < 400; ++i) {
        const float dt = (i % 3 == 0) ? 100000.0F : (i % 3 == 1 ? 1.0e9F : 7.0F);
        sim.Advance(dt, InRoom({400.0F, 0.0F}, 2));
        const glm::vec2 p = sim.EnemyViews()[0].pos;
        ASSERT_LT(std::abs(p.x), 100.0F - 16.0F + 0.01F) << "i=" << i << " left the pen in x";
        ASSERT_LT(std::abs(p.y), 100.0F - 16.0F + 0.01F) << "i=" << i << " left the pen in y";
    }
}

TEST(BodyCollisionSimTest, AdvanceNeverRunsMoreThanTheFrameDtCapWorthOfSteps) {
    Simulation sim(1, nullptr);
    EXPECT_EQ(sim.Advance(1.0e9F, InRoom({0.0F, 0.0F}, 0)), Game::Sim::kMaxStepsPerAdvance);
    EXPECT_EQ(Game::Sim::kMaxFrameDtMs, Game::Sim::kFixedStepMs * Game::Sim::kMaxStepsPerAdvance)
        << "the scene clamp equals what one Advance can run";
}

// NOLINTEND(readability-magic-numbers)
