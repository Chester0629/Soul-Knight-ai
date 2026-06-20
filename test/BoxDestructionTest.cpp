#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include "data/GameData.hpp" // WeaponDef
#include "sim/Simulation.hpp"
#include "sim/WorldCollision.hpp"
#include "sim/WorldInputs.hpp"
#include "world/RGBox.hpp"

using Game::RGBox;
using Game::Sim::Simulation;
using Game::Sim::WorldCollision;
using Game::Sim::WorldInputs;

// NOLINTBEGIN(readability-magic-numbers)

namespace {

// A world that is a single destructible box occupying x >= kWall -- the unit-level
// mirror of GameScene's box overlay + DamageObstacle wiring: it BLOCKS while alive,
// takes 1 damage per consumed bullet, and stops blocking once its RGBox HP hits 0.
class BreakableBoxWorld : public WorldCollision {
public:
    static constexpr float kWall = 100.0F;
    explicit BreakableBoxWorld(int hp) { m_Box.SetHp(hp); }
    bool Blocks(glm::vec2 pos, float /*radius*/) const override {
        return m_Alive && pos.x >= kWall;
    }
    void DamageObstacle(glm::vec2 pos, float /*radius*/) override {
        if (!m_Alive || pos.x < kWall) {
            return;
        }
        const RGBox::HitResult h = m_Box.Hit(1, /*sourceValid=*/true);
        if (h.registered && h.shouldDestroy) {
            m_Alive = false;
        }
    }
    bool Alive() const { return m_Alive; }

private:
    RGBox m_Box;
    bool m_Alive = true;
};

WorldInputs FirePlusX(glm::vec2 playerPos) {
    WorldInputs in;
    in.playerPos = playerPos;
    in.aimDir = glm::vec2(1.0F, 0.0F); // +x straight at the box
    in.firing = true;
    in.playerAlive = true;
    in.playerRoomId = 0;
    return in;
}

} // namespace

// Gate #2 (box blocks before break, passable after): a design box blocks bodies BEFORE destruction and
// is passable AFTER. Drives the REAL sim bullet-vs-world path
// (Simulation::IntegrateBullets -> WorldCollision::DamageObstacle) -- the exact
// wiring GameScene::DamageObstacle uses live.
TEST(BoxDestructionTest, BulletBreaksBoxBlocksBeforePassableAfter) {
    BreakableBoxWorld world(/*hp=*/1);
    Simulation sim(7, &world);
    Game::WeaponDef def{};
    def.weaponSpeed = 1.0F;
    def.bulletSpeed = 10.0F; // 150 px/s
    def.atk = 4;
    def.deviation = 0;
    sim.EquipWeapon(def, "Gun001", 808);

    // BEFORE: the box blocks at the wall.
    EXPECT_TRUE(world.Blocks(glm::vec2(BreakableBoxWorld::kWall + 1.0F, 0.0F), 16.0F));
    ASSERT_TRUE(world.Alive());

    // Fire one shot from the left; advance until the bullet crosses the wall and is
    // consumed (which fires DamageObstacle on the same step).
    WorldInputs in = FirePlusX(glm::vec2(0.0F, 0.0F));
    bool broke = false;
    for (int i = 0; i < 300 && !broke; ++i) {
        sim.Advance(20.0F, in);
        in.firing = false; // one shot is enough; let it travel into the box
        broke = !world.Alive();
    }

    // AFTER: the bullet hit the box, DamageObstacle fired, the box broke -> passable.
    EXPECT_TRUE(broke) << "the fired bullet never reached/broke the box";
    EXPECT_FALSE(world.Alive());
    EXPECT_FALSE(world.Blocks(glm::vec2(BreakableBoxWorld::kWall + 1.0F, 0.0F), 16.0F))
        << "box must be passable after destruction";
}

// A higher-HP box survives the first hit (still blocks) and breaks on the next --
// the RGBox durability gate (HP > 0) drives "blocks until enough damage".
TEST(BoxDestructionTest, MultiHpBoxSurvivesFirstHit) {
    BreakableBoxWorld world(/*hp=*/2);
    const glm::vec2 at(BreakableBoxWorld::kWall + 1.0F, 0.0F);
    ASSERT_TRUE(world.Blocks(at, 16.0F));
    world.DamageObstacle(at, 16.0F);
    EXPECT_TRUE(world.Alive()) << "2-HP box survives one hit (still blocks)";
    EXPECT_TRUE(world.Blocks(at, 16.0F));
    world.DamageObstacle(at, 16.0F);
    EXPECT_FALSE(world.Alive()) << "second hit breaks it";
    EXPECT_FALSE(world.Blocks(at, 16.0F)) << "passable after the breaking hit";
}

// NOLINTEND(readability-magic-numbers)
