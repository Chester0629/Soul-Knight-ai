#include <gtest/gtest.h>

#include "combat/BulletLaterFixedTarget.hpp"
#include "data/RGRandom.hpp"

using Game::BulletLaterFixedTarget;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- InvokeDelayMin / InvokeDelayMax: the [delay, 2*delay] window -----------
// FAITHFUL: FindTarget @ game_full.c:963434 --
//   Random.Range(min = fVar3, max = fVar3 + fVar3), fVar3 = *(float*)(this+0x44).

TEST(BulletLaterFixedTargetTest, MinBoundIsTheRawDelayField) {
    EXPECT_FLOAT_EQ(BulletLaterFixedTarget::InvokeDelayMin(0.0F), 0.0F);
    EXPECT_FLOAT_EQ(BulletLaterFixedTarget::InvokeDelayMin(0.5F), 0.5F);
    EXPECT_FLOAT_EQ(BulletLaterFixedTarget::InvokeDelayMin(2.0F), 2.0F);
}

TEST(BulletLaterFixedTargetTest, MaxBoundIsDelayPlusDelay) {
    // The decomp adds the field to itself (fVar3 + fVar3), not a 2* multiply.
    EXPECT_FLOAT_EQ(BulletLaterFixedTarget::InvokeDelayMax(0.0F), 0.0F);
    EXPECT_FLOAT_EQ(BulletLaterFixedTarget::InvokeDelayMax(0.5F), 1.0F);
    EXPECT_FLOAT_EQ(BulletLaterFixedTarget::InvokeDelayMax(2.0F), 4.0F);
}

TEST(BulletLaterFixedTargetTest, MaxIsAlwaysTwiceMinForPositiveDelay) {
    for (float d = 0.0F; d <= 3.0F; d += 0.25F) {
        EXPECT_FLOAT_EQ(BulletLaterFixedTarget::InvokeDelayMax(d),
                        2.0F * BulletLaterFixedTarget::InvokeDelayMin(d));
    }
}

// ---- ScheduledDelay: min + t*(max - min) over the engine RNG sample t -------
// FAITHFUL: FindTarget @ game_full.c:963434,:963435. With t in [0,1] the result
// is in [delay, 2*delay]; the actual sample is UnityEngine.Random, supplied here.

TEST(BulletLaterFixedTargetTest, ScheduledDelayHitsWindowEndpoints) {
    // t = 0 -> min (= delay); t = 1 -> max (= 2*delay).
    EXPECT_FLOAT_EQ(BulletLaterFixedTarget::ScheduledDelay(1.5F, 0.0F), 1.5F);
    EXPECT_FLOAT_EQ(BulletLaterFixedTarget::ScheduledDelay(1.5F, 1.0F), 3.0F);
}

TEST(BulletLaterFixedTargetTest, ScheduledDelayMidpointIsOneAndAHalfDelay) {
    // t = 0.5 -> min + 0.5*(max-min) = delay + 0.5*delay = 1.5*delay.
    EXPECT_FLOAT_EQ(BulletLaterFixedTarget::ScheduledDelay(2.0F, 0.5F), 3.0F);
    EXPECT_FLOAT_EQ(BulletLaterFixedTarget::ScheduledDelay(0.4F, 0.5F), 0.6F);
}

TEST(BulletLaterFixedTargetTest, ScheduledDelayStaysWithinWindow) {
    const float delay = 1.25F;
    const float min = BulletLaterFixedTarget::InvokeDelayMin(delay);
    const float max = BulletLaterFixedTarget::InvokeDelayMax(delay);
    for (float t = 0.0F; t <= 1.0F; t += 0.1F) {
        const float d = BulletLaterFixedTarget::ScheduledDelay(delay, t);
        EXPECT_GE(d, min);
        EXPECT_LE(d, max);
    }
}

TEST(BulletLaterFixedTargetTest, ScheduledDelayZeroDelayCollapsesWindow) {
    // delay 0 -> [0,0]; every sample schedules an immediate (0s) re-seek.
    for (float t = 0.0F; t <= 1.0F; t += 0.25F) {
        EXPECT_FLOAT_EQ(BulletLaterFixedTarget::ScheduledDelay(0.0F, t), 0.0F);
    }
}

// ---- RNG invariant: this brain makes ZERO RGRandom draws --------------------
// FindTarget draws from UnityEngine.Random (engine global stream), not RGRandom.
// A parallel same-seeded RGRandom must remain in lockstep across every call,
// proving the module never advances its own deterministic stream.

TEST(BulletLaterFixedTargetTest, MakesNoRgRandomDraws) {
    BulletLaterFixedTarget bullet;
    RGRandom ref;
    bullet.SetSeed(424242);
    ref.SetRandomSeed(424242);

    // Exercise every pure body repeatedly; none of them may touch the stream.
    for (int i = 0; i < 16; ++i) {
        const float delay = 0.1F * static_cast<float>(i);
        (void)BulletLaterFixedTarget::InvokeDelayMin(delay);
        (void)BulletLaterFixedTarget::InvokeDelayMax(delay);
        (void)BulletLaterFixedTarget::ScheduledDelay(delay, 0.5F);
    }

    // The bullet's stream must still produce exactly what the untouched parallel
    // stream does: same first draw, in order -> zero draws were made above.
    EXPECT_TRUE(bullet.Seeded());
    EXPECT_EQ(bullet.Rng().Range(0, 1000000), ref.Range(0, 1000000));
    EXPECT_FLOAT_EQ(bullet.Rng().Range(0.0F, 1.0F), ref.Range(0.0F, 1.0F));
}

TEST(BulletLaterFixedTargetTest, SeededReflectsSetSeed) {
    BulletLaterFixedTarget bullet;
    EXPECT_FALSE(bullet.Seeded());
    bullet.SetSeed(7);
    EXPECT_TRUE(bullet.Seeded());
}

// NOLINTEND(readability-magic-numbers)
