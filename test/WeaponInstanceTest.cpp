#include <gtest/gtest.h>

#include <cmath>

#include <glm/glm.hpp>

#include "combat/WeaponInstance.hpp"
#include "data/RGRandom.hpp"

using Game::BulletSpawn;
using Game::FirePlan;
using Game::RGRandom;
using Game::WeaponDef;
using Game::WeaponInstance;

namespace {
WeaponDef MakeWeapon(float weaponSpeed, int consume) {
    WeaponDef d;
    d.weaponSpeed = weaponSpeed;
    d.consume = consume;
    return d;
}

RGRandom Seeded(int seed) {
    RGRandom r;
    r.SetRandomSeed(seed);
    return r;
}

float Angle2D(glm::vec2 v) {
    return std::atan2(v.y, v.x) * 180.0F / 3.14159265358979323846F;
}
} // namespace

// NOLINTBEGIN(readability-magic-numbers)

// ---------------------------------------------------------------------------
// Fire-control timing (existing behaviour - must keep working).
// ---------------------------------------------------------------------------

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

// Fire-rate == base / weapon_speed for a representative weapon_speed.
TEST(WeaponInstanceTest, FireRateIsBaseOverWeaponSpeed) {
    const auto def = MakeWeapon(1.5F, 0);
    WeaponInstance w(def);
    EXPECT_FLOAT_EQ(w.FireIntervalMs(),
                    WeaponInstance::kBaseIntervalMs / 1.5F);
}

// ---------------------------------------------------------------------------
// Fire plan: shot count + energy.
// ---------------------------------------------------------------------------

TEST(WeaponInstanceTest, SingleShotByDefault) {
    auto def = MakeWeapon(1.0F, 3);
    def.atk = 15;
    WeaponInstance w(def);
    EXPECT_EQ(w.ShotCount(), 1);
    auto rng = Seeded(1);
    const FirePlan plan = w.BuildFirePlan(glm::vec2(1.0F, 0.0F), rng);
    EXPECT_EQ(plan.bullets.size(), 1U);
    EXPECT_EQ(plan.energyCost, 3);
    EXPECT_EQ(plan.bullets[0].damage, 15);
}

TEST(WeaponInstanceTest, ShotgunCountFromData) {
    auto def = MakeWeapon(1.0F, 4);
    def.count = 6; // Gun004-style burst
    WeaponInstance w(def);
    EXPECT_EQ(w.ShotCount(), 6);
    auto rng = Seeded(7);
    const FirePlan plan = w.BuildFirePlan(glm::vec2(1.0F, 0.0F), rng);
    EXPECT_EQ(plan.bullets.size(), 6U);
}

TEST(WeaponInstanceTest, EnergyCostSpentOncePerPull) {
    auto def = MakeWeapon(1.0F, 5);
    def.count = 3;
    WeaponInstance w(def);
    auto rng = Seeded(2);
    const FirePlan plan = w.BuildFirePlan(glm::vec2(1.0F, 0.0F), rng);
    // 3 bullets, but a single shot's worth of energy (MakeConsume per pull).
    EXPECT_EQ(plan.bullets.size(), 3U);
    EXPECT_EQ(plan.energyCost, 5);
}

// ---------------------------------------------------------------------------
// Deviation spread: determinism + bounds.
// ---------------------------------------------------------------------------

TEST(WeaponInstanceTest, SpreadEqualsDeviationByDefault) {
    auto def = MakeWeapon(1.0F, 0);
    def.deviation = 15;
    WeaponInstance w(def);
    EXPECT_FLOAT_EQ(w.SpreadDegrees(), 15.0F);
}

TEST(WeaponInstanceTest, RecoilFactorWidensSpread) {
    auto def = MakeWeapon(1.0F, 0);
    def.deviation = 10;
    WeaponInstance w(def);
    w.SetRecoilFactor(0.5F); // spread = dev + dev*0.5 = 15
    EXPECT_FLOAT_EQ(w.SpreadDegrees(), 15.0F);
}

TEST(WeaponInstanceTest, ZeroDeviationFiresStraight) {
    auto def = MakeWeapon(1.0F, 0);
    def.deviation = 0;
    def.bulletSpeed = 30.0F;
    WeaponInstance w(def);
    auto rng = Seeded(99);
    const FirePlan plan = w.BuildFirePlan(glm::vec2(1.0F, 0.0F), rng);
    ASSERT_EQ(plan.bullets.size(), 1U);
    // No jitter: dead-on +x and velocity == bullet_speed along +x.
    EXPECT_NEAR(plan.bullets[0].direction.x, 1.0F, 1e-5F);
    EXPECT_NEAR(plan.bullets[0].direction.y, 0.0F, 1e-5F);
    EXPECT_NEAR(plan.bullets[0].velocity.x, 30.0F, 1e-4F);
}

// Same seed -> bit-identical plan (the core determinism contract).
TEST(WeaponInstanceTest, DeterministicForSameSeed) {
    auto def = MakeWeapon(1.0F, 2);
    def.deviation = 12;
    def.count = 5;
    def.bulletSpeed = 40.0F;
    WeaponInstance w(def);

    auto rngA = Seeded(123);
    auto rngB = Seeded(123);
    const FirePlan a = w.BuildFirePlan(glm::vec2(0.0F, 1.0F), rngA);
    const FirePlan b = w.BuildFirePlan(glm::vec2(0.0F, 1.0F), rngB);

    ASSERT_EQ(a.bullets.size(), b.bullets.size());
    for (std::size_t i = 0; i < a.bullets.size(); ++i) {
        EXPECT_FLOAT_EQ(a.bullets[i].direction.x, b.bullets[i].direction.x);
        EXPECT_FLOAT_EQ(a.bullets[i].direction.y, b.bullets[i].direction.y);
        EXPECT_FLOAT_EQ(a.bullets[i].velocity.x, b.bullets[i].velocity.x);
        EXPECT_FLOAT_EQ(a.bullets[i].velocity.y, b.bullets[i].velocity.y);
    }
}

// Different seeds generally produce a different jittered plan.
TEST(WeaponInstanceTest, DifferentSeedsDiffer) {
    auto def = MakeWeapon(1.0F, 0);
    def.deviation = 20;
    def.count = 4;
    WeaponInstance w(def);

    auto rngA = Seeded(1);
    auto rngB = Seeded(2);
    const FirePlan a = w.BuildFirePlan(glm::vec2(1.0F, 0.0F), rngA);
    const FirePlan b = w.BuildFirePlan(glm::vec2(1.0F, 0.0F), rngB);

    bool anyDifferent = false;
    for (std::size_t i = 0; i < a.bullets.size(); ++i) {
        if (std::fabs(a.bullets[i].direction.y - b.bullets[i].direction.y) >
            1e-5F) {
            anyDifferent = true;
            break;
        }
    }
    EXPECT_TRUE(anyDifferent);
}

// Every jittered bullet stays inside the [-spread, +spread] cone around the aim.
TEST(WeaponInstanceTest, DeviationStaysWithinSpreadCone) {
    auto def = MakeWeapon(1.0F, 0);
    def.deviation = 10; // spread = 10 deg
    def.count = 8;
    WeaponInstance w(def);
    auto rng = Seeded(555);
    const FirePlan plan = w.BuildFirePlan(glm::vec2(1.0F, 0.0F), rng);
    for (const BulletSpawn &b : plan.bullets) {
        const float deg = Angle2D(b.direction); // aim is +x => 0 deg
        EXPECT_LE(std::fabs(deg), 10.0F + 1e-3F);
    }
}

// ---------------------------------------------------------------------------
// Fixed fan (count + angle) is symmetric about the aim.
// ---------------------------------------------------------------------------

TEST(WeaponInstanceTest, FanIsSymmetricAroundAim) {
    auto def = MakeWeapon(1.0F, 0);
    def.count = 3;
    def.angle = 15; // Gun002-style fan, no random deviation
    def.deviation = 0;
    WeaponInstance w(def);
    auto rng = Seeded(0);
    const FirePlan plan = w.BuildFirePlan(glm::vec2(1.0F, 0.0F), rng);
    ASSERT_EQ(plan.bullets.size(), 3U);
    // fanStart = -(3-1)/2 * 15 = -15; offsets: -15, 0, +15 about +x (0 deg).
    EXPECT_NEAR(Angle2D(plan.bullets[0].direction), -15.0F, 1e-3F);
    EXPECT_NEAR(Angle2D(plan.bullets[1].direction), 0.0F, 1e-3F);
    EXPECT_NEAR(Angle2D(plan.bullets[2].direction), 15.0F, 1e-3F);
}

// ---------------------------------------------------------------------------
// Pierce: through_count budget vs bool can_through.
// ---------------------------------------------------------------------------

TEST(WeaponInstanceTest, ThroughCountCarriedAsBudget) {
    auto def = MakeWeapon(1.0F, 0);
    def.throughCount = 3;
    WeaponInstance w(def);
    auto rng = Seeded(4);
    const FirePlan plan = w.BuildFirePlan(glm::vec2(1.0F, 0.0F), rng);
    ASSERT_EQ(plan.bullets.size(), 1U);
    EXPECT_TRUE(plan.bullets[0].canThrough);
    EXPECT_EQ(plan.bullets[0].pierce, 3);
}

TEST(WeaponInstanceTest, BoolCanThroughPathHasZeroBudget) {
    auto def = MakeWeapon(1.0F, 0);
    def.canThrough = 1; // GunCaliburn-style: bool pierce, through_count == 0
    def.throughCount = 0;
    WeaponInstance w(def);
    auto rng = Seeded(4);
    const FirePlan plan = w.BuildFirePlan(glm::vec2(1.0F, 0.0F), rng);
    ASSERT_EQ(plan.bullets.size(), 1U);
    EXPECT_TRUE(plan.bullets[0].canThrough);
    EXPECT_EQ(plan.bullets[0].pierce, 0); // 0 => "infinite" for the consumer
}

TEST(WeaponInstanceTest, NonPiercingByDefault) {
    auto def = MakeWeapon(1.0F, 0);
    WeaponInstance w(def);
    auto rng = Seeded(4);
    const FirePlan plan = w.BuildFirePlan(glm::vec2(1.0F, 0.0F), rng);
    ASSERT_EQ(plan.bullets.size(), 1U);
    EXPECT_FALSE(plan.bullets[0].canThrough);
    EXPECT_EQ(plan.bullets[0].pierce, 0);
}

// Velocity carries bullet_speed along the (possibly jittered) direction.
TEST(WeaponInstanceTest, VelocityMagnitudeIsBulletSpeed) {
    auto def = MakeWeapon(1.0F, 0);
    def.deviation = 8;
    def.count = 4;
    def.bulletSpeed = 32.0F;
    WeaponInstance w(def);
    auto rng = Seeded(321);
    const FirePlan plan = w.BuildFirePlan(glm::vec2(1.0F, 0.0F), rng);
    for (const BulletSpawn &b : plan.bullets) {
        const float mag = std::sqrt(b.velocity.x * b.velocity.x +
                                    b.velocity.y * b.velocity.y);
        EXPECT_NEAR(mag, 32.0F, 1e-3F);
    }
}

// NOLINTEND(readability-magic-numbers)
