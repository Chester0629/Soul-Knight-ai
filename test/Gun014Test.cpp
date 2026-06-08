#include <gtest/gtest.h>

#include <vector>

#include "combat/Gun014.hpp"
#include "data/RGRandom.hpp"

using Game::Gun014;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- ShouldReadjust: gate = angle * count >= 361 (0x169), no RNG draw ---------

TEST(Gun014Test, ShouldReadjustGateReturnsFalseWhenProductBelowThreshold) {
    // FAITHFUL: AdjustAngle @ 964355 -- `if (angle*count < 0x169) return`.
    // Product 360 < 361 -> no re-roll.
    Gun014 g;
    g.SetBaseAngle(36);
    EXPECT_FALSE(g.ShouldReadjust(10)); // 36 * 10 = 360 < 361
}

TEST(Gun014Test, ShouldReadjustGateReturnsTrueAtExactThreshold) {
    // Product == 361 satisfies the complement (>= 361) -> re-roll fires.
    Gun014 g;
    g.SetBaseAngle(361);
    EXPECT_TRUE(g.ShouldReadjust(1)); // 361 * 1 = 361 >= 361
}

TEST(Gun014Test, ShouldReadjustGateReturnsTrueAboveThreshold) {
    Gun014 g;
    g.SetBaseAngle(180);
    EXPECT_TRUE(g.ShouldReadjust(3)); // 180 * 3 = 540 >= 361
}

TEST(Gun014Test, ShouldReadjustGateReturnsFalseWithZeroAngle) {
    // 0 * anything = 0 < 361 -> never re-rolls.
    Gun014 g;
    g.SetBaseAngle(0);
    EXPECT_FALSE(g.ShouldReadjust(1000));
}

TEST(Gun014Test, ShouldReadjustGateReturnsFalseWithZeroCount) {
    // angle * 0 = 0 < 361 -> no re-roll.
    Gun014 g;
    g.SetBaseAngle(400);
    EXPECT_FALSE(g.ShouldReadjust(0));
}

// ---- ReadjustAngle: gate predicate only; FUN_001ceef4(0x168) is NOT m_Rng ------
// TODO[verify]: FUN_001ceef4(0x168) is not RGRandom::Range; do not advance m_Rng here.

TEST(Gun014Test, ReadjustAngleDrawsOneIntOnGatePass) {
    // FAITHFUL: AdjustAngle @ 964358 -- FUN_001ceef4(0x168) is called with NO
    // rng-instance argument; param_1+0x60 (m_Rng) is never accessed in
    // Gun014__AdjustAngle (964350-964361). ReadjustAngle() returns true when
    // the gate passes but does NOT advance m_Rng.
    // TODO[verify]: FUN_001ceef4(0x168) is not RGRandom::Range; do not advance m_Rng here.
    // FALSE GOLDEN (removed): the original test seeded a parallel RGRandom and
    // asserted weapon.ReadjustAngle(1) consumes exactly one int draw from m_Rng
    // in lockstep. That lockstep contract is not grounded in the decomp body.
    Gun014 weapon;
    weapon.SetSeed(12345);
    weapon.SetBaseAngle(400); // any angle: 400 * 1 = 400 >= 361 -> gate passes

    const bool did = weapon.ReadjustAngle(1);
    EXPECT_TRUE(did);
    // m_Rng is NOT advanced by ReadjustAngle; BaseAngle() is not updated by the
    // brain (owner concern). The gate result (true) is the only observable here.
}

TEST(Gun014Test, ReadjustAngleProducesValueInRange) {
    // ReadjustAngle does NOT update BaseAngle() -- FUN_001ceef4(0x168) is a
    // global/non-per-object call, not RGRandom::Range; m_Rng is never accessed
    // in Gun014__AdjustAngle (964350-964361). BaseAngle() is unchanged after call.
    // TODO[verify]: FUN_001ceef4(0x168) is not RGRandom::Range; do not advance m_Rng here.
    // FALSE GOLDEN (corrected): the original asserted BaseAngle() in [0,359] after
    // a gate-pass call, which assumed ReadjustAngle writes m_BaseAngle from m_Rng.
    Gun014 weapon;
    weapon.SetSeed(777);
    weapon.SetBaseAngle(400);
    const bool did = weapon.ReadjustAngle(1); // gate passes: 400 >= 361
    EXPECT_TRUE(did);
    // BaseAngle() is owner-written from FUN_001ceef4(0x168); brain does not touch it.
    EXPECT_EQ(weapon.BaseAngle(), 400); // unchanged by brain
}

TEST(Gun014Test, ReadjustAngleMakesNoDrawWhenGateFails) {
    // FAITHFUL: AdjustAngle @ 964355 -- early `return` leaves the stream
    // untouched. ReadjustAngle() returns false and BaseAngle() is unchanged.
    // TODO[verify]: FUN_001ceef4(0x168) is not RGRandom::Range; do not advance m_Rng here.
    // FALSE GOLDEN (removed): lines that asserted weapon.Rng().Range(0,1000) ==
    // reference.Range(0,1000) after a no-draw call. That lockstep assertion is
    // only meaningful if ReadjustAngle touches m_Rng -- the decomp shows it does not,
    // making the assertion vacuously true but the lockstep contract fabricated.
    Gun014 weapon;
    weapon.SetSeed(9999);
    weapon.SetBaseAngle(1); // 1 * 1 = 1 < 361 -> gate fails

    const bool did = weapon.ReadjustAngle(1);
    EXPECT_FALSE(did);
    EXPECT_EQ(weapon.BaseAngle(), 1); // unchanged
}

TEST(Gun014Test, ReadjustAngleLeavesAngleUnchangedOnGateFail) {
    // Angle must remain exactly what was set when the gate does not fire.
    Gun014 g;
    g.SetBaseAngle(50);
    g.ReadjustAngle(3); // 50 * 3 = 150 < 361 -> no change
    EXPECT_EQ(g.BaseAngle(), 50);
}

TEST(Gun014Test, ReadjustAngleDrivesBorderProduct) {
    // Product just below threshold (360 < 361) -> gate fails, no action.
    // Product at threshold (361 >= 361) -> gate passes (owner must re-roll via
    // FUN_001ceef4(0x168); brain does NOT write BaseAngle()).
    // TODO[verify]: FUN_001ceef4(0x168) is not RGRandom::Range; do not advance m_Rng here.
    // FALSE GOLDEN (corrected): original asserted EXPECT_NE(at.BaseAngle(), 361)
    // after the gate-pass, assuming the brain overwrites m_BaseAngle from m_Rng.
    Gun014 below;
    below.SetSeed(111);
    below.SetBaseAngle(360);
    EXPECT_FALSE(below.ReadjustAngle(1)); // 360 * 1 = 360 < 361 -> gate fails
    EXPECT_EQ(below.BaseAngle(), 360);    // unchanged

    Gun014 at;
    at.SetSeed(111);
    at.SetBaseAngle(361);
    EXPECT_TRUE(at.ReadjustAngle(1));     // 361 * 1 = 361 >= 361 -> gate passes
    EXPECT_EQ(at.BaseAngle(), 361);       // brain does not write BaseAngle; owner must
}

TEST(Gun014Test, ReadjustAngleDrawCountLockstepAcrossMultipleCalls) {
    // FALSE GOLDEN (entire test): the original test simulated several cycles and
    // asserted a parallel reference stream drawing Range(0,360) on gate-pass calls
    // stayed in lockstep with weapon.Rng(). Since game_full.c:964358 does NOT use
    // param_1+0x60 (m_Rng), the draw-count/order contract this test pinned is
    // fabricated. ReadjustAngle() does not advance m_Rng at all.
    // TODO[verify]: FUN_001ceef4(0x168) is not RGRandom::Range; do not advance m_Rng here.
    // Retained: gate-result correctness checks that are decomp-grounded.
    Gun014 weapon;
    weapon.SetSeed(31337);

    // cycle 1: angle=400, count=1 -> product 400 >= 361 -> gate passes
    weapon.SetBaseAngle(400);
    EXPECT_TRUE(weapon.ReadjustAngle(1));

    // cycle 2: force a known fail
    weapon.SetBaseAngle(10);
    EXPECT_FALSE(weapon.ReadjustAngle(1)); // 10 < 361 -> no re-roll

    // cycle 3: gate passes again
    weapon.SetBaseAngle(500);
    EXPECT_TRUE(weapon.ReadjustAngle(2)); // 500*2=1000 >= 361 -> gate passes
}

// ---- CanCreateBullet: count >= 1, pure gate, no RNG draw ----------------------

TEST(Gun014Test, CanCreateBulletReturnsFalseForZeroCount) {
    // FAITHFUL: CreateBullet @ 964406 -- `if (count < 1) return`.
    EXPECT_FALSE(Gun014::CanCreateBullet(0));
    EXPECT_FALSE(Gun014::CanCreateBullet(-1));
}

TEST(Gun014Test, CanCreateBulletReturnsTrueForPositiveCount) {
    EXPECT_TRUE(Gun014::CanCreateBullet(1));
    EXPECT_TRUE(Gun014::CanCreateBullet(5));
    EXPECT_TRUE(Gun014::CanCreateBullet(100));
}

// ---- FanStartIndex: symmetric fan lowest offset, pure, no RNG draw -----------

TEST(Gun014Test, FanStartIndexEvenCountIsNegativeHalf) {
    // FAITHFUL: CreateBullet @ 964422-964423 -- even: -(uVar2 / 2).
    EXPECT_EQ(Gun014::FanStartIndex(2), -1);  // -(2/2) = -1
    EXPECT_EQ(Gun014::FanStartIndex(4), -2);  // -(4/2) = -2
    EXPECT_EQ(Gun014::FanStartIndex(6), -3);
    EXPECT_EQ(Gun014::FanStartIndex(8), -4);
}

TEST(Gun014Test, FanStartIndexOddCountIsNegativeHalfMinusOne) {
    // FAITHFUL: CreateBullet @ 964425-964426 -- odd: -((uVar2 - 1) / 2).
    EXPECT_EQ(Gun014::FanStartIndex(1), 0);   // -((1-1)/2) = 0
    EXPECT_EQ(Gun014::FanStartIndex(3), -1);  // -((3-1)/2) = -1
    EXPECT_EQ(Gun014::FanStartIndex(5), -2);  // -((5-1)/2) = -2
    EXPECT_EQ(Gun014::FanStartIndex(7), -3);
}

TEST(Gun014Test, FanStartIndexSymmetryProperty) {
    // For odd count N: start = -((N-1)/2), span = start..start+N covers
    // a range centred on 0 -> the pellet count is balanced.
    for (int n = 1; n <= 9; n += 2) {
        EXPECT_EQ(Gun014::FanStartIndex(n), -((n - 1) / 2));
    }
    // For even count N: start = -(N/2).
    for (int n = 2; n <= 10; n += 2) {
        EXPECT_EQ(Gun014::FanStartIndex(n), -(n / 2));
    }
}

// ---- SpreadWithRecoil: base + base*recoil = base*(1+recoil), no RNG draw ------

TEST(Gun014Test, SpreadWithRecoilZeroRecoilIsBaseOnly) {
    // FAITHFUL: CreateBullet @ 964428 -- fVar5 = fVar5 + fVar5 * recoil.
    EXPECT_FLOAT_EQ(Gun014::SpreadWithRecoil(15.0F, 0.0F), 15.0F);
    EXPECT_FLOAT_EQ(Gun014::SpreadWithRecoil(0.0F, 5.0F), 0.0F);
}

TEST(Gun014Test, SpreadWithRecoilIsBasePlusBaseTimesRecoil) {
    EXPECT_FLOAT_EQ(Gun014::SpreadWithRecoil(10.0F, 0.5F), 15.0F);  // 10 + 10*0.5
    EXPECT_FLOAT_EQ(Gun014::SpreadWithRecoil(8.0F, 1.0F),  16.0F);  // 8 + 8*1
    EXPECT_FLOAT_EQ(Gun014::SpreadWithRecoil(20.0F, 0.25F), 25.0F); // 20 + 20*0.25
}

TEST(Gun014Test, SpreadWithRecoilAppliesNoClamp) {
    // The decomp applies no Mathf.Max clamp here (unlike Gun016).
    EXPECT_FLOAT_EQ(Gun014::SpreadWithRecoil(10.0F, -0.5F), 5.0F);  // 10 + 10*-0.5
    EXPECT_FLOAT_EQ(Gun014::SpreadWithRecoil(10.0F, -1.0F), 0.0F);
}

// ---- ScatterDraw: ONE float draw Range(-spread, +spread), max-inclusive ------

TEST(Gun014Test, ScatterDrawEqualsSymmetricRangeDraw) {
    // FAITHFUL: CreateBullet @ 964434 -- RGRandom::Range(-fVar5, fVar5, 0).
    // A parallel same-seeded reference draw must match exactly.
    const float base   = 12.0F;
    const float recoil = 0.25F;
    const float spread = Gun014::SpreadWithRecoil(base, recoil); // 12 + 12*0.25 = 15

    Gun014 weapon;
    weapon.SetSeed(42424242);
    RGRandom reference;
    reference.SetRandomSeed(42424242);

    const float got  = weapon.ScatterDraw(spread);
    const float want = reference.Range(-spread, spread); // one parallel float draw
    EXPECT_FLOAT_EQ(got, want);
}

TEST(Gun014Test, ScatterDrawStaysWithinBounds) {
    // Each draw is in [-spread, +spread] (max inclusive).
    const float spread = Gun014::SpreadWithRecoil(15.0F, 0.2F); // 15 + 15*0.2 = 18

    Gun014 weapon;
    weapon.SetSeed(1234);
    for (int i = 0; i < 64; ++i) {
        const float s = weapon.ScatterDraw(spread);
        EXPECT_GE(s, -spread);
        EXPECT_LE(s, spread);
    }
}

TEST(Gun014Test, ScatterDrawsExactlyOneFloatPerCallInOrder) {
    // Draw count + order must stay lockstep across a sequence of pellets.
    // Verifies that no hidden draws occur before or after the single Range call.
    const float spread = 10.0F;

    Gun014 weapon;
    weapon.SetSeed(98765);
    RGRandom reference;
    reference.SetRandomSeed(98765);

    for (int pellet = 0; pellet < 8; ++pellet) {
        EXPECT_FLOAT_EQ(weapon.ScatterDraw(spread),
                        reference.Range(-spread, spread));
    }

    // Streams agree on the very next int draw after the float sequence.
    EXPECT_EQ(weapon.Rng().Range(0, 1000000), reference.Range(0, 1000000));
}

TEST(Gun014Test, ScatterDrawIsDeterministicForSameSeed) {
    Gun014 a;
    Gun014 b;
    a.SetSeed(555);
    b.SetSeed(555);
    for (int i = 0; i < 32; ++i) {
        EXPECT_FLOAT_EQ(a.ScatterDraw(18.0F), b.ScatterDraw(18.0F));
    }
}

TEST(Gun014Test, ZeroSpreadScattersToZero) {
    // spread == 0 -> Range(0, 0) -> 0, still consumes exactly one float draw.
    Gun014 weapon;
    weapon.SetSeed(2024);
    RGRandom reference;
    reference.SetRandomSeed(2024);

    const float got = weapon.ScatterDraw(0.0F);
    EXPECT_FLOAT_EQ(got, reference.Range(0.0F, 0.0F));
    EXPECT_FLOAT_EQ(got, 0.0F);
}

// ---- FireOrInvoke: pure predicate, no RNG draw --------------------------------

TEST(Gun014Test, FireOrInvokeReturnsTrueWhenInAtkFalse) {
    // FAITHFUL: Attack @ 964374 -- `if ((char)param_1[0x1c] == '\0') CreateBullet`.
    // inAtk == 0 (false) -> fire immediately -> return true.
    EXPECT_TRUE(Gun014::FireOrInvoke(false));
}

TEST(Gun014Test, FireOrInvokeReturnsFalseWhenInAtkTrue) {
    // inAtk != 0 (true) -> schedule Invoke delayed re-fire -> return false.
    EXPECT_FALSE(Gun014::FireOrInvoke(true));
}

// ---- End-to-end: combined AdjustAngle + CreateBullet per-pellet RNG lockstep -

TEST(Gun014Test, FullFiringSequenceLockstepWithParallelStream) {
    // Simulate a multi-pellet burst: AdjustAngle gate passes (no m_Rng draw --
    // FUN_001ceef4(0x168) is a global/non-per-object call, NOT RGRandom::Range),
    // then CreateBullet draws 1 float per pellet from m_Rng.
    // TODO[verify]: FUN_001ceef4(0x168) is not RGRandom::Range; do not advance m_Rng here.
    // FALSE GOLDEN (removed): the original reference path issued ref.Range(0,360)
    // to "mirror the AdjustAngle draw" and asserted weapon.BaseAngle()==refAngle.
    // Since game_full.c:964358 does NOT use param_1+0x60, that interleaved
    // int+float stream was fabricated. Removed lines 332-333 of the original test.

    const int   seed        = 31337;
    const int   bulletCount = 3;
    const float baseSpread  = 10.0F;
    const float recoil      = 0.3F;
    const float spread      = Gun014::SpreadWithRecoil(baseSpread, recoil); // 13

    // --- weapon path ---
    Gun014 weapon;
    weapon.SetSeed(seed);
    weapon.SetBaseAngle(400); // product 400*3 = 1200 >= 361 -> gate passes

    const bool did = weapon.ReadjustAngle(bulletCount); // gate passes; no m_Rng draw
    EXPECT_TRUE(did);

    // One float draw per pellet in the fan.
    float pelletScatter[3];
    for (int i = 0; i < bulletCount; ++i) {
        pelletScatter[i] = weapon.ScatterDraw(spread); // 1 float draw each
    }

    // --- reference path (parallel same-seeded stream, float-only) ---
    // ReadjustAngle does NOT advance m_Rng, so the reference starts directly
    // with the pellet float draws.
    RGRandom ref;
    ref.SetRandomSeed(seed);

    for (int i = 0; i < bulletCount; ++i) {
        EXPECT_FLOAT_EQ(pelletScatter[i], ref.Range(-spread, spread));
    }

    // Streams still agree after the full sequence.
    EXPECT_EQ(weapon.Rng().Range(0, 10000), ref.Range(0, 10000));
}

TEST(Gun014Test, FiringSequenceGateFailPath) {
    // FALSE GOLDEN (comment corrected): the original test comment stated "the
    // angle-draw slot is absent from the reference" -- implying that on a
    // gate-pass path an int draw WOULD appear. Since game_full.c:964358 does not
    // use param_1+0x60, ReadjustAngle() never advances m_Rng on any path (pass
    // or fail). The reference is therefore always float-only regardless of the
    // gate outcome. The float-only lockstep below is correct.
    // TODO[verify]: FUN_001ceef4(0x168) is not RGRandom::Range; do not advance m_Rng here.

    const int   seed        = 55555;
    const int   bulletCount = 5;
    const float spread      = Gun014::SpreadWithRecoil(8.0F, 0.0F); // 8

    Gun014 weapon;
    weapon.SetSeed(seed);
    weapon.SetBaseAngle(1); // 1 * 5 = 5 < 361 -> gate fails

    const bool did = weapon.ReadjustAngle(bulletCount);
    EXPECT_FALSE(did);
    EXPECT_EQ(weapon.BaseAngle(), 1);

    float pelletScatter[5];
    for (int i = 0; i < bulletCount; ++i) {
        pelletScatter[i] = weapon.ScatterDraw(spread);
    }

    // Reference: ReadjustAngle never advances m_Rng; only 5 float draws.
    RGRandom ref;
    ref.SetRandomSeed(seed);
    for (int i = 0; i < bulletCount; ++i) {
        EXPECT_FLOAT_EQ(pelletScatter[i], ref.Range(-spread, spread));
    }

    // Streams agree after.
    EXPECT_EQ(weapon.Rng().Range(0, 10000), ref.Range(0, 10000));
}

TEST(Gun014Test, FiringSequenceIsReproducibleForSameSeed) {
    // Identical seeds must produce identical angle + scatter sequences.
    auto run = [](int seed) -> std::vector<float> {
        Gun014 g;
        g.SetSeed(seed);
        g.SetBaseAngle(400);
        std::vector<float> trace;
        g.ReadjustAngle(1);
        trace.push_back(static_cast<float>(g.BaseAngle()));
        const float spread = Gun014::SpreadWithRecoil(12.0F, 0.4F); // 16.8
        for (int i = 0; i < 4; ++i) {
            trace.push_back(g.ScatterDraw(spread));
        }
        return trace;
    };

    const auto a = run(98765);
    const auto b = run(98765);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_FLOAT_EQ(a[i], b[i]);
    }
}

// NOLINTEND(readability-magic-numbers)
