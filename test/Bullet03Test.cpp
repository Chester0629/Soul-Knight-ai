#include <gtest/gtest.h>

#include "combat/Bullet03.hpp"
#include "data/RGRandom.hpp"

using Game::Bullet03;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- Configure: seeds max_time/max_size, resets a_time to 0 -----------------

TEST(Bullet03Test, ConfigureSetsFieldsAndResetsTimer) {
    Bullet03 b;
    b.Configure(2.0F, 5.0F);
    EXPECT_FLOAT_EQ(b.MaxTime(), 2.0F);
    EXPECT_FLOAT_EQ(b.MaxSize(), 5.0F);
    EXPECT_FLOAT_EQ(b.ATime(), 0.0F);
    // Fresh bullet: progress 0, scaled size 0, not finished.
    EXPECT_FLOAT_EQ(b.Progress(), 0.0F);
    EXPECT_FLOAT_EQ(b.ScaledSize(), 0.0F);
    EXPECT_FALSE(b.Finished());
}

// ---- Tick: a_time += dt only while a_time < max_time (strict gate) -----------

TEST(Bullet03Test, TickAccumulatesTimerWhileBelowDuration) {
    Bullet03 b;
    b.Configure(1.0F, 10.0F);
    // dt = 0.25 each frame; a_time climbs 0.25, 0.50, 0.75.
    EXPECT_TRUE(b.Tick(0.25F));
    EXPECT_FLOAT_EQ(b.ATime(), 0.25F);
    EXPECT_TRUE(b.Tick(0.25F));
    EXPECT_FLOAT_EQ(b.ATime(), 0.50F);
    EXPECT_TRUE(b.Tick(0.25F));
    EXPECT_FLOAT_EQ(b.ATime(), 0.75F);
}

TEST(Bullet03Test, ProgressIsRawRatioNoClamp) {
    Bullet03 b;
    b.Configure(2.0F, 4.0F);
    // progress = a_time / max_time at each step; scaled = progress * max_size.
    b.Tick(0.5F); // a_time 0.5 -> progress 0.25
    EXPECT_FLOAT_EQ(b.Progress(), 0.25F);
    EXPECT_FLOAT_EQ(b.ScaledSize(), 1.0F); // 0.25 * 4
    b.Tick(0.5F); // a_time 1.0 -> progress 0.5
    EXPECT_FLOAT_EQ(b.Progress(), 0.5F);
    EXPECT_FLOAT_EQ(b.ScaledSize(), 2.0F); // 0.5 * 4
}

TEST(Bullet03Test, FinalStepOvershootsDurationNoClampThenFreezes) {
    Bullet03 b;
    b.Configure(1.0F, 6.0F);
    // a_time 0.8 (< 1.0) -> still ticking.
    EXPECT_TRUE(b.Tick(0.8F));
    EXPECT_FLOAT_EQ(b.ATime(), 0.8F);
    EXPECT_FALSE(b.Finished());
    // a_time was < max_time so this step RUNS and overshoots to 1.3 (no clamp);
    // the decomp accumulates inside the strict `<` gate and never clamps a_time.
    EXPECT_TRUE(b.Tick(0.5F));
    EXPECT_FLOAT_EQ(b.ATime(), 1.3F);
    EXPECT_TRUE(b.Finished());
    // Progress exceeds 1 exactly (1.3 / 1.0); scaled = 1.3 * 6 = 7.8.
    EXPECT_FLOAT_EQ(b.Progress(), 1.3F);
    EXPECT_FLOAT_EQ(b.ScaledSize(), 7.8F);
    // Once a_time >= max_time the gate is closed: no further accumulation/write.
    EXPECT_FALSE(b.Tick(1.0F));
    EXPECT_FLOAT_EQ(b.ATime(), 1.3F);
    EXPECT_FLOAT_EQ(b.Progress(), 1.3F);
}

TEST(Bullet03Test, ExactlyAtDurationGateIsClosed) {
    Bullet03 b;
    b.Configure(1.0F, 3.0F);
    // a_time reaches exactly max_time: the `a_time < max_time` gate is strict (<),
    // so at equality the branch does NOT run -- Finished() is true, Tick() false.
    EXPECT_TRUE(b.Tick(1.0F));
    EXPECT_FLOAT_EQ(b.ATime(), 1.0F);
    EXPECT_TRUE(b.Finished());
    EXPECT_FALSE(b.Tick(0.5F));
    EXPECT_FLOAT_EQ(b.ATime(), 1.0F);
    // progress = 1.0 exactly; scaled = 1.0 * 3 = 3.
    EXPECT_FLOAT_EQ(b.Progress(), 1.0F);
    EXPECT_FLOAT_EQ(b.ScaledSize(), 3.0F);
}

TEST(Bullet03Test, ScaledSizeIsProgressTimesMaxSizeNotStartSizeLerp) {
    // The decomp computes (a_time/max_time) * max_size -- NOT a start_size->max_size
    // lerp. At progress 0 the scaled value is 0 (it does not start at start_size).
    Bullet03 b;
    b.Configure(4.0F, 8.0F);
    EXPECT_FLOAT_EQ(b.ScaledSize(), 0.0F); // progress 0 -> 0, no start_size floor
    b.Tick(1.0F);                          // a_time 1 -> progress 0.25
    EXPECT_FLOAT_EQ(b.ScaledSize(), 2.0F); // 0.25 * 8
    b.Tick(1.0F);                          // a_time 2 -> progress 0.5
    EXPECT_FLOAT_EQ(b.ScaledSize(), 4.0F); // 0.5 * 8
}

// ---- Degenerate divisor guard (impossible in original, must not NaN) --------

TEST(Bullet03Test, NonPositiveMaxTimeGuardsDivision) {
    Bullet03 b;
    b.Configure(0.0F, 5.0F);
    // a_time (0) < max_time (0) is false -> gate closed from the start.
    EXPECT_FALSE(b.Tick(1.0F));
    EXPECT_FLOAT_EQ(b.ATime(), 0.0F);
    // Guard returns 0 rather than dividing by zero.
    EXPECT_FLOAT_EQ(b.Progress(), 0.0F);
    EXPECT_FLOAT_EQ(b.ScaledSize(), 0.0F);
}

// ---- Determinism: ZERO RNG draws (neither recovered body calls rg_random) ---

TEST(Bullet03Test, MakesNoRngDrawsAcrossFullLifetime) {
    // Drive a complete lifetime and assert the brain's stream never advances: a
    // parallel same-seeded RGRandom that is NEVER drawn must match the brain's
    // stream exactly afterward (count + order = zero).
    Bullet03 b;
    RGRandom ref;
    b.SetSeed(4242);
    ref.SetRandomSeed(4242);

    b.Configure(2.0F, 7.0F);
    for (int i = 0; i < 20; ++i) {
        b.Tick(0.3F);
        (void)b.Progress();
        (void)b.ScaledSize();
    }

    // After 20 ticks the brain made zero draws; both streams must produce the same
    // first draw, proving the brain's stream is still at its seeded position.
    EXPECT_TRUE(b.Seeded());
    EXPECT_TRUE(ref.Seeded());
    EXPECT_EQ(b.Rng().Range(0, 1000000), ref.Range(0, 1000000));
}

TEST(Bullet03Test, DeterministicForSameSeedAndTickSequence) {
    auto run = [](int seed) {
        Bullet03 b;
        b.SetSeed(seed);
        b.Configure(1.5F, 9.0F);
        float lastScaled = 0.0F;
        for (int i = 0; i < 10; ++i) {
            b.Tick(0.2F);
            lastScaled = b.ScaledSize();
        }
        return lastScaled;
    };
    EXPECT_FLOAT_EQ(run(777), run(777));
}

// NOLINTEND(readability-magic-numbers)
