#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/BossAI01.hpp"
#include "sim/BossController.hpp"
#include "sim/FireIntent.hpp"
#include "sim/Scheduler.hpp"

using Game::Sim::BossController;

// NOLINTBEGIN(readability-magic-numbers)

TEST(BossControllerTest, SpawnsWithBaseShootCd) {
    BossController b(/*baseShootCd=*/2.0F, glm::vec2{0.0F, 0.0F}, /*maxHp=*/600, 1);
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCdSeconds(), 2.0F);
    EXPECT_FLOAT_EQ(b.State().pos.x, 0.0F);
}

TEST(BossControllerTest, AngryHalvesShootCdOnceBelowHalfHp) {
    BossController b(2.0F, glm::vec2{0.0F, 0.0F}, 600, 1);
    b.OnHurt(400, 600);
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCdSeconds(), 2.0F);
    b.OnHurt(200, 600);
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCdSeconds(), 1.0F);
    b.OnHurt(50, 600);
    EXPECT_FLOAT_EQ(b.ShootCdSeconds(), 1.0F);
}

TEST(BossControllerTest, ChaseDirIsTowardPlayer) {
    BossController b(2.0F, glm::vec2{0.0F, 0.0F}, 600, 1);
    b.SetTarget(glm::vec2{10.0F, 0.0F});
    const glm::vec2 d = b.ChaseDir();
    EXPECT_NEAR(d.x, 1.0F, 1e-4F);
    EXPECT_NEAR(d.y, 0.0F, 1e-4F);
}

TEST(BossControllerTest, ShootTickEmitsBrainSelectedFanOnCadence) {
    BossController b(0.04F, glm::vec2{0.0F, 0.0F}, 600, 7); // shootCd 2 ticks
    Game::Sim::Scheduler sched;
    std::vector<Game::Sim::FireIntent> fire;
    b.MutableState().awake = true;
    b.SetTarget(glm::vec2{0.0F, 10.0F});
    b.Activate(sched, fire);

    sched.Tick();
    sched.Tick(); // shoot fires on tick 2
    ASSERT_FALSE(fire.empty());
    const Game::Sim::FireIntent &f = fire.front();
    EXPECT_EQ(f.pattern, Game::Sim::FirePattern::Fan);
    EXPECT_EQ(f.camp, 1);
    EXPECT_FLOAT_EQ(f.spreadDeg, BossController::kFanSpreadDeg);
    EXPECT_TRUE(f.count == BossController::kFanEven || f.count == BossController::kFanOdd);
}

TEST(BossControllerTest, FullCadenceReplayIsDeterministic) {
    auto run = [](int seed) {
        BossController b(0.06F, glm::vec2{0.0F, 0.0F}, 600, seed);
        Game::Sim::Scheduler sched;
        std::vector<Game::Sim::FireIntent> fire;
        b.MutableState().awake = true;
        b.SetTarget(glm::vec2{5.0F, 5.0F});
        b.Activate(sched, fire);
        std::vector<float> trace;
        for (int i = 0; i < 24; ++i) {
            sched.Tick();
            trace.push_back(b.MoveDir().x);
            trace.push_back(static_cast<float>(fire.size()));
            if (!fire.empty()) {
                trace.push_back(static_cast<float>(fire.back().count));
            }
        }
        return trace;
    };
    EXPECT_EQ(run(99), run(99));
}

// NOLINTEND(readability-magic-numbers)
