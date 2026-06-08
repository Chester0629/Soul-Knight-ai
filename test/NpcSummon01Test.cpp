#include <gtest/gtest.h>

#include <vector>

#include "combat/NpcSummon01.hpp"
#include "data/RGRandom.hpp"

using Game::NpcSummon01;
using Game::RGRandom;
using ShootResult = Game::NpcSummon01::ShootResult;
using StateChange = Game::NpcSummon01::StateChange;

// NOLINTBEGIN(readability-magic-numbers)

// ---- ShootReflection (the unconditional pre-gate draw) -------------------

TEST(NpcSummon01Test, ShootDrawIsTakenBeforeGateAndMatchesReferenceStream) {
    // FAITHFUL: ShootReflection draws Range(0,10) UNCONDITIONALLY (line 1671334)
    // BEFORE the gate, so every tick advances the stream draw-for-draw with a
    // parallel same-seeded reference -- whether or not the shot fires.
    NpcSummon01 s;
    RGRandom ref;
    s.SetSeed(123);
    ref.SetRandomSeed(123);
    for (int i = 0; i < 128; ++i) {
        const int expectedRoll = ref.Range(0, 10); // the reference draw
        // re-arm each tick so can-shoot is never the limiting factor here.
        s.SetCanShoot(true);
        float cadence = -1.0F;
        const ShootResult res = s.ShootReflection(cadence, 7.0F);
        // gate is roll < 8 && can-shoot; can-shoot is true, so it depends on roll.
        if (expectedRoll < NpcSummon01::kShootRollThreshold) {
            EXPECT_EQ(res, ShootResult::Fired);
        } else {
            EXPECT_EQ(res, ShootResult::Held);
        }
    }
}

TEST(NpcSummon01Test, ShootFiresClearsCanShootAndReportsBaseCadence) {
    // Find a seed/tick where roll < 8 fires; assert the 0x71 clear and that the
    // out cadence is the supplied base cadence (word 0x1b), per lines
    // 1671336 / 1671342.
    NpcSummon01 s;
    s.SetSeed(123);
    s.SetCanShoot(true);
    bool sawFire = false;
    for (int i = 0; i < 64 && !sawFire; ++i) {
        s.SetCanShoot(true);
        float cadence = -1.0F;
        if (s.ShootReflection(cadence, 42.0F) == ShootResult::Fired) {
            sawFire = true;
            EXPECT_FALSE(s.CanShoot());      // 0x71 cleared on a fire
            EXPECT_FLOAT_EQ(cadence, 42.0F); // owner Invoke("Shoot", word 0x1b)
        }
    }
    EXPECT_TRUE(sawFire);
}

TEST(NpcSummon01Test, ShootHeldWhenCanShootAlreadyZeroButStillDraws) {
    // can-shoot == false -> gate fails regardless of roll, but the draw is taken
    // first: every gated-out tick consumes exactly one draw, so the stream stays
    // lockstep with a parallel reference.
    NpcSummon01 s;
    RGRandom ref;
    s.SetSeed(555);
    ref.SetRandomSeed(555);
    for (int i = 0; i < 64; ++i) {
        s.SetCanShoot(false);            // re-assert: gate can never pass
        float cadence = -1.0F;
        const ShootResult res = s.ShootReflection(cadence, 9.0F);
        EXPECT_EQ(res, ShootResult::Held); // can-shoot was 0: never fires
        EXPECT_FALSE(s.CanShoot());        // no write toggled it back
        EXPECT_FLOAT_EQ(cadence, -1.0F);   // out cadence untouched on Held
        ref.Range(0, 10);                  // the one parallel draw per tick
    }
    // 64 gated-out ticks consumed 64 draws in order: a probe on each stream
    // must still match (lockstep preserved).
    EXPECT_EQ(s.Rng().Range(0, 10), ref.Range(0, 10));
}

TEST(NpcSummon01Test, ShootGatedOutPathConsumesExactlyOneDraw) {
    // Whether Fired or Held, ShootReflection consumes EXACTLY one draw. Drive a
    // summon and a parallel reference and assert they stay aligned after each
    // tick by interleaving a probe draw.
    NpcSummon01 s;
    RGRandom ref;
    s.SetSeed(2024);
    ref.SetRandomSeed(2024);
    for (int i = 0; i < 96; ++i) {
        s.SetCanShoot((i & 1) != 0); // alternate the gate; draw count is invariant
        float cadence = 0.0F;
        s.ShootReflection(cadence, 1.0F);
        ref.Range(0, 10); // the one parallel draw
    }
    // streams must now be perfectly aligned: probe both, expect equality.
    EXPECT_EQ(s.Rng().Range(0, 10), ref.Range(0, 10));
}

// ---- RunReflection (wander draw + zero move_direction) -------------------

TEST(NpcSummon01Test, WanderRollInRange) {
    NpcSummon01 s;
    s.SetSeed(7);
    for (int i = 0; i < 128; ++i) {
        const int r = s.RunReflection();
        EXPECT_GE(r, 0);
        EXPECT_LT(r, NpcSummon01::kWanderRollCeiling); // Range(0,10) max EXCL
    }
}

TEST(NpcSummon01Test, WanderConsumesExactlyOneDrawInOrder) {
    // RunReflection draws Range(0,10) once; a reference from the same seed must
    // match draw-for-draw.
    NpcSummon01 s;
    RGRandom ref;
    s.SetSeed(808);
    ref.SetRandomSeed(808);
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(s.RunReflection(), ref.Range(0, 10));
    }
}

TEST(NpcSummon01Test, WanderZeroesMoveDirection) {
    // FAITHFUL: the tail builds a zero vector and writes move_direction
    // (words 0x14/0x15) = (0,0), lines 1671524-1671525.
    NpcSummon01 s;
    s.SetSeed(1);
    s.RunReflection();
    EXPECT_FLOAT_EQ(s.MoveDirection().x, 0.0F);
    EXPECT_FLOAT_EQ(s.MoveDirection().y, 0.0F);
}

TEST(NpcSummon01Test, WanderIsDeterministic) {
    NpcSummon01 a;
    NpcSummon01 b;
    a.SetSeed(31337);
    b.SetSeed(31337);
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(a.RunReflection(), b.RunReflection());
    }
}

// ---- EndCycle (zero move_direction; no RNG) ------------------------------

TEST(NpcSummon01Test, EndCycleZeroesMoveDirection) {
    // FAITHFUL: the only recoverable write is move_direction = Vector2.zero
    // (lines 1671585-1671586).
    NpcSummon01 s;
    s.SetSeed(1);
    s.EndCycle();
    EXPECT_FLOAT_EQ(s.MoveDirection().x, 0.0F);
    EXPECT_FLOAT_EQ(s.MoveDirection().y, 0.0F);
}

TEST(NpcSummon01Test, EndCycleTakesNoRngDraw) {
    // EndCycle has no rg_random draw; the stream after EndCycle must equal an
    // untouched stream.
    NpcSummon01 cycled;
    NpcSummon01 quiet;
    cycled.SetSeed(456);
    quiet.SetSeed(456);
    cycled.EndCycle();
    cycled.EndCycle();
    EXPECT_EQ(cycled.RunReflection(), quiet.RunReflection());
}

// ---- GetHurt (dead-gate on byte 0x0d; no RNG) ----------------------------

TEST(NpcSummon01Test, GetHurtRoutesWhenAlive) {
    // FAITHFUL: gate byte 0x0d == 0 (alive) -> route the hit to UICanvas
    // (line 1671675/1671677). A fresh summon is alive.
    NpcSummon01 s;
    s.SetSeed(1);
    EXPECT_FALSE(s.Destroyed());
    EXPECT_TRUE(s.GetHurt()); // alive -> routed
}

TEST(NpcSummon01Test, GetHurtIgnoredWhenDead) {
    // byte 0x0d != 0 (dead) -> the body skips the UICanvas route.
    NpcSummon01 s;
    s.SetSeed(1);
    s.SetDestroyed(true);
    EXPECT_FALSE(s.GetHurt()); // dead -> ignored
}

TEST(NpcSummon01Test, GetHurtTakesNoRngDrawAndTouchesNoVitals) {
    // GetHurt has no draw; the stream is non-advancing. It also writes no state
    // of its own (HP is the base chain's job), so Destroyed stays as set.
    NpcSummon01 hit;
    NpcSummon01 quiet;
    hit.SetSeed(2718);
    quiet.SetSeed(2718);
    hit.GetHurt();
    hit.GetHurt();
    EXPECT_FALSE(hit.Destroyed()); // GetHurt never sets the dead latch
    EXPECT_EQ(hit.RunReflection(), quiet.RunReflection());
}

// ---- OnGameStateChange (active-gate; resume/pause branches; no RNG) -------

TEST(NpcSummon01Test, StateChangeIgnoredWhenNotActive) {
    // FAITHFUL: gate byte 0x0c == 0 -> whole body skipped, no write (line 1671731).
    NpcSummon01 s;
    s.SetSeed(1);
    EXPECT_FALSE(s.Active());
    float speed = 1.0F;
    EXPECT_EQ(s.OnGameStateChange(NpcSummon01::kStateResume, speed),
              StateChange::Ignored);
    EXPECT_FLOAT_EQ(speed, 1.0F); // untouched
    EXPECT_FALSE(s.Paused());     // untouched
}

TEST(NpcSummon01Test, StateChangeResumeClearsPausedAndBoostsSpeedRate) {
    // active && game_state == 2 -> paused = 0 and speed_rate += 0.5
    // (lines 1671733 / 1671739).
    NpcSummon01 s;
    s.SetSeed(1);
    s.SetActive(true);
    s.SetPaused(true);
    float speed = 1.0F;
    EXPECT_EQ(s.OnGameStateChange(NpcSummon01::kStateResume, speed),
              StateChange::Resumed);
    EXPECT_FALSE(s.Paused());                                   // 0x44 = 0
    EXPECT_FLOAT_EQ(speed, 1.0F + NpcSummon01::kResumeSpeedRateBoost); // += 0.5
}

TEST(NpcSummon01Test, StateChangePauseSetsPausedAndLeavesSpeedRate) {
    // active && game_state == 1 -> paused = 1; speed_rate untouched
    // (line 1671742).
    NpcSummon01 s;
    s.SetSeed(1);
    s.SetActive(true);
    s.SetPaused(false);
    float speed = 3.0F;
    EXPECT_EQ(s.OnGameStateChange(NpcSummon01::kStatePause, speed),
              StateChange::Paused);
    EXPECT_TRUE(s.Paused());      // 0x44 = 1
    EXPECT_FLOAT_EQ(speed, 3.0F); // not the resume branch: untouched
}

TEST(NpcSummon01Test, StateChangeNoChangeForOtherStates) {
    // active but game_state not in {1,2} -> no write.
    NpcSummon01 s;
    s.SetSeed(1);
    s.SetActive(true);
    s.SetPaused(false);
    float speed = 2.0F;
    EXPECT_EQ(s.OnGameStateChange(0, speed), StateChange::NoChange);
    EXPECT_EQ(s.OnGameStateChange(3, speed), StateChange::NoChange);
    EXPECT_EQ(s.OnGameStateChange(-1, speed), StateChange::NoChange);
    EXPECT_FALSE(s.Paused());     // untouched
    EXPECT_FLOAT_EQ(speed, 2.0F); // untouched
}

TEST(NpcSummon01Test, StateChangeResumeOnlyBoostsWhenActive) {
    // The active gate dominates: an inactive summon never boosts speed even on
    // a resume code.
    NpcSummon01 s;
    s.SetSeed(1);
    s.SetActive(false);
    float speed = 5.0F;
    s.OnGameStateChange(NpcSummon01::kStateResume, speed);
    EXPECT_FLOAT_EQ(speed, 5.0F); // gated out: no boost
}

TEST(NpcSummon01Test, StateChangeTakesNoRngDraw) {
    NpcSummon01 changed;
    NpcSummon01 quiet;
    changed.SetSeed(2024);
    quiet.SetSeed(2024);
    changed.SetActive(true);
    float speed = 1.0F;
    changed.OnGameStateChange(NpcSummon01::kStateResume, speed);
    changed.OnGameStateChange(NpcSummon01::kStatePause, speed);
    EXPECT_EQ(changed.RunReflection(), quiet.RunReflection());
}

// ---- Full interleaved replay determinism ----------------------------------

TEST(NpcSummon01Test, FullStreamReplayIsDeterministic) {
    // Interleave ShootReflection (1 draw), RunReflection (1 draw), EndCycle
    // (0 draws), GetHurt (0 draws), OnGameStateChange (0 draws); replay from the
    // same seed -> identical sequence (lockstep guarantee).
    auto run = [](int seed) {
        NpcSummon01 s;
        s.SetSeed(seed);
        s.SetActive(true);
        std::vector<int> trace;
        for (int i = 0; i < 24; ++i) {
            s.SetCanShoot((i & 1) != 0);
            float cadence = 0.0F;
            trace.push_back(s.ShootReflection(cadence, 5.0F) == ShootResult::Fired
                                ? 1
                                : 0);            // 1 draw
            trace.push_back(s.RunReflection());  // 1 draw
            s.EndCycle();                        // 0 draws
            trace.push_back(s.GetHurt() ? 1 : 0); // 0 draws
            float speed = 1.0F;
            s.OnGameStateChange(i % 3, speed);   // 0 draws
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

TEST(NpcSummon01Test, FullStreamMatchesParallelReferenceDrawCount) {
    // ShootReflection (1) + RunReflection (1) = 2 draws per loop; EndCycle,
    // GetHurt, OnGameStateChange take none. A parallel reference drawing exactly
    // 2 per loop must stay aligned.
    NpcSummon01 s;
    RGRandom ref;
    s.SetSeed(13579);
    ref.SetRandomSeed(13579);
    s.SetActive(true);
    for (int i = 0; i < 50; ++i) {
        s.SetCanShoot(true);
        float cadence = 0.0F;
        s.ShootReflection(cadence, 1.0F);
        ref.Range(0, 10);                  // mirror the shoot draw
        EXPECT_EQ(s.RunReflection(), ref.Range(0, 10)); // mirror the wander draw
        s.EndCycle();
        s.GetHurt();
        float speed = 1.0F;
        s.OnGameStateChange(i % 4, speed);
    }
    EXPECT_EQ(s.Rng().Range(0, 10), ref.Range(0, 10)); // still aligned
}

TEST(NpcSummon01Test, DifferentSeedsDiverge) {
    NpcSummon01 a;
    NpcSummon01 b;
    a.SetSeed(1);
    b.SetSeed(424242);
    bool diverged = false;
    for (int i = 0; i < 64; ++i) {
        if (a.RunReflection() != b.RunReflection()) {
            diverged = true;
        }
    }
    EXPECT_TRUE(diverged);
}

// NOLINTEND(readability-magic-numbers)
