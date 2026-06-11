#include <gtest/gtest.h>

#include <vector>

#include "combat/BossAINianLantern.hpp"
#include "data/RGRandom.hpp"

using Game::BossAINianLantern;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

namespace {

// Run the Attacking coroutine to completion, returning the ordered list of pc
// values visited (the iterator's +0x30 field as observed before each step).
// Caps iterations so a modelling bug can't hang the suite.
std::vector<int> RunCoroutine(BossAINianLantern &l) {
    std::vector<int> steps;
    for (int guard = 0; guard < 32; ++guard) {
        steps.push_back(l.CurrentStep());
        if (!l.MoveNext()) {
            break;
        }
    }
    return steps;
}

} // namespace

// ---- SetTarget gate (op_Implicit && isAttack == 0) -------------------------

TEST(BossAINianLanternTest, StartsIdleUnarmed) {
    BossAINianLantern l;
    EXPECT_FALSE(l.Armed());
    EXPECT_FALSE(l.IsAttack());
    EXPECT_FALSE(l.Exploded());
    EXPECT_EQ(l.CurrentStep(), -1);
}

TEST(BossAINianLanternTest, SetTargetArmsWhenAlive) {
    BossAINianLantern l;
    EXPECT_TRUE(l.SetTarget(true));
    EXPECT_TRUE(l.Armed());
    EXPECT_EQ(l.CurrentStep(), 0); // armed at case 0, awaiting first MoveNext.
}

TEST(BossAINianLanternTest, SetTargetGatedOutWhenDead) {
    BossAINianLantern l;
    // op_Implicit(this) == false -> coroutine never launches.
    EXPECT_FALSE(l.SetTarget(false));
    EXPECT_FALSE(l.Armed());
    EXPECT_EQ(l.CurrentStep(), -1);
}

TEST(BossAINianLanternTest, SetTargetGatedOutWhenAlreadyAttacking) {
    BossAINianLantern l;
    ASSERT_TRUE(l.SetTarget(true));
    ASSERT_TRUE(l.MoveNext()); // case 0 sets isAttack = true.
    ASSERT_TRUE(l.IsAttack());
    // isAttack != 0 -> the SetTarget guard rejects a re-launch.
    EXPECT_FALSE(l.SetTarget(true));
}

// ---- MoveNext state machine ------------------------------------------------
// FAITHFUL: per-case field writes + the case-3 explode gate (recovered from the
// decomp switch). RECONSTRUCTED: the inter-case resume order / step count (the
// next-state pc writes are inlined into the non-returning yield thunk and not
// recoverable) -- such assertions are labelled and live in the Reconstructed*
// tests below.

TEST(BossAINianLanternTest, ArmStepSetsIsAttackAndExploded) {
    BossAINianLantern l;
    ASSERT_TRUE(l.SetTarget(true));
    // case 0: isAttack (0x18) = 1, exploded (0x19) = 1.
    ASSERT_TRUE(l.MoveNext());
    EXPECT_TRUE(l.IsAttack());
    EXPECT_TRUE(l.Exploded());
    EXPECT_FALSE(l.DidExplode()); // arming does not detonate.
    // RECONSTRUCTED resume target (next-state pc write not recovered):
    EXPECT_EQ(l.CurrentStep(), 1); // next resume is case 1 (the tail trigger).
}

TEST(BossAINianLanternTest, TailClearsExplodedFlag) {
    BossAINianLantern l;
    ASSERT_TRUE(l.SetTarget(true));
    ASSERT_TRUE(l.MoveNext()); // case 0  -> exploded = true,  pc -> 1
    ASSERT_TRUE(l.MoveNext()); // case 1 break -> shared tail -> exploded = false
    EXPECT_FALSE(l.Exploded()); // FAITHFUL tail: target.exploded (0x19) = 0.
    // RECONSTRUCTED resume target (next-state pc write not recovered):
    EXPECT_EQ(l.CurrentStep(), 2); // tail continues at the approach step.
}

TEST(BossAINianLanternTest, DetonateStepExplodesWhenNotSpent) {
    // FAITHFUL core: case 3's recovered gate `if (exploded == false) Explode()`.
    // Reaching case 3 with exploded == false relies on the RECONSTRUCTED resume
    // order (next-state pc writes not recovered); the gate behaviour itself is
    // the recovered fact under test.
    BossAINianLantern l;
    ASSERT_TRUE(l.SetTarget(true));
    ASSERT_TRUE(l.MoveNext()); // case 0      -> exploded = true   (reconstructed pc -> 1)
    ASSERT_TRUE(l.MoveNext()); // case 1 tail  -> exploded = false  (reconstructed pc -> 2)
    ASSERT_TRUE(l.MoveNext()); // case 2       -> approach          (reconstructed pc -> 3)
    EXPECT_EQ(l.CurrentStep(), 3); // RECONSTRUCTED resume target.
    ASSERT_TRUE(l.MoveNext()); // case 3       -> explode gate (exploded == false)
    EXPECT_TRUE(l.DidExplode()); // FAITHFUL gate: !exploded -> Explode() fires.
    EXPECT_EQ(l.CurrentStep(), 4); // RECONSTRUCTED resume target.
}

// RECONSTRUCTION (NOT a faithfulness assertion): the inter-case resume order is
// NOT recoverable from the decomp -- every case ends in the non-returning yield
// thunk FUN_010b7dcc, which swallows the inlined `iterator.pc = N` next-state
// write (the only +0x30 writes in MoveNext are the entry read and = -1). The pc
// chain {0,1,2,3,4,5,-1} below is the order chosen by our reconstruction, so this
// test pins the RECONSTRUCTED order for regression only -- it does NOT assert a
// recovered fact. If the resume order is later recovered, update this expectation.
TEST(BossAINianLanternTest, ReconstructedCoroutinePcSequence) {
    BossAINianLantern l;
    ASSERT_TRUE(l.SetTarget(true));
    // Reconstructed pc before each step: 0 (arm), 1 (tail), 2, 3, 4, 5, finished.
    const std::vector<int> reconstructed = {0, 1, 2, 3, 4, 5, -1};
    EXPECT_EQ(RunCoroutine(l), reconstructed);
}

// RECONSTRUCTION (NOT a faithfulness assertion): the step COUNT is a function of
// the unrecovered resume order (see ReconstructedCoroutinePcSequence). Pins the
// reconstructed count (6 yielding cases + 1 terminating call) for regression only.
TEST(BossAINianLanternTest, ReconstructedCoroutineFinishesAfterLastStep) {
    BossAINianLantern l;
    ASSERT_TRUE(l.SetTarget(true));
    bool running = true;
    int steps = 0;
    while (running && steps < 16) {
        running = l.MoveNext();
        ++steps;
    }
    EXPECT_FALSE(running);   // FAITHFUL: default case returns 0 -> coroutine done.
    EXPECT_EQ(steps, 7);     // RECONSTRUCTED count: 6 yielding cases + 1 terminating call.
    EXPECT_EQ(l.CurrentStep(), -1);
}

TEST(BossAINianLanternTest, MoveNextOnUnarmedDoesNothing) {
    BossAINianLantern l;
    // pc == -1 folds to the default path -> finished, no field writes.
    EXPECT_FALSE(l.MoveNext());
    EXPECT_FALSE(l.IsAttack());
    EXPECT_FALSE(l.Exploded());
    EXPECT_FALSE(l.DidExplode());
}

// RECONSTRUCTION (NOT a faithfulness assertion): "exactly once per run" is a
// consequence of the reconstructed arm(exploded=1) -> tail(exploded=0) ->
// case3-gate ordering, which is NOT recoverable (next-state pc writes are inlined
// into the non-returning yield thunk). The FAITHFUL, recovered fact is only the
// per-case gate itself (`if (!exploded) Explode()`), covered by
// DetonateStepExplodesWhenNotSpent. This test pins the reconstructed once-per-run
// count for regression only.
TEST(BossAINianLanternTest, ReconstructedDetonatesOncePerRun) {
    BossAINianLantern l;
    ASSERT_TRUE(l.SetTarget(true));
    int explodeCount = 0;
    for (int guard = 0; guard < 16; ++guard) {
        const bool more = l.MoveNext();
        if (l.DidExplode()) {
            ++explodeCount;
        }
        if (!more) {
            break;
        }
    }
    EXPECT_EQ(explodeCount, 1); // RECONSTRUCTED: once per run under the chosen order.
}

// ---- Stream-neutral invariant: the lantern draws ZERO RNG values -----------

TEST(BossAINianLanternTest, FullLifecycleDrawsNoRng) {
    // Seed the lantern's stream and a parallel reference with the SAME seed.
    // After a complete attack lifecycle the lantern's stream must be UNTOUCHED:
    // its very next draw equals the reference's FIRST draw (proving no draw
    // happened during SetTarget + the whole coroutine).
    constexpr int kSeed = 13579;

    RGRandom reference;
    reference.SetRandomSeed(kSeed);
    const int referenceFirstInt = reference.Range(0, 1000000);

    BossAINianLantern l;
    l.SetSeed(kSeed);
    ASSERT_TRUE(l.SetTarget(true));
    RunCoroutine(l);          // arm + approach + detonate + tail, full pass.
    (void)l.SetTarget(true);  // gated-out re-arm: must also draw nothing.

    // If anything in the lantern advanced the stream, these would differ.
    EXPECT_EQ(l.Rng().Range(0, 1000000), referenceFirstInt);
}

TEST(BossAINianLanternTest, RepeatedRunsDrawNoRng) {
    // Even across many re-arm/run cycles the stream stays pinned at draw #0.
    constexpr int kSeed = 24680;

    RGRandom reference;
    reference.SetRandomSeed(kSeed);
    const float referenceFirstFloat = reference.Range(0.0F, 1.0F);

    BossAINianLantern l;
    l.SetSeed(kSeed);
    for (int cycle = 0; cycle < 5; ++cycle) {
        // isAttack is sticky once set, so only the first SetTarget arms; the
        // rest are gated out. Either way: no RNG draw on any path.
        l.SetTarget(true);
        RunCoroutine(l);
    }
    EXPECT_FLOAT_EQ(l.Rng().Range(0.0F, 1.0F), referenceFirstFloat);
}

// NOLINTEND(readability-magic-numbers)
