#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include "combat/Gun005.hpp"
#include "data/RGRandom.hpp"

using Game::Gun005;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- Default maxCharge: the recoverable ctor immediate 0x40000000 == 2.0f ----

TEST(Gun005Test, DefaultMaxChargeIsTwo) {
    Gun005 g;
    EXPECT_FLOAT_EQ(g.MaxCharge(), 2.0F);
    EXPECT_FLOAT_EQ(Gun005::kDefaultMaxCharge, 2.0F);
}

TEST(Gun005Test, NonPositiveMaxChargeFallsBackToDefault) {
    // A zero/negative divisor is impossible in the original (the 316772 division
    // would be a divide-by-zero); the brain falls back to the seeded default.
    EXPECT_FLOAT_EQ(Gun005(0.0F).MaxCharge(), Gun005::kDefaultMaxCharge);
    EXPECT_FLOAT_EQ(Gun005(-3.0F).MaxCharge(), Gun005::kDefaultMaxCharge);
    EXPECT_FLOAT_EQ(Gun005(5.0F).MaxCharge(), 5.0F);
}

TEST(Gun005Test, StartsWithZeroCharge) {
    Gun005 g;
    EXPECT_FLOAT_EQ(g.Charge(), 0.0F);
    EXPECT_FLOAT_EQ(g.ChargeRatio(), 0.0F); // 0 / 2 == 0
}

// ---- ChargeRatio: charge(0x8c) / maxCharge(0x90), NO clamp -------------------

TEST(Gun005Test, ChargeRatioIsChargeOverMax) {
    Gun005 g(2.0F);
    g.SetCharge(1.0F);
    EXPECT_FLOAT_EQ(g.ChargeRatio(), 0.5F); // half charge -> half ratio
    g.SetCharge(2.0F);
    EXPECT_FLOAT_EQ(g.ChargeRatio(), 1.0F); // full charge -> full ratio
}

TEST(Gun005Test, ChargeRatioOverchargeExceedsOne) {
    // The decomp applies NO clamp: charge above maxCharge yields ratio > 1.
    Gun005 g(2.0F);
    g.SetCharge(3.0F);
    EXPECT_FLOAT_EQ(g.ChargeRatio(), 1.5F);
}

TEST(Gun005Test, ChargeRatioHonoursCustomMax) {
    Gun005 g(4.0F);
    g.SetCharge(1.0F);
    EXPECT_FLOAT_EQ(g.ChargeRatio(), 0.25F); // 1 / 4
}

// ---- AddCharge / SetCharge clamping -----------------------------------------

TEST(Gun005Test, AddChargeAccumulates) {
    Gun005 g(2.0F);
    g.AddCharge(0.5F);
    g.AddCharge(0.75F);
    EXPECT_FLOAT_EQ(g.Charge(), 1.25F);
}

TEST(Gun005Test, ChargeNeverGoesNegative) {
    Gun005 g(2.0F);
    g.SetCharge(0.5F);
    g.AddCharge(-2.0F); // would drive negative -> clamped to 0
    EXPECT_FLOAT_EQ(g.Charge(), 0.0F);
    g.SetCharge(-1.0F); // direct negative also clamps
    EXPECT_FLOAT_EQ(g.Charge(), 0.0F);
}

// ---- ScaleAim: per-axis aim * chargeRatio (the bullet velocity) -------------

TEST(Gun005Test, ScaleAimScalesEachAxisByRatio) {
    // FAITHFUL: Attack @ 316757 scales aimDir (0x80/0x84/0x88) by fVar2.
    Gun005 g(2.0F);
    g.SetCharge(1.0F); // ratio 0.5
    const glm::vec3 v = g.ScaleAim(glm::vec3(10.0F, -4.0F, 6.0F));
    EXPECT_FLOAT_EQ(v.x, 5.0F);
    EXPECT_FLOAT_EQ(v.y, -2.0F);
    EXPECT_FLOAT_EQ(v.z, 3.0F);
}

TEST(Gun005Test, ScaleAimFullChargeIsIdentity) {
    Gun005 g(2.0F);
    g.SetCharge(2.0F); // ratio 1.0
    const glm::vec3 aim(3.0F, 7.0F, -1.0F);
    const glm::vec3 v = g.ScaleAim(aim);
    EXPECT_FLOAT_EQ(v.x, aim.x);
    EXPECT_FLOAT_EQ(v.y, aim.y);
    EXPECT_FLOAT_EQ(v.z, aim.z);
}

TEST(Gun005Test, ScaleAimZeroChargeIsZeroVelocity) {
    Gun005 g(2.0F); // charge starts at 0 -> ratio 0
    const glm::vec3 v = g.ScaleAim(glm::vec3(9.0F, 9.0F, 9.0F));
    EXPECT_FLOAT_EQ(v.x, 0.0F);
    EXPECT_FLOAT_EQ(v.y, 0.0F);
    EXPECT_FLOAT_EQ(v.z, 0.0F);
}

// ---- ConsumeCharge: StopWeapon resets charge (0x8c) to 0 --------------------

TEST(Gun005Test, ConsumeChargeResetsToZero) {
    // FAITHFUL: StopWeapon @ 316816 sets this+0x8c = 0 after firing.
    Gun005 g(2.0F);
    g.SetCharge(1.8F);
    EXPECT_GT(g.ChargeRatio(), 0.0F);
    g.ConsumeCharge();
    EXPECT_FLOAT_EQ(g.Charge(), 0.0F);
    EXPECT_FLOAT_EQ(g.ChargeRatio(), 0.0F); // next shot starts uncharged
}

// ---- RNG determinism: Gun005's modelled math draws ZERO from the stream -----

TEST(Gun005Test, ChargeMathConsumesNoRandomDraws) {
    // Gun005 is deterministic (no rg_random in any body). A parallel same-seeded
    // stream must stay in lockstep (un-advanced) after exercising every method.
    Gun005 weapon(2.0F);
    weapon.SetSeed(987654);
    RGRandom reference;
    reference.SetRandomSeed(987654);

    for (int i = 0; i < 16; ++i) {
        weapon.AddCharge(0.25F);
        (void)weapon.ChargeRatio();
        (void)weapon.ScaleAim(glm::vec3(1.0F, 2.0F, 3.0F));
        weapon.ConsumeCharge();
    }

    // The weapon stream must be byte-for-byte where it started: its next draw
    // equals the reference's first draw (no hidden advance happened).
    EXPECT_EQ(weapon.Rng().Range(0, 1000000), reference.Range(0, 1000000));
}

// NOLINTEND(readability-magic-numbers)
