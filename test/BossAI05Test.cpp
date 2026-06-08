#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/BossAI05.hpp"
#include "data/RGRandom.hpp"

using Game::BossAI05;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

namespace {

// Build a brain seeded + fully un-gated so the RNG-bearing methods can fire.
BossAI05 MakeReady(int seed) {
    BossAI05 b;
    b.SetSeed(seed);
    b.SetAwake(true);
    b.SetDead(false);
    b.SetCanShoot(true); // ShootReflection's first gate (can_shoot 0x40)
    return b;
}

} // namespace

// --------------------------------------------------------------------------
// RNG draw count + order: a same-seeded reference stream must reproduce every
// draw the brain makes, in the exact order, with the exact Range() semantics.
// --------------------------------------------------------------------------

TEST(BossAI05Test, WanderDrawsTwoFloatsInOrder) {
    // RunReflection: Range(-1f, 1f) x2 (x then y), float max-INCLUSIVE.
    BossAI05 b = MakeReady(12345);
    RGRandom ref;
    ref.SetRandomSeed(12345);

    for (int i = 0; i < 32; ++i) {
        const float ex = ref.Range(BossAI05::kWanderMin, BossAI05::kWanderMax);
        const float ey = ref.Range(BossAI05::kWanderMin, BossAI05::kWanderMax);
        const glm::vec2 d = b.WanderDirection();
        const float len = std::sqrt(ex * ex + ey * ey);
        const glm::vec2 expected =
            len > 0.0F ? glm::vec2(ex / len, ey / len) : glm::vec2(0.0F, 0.0F);
        EXPECT_FLOAT_EQ(d.x, expected.x);
        EXPECT_FLOAT_EQ(d.y, expected.y);
    }
}

TEST(BossAI05Test, WanderDirectionIsUnitLength) {
    BossAI05 b = MakeReady(999);
    for (int i = 0; i < 64; ++i) {
        const glm::vec2 d = b.WanderDirection();
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        // Either the (vanishingly rare) zero vector or a unit vector.
        EXPECT_TRUE(len == 0.0F || std::fabs(len - 1.0F) < 1e-4F);
    }
}

TEST(BossAI05Test, ShootReflectionDrawsOneIntRollWhenUngated) {
    // can_shoot && !dead && !dizzy -> single Range(0, 100), int max-EXCLUSIVE.
    BossAI05 b = MakeReady(54321);
    RGRandom ref;
    ref.SetRandomSeed(54321);

    for (int i = 0; i < 32; ++i) {
        const int expected = ref.Range(0, BossAI05::kRollCeiling);
        int roll = -1;
        ASSERT_TRUE(b.ShootReflection(roll));
        EXPECT_EQ(roll, expected);
        EXPECT_GE(roll, 0);
        EXPECT_LT(roll, BossAI05::kRollCeiling);
    }
}

TEST(BossAI05Test, InAtk01DrawsOneIntRoll) {
    BossAI05 b = MakeReady(2468);
    RGRandom ref;
    ref.SetRandomSeed(2468);
    for (int i = 0; i < 32; ++i) {
        const int expected = ref.Range(0, BossAI05::kRollCeiling);
        const int roll = b.InAtk01Roll();
        EXPECT_EQ(roll, expected);
        EXPECT_GE(roll, 0);
        EXPECT_LT(roll, BossAI05::kRollCeiling);
    }
}

TEST(BossAI05Test, InterleavedStreamStaysInLockstep) {
    // Wander(2 floats) then ShootReflection(1 int) then InAtk01(1 int), looped.
    // A parallel reference stream must match draw-for-draw across all three.
    BossAI05 b = MakeReady(777);
    RGRandom ref;
    ref.SetRandomSeed(777);

    for (int i = 0; i < 16; ++i) {
        const float ex = ref.Range(BossAI05::kWanderMin, BossAI05::kWanderMax);
        const float ey = ref.Range(BossAI05::kWanderMin, BossAI05::kWanderMax);
        const glm::vec2 d = b.WanderDirection();
        const float len = std::sqrt(ex * ex + ey * ey);
        const glm::vec2 expected =
            len > 0.0F ? glm::vec2(ex / len, ey / len) : glm::vec2(0.0F, 0.0F);
        EXPECT_FLOAT_EQ(d.x, expected.x);
        EXPECT_FLOAT_EQ(d.y, expected.y);

        int roll = -1;
        ASSERT_TRUE(b.ShootReflection(roll));
        EXPECT_EQ(roll, ref.Range(0, BossAI05::kRollCeiling));

        EXPECT_EQ(b.InAtk01Roll(), ref.Range(0, BossAI05::kRollCeiling));
    }
}

// --------------------------------------------------------------------------
// Gates consume NO draw: a gated-out path must leave the stream untouched.
// --------------------------------------------------------------------------

TEST(BossAI05Test, ShootReflectionGatedByCanShootDrawsNothing) {
    // can_shoot == 0 (the first gate): no draw at all.
    BossAI05 b;
    b.SetSeed(303);
    b.SetAwake(true);
    b.SetCanShoot(false); // gate fails before the roll

    int roll = -1;
    EXPECT_FALSE(b.ShootReflection(roll));
    EXPECT_EQ(roll, -1); // out-param untouched

    // The stream must be exactly at seed position: next draw == fresh draw.
    RGRandom ref;
    ref.SetRandomSeed(303);
    EXPECT_EQ(b.InAtk01Roll(), ref.Range(0, BossAI05::kRollCeiling));
}

TEST(BossAI05Test, ShootReflectionGatedByDeadDrawsNothing) {
    BossAI05 b;
    b.SetSeed(404);
    b.SetCanShoot(true);
    b.SetDead(true); // dead 0x38 != 0 -> gated out after can_shoot

    int roll = -1;
    EXPECT_FALSE(b.ShootReflection(roll));
    EXPECT_EQ(roll, -1);

    RGRandom ref;
    ref.SetRandomSeed(404);
    EXPECT_EQ(b.InAtk01Roll(), ref.Range(0, BossAI05::kRollCeiling));
}

TEST(BossAI05Test, ShootReflectionGatedByDizzyDrawsNothing) {
    BossAI05 b;
    b.SetSeed(505);
    b.SetCanShoot(true);
    b.SetAwake(true);
    EXPECT_TRUE(b.Dizzy(1.0F)); // latch dizzy 0xA1 (no RNG)

    int roll = -1;
    EXPECT_FALSE(b.ShootReflection(roll));
    EXPECT_EQ(roll, -1);

    RGRandom ref;
    ref.SetRandomSeed(505);
    EXPECT_EQ(b.InAtk01Roll(), ref.Range(0, BossAI05::kRollCeiling));
}

// --------------------------------------------------------------------------
// Scout gate (dead 0x38 / dizzy 0xA1): clears target, draws no RNG.
// --------------------------------------------------------------------------

TEST(BossAI05Test, ScoutClearsTargetWhenIdle) {
    BossAI05 b;
    b.SetHasTarget(true);
    EXPECT_TRUE(b.Scout());      // not dead, not dizzy
    EXPECT_FALSE(b.HasTarget()); // target_obj (0x7C) = null
}

TEST(BossAI05Test, ScoutGatedWhenDead) {
    BossAI05 b;
    b.SetDead(true);
    b.SetHasTarget(true);
    EXPECT_FALSE(b.Scout());
    EXPECT_TRUE(b.HasTarget()); // untouched: gate failed
}

TEST(BossAI05Test, ScoutGatedWhenDizzy) {
    BossAI05 b;
    EXPECT_TRUE(b.Dizzy(0.5F));
    b.SetHasTarget(true);
    EXPECT_FALSE(b.Scout());
    EXPECT_TRUE(b.HasTarget());
}

TEST(BossAI05Test, ScoutDrawsNoRng) {
    BossAI05 b;
    b.SetSeed(606);
    b.Scout();
    b.Scout();
    RGRandom ref;
    ref.SetRandomSeed(606);
    EXPECT_EQ(b.InAtk01Roll(), ref.Range(0, BossAI05::kRollCeiling));
}

// --------------------------------------------------------------------------
// GetHurt damage gate + one-shot angry transition + shoot_cd halving.
// --------------------------------------------------------------------------

TEST(BossAI05Test, GetHurtRejectedWhenNotAwake) {
    BossAI05 b;
    b.SetAwake(false);
    EXPECT_FALSE(b.OnHurt(1, 600)); // gate: must be awake
    EXPECT_FALSE(b.Angry());
}

TEST(BossAI05Test, GetHurtRejectedWhenDead) {
    BossAI05 b;
    b.SetAwake(true);
    b.SetDead(true);
    EXPECT_FALSE(b.OnHurt(1, 600));
    EXPECT_FALSE(b.Angry());
}

TEST(BossAI05Test, GetHurtRejectedWhenInvisible) {
    BossAI05 b;
    b.SetAwake(true);
    EXPECT_TRUE(b.TurnInvisible()); // invisible 0xD0 = 1
    EXPECT_FALSE(b.OnHurt(1, 600)); // invisible -> damage gate fails
    EXPECT_FALSE(b.Angry());
}

TEST(BossAI05Test, EntersAngryBelowHalfHp) {
    BossAI05 b;
    b.SetAwake(true);
    EXPECT_TRUE(b.OnHurt(301, 600)); // > 50% -> accepted, still calm
    EXPECT_FALSE(b.Angry());
    EXPECT_TRUE(b.OnHurt(299, 600)); // < 50% -> angry
    EXPECT_TRUE(b.Angry());
}

TEST(BossAI05Test, ExactlyHalfIsNotAngry) {
    BossAI05 b;
    b.SetAwake(true);
    EXPECT_TRUE(b.OnHurt(300, 600)); // 0.5 is not < 0.5
    EXPECT_FALSE(b.Angry());
}

TEST(BossAI05Test, ZeroMaxHpAcceptsHitButNoAngry) {
    BossAI05 b;
    b.SetAwake(true);
    EXPECT_TRUE(b.OnHurt(0, 0)); // hit accepted; div-by-zero guarded
    EXPECT_FALSE(b.Angry());
}

TEST(BossAI05Test, BossAngryHalvesShootCdOnce) {
    BossAI05 b;
    b.SetAwake(true);
    b.SetShootCd(2.0F);
    EXPECT_TRUE(b.OnHurt(1, 600)); // -> BossAngry
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 2.0F * BossAI05::kAngryShootCdScale); // 1.0

    // Further hits below 50% must not re-halve (angry 0xB0 gate is one-shot).
    EXPECT_TRUE(b.OnHurt(1, 600));
    EXPECT_FLOAT_EQ(b.ShootCd(), 1.0F);
}

TEST(BossAI05Test, GetHurtDrawsNoRng) {
    BossAI05 b;
    b.SetSeed(808);
    b.SetAwake(true);
    b.OnHurt(1, 600); // angry transition: pure field writes, no draw
    RGRandom ref;
    ref.SetRandomSeed(808);
    EXPECT_EQ(b.InAtk01Roll(), ref.Range(0, BossAI05::kRollCeiling));
}

// --------------------------------------------------------------------------
// Dizzy / invisibility latches.
// --------------------------------------------------------------------------

TEST(BossAI05Test, DizzyLatchesWhenAlive) {
    BossAI05 b;
    EXPECT_FALSE(b.Dizzy());
    EXPECT_TRUE(b.Dizzy(1.5F));
    EXPECT_TRUE(b.Dizzy());
}

TEST(BossAI05Test, DizzyGatedWhenDead) {
    BossAI05 b;
    b.SetDead(true);
    EXPECT_FALSE(b.Dizzy(1.5F)); // dead 0x38 gate
    EXPECT_FALSE(b.Dizzy());
}

TEST(BossAI05Test, TurnInvisibleLatchesOnce) {
    BossAI05 b;
    EXPECT_FALSE(b.Invisible());
    EXPECT_TRUE(b.TurnInvisible());  // first call latches
    EXPECT_TRUE(b.Invisible());
    EXPECT_FALSE(b.TurnInvisible()); // already invisible -> gated, no-op
    EXPECT_TRUE(b.Invisible());
}

TEST(BossAI05Test, BackInvisibleClearsFlag) {
    BossAI05 b;
    EXPECT_TRUE(b.TurnInvisible());
    b.BackInvisible();
    EXPECT_FALSE(b.Invisible());
    // After BackInvisible, TurnInvisible can latch again.
    EXPECT_TRUE(b.TurnInvisible());
}

// --------------------------------------------------------------------------
// StartAtk03 weapon-lock clear.
// --------------------------------------------------------------------------

TEST(BossAI05Test, StartAtk03ClearsWeaponLock) {
    BossAI05 b;
    EXPECT_FALSE(b.WeaponLockTarget());
    b.StartAtk03(); // writes weapon_lock_target (0x1C) = 0 (already false)
    EXPECT_FALSE(b.WeaponLockTarget());
}

TEST(BossAI05Test, StartAtk03DrawsNoRng) {
    BossAI05 b;
    b.SetSeed(909);
    b.StartAtk03();
    RGRandom ref;
    ref.SetRandomSeed(909);
    EXPECT_EQ(b.InAtk01Roll(), ref.Range(0, BossAI05::kRollCeiling));
}

// --------------------------------------------------------------------------
// Determinism: same seed -> identical full trace.
// --------------------------------------------------------------------------

TEST(BossAI05Test, FullStreamReplayIsDeterministic) {
    auto run = [](int seed) {
        BossAI05 b = MakeReady(seed);
        std::vector<float> trace;
        for (int i = 0; i < 12; ++i) {
            const glm::vec2 d = b.WanderDirection();
            trace.push_back(d.x);
            trace.push_back(d.y);
            int roll = -1;
            b.ShootReflection(roll);
            trace.push_back(static_cast<float>(roll));
            trace.push_back(static_cast<float>(b.InAtk01Roll()));
        }
        return trace;
    };
    const auto a = run(31415);
    const auto b = run(31415);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_FLOAT_EQ(a[i], b[i]);
    }
}

TEST(BossAI05Test, DifferentSeedsDiverge) {
    BossAI05 a = MakeReady(1);
    BossAI05 b = MakeReady(987654);
    bool diverged = false;
    for (int i = 0; i < 32; ++i) {
        if (a.InAtk01Roll() != b.InAtk01Roll()) {
            diverged = true;
        }
    }
    EXPECT_TRUE(diverged);
}

// NOLINTEND(readability-magic-numbers)
