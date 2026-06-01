#include <gtest/gtest.h>

#include "combat/WeaponInstance.hpp"

using Game::WeaponDef;
using Game::WeaponInstance;

namespace {
WeaponDef MakeWeapon(float weaponSpeed, int consume) {
    WeaponDef d;
    d.weaponSpeed = weaponSpeed;
    d.consume = consume;
    return d;
}
} // namespace

// NOLINTBEGIN(readability-magic-numbers)

TEST(WeaponInstanceTest, ReadyInitially) {
    const auto def = MakeWeapon(1.0F, 5);
    WeaponInstance w(def);
    EXPECT_TRUE(w.Ready());
    EXPECT_EQ(w.EnergyCost(), 5);
}

TEST(WeaponInstanceTest, FireStartsCooldown) {
    const auto def = MakeWeapon(1.0F, 5);
    WeaponInstance w(def);
    EXPECT_TRUE(w.TryFire());
    EXPECT_FALSE(w.Ready());
    EXPECT_FALSE(w.TryFire()); // still cooling down
}

TEST(WeaponInstanceTest, CooldownElapsesThenReady) {
    const auto def = MakeWeapon(1.0F, 5);
    WeaponInstance w(def);
    w.TryFire();
    w.Update(w.FireIntervalMs()); // advance exactly one interval
    EXPECT_TRUE(w.Ready());
    EXPECT_TRUE(w.TryFire());
}

TEST(WeaponInstanceTest, PartialCooldownStaysNotReady) {
    const auto def = MakeWeapon(1.0F, 5);
    WeaponInstance w(def);
    w.TryFire();
    w.Update(w.FireIntervalMs() * 0.5F);
    EXPECT_FALSE(w.Ready());
}

TEST(WeaponInstanceTest, WeaponSpeedScalesInterval) {
    const auto slow = MakeWeapon(1.0F, 0);
    const auto fast = MakeWeapon(2.0F, 0);
    WeaponInstance ws(slow);
    WeaponInstance wf(fast);
    EXPECT_GT(ws.FireIntervalMs(), wf.FireIntervalMs());
    EXPECT_FLOAT_EQ(wf.FireIntervalMs(), WeaponInstance::kBaseIntervalMs / 2.0F);
}

// NOLINTEND(readability-magic-numbers)
