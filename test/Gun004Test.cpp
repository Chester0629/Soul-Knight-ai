#include <gtest/gtest.h>

#include <vector>

#include "combat/Gun004.hpp"
#include "data/RGRandom.hpp"

using Game::Gun004;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- ShotSpread: base + base*recoil, deterministic, no RNG ------------------
// FAITHFUL: Gun004__CreateBullet @ game_full.c:316365-316366
//   fVar4 = VectorSignedToFloat(uVar3, ...)        // base spread
//   fVar4 = fVar4 + fVar4 * *(float *)(iVar1+0x20) // + base*recoil

TEST(Gun004Test, ShotSpreadZeroRecoilIsBase) {
    // recoil 0 -> spread equals base.
    EXPECT_FLOAT_EQ(Gun004::ShotSpread(10.0F, 0.0F), 10.0F);
    EXPECT_FLOAT_EQ(Gun004::ShotSpread(0.0F, 0.0F), 0.0F);
}

TEST(Gun004Test, ShotSpreadScalesWithRecoil) {
    // base + base*recoil: 10 + 10*0.5 = 15.
    EXPECT_FLOAT_EQ(Gun004::ShotSpread(10.0F, 0.5F), 15.0F);
    // base + base*1 = 2*base: 8 + 8*1 = 16.
    EXPECT_FLOAT_EQ(Gun004::ShotSpread(8.0F, 1.0F), 16.0F);
}

TEST(Gun004Test, ShotSpreadNegativeBasePassesThrough) {
    // Decomp applies no clamp; negative inputs (degenerate but possible) pass
    // through the formula unchanged.
    EXPECT_FLOAT_EQ(Gun004::ShotSpread(-5.0F, 0.0F), -5.0F);
    EXPECT_FLOAT_EQ(Gun004::ShotSpread(-5.0F, 1.0F), -10.0F);
}

// ---- ShouldFireShot: burst counter + limit + interrupt gate -----------------
// FAITHFUL: FUN_003ed07c @ game_full.c:316295-316309
//   unaff_r5 = unaff_r5 + 1;
//   if (unaff_r4[0x1b] <= unaff_r5) -> END (stop anim + SFX; OWNER)
//   if ((char)unaff_r4[0x1c] != 0)  -> END (interrupt break)
//   else -> CreateBullet()           -> fire

TEST(Gun004Test, ShouldFireShotBurstOfThreeFiresTwice) {
    // burstCount=3 fires exactly 2 shots (pumps 1,2 -> fire; pump 3 hits limit).
    // The counter increments BEFORE the limit compare (316295); the <= check at
    // 316296 ends the burst when unaff_r5 reaches burstCount, so a burst of N
    // fires N-1 bullets. Limit is <= not <.
    Gun004 g(3);
    g.BeginBurst();
    EXPECT_TRUE(g.ShouldFireShot(false));   // unaff_r5=1, 3<=1 false -> fire
    EXPECT_TRUE(g.ShouldFireShot(false));   // unaff_r5=2, 3<=2 false -> fire
    EXPECT_FALSE(g.ShouldFireShot(false));  // unaff_r5=3, 3<=3 true  -> end
}

TEST(Gun004Test, ShouldFireShotBurstOfOne) {
    // Burst size 1: first pump increments to 1, limit 1<=1 is true -> end immediately.
    Gun004 g(1);
    g.BeginBurst();
    EXPECT_FALSE(g.ShouldFireShot(false));
}

TEST(Gun004Test, ShouldFireShotBurstOfZeroEndsImmediately) {
    // Burst size 0: first pump gives unaff_r5=1, 0<=1 true -> end.
    Gun004 g(0);
    g.BeginBurst();
    EXPECT_FALSE(g.ShouldFireShot(false));
}

TEST(Gun004Test, ShouldFireShotInterruptStopsBurstBeforeLimit) {
    // Interrupt flag set (unaff_r4[0x1c] != 0) before the limit is reached.
    // The limit compare fires first, then the interrupt check.
    Gun004 g(5);
    g.BeginBurst();
    EXPECT_TRUE(g.ShouldFireShot(false));    // pump 1 -> fire
    EXPECT_FALSE(g.ShouldFireShot(true));    // pump 2, not at limit, but interrupted
}

TEST(Gun004Test, ShouldFireShotInterruptAtLimitUsesLimitPath) {
    // When the limit is reached simultaneously with an interrupt, the limit check
    // fires first (decomp order: limit compare is ABOVE the interrupt check).
    // Both end the burst; the result is false either way.
    Gun004 g(2);
    g.BeginBurst();
    EXPECT_TRUE(g.ShouldFireShot(false));   // pump 1 -> fire
    EXPECT_FALSE(g.ShouldFireShot(true));   // pump 2, 2<=2 true -> END (limit path)
}

TEST(Gun004Test, BeginBurstResetsCounter) {
    // BeginBurst sets m_ShotsFired to 0; a second burst must restart cleanly.
    Gun004 g(2);
    g.BeginBurst();
    EXPECT_TRUE(g.ShouldFireShot(false));
    EXPECT_FALSE(g.ShouldFireShot(false));
    // restart
    g.BeginBurst();
    EXPECT_TRUE(g.ShouldFireShot(false));
    EXPECT_FALSE(g.ShouldFireShot(false));
}

TEST(Gun004Test, ShotsFiredCounterMatchesPumps) {
    // ShotsFired() tracks unaff_r5 (incremented each pump regardless of fire/end).
    Gun004 g(4);
    g.BeginBurst();
    EXPECT_EQ(g.ShotsFired(), 0);
    g.ShouldFireShot(false);
    EXPECT_EQ(g.ShotsFired(), 1);
    g.ShouldFireShot(false);
    EXPECT_EQ(g.ShotsFired(), 2);
}

// ---- ShotScatterAngle: exactly ONE float draw, symmetric in [-spread,spread] -
// FAITHFUL: Gun004__CreateBullet @ game_full.c:316371
//   RGRandom__Range(*(int*)(param_1+0x60), -fVar4, fVar4, 0)

TEST(Gun004Test, ScatterAngleStaysWithinBounds) {
    Gun004 g(10);
    g.SetSeed(1234);
    const float spread = Gun004::ShotSpread(15.0F, 0.2F);
    for (int i = 0; i < 64; ++i) {
        const float a = g.ShotScatterAngle(spread);
        EXPECT_GE(a, -spread);
        EXPECT_LE(a, spread);
    }
}

TEST(Gun004Test, ScatterDrawsExactlyOneFloatInOrder) {
    // A parallel same-seeded RGRandom must reproduce each scatter draw exactly:
    // proves the brain draws ONE float per shot, in order, with -spread/+spread
    // bounds and nothing else advancing the stream.
    // FAITHFUL: Gun004__CreateBullet @ game_full.c:316371
    Gun004 g(5);
    RGRandom ref;
    g.SetSeed(98765);
    ref.SetRandomSeed(98765);

    const std::vector<float> spreads = {5.0F, 12.5F, 0.0F, 7.0F, 18.0F};
    for (const float spread : spreads) {
        const float got      = g.ShotScatterAngle(spread);
        const float expected = ref.Range(-spread, spread); // one parallel draw
        EXPECT_FLOAT_EQ(got, expected);
    }
}

TEST(Gun004Test, ScatterIsDeterministicForSameSeed) {
    Gun004 a(10);
    Gun004 b(10);
    a.SetSeed(555);
    b.SetSeed(555);
    for (int i = 0; i < 32; ++i) {
        EXPECT_FLOAT_EQ(a.ShotScatterAngle(15.0F), b.ShotScatterAngle(15.0F));
    }
}

TEST(Gun004Test, ZeroSpreadProducesZeroScatter) {
    // Range(-0, +0) must produce 0 regardless of seed.
    Gun004 g(1);
    g.SetSeed(42);
    EXPECT_FLOAT_EQ(g.ShotScatterAngle(0.0F), 0.0F);
}

TEST(Gun004Test, ShotsFiredDoesNotAdvanceRngStream) {
    // ShouldFireShot / BeginBurst perform NO RNG draw.
    // A parallel stream seeded identically must still match after driving the
    // burst machinery without calling ShotScatterAngle.
    Gun004 g(3);
    RGRandom ref;
    g.SetSeed(77777);
    ref.SetRandomSeed(77777);

    g.BeginBurst();
    g.ShouldFireShot(false);
    g.ShouldFireShot(false);
    g.ShouldFireShot(false); // end of burst; no RNG consumed

    // Now the first scatter draw must still match the reference stream's first draw.
    const float spread = Gun004::ShotSpread(10.0F, 0.3F);
    EXPECT_FLOAT_EQ(g.ShotScatterAngle(spread), ref.Range(-spread, spread));
}

// ---- end-to-end: full burst, one scatter draw per fired shot ----------------
// FAITHFUL: FUN_003ed07c + Gun004__CreateBullet @ game_full.c:316285-316379

TEST(Gun004Test, FullBurstDrawsOneFloatPerFiredShot) {
    // Simulate a complete burst of 4: pumps 1-3 fire (unaff_r5 1,2,3 < 4),
    // pump 4 hits the limit (4<=4). Each fired shot draws one scatter angle.
    // A parallel same-seeded stream must reproduce the whole sequence lockstep.
    auto run = [](int seed, int burstSize, float spread) -> std::vector<float> {
        Gun004 g(burstSize);
        g.SetSeed(seed);
        std::vector<float> trace;
        g.BeginBurst();
        while (g.ShouldFireShot(false)) {
            trace.push_back(g.ShotScatterAngle(spread));
        }
        return trace;
    };

    // burst of 4 -> 3 fired shots (pumps 1,2,3 fire; pump 4 hits limit)
    const int   burstSize = 4;
    const float spread    = Gun004::ShotSpread(12.0F, 0.25F);
    const auto  a         = run(31337, burstSize, spread);
    const auto  b         = run(31337, burstSize, spread);

    ASSERT_EQ(a.size(), static_cast<std::size_t>(3));
    ASSERT_EQ(b.size(), static_cast<std::size_t>(3));
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_FLOAT_EQ(a[i], b[i]);
    }
}

TEST(Gun004Test, FullBurstGoldenAgainstParallelRng) {
    // Replay a complete burst draw-for-draw against a parallel same-seeded
    // RGRandom to prove count + order lockstep fidelity.
    const int   kSeed      = 112233;
    const int   kBurstSize = 5;
    const float kBase      = 10.0F;
    const float kRecoil    = 0.5F;
    const float spread     = Gun004::ShotSpread(kBase, kRecoil);

    Gun004   g(kBurstSize);
    RGRandom ref;
    g.SetSeed(kSeed);
    ref.SetRandomSeed(kSeed);

    g.BeginBurst();
    int firedCount = 0;
    while (g.ShouldFireShot(false)) {
        const float got      = g.ShotScatterAngle(spread);
        const float expected = ref.Range(-spread, spread);
        EXPECT_FLOAT_EQ(got, expected);
        ++firedCount;
    }
    // burst of 5: pumps 1-4 fire, pump 5 hits limit -> exactly 4 fired shots.
    EXPECT_EQ(firedCount, kBurstSize - 1);
    // Reference stream must now be in lockstep (no extra draws consumed).
    EXPECT_FLOAT_EQ(g.ShotScatterAngle(spread), ref.Range(-spread, spread));
}

// NOLINTEND(readability-magic-numbers)
