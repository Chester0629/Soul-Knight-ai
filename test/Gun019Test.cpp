#include <gtest/gtest.h>

#include <vector>

#include "combat/Gun019.hpp"
#include "data/RGRandom.hpp"

using Game::Gun019;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- IsBurstReschedule: x < -x iff x < 0 (c_count < 0), no RNG -------------
// FAITHFUL: Gun019__CreateBullet @ game_full.c:965357
// `if (*(int*)(param_1+0x70) < -*(int*)(param_1+0x70))`
// For signed int x: x < -x  <=>  2x < 0  <=>  x < 0.
// So NEGATIVE c_count -> burst-reschedule; non-negative -> main-shot.

TEST(Gun019Test, BurstRescheduleGateNegativeCCount) {
    // Negative c_count -> burst-reschedule path.
    EXPECT_TRUE(Gun019::IsBurstReschedule(-1));
    EXPECT_TRUE(Gun019::IsBurstReschedule(-5));
    EXPECT_TRUE(Gun019::IsBurstReschedule(-100));
}

TEST(Gun019Test, BurstRescheduleGateZero) {
    // c_count == 0: 0 < -0 is false -> main-shot path.
    EXPECT_FALSE(Gun019::IsBurstReschedule(0));
}

TEST(Gun019Test, BurstRescheduleGatePositiveCCount) {
    // Positive c_count: main-shot path (x < -x fails when x > 0).
    EXPECT_FALSE(Gun019::IsBurstReschedule(1));
    EXPECT_FALSE(Gun019::IsBurstReschedule(10));
}

// ---- BurstAdvance: index+1, then compare to count, set in_atk, no RNG -------
// FAITHFUL: Gun019__CreateBullet @ game_full.c:965364-965373.
// `iVar1 = *(param_1+0x80) + 1; if (iVar1 < *(param_1+0x78)) Invoke(...)
//  else *(param_1+0x84) = 0;`

TEST(Gun019Test, BurstAdvanceIncrementsIndex) {
    // The post-increment index is always index+1.
    auto s0 = Gun019::BurstAdvance(0, 3);
    EXPECT_EQ(s0.index, 1);
    auto s2 = Gun019::BurstAdvance(2, 3);
    EXPECT_EQ(s2.index, 3);
}

TEST(Gun019Test, BurstAdvanceReschedulesWhileIndexLtCount) {
    // index+1 < count -> re-schedule, in_atk stays set.
    // e.g. index=0, count=3 -> new index=1 < 3.
    auto step = Gun019::BurstAdvance(0, 3);
    EXPECT_TRUE(step.reschedule);
    EXPECT_TRUE(step.inAtk);
}

TEST(Gun019Test, BurstAdvanceStopsWhenIndexReachesCount) {
    // index+1 == count (== 3) -> NOT < count -> stop, in_atk cleared.
    auto step = Gun019::BurstAdvance(2, 3);
    EXPECT_FALSE(step.reschedule);
    EXPECT_EQ(step.inAtk, Gun019::kInAtkStopped);
}

TEST(Gun019Test, BurstAdvanceStopsWhenIndexExceedsCount) {
    // index+1 > count (over-run guard): also not rescheduled.
    auto step = Gun019::BurstAdvance(3, 3);
    EXPECT_FALSE(step.reschedule);
    EXPECT_EQ(step.inAtk, Gun019::kInAtkStopped);
}

TEST(Gun019Test, BurstAdvanceFullSequenceCount3) {
    // Simulate a 3-shot burst: index starts at 0, count=3.
    // Shot  0->1: reschedule=true
    // Shot  1->2: reschedule=true
    // Shot  2->3: reschedule=false (exhausted)
    int idx = 0;
    const int count = 3;

    auto s1 = Gun019::BurstAdvance(idx, count);
    EXPECT_EQ(s1.index, 1);
    EXPECT_TRUE(s1.reschedule);
    EXPECT_TRUE(s1.inAtk);
    idx = s1.index;

    auto s2 = Gun019::BurstAdvance(idx, count);
    EXPECT_EQ(s2.index, 2);
    EXPECT_TRUE(s2.reschedule);
    EXPECT_TRUE(s2.inAtk);
    idx = s2.index;

    auto s3 = Gun019::BurstAdvance(idx, count);
    EXPECT_EQ(s3.index, 3);
    EXPECT_FALSE(s3.reschedule);
    EXPECT_FALSE(s3.inAtk);
}

// ---- SpreadHalfAngle: angle + angle*recoil = angle*(1+recoil), no RNG -------
// FAITHFUL: Gun019__CreateBullet @ game_full.c:965386-965387.
// `fVar4 = (float)VectorSignedToFloat(uVar3,...);
//  fVar4 = fVar4 + fVar4 * *(float*)(iVar1+0x20);`

TEST(Gun019Test, SpreadHalfAngleZeroRecoil) {
    // recoil=0 -> spread == angle.
    EXPECT_FLOAT_EQ(Gun019::SpreadHalfAngle(20.0F, 0.0F), 20.0F);
    EXPECT_FLOAT_EQ(Gun019::SpreadHalfAngle(0.0F, 0.5F), 0.0F);
}

TEST(Gun019Test, SpreadHalfAngleFormula) {
    // angle=10, recoil=0.5 -> 10 + 10*0.5 = 15.
    EXPECT_FLOAT_EQ(Gun019::SpreadHalfAngle(10.0F, 0.5F), 15.0F);
    // angle=12, recoil=1.0 -> 12 + 12 = 24.
    EXPECT_FLOAT_EQ(Gun019::SpreadHalfAngle(12.0F, 1.0F), 24.0F);
    // angle=8, recoil=0.25 -> 8 + 2 = 10.
    EXPECT_FLOAT_EQ(Gun019::SpreadHalfAngle(8.0F, 0.25F), 10.0F);
}

TEST(Gun019Test, SpreadHalfAngleNegativeRecoilNarrows) {
    // Negative recoil subtracts from the spread (no clamp here -- decomp does
    // not apply Max(0,...) in this method; that would be owner-side if needed).
    EXPECT_FLOAT_EQ(Gun019::SpreadHalfAngle(10.0F, -0.5F), 5.0F);
}

// ---- ScatterAngle: exactly ONE float draw, symmetric in [-spread, spread] ---
// FAITHFUL: Gun019__CreateBullet @ game_full.c:965392
// `RGRandom__Range(*(param_1+0x60), -fVar4, fVar4, 0)`

TEST(Gun019Test, ScatterStaysWithinSpreadBounds) {
    Gun019 g;
    g.SetSeed(1234);
    const float angle  = 15.0F;
    const float recoil = 0.3F;
    const float spread = Gun019::SpreadHalfAngle(angle, recoil);
    for (int i = 0; i < 64; ++i) {
        const float a = g.ScatterAngle(spread);
        EXPECT_GE(a, -spread);
        EXPECT_LE(a, spread);
    }
}

TEST(Gun019Test, ScatterDrawsExactlyOneFloatInLockstep) {
    // A parallel same-seeded RGRandom must reproduce each scatter draw exactly:
    // proves the brain draws ONE float per main shot with the -spread/+spread
    // bounds and nothing else advancing the stream.
    // FAITHFUL: game_full.c:965392 -- RGRandom__Range(-fVar4, fVar4, 0)
    Gun019 g;
    RGRandom ref;
    g.SetSeed(98765);
    ref.SetRandomSeed(98765);

    const std::vector<float> spreads = {5.0F, 12.5F, 0.0F, 7.0F, 18.0F};
    for (const float spread : spreads) {
        const float got      = g.ScatterAngle(spread);
        const float expected = ref.Range(-spread, spread); // one parallel draw
        EXPECT_FLOAT_EQ(got, expected);
    }
}

TEST(Gun019Test, ScatterIsDeterministicForSameSeed) {
    Gun019 a;
    Gun019 b;
    a.SetSeed(555);
    b.SetSeed(555);
    for (int i = 0; i < 32; ++i) {
        EXPECT_FLOAT_EQ(a.ScatterAngle(15.0F), b.ScatterAngle(15.0F));
    }
}

// ---- Burst path draws ZERO RNG; main-shot path draws exactly ONE ------------
// FAITHFUL: game_full.c:965364-965373 (burst path has no RGRandom call)
//           game_full.c:965392         (main-shot path has exactly one)

TEST(Gun019Test, BurstPathDrawsNoRng) {
    // Calling BurstAdvance must NOT advance the RGRandom stream: a parallel
    // stream seeded identically must still match after a sequence of burst steps.
    Gun019 g;
    RGRandom ref;
    g.SetSeed(77777);
    ref.SetRandomSeed(77777);

    // Pump several burst steps; the stream must be untouched.
    (void)Gun019::BurstAdvance(0, 5);
    (void)Gun019::BurstAdvance(1, 5);
    (void)Gun019::BurstAdvance(4, 5); // last shot in burst

    // The first scatter draw from g must still match the very first draw from ref.
    const float spread = Gun019::SpreadHalfAngle(10.0F, 0.2F);
    EXPECT_FLOAT_EQ(g.ScatterAngle(spread), ref.Range(-spread, spread));
}

TEST(Gun019Test, SevenMainShotsProduceSingleDrawEach) {
    // End-to-end: simulate 7 main shots (c_count <= 0 path each time).
    // The scatter draw sequence from g must be draw-for-draw identical to ref.
    Gun019 g;
    RGRandom ref;
    g.SetSeed(31337);
    ref.SetRandomSeed(31337);

    // Angles/recoils vary to exercise different spread values.
    struct ShotParam { float angle; float recoil; };
    const std::vector<ShotParam> shots = {
        {10.0F, 0.0F}, {15.0F, 0.5F}, {5.0F,  1.0F},
        {20.0F, 0.3F}, {8.0F,  0.1F}, {12.0F, 0.7F},
        {0.0F,  0.5F}
    };
    for (const auto &p : shots) {
        const float spread = Gun019::SpreadHalfAngle(p.angle, p.recoil);
        const float got    = g.ScatterAngle(spread);
        const float exp    = ref.Range(-spread, spread);
        EXPECT_FLOAT_EQ(got, exp);
    }
}

// NOLINTEND(readability-magic-numbers)
