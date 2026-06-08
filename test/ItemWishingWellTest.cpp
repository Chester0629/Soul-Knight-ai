#include <gtest/gtest.h>

#include <vector>

#include "data/RGRandom.hpp"
#include "world/ItemWishingWell.hpp"

using Game::ItemWishingWell;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- Trigger: counter += 1; THIS trigger arms success at exactly 2 ----------

TEST(ItemWishingWellTest, TriggerCounterStartsAtZero) {
    ItemWishingWell w;
    EXPECT_EQ(w.TriggerCount(), 0);
}

TEST(ItemWishingWellTest, TriggerArmsExactlyOnSecondTrigger) {
    // FAITHFUL: Triggerable does counter+=1 then fires only when counter==2.
    ItemWishingWell w;
    EXPECT_FALSE(w.Trigger()); // counter 1 -> not armed
    EXPECT_EQ(w.TriggerCount(), 1);
    EXPECT_TRUE(w.Trigger()); // counter 2 -> arms success
    EXPECT_EQ(w.TriggerCount(), 2);
}

TEST(ItemWishingWellTest, TriggerDoesNotReArmPastTwo) {
    // A third trigger pushes the counter to 3 and the ==2 gate fails again.
    ItemWishingWell w;
    w.Trigger();
    w.Trigger();
    EXPECT_FALSE(w.Trigger()); // counter 3 -> not armed
    EXPECT_EQ(w.TriggerCount(), 3);
    EXPECT_FALSE(w.Trigger()); // counter 4 -> still not armed
}

TEST(ItemWishingWellTest, TriggerThresholdConstantIsTwo) {
    EXPECT_EQ(ItemWishingWell::kTriggerThreshold, 2);
}

// ---- ClassifyFail: branch on the SAME counter (==2 / ==1 / else) ------------

TEST(ItemWishingWellTest, FailIsIdleWhenCounterZero) {
    // FAITHFUL: OnItemTriggerFail no-ops unless the counter is 1 or 2.
    ItemWishingWell w;
    EXPECT_EQ(w.ClassifyFail(), ItemWishingWell::FailPhase::Idle);
}

TEST(ItemWishingWellTest, FailIsChargingWhenCounterOne) {
    ItemWishingWell w;
    w.Trigger(); // counter 1
    EXPECT_EQ(w.ClassifyFail(), ItemWishingWell::FailPhase::Charging);
}

TEST(ItemWishingWellTest, FailIsArmedWhenCounterTwo) {
    ItemWishingWell w;
    w.Trigger();
    w.Trigger(); // counter 2
    EXPECT_EQ(w.ClassifyFail(), ItemWishingWell::FailPhase::Armed);
}

TEST(ItemWishingWellTest, FailIsIdleWhenCounterThree) {
    // counter !=2 && !=1 -> the decomp's outer (!=2, inner !=1) no-op path.
    ItemWishingWell w;
    w.Trigger();
    w.Trigger();
    w.Trigger(); // counter 3
    EXPECT_EQ(w.ClassifyFail(), ItemWishingWell::FailPhase::Idle);
}

TEST(ItemWishingWellTest, ClassifyFailDoesNotMutateCounter) {
    ItemWishingWell w;
    w.Trigger(); // counter 1
    (void)w.ClassifyFail();
    (void)w.ClassifyFail();
    EXPECT_EQ(w.TriggerCount(), 1);
}

// ---- NextStep: coroutine state selector (1 -> Open, 0 -> Wait, else Done) ----

TEST(ItemWishingWellTest, NextStepMapsStateOneToOpen) {
    // FAITHFUL: MoveNext state 1 -> selector 4 -> OpenChest.
    EXPECT_EQ(ItemWishingWell::NextStep(1), ItemWishingWell::Step::Open);
}

TEST(ItemWishingWellTest, NextStepMapsStateZeroToWait) {
    // FAITHFUL: MoveNext state 0 -> selector 3 -> yield WaitForSeconds.
    EXPECT_EQ(ItemWishingWell::NextStep(0), ItemWishingWell::Step::Wait);
}

TEST(ItemWishingWellTest, NextStepMapsOtherStatesToDone) {
    // -1 is the iterator's "finished" sentinel; any non-{0,1} state -> Done.
    EXPECT_EQ(ItemWishingWell::NextStep(-1), ItemWishingWell::Step::Done);
    EXPECT_EQ(ItemWishingWell::NextStep(2), ItemWishingWell::Step::Done);
    EXPECT_EQ(ItemWishingWell::NextStep(99), ItemWishingWell::Step::Done);
}

// ---- OpenChest: exactly ONE int draw, Range(0, poolSize), in order ----------

TEST(ItemWishingWellTest, OpenChestStaysWithinPool) {
    ItemWishingWell w;
    w.SetSeed(1234);
    for (int i = 0; i < 64; ++i) {
        const int idx = w.OpenChest(7);
        EXPECT_GE(idx, 0);
        EXPECT_LT(idx, 7); // max EXCLUSIVE
    }
}

TEST(ItemWishingWellTest, OpenChestDrawsExactlyOneIntInOrder) {
    // A parallel same-seeded RGRandom must reproduce each reward index exactly:
    // proves OpenChest draws ONE int per open, in order, as Range(0, poolSize),
    // and nothing else advances the stream.
    ItemWishingWell w;
    RGRandom ref;
    w.SetSeed(98765);
    ref.SetRandomSeed(98765);

    const std::vector<int> pools = {5, 12, 3, 7, 18};
    for (const int pool : pools) {
        const int got = w.OpenChest(pool);
        const int expected = ref.Range(0, pool); // one parallel draw
        EXPECT_EQ(got, expected);
    }
}

TEST(ItemWishingWellTest, OpenChestIsDeterministicForSameSeed) {
    ItemWishingWell a;
    ItemWishingWell b;
    a.SetSeed(555);
    b.SetSeed(555);
    for (int i = 0; i < 32; ++i) {
        EXPECT_EQ(a.OpenChest(10), b.OpenChest(10));
    }
}

TEST(ItemWishingWellTest, OpenChestEmptyPoolReturnsZeroWithoutAdvancing) {
    // Range(0, n<=0) is the degenerate guard: returns 0 and does NOT advance the
    // stream. Prove a parallel stream still matches the very next real draw.
    ItemWishingWell w;
    RGRandom ref;
    w.SetSeed(4242);
    ref.SetRandomSeed(4242);

    EXPECT_EQ(w.OpenChest(0), 0);   // empty pool -> 0, no draw
    EXPECT_EQ(w.OpenChest(-3), 0);  // negative pool -> 0, no draw

    // The next genuine draw must still line up with the untouched ref stream.
    const int got = w.OpenChest(9);
    const int expected = ref.Range(0, 9);
    EXPECT_EQ(got, expected);
}

// ---- Zero-draw bodies leave the stream non-advancing ------------------------

TEST(ItemWishingWellTest, GateAndStepLogicTakeNoDraw) {
    // Trigger / ClassifyFail / NextStep are pure decision logic: a parallel
    // same-seeded stream must still match the first OpenChest draw after them.
    ItemWishingWell w;
    RGRandom ref;
    w.SetSeed(31337);
    ref.SetRandomSeed(31337);

    (void)w.Trigger();
    (void)w.Trigger();
    (void)w.ClassifyFail();
    (void)ItemWishingWell::NextStep(1);
    (void)ItemWishingWell::NextStep(0);

    const int got = w.OpenChest(11);
    const int expected = ref.Range(0, 11); // stream never advanced before this
    EXPECT_EQ(got, expected);
}

// NOLINTEND(readability-magic-numbers)
