#include <gtest/gtest.h>

#include <vector>

#include "combat/RGBatteryController.hpp"

using Game::RGBatteryController;
using FireResult = RGBatteryController::FireResult;

// NOLINTBEGIN(readability-magic-numbers)

// ---- ShootReflection (detect-gate + charge latch) -------------------------

TEST(RGBatteryControllerTest, ShootReflectionGatedWhenCanShootClear) {
    // Gate (line 467958): can_shoot(0x71) == 0 -> charge block skipped entirely.
    RGBatteryController b;
    EXPECT_FALSE(b.CanShoot()); // ctor default: not armed
    float stopDelay = -1.0F;
    EXPECT_EQ(b.ShootReflection(stopDelay, 2.0F), FireResult::Gated);
    EXPECT_FALSE(b.IsCharging());  // no latch on the gated path
    EXPECT_FLOAT_EQ(stopDelay, -1.0F); // out-param untouched when gated
}

TEST(RGBatteryControllerTest, ShootReflectionGatedWhenAlreadyCharging) {
    // Gate (line 467958): is_charging(0x98) != 0 -> a second shot is blocked
    // even with can_shoot set.
    RGBatteryController b;
    b.SetCanShoot(true);
    b.SetIsCharging(true);
    float stopDelay = -1.0F;
    EXPECT_EQ(b.ShootReflection(stopDelay, 2.0F), FireResult::Gated);
    EXPECT_TRUE(b.CanShoot());          // can_shoot not consumed on the gated path
    EXPECT_TRUE(b.IsCharging());        // latch untouched
    EXPECT_FLOAT_EQ(stopDelay, -1.0F);  // out-param untouched
}

TEST(RGBatteryControllerTest, ShootReflectionChargedClearsArmAndLatches) {
    // Pass: clear can_shoot (0x71 = 0, line 467959), set is_charging
    // (0x98 = 1, line 467960), schedule StopShooting at shoot_duration (0x90).
    // rocket_mode false -> result is Charged, not Rocket.
    RGBatteryController b;
    b.SetCanShoot(true);
    EXPECT_FALSE(b.RocketMode());
    float stopDelay = -1.0F;
    EXPECT_EQ(b.ShootReflection(stopDelay, 3.5F), FireResult::Charged);
    EXPECT_FALSE(b.CanShoot());          // 0x71 = 0
    EXPECT_TRUE(b.IsCharging());         // 0x98 = 1
    EXPECT_FLOAT_EQ(stopDelay, 3.5F);    // Invoke("StopShooting", shoot_duration)
}

TEST(RGBatteryControllerTest, ShootReflectionRocketWhenRocketModeSet) {
    // Pass AND rocket_mode(0x80) set (line 467979) -> CreateRocket also runs;
    // result is Rocket. The charge state writes are identical to Charged.
    RGBatteryController b;
    b.SetCanShoot(true);
    b.SetRocketMode(true);
    float stopDelay = -1.0F;
    EXPECT_EQ(b.ShootReflection(stopDelay, 1.25F), FireResult::Rocket);
    EXPECT_FALSE(b.CanShoot());       // 0x71 = 0
    EXPECT_TRUE(b.IsCharging());      // 0x98 = 1
    EXPECT_FLOAT_EQ(stopDelay, 1.25F); // shoot_duration still scheduled
}

// ---- StopShooting (clear latch + re-arm cadence) --------------------------

TEST(RGBatteryControllerTest, StopShootingClearsChargeAndSchedulesReInvoke) {
    // The only state write is is_charging (0x98 = 0, line 468041) -- inverse of
    // ShootReflection's latch. outReInvokeDelay = shoot-cadence (0x6c).
    RGBatteryController b;
    b.SetIsCharging(true);
    float reInvoke = -1.0F;
    b.StopShooting(reInvoke, 4.0F);
    EXPECT_FALSE(b.IsCharging());      // 0x98 = 0
    EXPECT_FLOAT_EQ(reInvoke, 4.0F);  // Invoke("ShootReflection", shoot-cadence)
}

TEST(RGBatteryControllerTest, StopShootingIsUngatedAndIdempotentOnLatch) {
    // No gate: StopShooting always writes is_charging = 0 and reports the cadence,
    // even if already not charging.
    RGBatteryController b;
    EXPECT_FALSE(b.IsCharging());
    float reInvoke = 0.0F;
    b.StopShooting(reInvoke, 2.5F);
    EXPECT_FALSE(b.IsCharging());
    EXPECT_FLOAT_EQ(reInvoke, 2.5F);
}

TEST(RGBatteryControllerTest, ShootThenStopRoundTripsTheChargeLatch) {
    // ShootReflection sets the latch, StopShooting clears it: the cadence handoff
    // that keeps the turret firing.
    RGBatteryController b;
    b.SetCanShoot(true);
    float stopDelay = 0.0F;
    EXPECT_EQ(b.ShootReflection(stopDelay, 2.0F), FireResult::Charged);
    EXPECT_TRUE(b.IsCharging());
    float reInvoke = 0.0F;
    b.StopShooting(reInvoke, 5.0F);
    EXPECT_FALSE(b.IsCharging());
    // After StopShooting the turret is still disarmed (can_shoot stays 0 until the
    // owner's TurnCanShoot re-arms it); a fresh ShootReflection is gated.
    EXPECT_FALSE(b.CanShoot());
    EXPECT_EQ(b.ShootReflection(stopDelay, 2.0F), FireResult::Gated);
}

// ---- CreateRocket (re-Invoke head gated on is_charging) -------------------

TEST(RGBatteryControllerTest, CreateRocketReInvokesWhileCharging) {
    // Recoverable head (line 468008): when is_charging(0x98) still set, re-Invoke
    // at the fixed 1.0s delay (line 468009).
    RGBatteryController b;
    b.SetIsCharging(true);
    float reInvoke = -1.0F;
    EXPECT_TRUE(b.CreateRocket(reInvoke));
    EXPECT_FLOAT_EQ(reInvoke, RGBatteryController::kRocketReInvokeDelay);
    EXPECT_FLOAT_EQ(reInvoke, 1.0F);
}

TEST(RGBatteryControllerTest, CreateRocketDoesNotReInvokeWhenNotCharging) {
    RGBatteryController b;
    EXPECT_FALSE(b.IsCharging());
    float reInvoke = -1.0F;
    EXPECT_FALSE(b.CreateRocket(reInvoke));
    EXPECT_FLOAT_EQ(reInvoke, -1.0F); // out-param untouched when not charging
}

TEST(RGBatteryControllerTest, RocketShotLeavesCreateRocketReInvokeArmed) {
    // After a Rocket-mode ShootReflection, is_charging is set, so CreateRocket's
    // re-Invoke head would fire on the same tick.
    RGBatteryController b;
    b.SetCanShoot(true);
    b.SetRocketMode(true);
    float stopDelay = 0.0F;
    EXPECT_EQ(b.ShootReflection(stopDelay, 1.0F), FireResult::Rocket);
    float reInvoke = 0.0F;
    EXPECT_TRUE(b.CreateRocket(reInvoke)); // is_charging set by the shot
    EXPECT_FLOAT_EQ(reInvoke, 1.0F);
}

// ---- EndCycle (zero move_direction; no dead-gate) -------------------------

TEST(RGBatteryControllerTest, EndCycleZeroesMoveDirection) {
    RGBatteryController b;
    b.SetMoveDirection(glm::vec2(3.0F, -4.0F));
    b.EndCycle();
    EXPECT_FLOAT_EQ(b.MoveDirection().x, 0.0F); // 0x50 = Vector2.zero
    EXPECT_FLOAT_EQ(b.MoveDirection().y, 0.0F);
}

TEST(RGBatteryControllerTest, EndCycleHasNoDeadGate) {
    // EndCycle (line 468053) has no dead-latch gate: it zeroes move_direction
    // even on a dead battery (unlike GetHurt).
    RGBatteryController b;
    b.SetDead(true);
    b.SetMoveDirection(glm::vec2(1.0F, 1.0F));
    b.EndCycle();
    EXPECT_FLOAT_EQ(b.MoveDirection().x, 0.0F);
    EXPECT_FLOAT_EQ(b.MoveDirection().y, 0.0F);
}

// ---- GetHurt (dead-latch gate) --------------------------------------------

TEST(RGBatteryControllerTest, GetHurtRoutedWhenAlive) {
    RGBatteryController b;
    EXPECT_FALSE(b.Dead());
    EXPECT_TRUE(b.GetHurt()); // 0x0d == 0 -> routes to UICanvas
}

TEST(RGBatteryControllerTest, GetHurtIgnoredWhenDead) {
    RGBatteryController b;
    b.SetDead(true);
    EXPECT_FALSE(b.GetHurt()); // 0x0d gate -> ignored
}

// ---- Determinism: the Battery takes ZERO rng draws ------------------------

TEST(RGBatteryControllerTest, NoMethodConsumesTheRngStream) {
    // RGBatteryController has no rg_random draws anywhere; exercising every brain
    // method must leave the stream byte-identical to one that was never touched.
    RGBatteryController exercised;
    Game::RGRandom reference;
    exercised.SetSeed(2024);
    reference.SetRandomSeed(2024);

    float stopDelay = 0.0F;
    float reInvoke = 0.0F;
    for (int i = 0; i < 16; ++i) {
        exercised.SetCanShoot(true);
        exercised.SetRocketMode((i & 1) != 0);
        exercised.ShootReflection(stopDelay, 2.0F);
        exercised.CreateRocket(reInvoke);
        exercised.StopShooting(reInvoke, 3.0F);
        exercised.SetMoveDirection(glm::vec2(static_cast<float>(i), 1.0F));
        exercised.EndCycle();
        exercised.GetHurt();
    }

    // Both streams must still produce the identical next sequence.
    for (int i = 0; i < 32; ++i) {
        EXPECT_EQ(exercised.Rng().Range(0, 1000), reference.Range(0, 1000));
    }
}

TEST(RGBatteryControllerTest, GateBranchesAreDeterministic) {
    // Same input sequence -> same FireResult trace, every time (no hidden RNG).
    auto run = [] {
        RGBatteryController b;
        std::vector<int> trace;
        float stopDelay = 0.0F;
        for (int i = 0; i < 8; ++i) {
            b.SetCanShoot((i % 3) != 0);          // re-arm on some ticks
            b.SetRocketMode((i % 2) == 0);
            trace.push_back(static_cast<int>(b.ShootReflection(stopDelay, 1.0F)));
            float reInvoke = 0.0F;
            b.StopShooting(reInvoke, 1.0F);        // clear latch for the next tick
        }
        return trace;
    };
    EXPECT_EQ(run(), run());
}

// NOLINTEND(readability-magic-numbers)
