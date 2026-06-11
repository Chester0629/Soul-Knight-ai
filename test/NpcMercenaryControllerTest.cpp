#include <gtest/gtest.h>

#include <vector>

#include "combat/NpcMercenaryController.hpp"
#include "data/RGRandom.hpp"

using Game::NpcMercenaryController;
using ShootResult = Game::NpcMercenaryController::ShootResult;
using ShootBranch = Game::NpcMercenaryController::ShootBranch;

// NOLINTBEGIN(readability-magic-numbers)

// ---- MeleeShootReflection (1 draw, gate roll<8 && can_shoot) --------------

TEST(NpcMercenaryControllerTest, MeleeShootDrawsExactlyOneInOrder) {
    // The Range(0,10) draw is unconditional and precedes the gate; a reference
    // stream from the same seed must match draw-for-draw whether or not it fires.
    NpcMercenaryController m;
    Game::RGRandom ref;
    m.SetSeed(1234);
    ref.SetRandomSeed(1234);
    for (int i = 0; i < 64; ++i) {
        m.SetCanShoot(true); // re-arm so the gate state never blocks the draw
        float cadence = -1.0F;
        m.MeleeShootReflection(cadence, 2.0F, 0);
        EXPECT_EQ(m.LastRoll(), ref.Range(0, 10)); // lockstep with the parallel stream
    }
}

TEST(NpcMercenaryControllerTest, MeleeShootGatedOutStillAdvancesStream) {
    // can_shoot == false: the draw is still taken (it is before the gate), so the
    // stream stays in lockstep with a parallel one that fires.
    NpcMercenaryController gated;
    NpcMercenaryController armed;
    gated.SetSeed(77);
    armed.SetSeed(77);
    gated.SetCanShoot(false);
    armed.SetCanShoot(true);
    float c0 = -1.0F;
    float c1 = -1.0F;
    EXPECT_EQ(gated.MeleeShootReflection(c0, 1.0F, 0), ShootResult::Held);
    // armed may Fire or Hold depending on the roll, but the draw count is the same
    armed.MeleeShootReflection(c1, 1.0F, 0);
    EXPECT_EQ(gated.LastRoll(), armed.LastRoll()); // both advanced one draw
}

TEST(NpcMercenaryControllerTest, MeleeShootFiresClearsCanShootWithAdditiveCadence) {
    // Pick a seed whose first Range(0,10) is < 8 so the gate passes.
    NpcMercenaryController m;
    Game::RGRandom ref;
    int seed = 0;
    for (seed = 1; seed < 10000; ++seed) {
        ref.SetRandomSeed(seed);
        if (ref.Range(0, 10) < 8) {
            break;
        }
    }
    m.SetSeed(seed);
    m.SetCanShoot(true);
    float cadence = -1.0F;
    const float baseCadence = 3.0F;
    const int itemLevel = 4;
    EXPECT_EQ(m.MeleeShootReflection(cadence, baseCadence, itemLevel), ShootResult::Fired);
    EXPECT_FALSE(m.CanShoot()); // 0x71 cleared on a fire
    // line 1670695: base + itemLevel * 0.25
    EXPECT_FLOAT_EQ(cadence, baseCadence + static_cast<float>(itemLevel) * 0.25F);
}

TEST(NpcMercenaryControllerTest, MeleeShootHeldWhenRollTooHigh) {
    // Pick a seed whose first Range(0,10) is >= 8 so the gate fails on the roll.
    NpcMercenaryController m;
    Game::RGRandom ref;
    int seed = 0;
    for (seed = 1; seed < 10000; ++seed) {
        ref.SetRandomSeed(seed);
        if (ref.Range(0, 10) >= 8) {
            break;
        }
    }
    m.SetSeed(seed);
    m.SetCanShoot(true);
    float cadence = -99.0F;
    EXPECT_EQ(m.MeleeShootReflection(cadence, 3.0F, 4), ShootResult::Held);
    EXPECT_TRUE(m.CanShoot());      // gate failed: 0x71 untouched
    EXPECT_FLOAT_EQ(cadence, -99.0F); // out-param untouched when Held
}

// ---- RemoteShootReflection (1 draw, gate roll<8 && can_shoot) -------------

TEST(NpcMercenaryControllerTest, RemoteShootDrawsExactlyOneInOrder) {
    NpcMercenaryController m;
    Game::RGRandom ref;
    m.SetSeed(4321);
    ref.SetRandomSeed(4321);
    for (int i = 0; i < 64; ++i) {
        m.SetCanShoot(true);
        float cadence = -1.0F;
        m.RemoteShootReflection(cadence, 2.0F, 0);
        EXPECT_EQ(m.LastRoll(), ref.Range(0, 10));
    }
}

TEST(NpcMercenaryControllerTest, RemoteShootFiresWithMultiplicativeCadence) {
    NpcMercenaryController m;
    Game::RGRandom ref;
    int seed = 0;
    for (seed = 1; seed < 10000; ++seed) {
        ref.SetRandomSeed(seed);
        if (ref.Range(0, 10) < 8) {
            break;
        }
    }
    m.SetSeed(seed);
    m.SetCanShoot(true);
    float cadence = -1.0F;
    const float baseCadence = 3.0F;
    const int itemLevel = 4;
    EXPECT_EQ(m.RemoteShootReflection(cadence, baseCadence, itemLevel), ShootResult::Fired);
    EXPECT_FALSE(m.CanShoot());
    // line 1670804: base * (itemLevel * 0.3 + 1.0)
    EXPECT_FLOAT_EQ(cadence, baseCadence * (static_cast<float>(itemLevel) * 0.3F + 1.0F));
}

TEST(NpcMercenaryControllerTest, RemoteShootHeldWhenCanShootFalse) {
    NpcMercenaryController m;
    m.SetSeed(55);
    m.SetCanShoot(false);
    float cadence = -7.0F;
    EXPECT_EQ(m.RemoteShootReflection(cadence, 3.0F, 4), ShootResult::Held);
    EXPECT_FALSE(m.CanShoot());      // already false; no write
    EXPECT_FLOAT_EQ(cadence, -7.0F); // untouched
}

// ---- ShootReflection dispatch (melee flag 0xa8) --------------------------

TEST(NpcMercenaryControllerTest, ShootReflectionDispatchesByMeleeFlag) {
    // Same seed: a melee dispatch must match a direct MeleeShootReflection, and a
    // remote dispatch a direct RemoteShootReflection (same single draw, same gate).
    NpcMercenaryController viaDispatch;
    NpcMercenaryController direct;
    viaDispatch.SetSeed(909);
    direct.SetSeed(909);
    viaDispatch.SetMelee(true);
    viaDispatch.SetCanShoot(true);
    direct.SetCanShoot(true);
    float c0 = -1.0F;
    float c1 = -1.0F;
    ShootBranch branch = ShootBranch::Remote;
    const ShootResult r0 = viaDispatch.ShootReflection(c0, 2.0F, 1, branch);
    const ShootResult r1 = direct.MeleeShootReflection(c1, 2.0F, 1);
    EXPECT_EQ(branch, ShootBranch::Melee);
    EXPECT_EQ(r0, r1);
    EXPECT_EQ(viaDispatch.LastRoll(), direct.LastRoll());
    EXPECT_FLOAT_EQ(c0, c1);
}

TEST(NpcMercenaryControllerTest, ShootReflectionRemoteBranchWhenNotMelee) {
    NpcMercenaryController viaDispatch;
    NpcMercenaryController direct;
    viaDispatch.SetSeed(1010);
    direct.SetSeed(1010);
    viaDispatch.SetMelee(false);
    viaDispatch.SetCanShoot(true);
    direct.SetCanShoot(true);
    float c0 = -1.0F;
    float c1 = -1.0F;
    ShootBranch branch = ShootBranch::Melee;
    const ShootResult r0 = viaDispatch.ShootReflection(c0, 2.0F, 1, branch);
    const ShootResult r1 = direct.RemoteShootReflection(c1, 2.0F, 1);
    EXPECT_EQ(branch, ShootBranch::Remote);
    EXPECT_EQ(r0, r1);
    EXPECT_EQ(viaDispatch.LastRoll(), direct.LastRoll());
    EXPECT_FLOAT_EQ(c0, c1);
}

// ---- RemoteRunReflection (1 draw, zeroes move_direction) -----------------

TEST(NpcMercenaryControllerTest, RunReflectionDrawsExactlyOneInOrder) {
    NpcMercenaryController m;
    Game::RGRandom ref;
    m.SetSeed(808);
    ref.SetRandomSeed(808);
    for (int i = 0; i < 32; ++i) {
        float runCad = -1.0F;
        const int roll = m.RemoteRunReflection(runCad, 1.5F);
        EXPECT_EQ(roll, ref.Range(0, 10));    // lockstep
        EXPECT_GE(roll, 0);
        EXPECT_LT(roll, NpcMercenaryController::kRollCeiling); // max EXCL
    }
}

TEST(NpcMercenaryControllerTest, RunReflectionZeroesMoveDirAndReportsCadence) {
    NpcMercenaryController m;
    m.SetSeed(1);
    float runCad = -1.0F;
    m.RemoteRunReflection(runCad, 2.25F);
    EXPECT_FLOAT_EQ(m.MoveDirection().x, 0.0F); // 0x50 = 0
    EXPECT_FLOAT_EQ(m.MoveDirection().y, 0.0F); // 0x54 = 0
    EXPECT_FLOAT_EQ(runCad, 2.25F);             // word 0x1a (owner Invoke delay)
}

// ---- Scout (target = owner; no draw) -------------------------------------

TEST(NpcMercenaryControllerTest, MeleeScoutTargetsOwnerNoDraw) {
    NpcMercenaryController scouted;
    NpcMercenaryController quiet;
    scouted.SetSeed(33);
    quiet.SetSeed(33);
    EXPECT_FALSE(scouted.TargetIsOwner());
    scouted.MeleeScout();
    EXPECT_TRUE(scouted.TargetIsOwner()); // target_obj (0x1c) = owner ref (0x74)
    // Scout takes no draw: a parallel stream still matches.
    float c0 = 0.0F;
    float c1 = 0.0F;
    scouted.SetCanShoot(true);
    quiet.SetCanShoot(true);
    EXPECT_EQ(scouted.LastRoll(), quiet.LastRoll()); // both -1, untouched
    scouted.MeleeShootReflection(c0, 1.0F, 0);
    quiet.MeleeShootReflection(c1, 1.0F, 0);
    EXPECT_EQ(scouted.LastRoll(), quiet.LastRoll());
}

TEST(NpcMercenaryControllerTest, RemoteScoutTargetsOwnerNoDraw) {
    NpcMercenaryController m;
    m.SetSeed(44);
    EXPECT_FALSE(m.TargetIsOwner());
    m.RemoteScout();
    EXPECT_TRUE(m.TargetIsOwner());
}

// ---- EndCycle (zero move_direction; no draw) -----------------------------

TEST(NpcMercenaryControllerTest, EndCycleZeroesMoveDirNoDraw) {
    NpcMercenaryController cycled;
    NpcMercenaryController quiet;
    cycled.SetSeed(456);
    quiet.SetSeed(456);
    cycled.EndCycle();
    EXPECT_FLOAT_EQ(cycled.MoveDirection().x, 0.0F);
    EXPECT_FLOAT_EQ(cycled.MoveDirection().y, 0.0F);
    // No draw: the stream after EndCycle must still match an untouched one.
    float runA = 0.0F;
    float runB = 0.0F;
    EXPECT_EQ(cycled.RemoteRunReflection(runA, 1.0F), quiet.RemoteRunReflection(runB, 1.0F));
}

// ---- GetHurt / Dead gates (byte 0x0d) ------------------------------------

TEST(NpcMercenaryControllerTest, GetHurtRoutesWhenAliveIgnoresWhenDead) {
    NpcMercenaryController m;
    m.SetSeed(1);
    EXPECT_TRUE(m.GetHurt());  // alive (0x0d == 0): route to UICanvas
    m.SetDead(true);
    EXPECT_FALSE(m.GetHurt()); // dead: ignored
}

TEST(NpcMercenaryControllerTest, DeadGateShortCircuitsWhenAlreadyDead) {
    NpcMercenaryController m;
    m.SetSeed(1);
    EXPECT_TRUE(m.Dead());  // was alive (0x0d == 0): owner body would run
    m.SetDead(true);
    EXPECT_FALSE(m.Dead()); // already dead (0x0d != 0): early return
}

TEST(NpcMercenaryControllerTest, GetHurtAndDeadTakeNoDraw) {
    NpcMercenaryController used;
    NpcMercenaryController quiet;
    used.SetSeed(2024);
    quiet.SetSeed(2024);
    used.GetHurt();
    used.Dead();
    used.SetDead(true);
    used.GetHurt();
    used.Dead();
    float a = 0.0F;
    float b = 0.0F;
    EXPECT_EQ(used.RemoteRunReflection(a, 1.0F), quiet.RemoteRunReflection(b, 1.0F));
}

// ---- Full interleaved replay determinism ----------------------------------

TEST(NpcMercenaryControllerTest, FullStreamReplayIsDeterministic) {
    // Interleave Scout (0 draws), ShootReflection (1 draw), RunReflection (1 draw),
    // EndCycle/GetHurt/Dead (0 draws); replay from the same seed -> identical.
    auto run = [](int seed) {
        NpcMercenaryController m;
        m.SetSeed(seed);
        std::vector<int> trace;
        for (int i = 0; i < 24; ++i) {
            m.SetMelee((i & 1) != 0);
            m.SetCanShoot(true);
            m.MeleeScout();                    // 0 draws
            float cadence = 0.0F;
            ShootBranch branch = ShootBranch::Remote;
            m.ShootReflection(cadence, 2.0F, i % 5, branch); // 1 draw
            trace.push_back(m.LastRoll());
            float runCad = 0.0F;
            trace.push_back(m.RemoteRunReflection(runCad, 1.0F)); // 1 draw
            m.EndCycle();                      // 0 draws
            m.GetHurt();                       // 0 draws
            m.Dead();                          // 0 draws
        }
        return trace;
    };
    const auto a = run(2718);
    const auto b = run(2718);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i], b[i]);
    }
}

TEST(NpcMercenaryControllerTest, DifferentSeedsDiverge) {
    NpcMercenaryController a;
    NpcMercenaryController b;
    a.SetSeed(1);
    b.SetSeed(424242);
    bool diverged = false;
    for (int i = 0; i < 64; ++i) {
        float ca = 0.0F;
        float cb = 0.0F;
        if (a.RemoteRunReflection(ca, 1.0F) != b.RemoteRunReflection(cb, 1.0F)) {
            diverged = true;
        }
    }
    EXPECT_TRUE(diverged);
}

// NOLINTEND(readability-magic-numbers)
