#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/Gun001.hpp"
#include "combat/Gun016.hpp"
#include "sim/WeaponController.hpp"

using Game::Sim::WeaponController;

// NOLINTBEGIN(readability-magic-numbers)

TEST(WeaponControllerTest, SingleFiresOnIntervalWhileHeld) {
    WeaponController::Params p;
    p.kind = WeaponController::Kind::Single;
    p.fireIntervalSeconds = 0.04F; // every 2 ticks
    p.baseAngle = 0.0F;            // no spread for a clean direction check
    WeaponController w(p, 5);
    std::vector<Game::Sim::FireIntent> out;
    const glm::vec2 origin{0.0F, 0.0F};
    const glm::vec2 aim{1.0F, 0.0F};

    w.Tick(true, origin, aim, out); // tick1: first shot fires immediately
    EXPECT_EQ(out.size(), 1U);
    w.Tick(true, origin, aim, out); // tick2: on cooldown
    EXPECT_EQ(out.size(), 1U);
    w.Tick(true, origin, aim, out); // tick3: fires again
    EXPECT_EQ(out.size(), 2U);
    EXPECT_EQ(out[0].pattern, Game::Sim::FirePattern::Single);
    EXPECT_EQ(out[0].camp, 0); // player bullet
}

TEST(WeaponControllerTest, NoFireWhenNotHeld) {
    WeaponController::Params p;
    p.kind = WeaponController::Kind::Single;
    p.fireIntervalSeconds = 0.02F;
    WeaponController w(p, 1);
    std::vector<Game::Sim::FireIntent> out;
    for (int i = 0; i < 5; ++i) {
        w.Tick(false, glm::vec2{0.0F, 0.0F}, glm::vec2{1.0F, 0.0F}, out);
    }
    EXPECT_TRUE(out.empty());
}

TEST(WeaponControllerTest, ScatterMatchesGun001InLockstep) {
    WeaponController::Params p;
    p.kind = WeaponController::Kind::Single;
    p.fireIntervalSeconds = 0.02F; // every tick
    p.baseAngle = 10.0F;
    p.recoil = 0.5F;
    WeaponController w(p, 808);
    Game::Gun001 ref;
    ref.SetSeed(808);
    std::vector<Game::Sim::FireIntent> out;
    for (int i = 0; i < 6; ++i) {
        w.Tick(true, glm::vec2{0.0F, 0.0F}, glm::vec2{1.0F, 0.0F}, out);
        const float refScatter = ref.ScatterAngle(10.0F, 0.5F);
        const float gotAngle = std::atan2(out.back().dir.y, out.back().dir.x) * 180.0F / 3.14159265358979F;
        EXPECT_NEAR(gotAngle, refScatter, 1e-3F); // aim is +x(0deg), so dir angle == scatter
    }
}

TEST(WeaponControllerTest, HeatRampsWhileFiringAndCoolsWhenReleased) {
    WeaponController::Params p;
    p.kind = WeaponController::Kind::HeatMinigun;
    p.fireIntervalSeconds = 0.02F; // every tick
    p.heatMaxTime = 0.2F;          // 10 ticks to full
    WeaponController w(p, 3);
    std::vector<Game::Sim::FireIntent> out;
    for (int i = 0; i < 5; ++i) {
        w.Tick(true, glm::vec2{0.0F, 0.0F}, glm::vec2{1.0F, 0.0F}, out);
    }
    EXPECT_GT(w.HeatTime(), 0.0F);     // heat built while firing
    const float hot = w.HeatTime();
    w.Tick(false, glm::vec2{0.0F, 0.0F}, glm::vec2{1.0F, 0.0F}, out);
    EXPECT_LT(w.HeatTime(), hot);      // cooled when released
}

TEST(WeaponControllerTest, HeatMinigunScatterMatchesGun016InLockstep) {
    WeaponController::Params p;
    p.kind = WeaponController::Kind::HeatMinigun;
    p.fireIntervalSeconds = 0.02F;
    p.heatMaxTime = 2.0F;
    p.heatBaseAngle = 20.0F;
    p.heatRecoil = 0.0F;
    WeaponController w(p, 1234);
    Game::Gun016 ref;
    ref.SetSeed(1234);
    std::vector<Game::Sim::FireIntent> out;
    // Drive the heat model in parallel: heat ramps kFixedStepSeconds (0.02) per tick.
    float heat = 0.0F;
    for (int i = 0; i < 5; ++i) {
        // controller will ramp THEN fire; mirror that order.
        if (heat < 2.0F) heat += 0.02F;
        const float spread = Game::Gun016::Spread(20.0F, 0.0F, Game::Gun016::HeatRatio(heat, 2.0F));
        const float refScatter = ref.ScatterAngle(spread);
        w.Tick(true, glm::vec2{0.0F, 0.0F}, glm::vec2{1.0F, 0.0F}, out);
        const float gotAngle = std::atan2(out.back().dir.y, out.back().dir.x) * 180.0F / 3.14159265358979F;
        EXPECT_NEAR(gotAngle, refScatter, 1e-2F);
    }
}

// Determinism guard: the heat ramp runs every tick, but the gun's RNG stream must
// advance ONLY on ticks that actually emit a shot. A parallel Gun016 `ref` is drawn
// only on emit ticks; if the ramp ever made a stray draw, the k-th shot's angle would
// desync from ref's k-th draw and this test would fail.
TEST(WeaponControllerTest, HeatRampOnNoEmitTickDoesNotAdvanceRng) {
    WeaponController::Params p;
    p.kind = WeaponController::Kind::HeatMinigun;
    p.fireIntervalSeconds = 0.04F; // fires every 2 ticks -> ticks 1,3,5 ramp but don't draw
    p.heatMaxTime = 2.0F;
    p.heatBaseAngle = 20.0F;
    p.heatRecoil = 0.0F;
    WeaponController w(p, 4242);
    Game::Gun016 ref;
    ref.SetSeed(4242);

    std::vector<Game::Sim::FireIntent> out;
    float heat = 0.0F;
    int cooldown = 0;
    std::size_t emitted = 0;
    for (int i = 0; i < 8; ++i) {
        // Mirror the controller's per-tick logic: ramp heat, then the cooldown gate.
        if (heat < 2.0F) {
            heat += 0.02F;
        }
        if (cooldown > 0) {
            --cooldown;
        }
        const bool emit = (cooldown == 0);

        w.Tick(true, glm::vec2{0.0F, 0.0F}, glm::vec2{1.0F, 0.0F}, out);

        if (emit) {
            const float spread = Game::Gun016::Spread(20.0F, 0.0F, Game::Gun016::HeatRatio(heat, 2.0F));
            const float refScatter = ref.ScatterAngle(spread); // ref advances ONLY on emit ticks
            ASSERT_EQ(out.size(), emitted + 1U);               // controller emitted exactly one new shot
            ++emitted;
            const float gotAngle = std::atan2(out.back().dir.y, out.back().dir.x) * 180.0F / 3.14159265358979F;
            EXPECT_NEAR(gotAngle, refScatter, 1e-2F);
            cooldown = 2; // SecondsToTicks(0.04) == 2
        } else {
            ASSERT_EQ(out.size(), emitted); // no shot, no new FireIntent on a ramp-only tick
        }
    }
    EXPECT_EQ(emitted, 4U); // ticks 0,2,4,6 emit
}

// Faithful plateau: ShouldTickHeat's strict `shootTime < shootMaxTime` gate stops the
// ramp at the cap, so heat never runs away no matter how long firing is held.
TEST(WeaponControllerTest, HeatPlateausAtCapWhileFiring) {
    WeaponController::Params p;
    p.kind = WeaponController::Kind::HeatMinigun;
    p.fireIntervalSeconds = 0.02F; // every tick
    p.heatMaxTime = 0.04F;         // caps after 2 ramp steps
    WeaponController w(p, 7);
    std::vector<Game::Sim::FireIntent> out;
    for (int i = 0; i < 6; ++i) {
        w.Tick(true, glm::vec2{0.0F, 0.0F}, glm::vec2{1.0F, 0.0F}, out);
    }
    EXPECT_NEAR(w.HeatTime(), 0.04F, 1e-4F); // plateaued at the cap, not 6*0.02
}

TEST(WeaponControllerTest, ShotCarriesCritRepelPierceFromParams) {
    WeaponController::Params p;
    p.kind = WeaponController::Kind::Single;
    p.fireIntervalSeconds = 0.02F;
    p.baseAngle = 0.0F;
    p.critical = 25;
    p.repel = 3.0F;
    p.canThrough = true;
    p.pierce = 2;
    WeaponController w(p, 1);
    std::vector<Game::Sim::FireIntent> out;
    w.Tick(true, glm::vec2{0.0F, 0.0F}, glm::vec2{1.0F, 0.0F}, out);
    ASSERT_EQ(out.size(), 1U);
    EXPECT_EQ(out[0].critical, 25);
    EXPECT_FLOAT_EQ(out[0].repel, 3.0F);
    EXPECT_TRUE(out[0].canThrough);
    EXPECT_EQ(out[0].pierce, 2);
}

// B3 (multi-shot): a WeaponDef.count > 1 weapon emits ONE Fan intent of `count` bullets
// over fanSpreadDeg (FireSystem then expands it). No RNG scatter draw on this path.
TEST(WeaponControllerTest, MultiShotEmitsFanOfCount) {
    WeaponController::Params p;
    p.kind = WeaponController::Kind::Single; // count>1 fans regardless of the (non-heat) kind
    p.fireIntervalSeconds = 0.02F;
    p.count = 3;
    p.fanSpreadDeg = 30.0F;
    WeaponController w(p, 1);
    std::vector<Game::Sim::FireIntent> out;
    w.Tick(true, glm::vec2{0.0F, 0.0F}, glm::vec2{1.0F, 0.0F}, out);
    ASSERT_EQ(out.size(), 1U);
    EXPECT_EQ(out[0].pattern, Game::Sim::FirePattern::Fan);
    EXPECT_EQ(out[0].count, 3);
    EXPECT_FLOAT_EQ(out[0].spreadDeg, 30.0F);
    EXPECT_EQ(out[0].camp, 0);
}

// Regression: a single-shot weapon (count defaults to 1) stays on the Single path.
TEST(WeaponControllerTest, CountOneStaysSingle) {
    WeaponController::Params p;
    p.kind = WeaponController::Kind::Single;
    p.fireIntervalSeconds = 0.02F;
    p.baseAngle = 0.0F;
    WeaponController w(p, 1);
    std::vector<Game::Sim::FireIntent> out;
    w.Tick(true, glm::vec2{0.0F, 0.0F}, glm::vec2{1.0F, 0.0F}, out);
    ASSERT_EQ(out.size(), 1U);
    EXPECT_EQ(out[0].pattern, Game::Sim::FirePattern::Single);
}

// NOLINTEND(readability-magic-numbers)
