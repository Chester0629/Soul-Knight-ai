#include <gtest/gtest.h>

#include <vector>

#include "combat/Gun007.hpp"
#include "data/RGRandom.hpp"

using Game::Gun007;
using Game::Gun007BurstIterator;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- ChargeRatio: aTime / maxTime, no clamp, no RNG draw -------------------

TEST(Gun007Test, ChargeRatioZeroAtColdStart) {
    // No charge held -> ratio 0.
    EXPECT_FLOAT_EQ(Gun007::ChargeRatio(0.0F, 2.0F), 0.0F);
}

TEST(Gun007Test, ChargeRatioHalfAndFull) {
    // 1.0 / 2.0 = 0.5 ; 2.0 / 2.0 = 1.0.
    EXPECT_FLOAT_EQ(Gun007::ChargeRatio(1.0F, 2.0F), 0.5F);
    EXPECT_FLOAT_EQ(Gun007::ChargeRatio(2.0F, 2.0F), 1.0F);
}

TEST(Gun007Test, ChargeRatioOverHoldExceedsOneNoClamp) {
    // FAITHFUL: Gun007__Attack @ game_full.c:317110 applies NO clamp;
    // an over-hold yields a ratio > 1 exactly as the division produces.
    EXPECT_FLOAT_EQ(Gun007::ChargeRatio(3.0F, 2.0F), 1.5F);
}

TEST(Gun007Test, ChargeRatioGuardsNonPositiveMaxTime) {
    // Degenerate divisor guard (impossible in the original): returns 0, not NaN.
    EXPECT_FLOAT_EQ(Gun007::ChargeRatio(1.0F, 0.0F), 0.0F);
    EXPECT_FLOAT_EQ(Gun007::ChargeRatio(1.0F, -1.0F), 0.0F);
}

TEST(Gun007Test, ChargeRatioTakesNoRngDraw) {
    // No RNG draw: a parallel stream stays in lockstep after any number of calls.
    Gun007 g;
    RGRandom ref;
    g.SetSeed(1111);
    ref.SetRandomSeed(1111);
    for (int i = 0; i < 8; ++i) {
        Gun007::ChargeRatio(static_cast<float>(i) * 0.5F, 2.0F);
    }
    // Both streams must still agree on the FIRST draw they each take.
    EXPECT_FLOAT_EQ(g.Rng().Range(0.0F, 1.0F), ref.Range(0.0F, 1.0F));
}

// ---- IsFullCharge: 1.0 <= charge (Gate from Attack @ 317119) ---------------

TEST(Gun007Test, FullChargeGateBelowThreshold) {
    EXPECT_FALSE(Gun007::IsFullCharge(0.0F));
    EXPECT_FALSE(Gun007::IsFullCharge(0.5F));
    EXPECT_FALSE(Gun007::IsFullCharge(0.9999F));
}

TEST(Gun007Test, FullChargeGateAtAndAboveThreshold) {
    // FAITHFUL: `if (1.0 <= charge)` -- exactly 1.0 and above all trigger.
    EXPECT_TRUE(Gun007::IsFullCharge(Gun007::kFullChargeRatio));
    EXPECT_TRUE(Gun007::IsFullCharge(1.5F));
}

// ---- BulletCount: Max(1, FloorToInt(maxCount * Min(1, charge))) -------------

TEST(Gun007Test, BulletCountHalfChargeHalfBurst) {
    // 6 * 0.5 = 3.0 -> floor 3 -> Max(1,3) = 3.
    EXPECT_EQ(Gun007::BulletCount(6, 0.5F), 3);
}

TEST(Gun007Test, BulletCountFullChargeIsMaxCount) {
    // FAITHFUL: Min(1, charge) caps at 1.0; floor(maxCount*1.0) = maxCount.
    EXPECT_EQ(Gun007::BulletCount(8, 1.0F), 8);
}

TEST(Gun007Test, BulletCountOverChargeStillCapsAtMaxCount) {
    // FAITHFUL: the Min(1.0, charge) cap is applied BEFORE the multiply, so
    // an over-charge never grows past maxCount.
    EXPECT_EQ(Gun007::BulletCount(5, 1.5F), 5);
    EXPECT_EQ(Gun007::BulletCount(5, 99.0F), 5);
}

TEST(Gun007Test, BulletCountMinimumIsOne) {
    // FAITHFUL: Mathf.Max(1, floor) @ 317251: even near-zero charge gives 1.
    EXPECT_EQ(Gun007::BulletCount(10, 0.0F), 1);
    EXPECT_EQ(Gun007::BulletCount(1, 0.0F), 1);
    EXPECT_EQ(Gun007::BulletCount(10, -1.0F), 1);
}

TEST(Gun007Test, BulletCountFloorsBetweenWholeNumbers) {
    // 5 * 0.7 = 3.5 -> floor 3 -> Max(1,3) = 3.
    EXPECT_EQ(Gun007::BulletCount(5, 0.7F), 3);
    // 5 * 0.2 = 1.0 -> floor 1 -> Max(1,1) = 1.
    EXPECT_EQ(Gun007::BulletCount(5, 0.2F), 1);
}

TEST(Gun007Test, BulletCountTakesNoRngDraw) {
    Gun007 g;
    RGRandom ref;
    g.SetSeed(2222);
    ref.SetRandomSeed(2222);
    for (int i = 0; i < 16; ++i) {
        Gun007::BulletCount(8, static_cast<float>(i) * 0.1F);
    }
    EXPECT_FLOAT_EQ(g.Rng().Range(0.0F, 1.0F), ref.Range(0.0F, 1.0F));
}

// ---- MuzzleOffset: base + charge * dir, no RNG draw ------------------------

TEST(Gun007Test, MuzzleOffsetZeroChargeIsBase) {
    // charge 0 -> base + 0*dir = base.
    EXPECT_FLOAT_EQ(Gun007::MuzzleOffset(5.0F, 3.0F, 0.0F), 5.0F);
}

TEST(Gun007Test, MuzzleOffsetFullCharge) {
    // FAITHFUL: *(owner+0x20) + (int)(charge * *(owner+0x78)) -- base + charge*dir.
    EXPECT_FLOAT_EQ(Gun007::MuzzleOffset(5.0F, 3.0F, 1.0F), 8.0F);
}

TEST(Gun007Test, MuzzleOffsetHalfCharge) {
    EXPECT_FLOAT_EQ(Gun007::MuzzleOffset(4.0F, 6.0F, 0.5F), 7.0F);
}

TEST(Gun007Test, MuzzleOffsetOverchargeExtrapolates) {
    // The decomp uses the RAW (uncapped) charge for muzzle: over-hold pushes
    // the muzzle further than the full-charge position.
    EXPECT_FLOAT_EQ(Gun007::MuzzleOffset(2.0F, 4.0F, 1.5F), 8.0F);
}

TEST(Gun007Test, MuzzleOffsetNegativeDir) {
    // Negative dir means the muzzle moves opposite the charge direction.
    EXPECT_FLOAT_EQ(Gun007::MuzzleOffset(10.0F, -2.0F, 0.5F), 9.0F);
}

// ---- SizeForCharge: start + (end-start)*charge (unclamped lerp) ------------

TEST(Gun007Test, SizeAtZeroChargeIsStartSize) {
    EXPECT_FLOAT_EQ(Gun007::SizeForCharge(1.0F, 3.0F, 0.0F), 1.0F);
}

TEST(Gun007Test, SizeAtFullChargeIsEndSize) {
    // FAITHFUL: *(owner+0x90) + (*(owner+0x94) - *(owner+0x90)) * charge.
    EXPECT_FLOAT_EQ(Gun007::SizeForCharge(1.0F, 3.0F, 1.0F), 3.0F);
}

TEST(Gun007Test, SizeAtHalfChargeIsMidpoint) {
    EXPECT_FLOAT_EQ(Gun007::SizeForCharge(1.0F, 3.0F, 0.5F), 2.0F);
}

TEST(Gun007Test, SizeOverChargeExtrapolatesPastEnd) {
    // FAITHFUL: unclamped lerp -- an over-charge interpolates past `end`.
    // 1 + (3-1)*1.5 = 1 + 3 = 4.
    EXPECT_FLOAT_EQ(Gun007::SizeForCharge(1.0F, 3.0F, 1.5F), 4.0F);
}

TEST(Gun007Test, SizeTakesNoRngDraw) {
    Gun007 g;
    RGRandom ref;
    g.SetSeed(3333);
    ref.SetRandomSeed(3333);
    Gun007::SizeForCharge(1.0F, 5.0F, 0.75F);
    EXPECT_FLOAT_EQ(g.Rng().Range(0.0F, 1.0F), ref.Range(0.0F, 1.0F));
}

// ---- IsStrongShot: 0.6 < charge (MoveNext @ 317300) -----------------------

TEST(Gun007Test, StrongShotFalseBelowOrAtThreshold) {
    EXPECT_FALSE(Gun007::IsStrongShot(0.0F));
    EXPECT_FALSE(Gun007::IsStrongShot(0.5F));
    // FAITHFUL: `0.6 < charge` -- exactly 0.6 does NOT trigger (strict less-than).
    EXPECT_FALSE(Gun007::IsStrongShot(Gun007::kStrongShotThreshold));
}

TEST(Gun007Test, StrongShotTrueAboveThreshold) {
    EXPECT_TRUE(Gun007::IsStrongShot(0.601F));
    EXPECT_TRUE(Gun007::IsStrongShot(1.0F));
    EXPECT_TRUE(Gun007::IsStrongShot(1.5F));
}

// ---- Gun007BurstIterator state machine -------------------------------------

TEST(Gun007Test, IteratorStartsAtSetupState) {
    // Fresh iterator starts in kSetup (state 0).
    Gun007BurstIterator it(3);
    EXPECT_EQ(it.State(), Gun007BurstIterator::kSetup);
    EXPECT_EQ(it.Counter(), 0);
    EXPECT_FALSE(it.Fired());
}

TEST(Gun007Test, IteratorSetupYieldsNoBulletAdvancesToBurst) {
    // FAITHFUL: mapped 3 (kSetup) yields once with no bullet, advances to kBurst.
    Gun007BurstIterator it(3);
    const bool alive = it.MoveNext();
    EXPECT_TRUE(alive);                               // coroutine still alive
    EXPECT_FALSE(it.Fired());                         // no bullet on setup pump
    EXPECT_EQ(it.State(), Gun007BurstIterator::kBurst); // staged for burst
    EXPECT_EQ(it.Counter(), 0);                       // counter not yet touched
}

TEST(Gun007Test, IteratorBurstTickFiresBulletWhileCounterBelowLimit) {
    // FAITHFUL: counter < limit -> Fired() true on that tick.
    Gun007BurstIterator it(3);
    it.MoveNext(); // setup
    // First burst tick: counter becomes 1, limit 3 -> 1 < 3 -> fired.
    EXPECT_TRUE(it.MoveNext());
    EXPECT_TRUE(it.Fired());
    EXPECT_EQ(it.Counter(), 1);
    EXPECT_EQ(it.State(), Gun007BurstIterator::kBurst);
    // Second burst tick: counter 2, limit 3 -> 2 < 3 -> fired.
    EXPECT_TRUE(it.MoveNext());
    EXPECT_TRUE(it.Fired());
    EXPECT_EQ(it.Counter(), 2);
    EXPECT_EQ(it.State(), Gun007BurstIterator::kBurst);
}

TEST(Gun007Test, IteratorCleanupTickAtLimitNoFireAdvancesToEnd) {
    // FAITHFUL: counter >= limit -> no bullet spawn; cleanup (OWNER) then yield,
    // advancing to kEnd (state 2) for TurnActivate on the next MoveNext.
    Gun007BurstIterator it(3);
    it.MoveNext(); // setup
    it.MoveNext(); // burst: counter 1, fired
    it.MoveNext(); // burst: counter 2, fired
    // Cleanup tick: counter 3, limit 3 -> 3 < 3 is false -> no bullet.
    EXPECT_TRUE(it.MoveNext()); // yields once more (WaitForSeconds, OWNER)
    EXPECT_FALSE(it.Fired());
    EXPECT_EQ(it.Counter(), 3);
    EXPECT_EQ(it.State(), Gun007BurstIterator::kEnd); // staged for TurnActivate
}

TEST(Gun007Test, IteratorEndCallReturnsFalse) {
    // FAITHFUL: mapped 5 (kEnd) -> RGWeapon.TurnActivate (OWNER) then ends.
    Gun007BurstIterator it(3);
    it.MoveNext(); // setup
    it.MoveNext(); // burst tick 1
    it.MoveNext(); // burst tick 2
    it.MoveNext(); // cleanup tick -> kEnd
    // End call: MoveNext returns false (coroutine finished).
    EXPECT_FALSE(it.MoveNext());
}

TEST(Gun007Test, IteratorDoneAfterEndReturnsFalse) {
    // Once ended (kDone), any further MoveNext also returns false.
    Gun007BurstIterator it(1);
    it.MoveNext();           // setup -> kBurst
    it.MoveNext();           // cleanup tick (limit 1, counter 1 >= 1) -> kEnd
    it.MoveNext();           // end -> return false, sets kDone
    EXPECT_FALSE(it.MoveNext()); // kDone -> false
    EXPECT_EQ(it.State(), Gun007BurstIterator::kDone);
}

TEST(Gun007Test, IteratorMinimalLimitOneCleanupTickOnly) {
    // With limit=1: the first burst pump increments counter to 1; the
    // strict `if (counter < limit)` at decomp 317209 is 1 < 1 = false, so no
    // bullet fires -- only the cleanup tick runs and the iterator advances to
    // kEnd. FAITHFUL: BulletCount() ensures limit >= 1, so this is the
    // minimum valid limit; the burst fires 0 bullets, matching the decomp's
    // strict less-than gate.
    Gun007BurstIterator it(1);
    it.MoveNext(); // setup -> kBurst
    EXPECT_TRUE(it.MoveNext()); // cleanup tick: counter=1, 1 < 1 false -> no fire
    EXPECT_FALSE(it.Fired());
    EXPECT_EQ(it.State(), Gun007BurstIterator::kEnd);
}

TEST(Gun007Test, IteratorBurstCountMatchesLimit) {
    // A burst of limit N fires exactly (N-1) bullets (counters 1..N-1 are fired;
    // counter N is the cleanup tick). FAITHFUL: the decomp's counter < limit
    // strict less-than means the Nth pump is cleanup, not a fire.
    const int limit = 5;
    Gun007BurstIterator it(limit);
    it.MoveNext(); // setup
    int firedCount = 0;
    while (it.MoveNext()) {
        if (it.Fired()) {
            ++firedCount;
        }
    }
    // states: cleanup tick -> kEnd; end -> done. Total fired = limit - 1.
    EXPECT_EQ(firedCount, limit - 1);
}

TEST(Gun007Test, IteratorFullLifecycleForLimit3) {
    // Full lifecycle trace for limit=3: setup, fire, fire, cleanup, end, done.
    Gun007BurstIterator it(3);

    // Step 0: setup
    EXPECT_TRUE(it.MoveNext());
    EXPECT_EQ(it.State(), Gun007BurstIterator::kBurst);
    EXPECT_FALSE(it.Fired());

    // Step 1: burst tick counter=1 < 3 -> fired
    EXPECT_TRUE(it.MoveNext());
    EXPECT_EQ(it.Counter(), 1);
    EXPECT_TRUE(it.Fired());

    // Step 2: burst tick counter=2 < 3 -> fired
    EXPECT_TRUE(it.MoveNext());
    EXPECT_EQ(it.Counter(), 2);
    EXPECT_TRUE(it.Fired());

    // Step 3: cleanup tick counter=3 >= 3 -> no fire, advance to kEnd
    EXPECT_TRUE(it.MoveNext());
    EXPECT_EQ(it.Counter(), 3);
    EXPECT_FALSE(it.Fired());
    EXPECT_EQ(it.State(), Gun007BurstIterator::kEnd);

    // Step 4: end -> TurnActivate (OWNER), return false
    EXPECT_FALSE(it.MoveNext());
}

TEST(Gun007Test, IteratorIsDeterministicForSameLimit) {
    // Two iterators with the same limit trace identically.
    auto trace = [](int limit) -> std::vector<bool> {
        Gun007BurstIterator it(limit);
        std::vector<bool> fired;
        while (it.MoveNext()) {
            fired.push_back(it.Fired());
        }
        return fired;
    };
    const auto a = trace(4);
    const auto b = trace(4);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i], b[i]);
    }
}

// ---- Zero RNG draws across ALL Gun007 bodies --------------------------------

TEST(Gun007Test, NoRngDrawsOnAnyGun007Path) {
    // Gun007 is fully deterministic (no RGRandom draws on any path). A parallel
    // same-seeded stream must remain in lockstep after any number of calls to
    // any Gun007 method -- the Seeded() check confirms the stream was armed but
    // never advanced, and the first Range draw on both streams returns the same
    // value.
    Gun007 g;
    RGRandom ref;
    g.SetSeed(77777);
    ref.SetRandomSeed(77777);
    EXPECT_TRUE(g.Seeded());

    // Exercise every recoverable path.
    const float charge = 0.8F;
    Gun007::ChargeRatio(1.6F, 2.0F);
    Gun007::IsFullCharge(charge);
    Gun007::BulletCount(8, charge);
    Gun007::MuzzleOffset(3.0F, 2.0F, charge);
    Gun007::SizeForCharge(1.0F, 4.0F, charge);
    Gun007::IsStrongShot(charge);

    // Stream must be wholly untouched.
    EXPECT_FLOAT_EQ(g.Rng().Range(0.0F, 1.0F), ref.Range(0.0F, 1.0F));
}

TEST(Gun007Test, IteratorTakesNoRngDraw) {
    // The burst iterator also takes no RNG draw.
    Gun007 g;
    RGRandom ref;
    g.SetSeed(88888);
    ref.SetRandomSeed(88888);

    Gun007BurstIterator it(4);
    while (it.MoveNext()) { /* run full lifecycle */ }

    EXPECT_FLOAT_EQ(g.Rng().Range(0.0F, 1.0F), ref.Range(0.0F, 1.0F));
}

// ---- BulletCount + BurstIterator round-trip --------------------------------

TEST(Gun007Test, BulletCountFeedsIteratorCorrectly) {
    // BulletCount produces the limit; the iterator fires exactly (limit-1)
    // bullets. Verify the round-trip for several charge levels.
    const int maxCount = 6;
    struct Case { float charge; int expectFired; };
    // floor(6 * Min(1,c)) gives: 0.0->0->1, 0.5->3, 1.0->6, 1.5->6 (capped).
    // Fired = limit - 1 (strict less-than in the decomp).
    const std::vector<Case> cases = {
        {0.0F,  0}, // limit=1, fired=0
        {0.5F,  2}, // limit=3, fired=2
        {1.0F,  5}, // limit=6, fired=5
        {1.5F,  5}, // limit=6 (capped), fired=5
    };
    for (const auto &c : cases) {
        const int limit = Gun007::BulletCount(maxCount, c.charge);
        Gun007BurstIterator it(limit);
        it.MoveNext(); // setup
        int fired = 0;
        while (it.MoveNext()) {
            if (it.Fired()) { ++fired; }
        }
        EXPECT_EQ(fired, c.expectFired)
            << "charge=" << c.charge << " limit=" << limit;
    }
}

// NOLINTEND(readability-magic-numbers)
