#include <gtest/gtest.h>

#include "combat/BulletBoom.hpp"
#include "data/RGRandom.hpp"

using Game::BulletBoom;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- StartBoom cadence: two scheduled delays off boom_time, 1.0s lead -------

TEST(BulletBoomTest, ScheduledDelaysAreBoomTimeAndBoomTimeMinusOne) {
    // ExplodeStart fires at boom_time; SoonExplode 1.0s earlier (game_full.c:962705/06).
    EXPECT_FLOAT_EQ(BulletBoom::ExplodeStartDelay(3.0F), 3.0F);
    EXPECT_FLOAT_EQ(BulletBoom::SoonExplodeDelay(3.0F), 2.0F);
    // The lead is exactly the kSoonExplodeLead literal.
    EXPECT_FLOAT_EQ(BulletBoom::ExplodeStartDelay(5.0F) - BulletBoom::SoonExplodeDelay(5.0F),
                    BulletBoom::kSoonExplodeLead);
}

TEST(BulletBoomTest, SoonExplodeDelayHasNoClampBelowOneSecond) {
    // The decomp does `boom_time + -1.0` with no Mathf.Max: small boom_time gives
    // a non-positive (immediate) warning delay -- preserved verbatim.
    EXPECT_FLOAT_EQ(BulletBoom::SoonExplodeDelay(0.5F), -0.5F);
    EXPECT_FLOAT_EQ(BulletBoom::SoonExplodeDelay(0.0F), -1.0F);
}

// ---- Tick schedule: fixed-dt sequence fires warning then detonation ---------

TEST(BulletBoomTest, ArmStartsIdleThenArmed) {
    BulletBoom b;
    EXPECT_EQ(b.CurrentState(), BulletBoom::State::IDLE);
    b.Arm(3.0F);
    EXPECT_EQ(b.CurrentState(), BulletBoom::State::ARMED);
    EXPECT_FLOAT_EQ(b.Elapsed(), 0.0F);
    EXPECT_FALSE(b.SoonExplodeFired());
    EXPECT_FALSE(b.ExplodeStarted());
}

TEST(BulletBoomTest, WarningFiresAtBoomTimeMinusOneThenDetonationAtBoomTime) {
    // boom_time = 3.0 -> warning at 2.0s, detonation at 3.0s. Drive 0.5s steps.
    BulletBoom b;
    b.Arm(3.0F);

    b.Tick(0.5F); // 0.5
    b.Tick(0.5F); // 1.0
    b.Tick(0.5F); // 1.5
    EXPECT_FALSE(b.SoonExplodeFired());
    EXPECT_EQ(b.CurrentState(), BulletBoom::State::ARMED);

    b.Tick(0.5F); // 2.0 -> warning crosses (>= 2.0)
    EXPECT_TRUE(b.SoonExplodeFired());
    EXPECT_EQ(b.CurrentState(), BulletBoom::State::SOON_EXPLODE);
    EXPECT_FALSE(b.ExplodeStarted());

    b.Tick(0.5F); // 2.5 -> still warning state, not yet detonated
    EXPECT_EQ(b.CurrentState(), BulletBoom::State::SOON_EXPLODE);

    b.Tick(0.5F); // 3.0 -> detonation crosses (>= 3.0)
    EXPECT_TRUE(b.ExplodeStarted());
    EXPECT_EQ(b.CurrentState(), BulletBoom::State::EXPLODE_START);
    EXPECT_FLOAT_EQ(b.Elapsed(), 3.0F);
}

TEST(BulletBoomTest, NoTicksFireAfterDetonation) {
    BulletBoom b;
    b.Arm(2.0F);
    b.Tick(2.0F); // straight to detonation
    EXPECT_EQ(b.CurrentState(), BulletBoom::State::EXPLODE_START);
    const float elapsedAtBoom = b.Elapsed();
    // EXPLODE_START is terminal: further ticks are no-ops (CancelInvoke done).
    b.Tick(5.0F);
    EXPECT_EQ(b.CurrentState(), BulletBoom::State::EXPLODE_START);
    EXPECT_FLOAT_EQ(b.Elapsed(), elapsedAtBoom);
}

TEST(BulletBoomTest, TickWhileIdleIsNoOp) {
    BulletBoom b; // never armed
    b.Tick(10.0F);
    EXPECT_EQ(b.CurrentState(), BulletBoom::State::IDLE);
    EXPECT_FLOAT_EQ(b.Elapsed(), 0.0F);
}

// ---- Two independent timers: a single dt crossing both fires BOTH -----------

TEST(BulletBoomTest, SingleStepCrossingBothFiresWarningAndDetonation) {
    // The warning (boom_time-1.0) and detonation (boom_time) are two INDEPENDENT
    // absolute-time invokes. The no-arg CancelInvoke @ game_full.c:962740 cancels
    // only FUTURE pendings; the earlier-scheduled warning has already fired by the
    // time detonation runs, so it is never suppressed. A single dt crossing both
    // thresholds must fire BOTH: SoonExplodeFired() stays true at EXPLODE_START.
    BulletBoom b;
    b.Arm(3.0F);
    b.Tick(3.0F); // crosses warning (2.0) AND detonation (3.0) in one step
    EXPECT_EQ(b.CurrentState(), BulletBoom::State::EXPLODE_START);
    EXPECT_TRUE(b.ExplodeStarted());
    EXPECT_TRUE(b.SoonExplodeFired()); // warning fired; it is never cancelled
}

TEST(BulletBoomTest, ImmediateWarningWhenBoomTimeBelowLead) {
    // boom_time = 0.5 -> warning delay -0.5 (already due), detonation at 0.5.
    // The very first non-trivial tick that stays below 0.5 fires only the warning.
    BulletBoom b;
    b.Arm(0.5F);
    b.Tick(0.25F); // 0.25 < 0.5 detonation, but >= -0.5 warning -> warning fires
    EXPECT_TRUE(b.SoonExplodeFired());
    EXPECT_EQ(b.CurrentState(), BulletBoom::State::SOON_EXPLODE);
    EXPECT_FALSE(b.ExplodeStarted());
    b.Tick(0.25F); // 0.5 -> detonation
    EXPECT_EQ(b.CurrentState(), BulletBoom::State::EXPLODE_START);
}

// ---- Determinism: the brain makes ZERO RNG draws ----------------------------

TEST(BulletBoomTest, MakesNoRngDrawsAcrossFullLifecycle) {
    // A parallel same-seeded RGRandom must remain at its initial draw position:
    // no BulletBoom body touches rg_random, so the stream never advances.
    BulletBoom b;
    RGRandom ref;
    b.SetSeed(4775);
    ref.SetRandomSeed(4775);
    EXPECT_TRUE(b.Seeded());

    b.Arm(3.0F);
    for (int i = 0; i < 10; ++i) {
        b.Tick(0.5F);
    }
    EXPECT_EQ(b.CurrentState(), BulletBoom::State::EXPLODE_START);

    // The brain's own RNG must reproduce the untouched reference stream exactly:
    // if any draw had leaked, these first draws would diverge.
    EXPECT_EQ(b.Rng().Range(0, 1000), ref.Range(0, 1000));
    EXPECT_FLOAT_EQ(b.Rng().Range(-1.0F, 1.0F), ref.Range(-1.0F, 1.0F));
}

TEST(BulletBoomTest, SeedIsParityOnlyAndNeverConsumed) {
    // Two boxes seeded identically, one cycled through a full boom, one untouched:
    // their streams must still be in lockstep (the lifecycle draws nothing).
    BulletBoom cycled;
    BulletBoom fresh;
    cycled.SetSeed(99);
    fresh.SetSeed(99);

    cycled.Arm(4.0F);
    cycled.Tick(2.0F);
    cycled.Tick(2.0F);
    EXPECT_EQ(cycled.CurrentState(), BulletBoom::State::EXPLODE_START);

    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(cycled.Rng().Range(0, 10000), fresh.Rng().Range(0, 10000));
    }
}

// NOLINTEND(readability-magic-numbers)
