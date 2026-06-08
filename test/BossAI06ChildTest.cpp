#include <gtest/gtest.h>

#include <vector>

#include "combat/BossAI06Child.hpp"
#include "data/RGRandom.hpp"

using Game::BossAI06Child;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

namespace {

// Parallel reference: a same-seeded RGRandom replaying the EXACT draw the brain
// makes in GetToTarget (UnityEngine.Random.Range(0, 2), max EXCLUSIVE).
int ReferenceSelect(RGRandom &ref) { return ref.Range(0, BossAI06Child::kSelectCeiling); }

} // namespace

// ---- ctor / default-state scalars ----------------------------------------

TEST(BossAI06ChildTest, CtorScalarsMatchDecomp) {
    BossAI06Child c;
    EXPECT_EQ(c.GetTarget(), BossAI06Child::kCtorGetTarget);   // 0x29 = 1
    EXPECT_EQ(c.LockTarget(), BossAI06Child::kCtorLockTarget); // 0x2b = 1
    EXPECT_FLOAT_EQ(c.Speed(), BossAI06Child::kCtorSpeed);     // 0x2c = 10.0f
    EXPECT_FLOAT_EQ(BossAI06Child::kCtorSpeed, 10.0F);
    // free_move / has_target / in_atk3 are not ctor-written -> default false.
    EXPECT_FALSE(c.FreeMove());
    EXPECT_FALSE(c.HasTarget());
    EXPECT_FALSE(c.InAtk3());
}

TEST(BossAI06ChildTest, ResetToCtorStateRestoresScalars) {
    BossAI06Child c;
    c.ResetToCtorState();
    EXPECT_TRUE(c.GetTarget());
    EXPECT_TRUE(c.LockTarget());
    EXPECT_FLOAT_EQ(c.Speed(), 10.0F);
}

// ---- GetToTarget gate: free_move == false consumes NO draw -----------------

TEST(BossAI06ChildTest, GatedOutTakesNoDrawAndNoAttack) {
    BossAI06Child c;
    c.SetSeed(123);
    RGRandom ref;
    ref.SetRandomSeed(123);

    // free_move defaults false -> GetToTarget must NOT draw, must dispatch nothing.
    for (int i = 0; i < 16; ++i) {
        EXPECT_EQ(c.GetToTarget(), BossAI06Child::kAttackNone);
    }
    // The brain's stream is still pristine: its next draw equals the reference's
    // FIRST draw (no draws were consumed by the gated calls).
    EXPECT_EQ(c.Rng().Range(0, BossAI06Child::kSelectCeiling), ReferenceSelect(ref));
}

// ---- GetToTarget when free_move == true: exactly one draw, in lockstep -----

TEST(BossAI06ChildTest, FreeMoveDrawsOncePerCallInLockstep) {
    BossAI06Child c;
    c.SetSeed(987);
    c.SetFreeMove(true);
    RGRandom ref;
    ref.SetRandomSeed(987);

    for (int i = 0; i < 64; ++i) {
        const int got = c.GetToTarget();
        const int want = ReferenceSelect(ref); // exactly one parallel draw
        // GetToTarget forwards the raw {0,1} roll straight into Attack().
        EXPECT_EQ(got, want);
        EXPECT_TRUE(got == BossAI06Child::kAttackAtk1 ||
                    got == BossAI06Child::kAttackAtk02);
    }
}

TEST(BossAI06ChildTest, SelectorCeilingIsTwoSoAtk03NeverRolled) {
    // Range(0, 2) is max-exclusive: Atk03 (== 2) can never come from the roll.
    BossAI06Child c;
    c.SetSeed(55);
    c.SetFreeMove(true);
    for (int i = 0; i < 2000; ++i) {
        EXPECT_NE(c.GetToTarget(), BossAI06Child::kAttackAtk03);
    }
}

TEST(BossAI06ChildTest, RollCoversBothReachableAttacks) {
    BossAI06Child c;
    c.SetSeed(7);
    c.SetFreeMove(true);
    bool sawAtk1 = false;
    bool sawAtk02 = false;
    for (int i = 0; i < 2000; ++i) {
        const int v = c.GetToTarget();
        if (v == BossAI06Child::kAttackAtk1) {
            sawAtk1 = true;
        }
        if (v == BossAI06Child::kAttackAtk02) {
            sawAtk02 = true;
        }
    }
    EXPECT_TRUE(sawAtk1);  // value 0 reachable
    EXPECT_TRUE(sawAtk02); // value 1 reachable
}

// ---- Toggling the gate mid-stream: only ungated calls draw -----------------

TEST(BossAI06ChildTest, ToggledGateOnlyDrawsWhenFreeMove) {
    BossAI06Child c;
    c.SetSeed(4242);
    RGRandom ref;
    ref.SetRandomSeed(4242);

    // Pattern: gated, free, gated, free, ... only the free calls consume a draw.
    for (int i = 0; i < 40; ++i) {
        const bool free = (i % 2 == 1);
        c.SetFreeMove(free);
        const int got = c.GetToTarget();
        if (free) {
            EXPECT_EQ(got, ReferenceSelect(ref)); // one draw, lockstep
        } else {
            EXPECT_EQ(got, BossAI06Child::kAttackNone); // no draw taken
        }
    }
}

// ---- Determinism: same seed -> identical selector sequence -----------------

TEST(BossAI06ChildTest, SameSeedSameSequence) {
    BossAI06Child a;
    BossAI06Child b;
    a.SetSeed(31337);
    b.SetSeed(31337);
    a.SetFreeMove(true);
    b.SetFreeMove(true);
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(a.GetToTarget(), b.GetToTarget());
    }
}

TEST(BossAI06ChildTest, DifferentSeedsDiverge) {
    BossAI06Child a;
    BossAI06Child b;
    a.SetSeed(1);
    b.SetSeed(999983);
    a.SetFreeMove(true);
    b.SetFreeMove(true);
    bool diverged = false;
    for (int i = 0; i < 256; ++i) {
        if (a.GetToTarget() != b.GetToTarget()) {
            diverged = true;
        }
    }
    EXPECT_TRUE(diverged);
}

// ---- Attack() pure dispatch table (no RNG) ---------------------------------

TEST(BossAI06ChildTest, AttackDispatchMapsValuesToCodes) {
    BossAI06Child c;
    EXPECT_EQ(c.Attack(0), BossAI06Child::kAttackAtk1);
    EXPECT_EQ(c.Attack(1), BossAI06Child::kAttackAtk02);
    EXPECT_EQ(c.Attack(2), BossAI06Child::kAttackAtk03);
    // Anything outside {0,1,2} falls through (the decomp `return;` branch).
    EXPECT_EQ(c.Attack(3), BossAI06Child::kAttackNone);
    EXPECT_EQ(c.Attack(-5), BossAI06Child::kAttackNone);
}

TEST(BossAI06ChildTest, Atk1ClearsLockTarget) {
    BossAI06Child c;
    EXPECT_TRUE(c.LockTarget()); // ctor sets lock_target = 1
    c.Atk1();
    EXPECT_FALSE(c.LockTarget()); // Atk1 -> lock_target(0x2b) = 0
}

TEST(BossAI06ChildTest, AttackZeroDispatchesAtk1Write) {
    // Attack(0) must run the Atk1 field write; Attack(1)/Attack(2) must not.
    BossAI06Child c;
    EXPECT_TRUE(c.LockTarget());
    c.Attack(1); // Atk02: no lock_target write
    EXPECT_TRUE(c.LockTarget());
    c.Attack(2); // Atk03: no lock_target write
    EXPECT_TRUE(c.LockTarget());
    c.Attack(0); // Atk1: clears lock_target
    EXPECT_FALSE(c.LockTarget());
}

TEST(BossAI06ChildTest, AttackTakesNoDraw) {
    // Dispatch must not perturb the RNG stream (only GetToTarget draws).
    BossAI06Child c;
    c.SetSeed(2718);
    RGRandom ref;
    ref.SetRandomSeed(2718);
    for (int v = 0; v < 4; ++v) {
        c.Attack(v);
    }
    EXPECT_EQ(c.Rng().Range(0, BossAI06Child::kSelectCeiling), ReferenceSelect(ref));
}

// ---- Field-write bodies (FindTarget, EndAtk03) -----------------------------

TEST(BossAI06ChildTest, FindTargetClearsHasTarget) {
    BossAI06Child c;
    // has_target starts false; FindTarget always writes 0 per decomp.
    c.FindTarget();
    EXPECT_FALSE(c.HasTarget());
}

TEST(BossAI06ChildTest, EndAtk03ClearsInAtk3) {
    BossAI06Child c;
    c.EndAtk03();
    EXPECT_FALSE(c.InAtk3());
}

TEST(BossAI06ChildTest, FieldWriteBodiesTakeNoDraw) {
    BossAI06Child c;
    c.SetSeed(64);
    RGRandom ref;
    ref.SetRandomSeed(64);
    c.FindTarget();
    c.EndAtk03();
    c.Atk1();
    EXPECT_EQ(c.Rng().Range(0, BossAI06Child::kSelectCeiling), ReferenceSelect(ref));
}

// ---- Full interleaved replay -----------------------------------------------

TEST(BossAI06ChildTest, InterleavedReplayIsDeterministic) {
    auto run = [](int seed) {
        BossAI06Child c;
        c.SetSeed(seed);
        std::vector<int> trace;
        for (int i = 0; i < 50; ++i) {
            // Alternate gated/free + interleave non-drawing bodies.
            c.SetFreeMove(i % 3 != 0);
            trace.push_back(c.GetToTarget());
            c.FindTarget();
            c.EndAtk03();
        }
        return trace;
    };
    const auto a = run(2024);
    const auto b = run(2024);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i], b[i]);
    }
}

// NOLINTEND(readability-magic-numbers)
