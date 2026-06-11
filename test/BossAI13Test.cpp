#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

#include "combat/BossAI13.hpp"
#include "data/RGRandom.hpp"

using Game::BossAI13;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

namespace {

// A wide-awake, ready-to-shoot boss (gates open) for the RNG-path tests.
BossAI13 MakeArmed(int seed) {
    BossAI13 b;
    b.SetSeed(seed);
    b.SetCanShoot(true); // can_shoot(0x40)
    return b;
}

} // namespace

// ---- RunReflection: exactly two float draws Range(-1f, 1f), x then y ---------

TEST(BossAI13Test, RunReflectionDrawsTwoFloatsInOrder) {
    BossAI13 b;
    b.SetSeed(12345);

    // Parallel reference stream, same seed: the boss must draw x then y from
    // Range(-1f, 1f) and nothing else.
    RGRandom ref;
    ref.SetRandomSeed(12345);
    const float rx = ref.Range(BossAI13::kReflectComponentMin,
                               BossAI13::kReflectComponentMax);
    const float ry = ref.Range(BossAI13::kReflectComponentMin,
                               BossAI13::kReflectComponentMax);

    const glm::vec2 dir = b.RunReflection();

    // The returned vector is the NORMALIZED (rx, ry).
    const float len = std::sqrt(rx * rx + ry * ry);
    ASSERT_GT(len, 0.0F);
    EXPECT_FLOAT_EQ(dir.x, rx / len);
    EXPECT_FLOAT_EQ(dir.y, ry / len);

    // After two draws on each side the streams stay in lockstep.
    EXPECT_EQ(b.Rng().Range(0, 1000000), ref.Range(0, 1000000));
}

TEST(BossAI13Test, RunReflectionReturnsUnitVector) {
    BossAI13 b;
    b.SetSeed(7);
    for (int i = 0; i < 64; ++i) {
        const glm::vec2 d = b.RunReflection();
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        // Either a unit vector or the degenerate zero (both draws exactly 0).
        EXPECT_TRUE(std::fabs(len - 1.0F) < 1e-4F || len == 0.0F);
    }
}

TEST(BossAI13Test, RunReflectionIsUngatedAndConsumesTwoDrawsRegardlessOfState) {
    // RunReflection is the Invoke target; dead/dizzy/etc do NOT gate it.
    BossAI13 b;
    b.SetSeed(999);
    b.SetDead(true);
    b.SetDizzy(true);

    RGRandom ref;
    ref.SetRandomSeed(999);
    ref.Range(BossAI13::kReflectComponentMin, BossAI13::kReflectComponentMax);
    ref.Range(BossAI13::kReflectComponentMin, BossAI13::kReflectComponentMax);

    b.RunReflection(); // two draws taken despite dead+dizzy

    EXPECT_EQ(b.Rng().Range(0, 1000000), ref.Range(0, 1000000));
}

// ---- ShootReflection: one gated int draw Range(0, 100) ----------------------

TEST(BossAI13Test, ShootReflectionDrawsOnceWhenArmed) {
    BossAI13 b = MakeArmed(2024);

    RGRandom ref;
    ref.SetRandomSeed(2024);
    const int expected = ref.Range(0, BossAI13::kRollCeiling);

    int roll = -1;
    EXPECT_TRUE(b.ShootReflection(roll));
    EXPECT_EQ(roll, expected);
    EXPECT_GE(roll, 0);
    EXPECT_LT(roll, BossAI13::kRollCeiling);

    // Lockstep after the single draw.
    EXPECT_EQ(b.Rng().Range(0, 1000000), ref.Range(0, 1000000));
}

TEST(BossAI13Test, ShootReflectionGatedByCanShootTakesNoDraw) {
    BossAI13 b;
    b.SetSeed(55);
    b.SetCanShoot(false); // gate closed

    RGRandom ref;
    ref.SetRandomSeed(55);

    int roll = -1;
    EXPECT_FALSE(b.ShootReflection(roll));
    EXPECT_EQ(roll, -1); // untouched

    // Stream NOT advanced -> first draw still matches the fresh reference.
    EXPECT_EQ(b.Rng().Range(0, 1000000), ref.Range(0, 1000000));
}

TEST(BossAI13Test, ShootReflectionGatedByDeadTakesNoDraw) {
    BossAI13 b = MakeArmed(55);
    b.SetDead(true);

    RGRandom ref;
    ref.SetRandomSeed(55);

    int roll = -1;
    EXPECT_FALSE(b.ShootReflection(roll));
    EXPECT_EQ(b.Rng().Range(0, 1000000), ref.Range(0, 1000000));
}

TEST(BossAI13Test, ShootReflectionGatedByDizzyTakesNoDraw) {
    BossAI13 b = MakeArmed(55);
    b.SetDizzy(true);

    RGRandom ref;
    ref.SetRandomSeed(55);

    int roll = -1;
    EXPECT_FALSE(b.ShootReflection(roll));
    EXPECT_EQ(b.Rng().Range(0, 1000000), ref.Range(0, 1000000));
}

TEST(BossAI13Test, ShootReflectionGateOrderCanShootBeforeDeadAndDizzy) {
    // All closed: still no draw, and the order of checks doesn't matter to the
    // stream (no draw either way) -- assert the lockstep holds.
    BossAI13 b;
    b.SetSeed(77);
    b.SetCanShoot(false);
    b.SetDead(true);
    b.SetDizzy(true);

    RGRandom ref;
    ref.SetRandomSeed(77);
    int roll = -1;
    EXPECT_FALSE(b.ShootReflection(roll));
    EXPECT_EQ(b.Rng().Range(0, 99999), ref.Range(0, 99999));
}

// ---- Interleaved replay determinism (lockstep across both draw sites) -------

TEST(BossAI13Test, InterleavedRngReplayIsDeterministic) {
    auto run = [](int seed) {
        BossAI13 b = MakeArmed(seed);
        std::vector<float> trace;
        for (int i = 0; i < 16; ++i) {
            const glm::vec2 d = b.RunReflection(); // 2 float draws
            trace.push_back(d.x);
            trace.push_back(d.y);
            int roll = -1;
            b.ShootReflection(roll); // 1 int draw
            trace.push_back(static_cast<float>(roll));
        }
        return trace;
    };
    const auto a = run(31337);
    const auto b = run(31337);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_FLOAT_EQ(a[i], b[i]);
    }
}

TEST(BossAI13Test, FullStreamMatchesHandRolledReference) {
    // Drive the exact draw pattern the decomp performs and verify against a
    // parallel RGRandom: RunReflection(x,y) then ShootReflection(roll), x N.
    BossAI13 b = MakeArmed(8675309);
    RGRandom ref;
    ref.SetRandomSeed(8675309);

    for (int i = 0; i < 24; ++i) {
        const glm::vec2 d = b.RunReflection();
        const float rx = ref.Range(BossAI13::kReflectComponentMin,
                                   BossAI13::kReflectComponentMax);
        const float ry = ref.Range(BossAI13::kReflectComponentMin,
                                   BossAI13::kReflectComponentMax);
        const float len = std::sqrt(rx * rx + ry * ry);
        if (len > 0.0F) {
            EXPECT_FLOAT_EQ(d.x, rx / len);
            EXPECT_FLOAT_EQ(d.y, ry / len);
        }

        int roll = -1;
        ASSERT_TRUE(b.ShootReflection(roll));
        EXPECT_EQ(roll, ref.Range(0, BossAI13::kRollCeiling));
    }
}

// ---- BossAngry: hp/max_hp < 0.5 once, shoot_cd halved -----------------------

TEST(BossAI13Test, OnHurtNeedsAwakeAndNotDead) {
    BossAI13 b;
    b.SetShootCd(2.0F);
    // Not awake -> GetHurt returns immediately, no angry.
    b.OnHurt(1, 600);
    EXPECT_FALSE(b.Angry());

    b.OnGameStateChange(1, true); // wake
    ASSERT_TRUE(b.Awake());
    b.SetDead(true);
    b.OnHurt(1, 600); // dead gate
    EXPECT_FALSE(b.Angry());
}

TEST(BossAI13Test, EntersAngryBelowHalfHpAndHalvesShootCd) {
    BossAI13 b;
    b.SetShootCd(2.0F);
    ASSERT_TRUE(b.OnGameStateChange(1, true)); // awake

    b.OnHurt(301, 600); // > 50% -> calm
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 2.0F);

    b.OnHurt(299, 600); // < 50% -> angry, shoot_cd *= 0.5
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 1.0F);
}

TEST(BossAI13Test, ExactlyHalfIsNotAngry) {
    BossAI13 b;
    b.SetShootCd(2.0F);
    b.OnGameStateChange(1, true);
    b.OnHurt(300, 600); // 0.5 is not < 0.5
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 2.0F);
}

TEST(BossAI13Test, AngryTransitionAndShootCdHalveOnlyOnce) {
    BossAI13 b;
    b.SetShootCd(2.0F);
    b.OnGameStateChange(1, true);
    b.OnHurt(100, 600); // angry, 2.0 -> 1.0
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 1.0F);
    b.OnHurt(10, 600); // still <50% but already angry -> no second halving
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 1.0F);
}

TEST(BossAI13Test, ZeroMaxHpDoesNotEnterAngry) {
    BossAI13 b;
    b.OnGameStateChange(1, true);
    b.OnHurt(0, 0); // divide-by-zero guard
    EXPECT_FALSE(b.Angry());
}

TEST(BossAI13Test, AngryGoldenScalars) {
    // Lock the documented golden constants from BossAngry.
    EXPECT_FLOAT_EQ(BossAI13::kAngryHpFraction, 0.5F);
    EXPECT_FLOAT_EQ(BossAI13::kAngryShootCdScale, 0.5F);
    EXPECT_FLOAT_EQ(BossAI13::kAngryAnimSpeed, 1.2F);
}

// ---- Dizzy: latch gated on !dead -------------------------------------------

TEST(BossAI13Test, DizzyLatchesWhenAlive) {
    BossAI13 b;
    EXPECT_TRUE(b.OnDizzy());
    EXPECT_TRUE(b.Dizzy());
}

TEST(BossAI13Test, DizzyGatedByDead) {
    BossAI13 b;
    b.SetDead(true);
    EXPECT_FALSE(b.OnDizzy());
    EXPECT_FALSE(b.Dizzy());
}

// ---- Scout: target cleared, gated on !dead && !dizzy ------------------------

TEST(BossAI13Test, ScoutClearsTargetWhenAlertAndAwake) {
    BossAI13 b;
    b.SetHasTarget(true);
    EXPECT_TRUE(b.Scout());
    EXPECT_FALSE(b.HasTarget()); // target_obj(0x7c) = null
}

TEST(BossAI13Test, ScoutGatedByDead) {
    BossAI13 b;
    b.SetHasTarget(true);
    b.SetDead(true);
    EXPECT_FALSE(b.Scout());
    EXPECT_TRUE(b.HasTarget()); // untouched
}

TEST(BossAI13Test, ScoutGatedByDizzy) {
    BossAI13 b;
    b.SetHasTarget(true);
    b.SetDizzy(true);
    EXPECT_FALSE(b.Scout());
    EXPECT_TRUE(b.HasTarget());
}

// ---- OnGameStateChange: wake on room-ready ----------------------------------

TEST(BossAI13Test, WakesOnGameStartWhenRoomReady) {
    BossAI13 b;
    EXPECT_FALSE(b.Awake());
    EXPECT_TRUE(b.OnGameStateChange(1, true));
    EXPECT_TRUE(b.Awake());
}

TEST(BossAI13Test, DoesNotWakeOnNonStartState) {
    BossAI13 b;
    EXPECT_FALSE(b.OnGameStateChange(0, true));
    EXPECT_FALSE(b.Awake());
    EXPECT_FALSE(b.OnGameStateChange(2, true));
    EXPECT_FALSE(b.Awake());
}

TEST(BossAI13Test, DoesNotWakeWhenRoomNotReady) {
    BossAI13 b;
    EXPECT_FALSE(b.OnGameStateChange(1, false));
    EXPECT_FALSE(b.Awake());
}

// ---- FixedUpdate brake branch ----------------------------------------------

TEST(BossAI13Test, FixedUpdateBrakeClearsAwakeWhenAwakeNotShootingAndDead) {
    BossAI13 b;
    b.OnGameStateChange(1, true); // awake = true
    ASSERT_TRUE(b.Awake());
    b.SetShooting(false);
    b.SetDead(true);
    EXPECT_TRUE(b.FixedUpdateBrake());
    EXPECT_FALSE(b.Awake()); // awake(0x18) cleared
}

TEST(BossAI13Test, FixedUpdateBrakeNoOpWhenNotAwake) {
    BossAI13 b; // awake == false
    b.SetDead(true);
    EXPECT_FALSE(b.FixedUpdateBrake());
    EXPECT_FALSE(b.Awake());
}

TEST(BossAI13Test, FixedUpdateBrakeNoOpWhenShooting) {
    BossAI13 b;
    b.OnGameStateChange(1, true);
    b.SetShooting(true); // gate: !shooting
    b.SetDead(true);
    EXPECT_FALSE(b.FixedUpdateBrake());
    EXPECT_TRUE(b.Awake()); // not cleared
}

TEST(BossAI13Test, FixedUpdateBrakeNoOpWhenAliveAndAwake) {
    BossAI13 b;
    b.OnGameStateChange(1, true);
    b.SetShooting(false);
    b.SetDead(false); // alive -> the move branch (owner-side), awake stays set
    EXPECT_FALSE(b.FixedUpdateBrake());
    EXPECT_TRUE(b.Awake());
}

// ---- Pure attack selectors (no RNG) -----------------------------------------

TEST(BossAI13Test, Atk01BulletCountAngryGate) {
    BossAI13 b;
    EXPECT_EQ(b.Atk01BulletCount(), BossAI13::kAtk01BulletsCalm); // 5
    b.OnGameStateChange(1, true);
    b.SetShootCd(2.0F);
    b.OnHurt(1, 600); // -> angry
    ASSERT_TRUE(b.Angry());
    EXPECT_EQ(b.Atk01BulletCount(), BossAI13::kAtk01BulletsAngry); // 7
}

TEST(BossAI13Test, Atk01BulletGoldenCounts) {
    EXPECT_EQ(BossAI13::kAtk01BulletsCalm, 5);
    EXPECT_EQ(BossAI13::kAtk01BulletsAngry, 7);
}

TEST(BossAI13Test, Atk02FireGateByDizzy) {
    BossAI13 b;
    EXPECT_TRUE(b.Atk02WouldFire());
    b.SetDizzy(true);
    EXPECT_FALSE(b.Atk02WouldFire()); // gated by dizzy(0xa1)
}

TEST(BossAI13Test, Atk03PrefabSelectorByAngry) {
    BossAI13 b;
    EXPECT_FALSE(b.Atk03UsesAngryPrefab()); // bullet03(0xbc) when calm
    b.OnGameStateChange(1, true);
    b.SetShootCd(2.0F);
    b.OnHurt(1, 600);
    ASSERT_TRUE(b.Angry());
    EXPECT_TRUE(b.Atk03UsesAngryPrefab()); // bullet03_angry(0xc0) when angry
}

// ---- Roll ceiling golden ----------------------------------------------------

TEST(BossAI13Test, RollCeilingGolden) {
    EXPECT_EQ(BossAI13::kRollCeiling, 100);
}

// NOLINTEND(readability-magic-numbers)
