// P3.0 (closes T-25): every data speed / distance is in Unity UNITS and 1 unit = 32 world px
// (the map cell, Scale::kCellPx; PPU 16 drawn at kSpriteScale 2). These tests pin the
// conversion end to end with the REAL data values: a bullet flies speed * t CELLS in t
// seconds, an enemy walks its data speed in cells per second, and the boss retreat
// threshold (decomp units) is compared against a distance in units.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "data/GameData.hpp"
#include "game/WorldScale.hpp"
#include "sim/BossController.hpp"
#include "sim/BrainFactory.hpp"
#include "sim/EnemyController.hpp"
#include "sim/FireIntent.hpp"
#include "sim/IBossBrain.hpp"
#include "sim/Scheduler.hpp"
#include "sim/SimConfig.hpp"
#include "sim/Simulation.hpp"
#include "sim/WorldCollision.hpp"
#include "sim/WorldInputs.hpp"

// NOLINTBEGIN(readability-magic-numbers)

namespace {
Game::Sim::NullWorldCollision g_NullWorld;
const char *kResourceRoot = RESOURCE_DIR; // CMake-injected, as in GameDataTest.

Game::Sim::WorldInputs Fire(bool firing) {
    Game::Sim::WorldInputs in;
    in.playerPos = glm::vec2{0.0F, 0.0F};
    in.aimDir = glm::vec2{1.0F, 0.0F};
    in.playerRoomId = 0;
    in.firing = firing;
    return in;
}
} // namespace

TEST(SpeedScaleTest, ConversionIsTheMapCellAndSharesOneSource) {
    EXPECT_FLOAT_EQ(Game::Sim::kDataSpeedToPxPerSec, Game::Scale::kCellPx);
    EXPECT_FLOAT_EQ(Game::Sim::kDataSpeedToPxPerSec, Game::Sim::kWorldUnitPx);
    EXPECT_FLOAT_EQ(Game::Sim::kDataSpeedToPxPerSec, Game::Scale::kSourcePxPerUnit * Game::Scale::kSpriteScale);
    EXPECT_FLOAT_EQ(Game::Sim::kDataSpeedToPxPerSec, 32.0F);
}

// weapon_000 (the starter pistol, Gun001): the speed the sim uses is weapons.json weapon_000
// bullet_speed = 36 units/s (WeaponDef::bulletSpeed -> BrainFactory). bullets.json bullet_0.speed
// is 10 (the bullet prefab's own default); the weapon's value overrides it, bullet_0's 10 is
// never read for the flight speed. Fired in a straight line it must cover exactly speed * t
// cells in t seconds.
TEST(SpeedScaleTest, Weapon000BulletFliesSpeedTimesTCells) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::WeaponDef *w0 = gd.FindWeapon("weapon_000");
    ASSERT_NE(w0, nullptr);
    ASSERT_FLOAT_EQ(w0->bulletSpeed, 36.0F); // the real data value (units/s)
    Game::WeaponDef w = *w0;
    w.deviation = 0; // dead straight: no scatter cone

    Game::Sim::Simulation sim(20240607, &g_NullWorld);
    sim.EquipWeapon(w, "Gun001", 5, &gd);
    sim.Advance(20.0F, Fire(true)); // step 1: the shot spawns and takes its first step
    ASSERT_EQ(sim.Bullets().size(), 1U);
    const float x0 = sim.Bullets()[0].pos.x;
    const float y0 = sim.Bullets()[0].pos.y;

    const float stepCells = 36.0F * Game::Sim::kFixedStepSeconds; // 0.72 cells per fixed step
    for (const float seconds : {0.5F, 1.0F}) {
        // Re-measure from a fresh sim per horizon so the lifetime (1.5 s) never culls it.
        Game::Sim::Simulation s(20240607, &g_NullWorld);
        s.EquipWeapon(w, "Gun001", 5, &gd);
        s.Advance(20.0F, Fire(true));
        ASSERT_EQ(s.Bullets().size(), 1U);
        const glm::vec2 start = s.Bullets()[0].pos;
        const int steps = static_cast<int>(std::lround(seconds / Game::Sim::kFixedStepSeconds));
        for (int i = 0; i < steps; ++i) {
            s.Advance(20.0F, Fire(false));
        }
        ASSERT_EQ(s.Bullets().size(), 1U);
        const float cells = (s.Bullets()[0].pos.x - start.x) / Game::Scale::kCellPx;
        // exactly speed * t cells (36 * 0.5 = 18, 36 * 1.0 = 36), well inside one step (0.72).
        EXPECT_NEAR(cells, 36.0F * seconds, 0.01F) << "t=" << seconds << " s";
        EXPECT_NEAR(cells, 36.0F * seconds, stepCells);
        EXPECT_NEAR(s.Bullets()[0].pos.y, start.y, 1e-3F); // straight line
    }
    // SLICE 4: the bullet leaves the weapon_000 gun_point, 0.15 unit (4.8 px) above the hand, and flies straight
    // along the aim, so its y stays at the muzzle height (it was 0 when the shot left the body centre).
    EXPECT_NEAR(y0, Game::Scale::UnitsToPx(w.held.gunPointY), 1e-3F);
    EXPECT_NEAR(x0, Game::Scale::UnitsToPx(w.held.gunPointX) + 36.0F * Game::Scale::kCellPx * Game::Sim::kFixedStepSeconds,
                1e-2F); // muzzle x + one fixed step of flight
}

// An enemy with data speed v (units/s) walks v cells per second. Pure steering, no wall.
TEST(SpeedScaleTest, EnemyWithDataSpeedWalksThatManyCellsPerSecond) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    for (const char *id : {"e_slime01", "e_yeti01"}) { // data speed 3 and 8 units/s
        const Game::EnemyDef *def = gd.FindEnemy(id);
        ASSERT_NE(def, nullptr) << id;
        ASSERT_GT(def->speed, 0.0F);
        Game::Sim::EnemyController e = Game::Sim::BrainFactory::MakeEnemy(*def, glm::vec2{0.0F, 0.0F}, 1);
        e.SetMoveDir(glm::vec2{1.0F, 0.0F});
        glm::vec2 pos{0.0F, 0.0F};
        const int steps = 50; // 1 s of fixed steps
        for (int i = 0; i < steps; ++i) {
            pos += e.ComputeVelocity() * Game::Sim::kFixedStepSeconds;
        }
        const float cells = pos.x / Game::Scale::kCellPx;
        EXPECT_NEAR(cells, def->speed * (1.0F + def->speedRate), 1e-3F) << id;
    }
}

// The same through the Simulation's MoveControllers (the real integration site): the enemy's
// per-step displacement while it moves is exactly speed * 32 * 0.02 px.
TEST(SpeedScaleTest, SimulationMovesAnEnemyAtItsDataSpeed) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::EnemyDef *def = gd.FindEnemy("e_slime01"); // 3 units/s = 96 px/s = 1.92 px/step
    ASSERT_NE(def, nullptr);
    Game::Sim::Simulation sim(20240607, &g_NullWorld);
    sim.AddEnemy(*def, glm::vec2{0.0F, 0.0F}, /*roomId=*/0, /*seed=*/21000);
    float maxStep = 0.0F;
    glm::vec2 prev = sim.EnemyViews()[0].pos;
    for (int i = 0; i < 200; ++i) {
        sim.Advance(20.0F, Fire(false)); // wakes the room-0 enemy; it wanders at full speed
        const glm::vec2 now = sim.EnemyViews()[0].pos;
        maxStep = std::max(maxStep, glm::length(now - prev));
        prev = now;
    }
    EXPECT_NEAR(maxStep, def->speed * Game::Sim::kDataSpeedToPxPerSec * Game::Sim::kFixedStepSeconds, 1e-3F);
}

// BossAI01's retreat threshold (Range(5,10), game_named.c:122666) is a distance in UNITS.
// The controller must hand the brain the player distance in units, not px.
namespace {
class RecordingBossBrain final : public Game::Sim::IBossBrain {
public:
    void SetSeed(int /*seed*/) override {}
    void OnHurt(int /*hpAfter*/, int /*maxHp*/) override {}
    bool Angry() const override { return false; }
    float ShootCd() const override { return 100.0F; }
    glm::vec2 MoveDecision(glm::vec2 /*chase*/, float dist) override {
        lastDist = dist;
        ++calls;
        return glm::vec2{0.0F, 0.0F};
    }
    AttackResult AttackTick(float baseCd) override {
        AttackResult r;
        r.nextCd = baseCd;
        return r;
    }
    int RngRange(int lo, int /*hi*/) override { return lo; }
    float lastDist = -1.0F;
    int calls = 0;
};
} // namespace

TEST(SpeedScaleTest, BossRetreatDistanceIsHandedToTheBrainInUnits) {
    auto brain = std::make_unique<RecordingBossBrain>();
    RecordingBossBrain *probe = brain.get();
    Game::Sim::BossController boss(std::move(brain), 2.0F, glm::vec2{0.0F, 0.0F}, 600, 1);
    Game::Sim::Scheduler sched;
    std::vector<Game::Sim::FireIntent> fire;
    boss.MutableState().awake = true;
    boss.Activate(sched, fire);
    boss.SetTarget(glm::vec2{96.0F, 0.0F}); // 96 px = 3 cells = 3 units from the boss
    for (int i = 0; i < 40; ++i) {          // past the first 0.5 s wander tick
        sched.Tick();
    }
    ASSERT_GE(probe->calls, 1);
    EXPECT_NEAR(probe->lastDist, 3.0F, 1e-4F);
}

// NOLINTEND(readability-magic-numbers)
