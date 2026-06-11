#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/BossAI09.hpp"
#include "data/RGRandom.hpp"

using Game::BossAI09;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

namespace {

// A same-seeded reference stream, used to prove draw COUNT + ORDER lockstep:
// any draw the brain performs must match this parallel RGRandom exactly.
RGRandom MakeRef(int seed) {
    RGRandom r;
    r.SetRandomSeed(seed);
    return r;
}

// Component-wise vector equality (gtest does not pretty-print glm::vec2).
bool VecEq(const glm::vec2 &a, const glm::vec2 &b) {
    return a.x == b.x && a.y == b.y;
}

bool IsZero(const glm::vec2 &v) { return v.x == 0.0F && v.y == 0.0F; }

} // namespace

// ---- RunReflection: two Range(-1,1) float draws, normalized ----------------

TEST(BossAI09Test, RunReflectionDrawsTwoFloatsInOrder) {
    BossAI09 b;
    b.SetSeed(12345);
    RGRandom ref = MakeRef(12345);

    const float rx = ref.Range(BossAI09::kWanderMin, BossAI09::kWanderMax);
    const float ry = ref.Range(BossAI09::kWanderMin, BossAI09::kWanderMax);
    const float len = std::sqrt(rx * rx + ry * ry);
    const glm::vec2 expected =
        len > 0.0F ? glm::vec2(rx / len, ry / len) : glm::vec2(0.0F, 0.0F);

    const glm::vec2 got = b.RunReflection();
    EXPECT_FLOAT_EQ(got.x, expected.x);
    EXPECT_FLOAT_EQ(got.y, expected.y);
    // Brain consumed exactly two draws: the next brain draw must equal ref's 3rd.
    EXPECT_FLOAT_EQ(b.Rng().Range(BossAI09::kWanderMin, BossAI09::kWanderMax),
                    ref.Range(BossAI09::kWanderMin, BossAI09::kWanderMax));
}

TEST(BossAI09Test, RunReflectionIsUnitLengthOrZero) {
    BossAI09 b;
    b.SetSeed(777);
    for (int i = 0; i < 64; ++i) {
        const glm::vec2 d = b.RunReflection();
        const float mag = std::sqrt(d.x * d.x + d.y * d.y);
        EXPECT_TRUE(std::fabs(mag - 1.0F) < 1e-4F || mag == 0.0F);
        EXPECT_TRUE(VecEq(d, b.MoveDirection())); // writes move_direction
    }
}

TEST(BossAI09Test, RunReflectionDeterministic) {
    BossAI09 a;
    BossAI09 b;
    a.SetSeed(2024);
    b.SetSeed(2024);
    for (int i = 0; i < 32; ++i) {
        EXPECT_TRUE(VecEq(a.RunReflection(), b.RunReflection()));
    }
}

// ---- ShootReflection: one gated Range(0,100) draw --------------------------

TEST(BossAI09Test, ShootReflectionGateOpenDrawsOnce) {
    BossAI09 b;
    b.SetSeed(99);
    b.SetCanShoot(true); // gate: can_shoot && !dead && !dizzy
    RGRandom ref = MakeRef(99);

    const int roll = ref.Range(0, BossAI09::kRollCeiling);
    int expectedIdx = roll / (BossAI09::kRollCeiling / BossAI09::kAttackCount) + 1;
    if (expectedIdx > BossAI09::kAttackCount) {
        expectedIdx = BossAI09::kAttackCount;
    }

    const int idx = b.ShootReflection();
    EXPECT_EQ(idx, expectedIdx);
    EXPECT_GE(idx, 1);
    EXPECT_LE(idx, BossAI09::kAttackCount);
    // Exactly one draw consumed: next brain draw aligns with ref's 2nd.
    EXPECT_EQ(b.Rng().Range(0, BossAI09::kRollCeiling),
              ref.Range(0, BossAI09::kRollCeiling));
}

TEST(BossAI09Test, ShootReflectionGatedByCanShootTakesNoDraw) {
    BossAI09 b;
    b.SetSeed(99);
    b.SetCanShoot(false); // gate closed -> no draw
    RGRandom ref = MakeRef(99);

    EXPECT_EQ(b.ShootReflection(), BossAI09::kNoAttack);
    // Stream untouched: the brain's first draw equals ref's first draw.
    EXPECT_EQ(b.Rng().Range(0, BossAI09::kRollCeiling),
              ref.Range(0, BossAI09::kRollCeiling));
}

TEST(BossAI09Test, ShootReflectionGatedByDeadTakesNoDraw) {
    BossAI09 b;
    b.SetSeed(99);
    b.SetCanShoot(true);
    b.SetDead(true); // dead gates the roll out
    RGRandom ref = MakeRef(99);

    EXPECT_EQ(b.ShootReflection(), BossAI09::kNoAttack);
    EXPECT_EQ(b.Rng().Range(0, BossAI09::kRollCeiling),
              ref.Range(0, BossAI09::kRollCeiling));
}

TEST(BossAI09Test, ShootReflectionGatedByDizzyTakesNoDraw) {
    BossAI09 b;
    b.SetSeed(99);
    b.SetCanShoot(true);
    b.SetDizzy(true); // dizzy gates the roll out
    RGRandom ref = MakeRef(99);

    EXPECT_EQ(b.ShootReflection(), BossAI09::kNoAttack);
    EXPECT_EQ(b.Rng().Range(0, BossAI09::kRollCeiling),
              ref.Range(0, BossAI09::kRollCeiling));
}

TEST(BossAI09Test, ShootReflectionCoversAllFiveAttacks) {
    BossAI09 b;
    b.SetSeed(7);
    b.SetCanShoot(true);
    std::vector<int> seen(BossAI09::kAttackCount + 1, 0);
    for (int i = 0; i < 600; ++i) {
        seen[static_cast<std::size_t>(b.ShootReflection())]++;
    }
    EXPECT_EQ(seen[0], 0); // gate open: never returns kNoAttack
    for (int idx = 1; idx <= BossAI09::kAttackCount; ++idx) {
        EXPECT_GT(seen[static_cast<std::size_t>(idx)], 0);
    }
}

// ---- move-zeroing attack starts (no RNG draw) ------------------------------

TEST(BossAI09Test, StartAtk02ZeroesMoveDirectionNoDraw) {
    BossAI09 b;
    b.SetSeed(5);
    b.RunReflection(); // give move_direction a non-zero value first
    ASSERT_FALSE(IsZero(b.MoveDirection()));
    RGRandom ref = MakeRef(5);
    ref.Range(BossAI09::kWanderMin, BossAI09::kWanderMax);
    ref.Range(BossAI09::kWanderMin, BossAI09::kWanderMax);

    b.StartAtk02();
    EXPECT_TRUE(IsZero(b.MoveDirection()));
    // No draw in StartAtk02: streams still aligned.
    EXPECT_FLOAT_EQ(b.Rng().Range(0.0F, 1.0F), ref.Range(0.0F, 1.0F));
}

TEST(BossAI09Test, StartAtk03ZeroesMoveDirection) {
    BossAI09 b;
    b.SetSeed(5);
    b.RunReflection();
    b.StartAtk03();
    EXPECT_TRUE(IsZero(b.MoveDirection()));
}

TEST(BossAI09Test, InAtk04ZeroesMoveDirection) {
    BossAI09 b;
    b.SetSeed(5);
    b.RunReflection();
    b.InAtk04();
    EXPECT_TRUE(IsZero(b.MoveDirection()));
}

// ---- Scout: clears target only when alive and not stunned ------------------

TEST(BossAI09Test, ScoutClearsTargetWhenAlive) {
    BossAI09 b;
    EXPECT_FALSE(b.TargetCleared());
    b.Scout();
    EXPECT_TRUE(b.TargetCleared()); // target_obj = null
}

TEST(BossAI09Test, ScoutGatedByDeadDoesNothing) {
    BossAI09 b;
    b.SetDead(true);
    b.Scout();
    EXPECT_FALSE(b.TargetCleared());
}

TEST(BossAI09Test, ScoutGatedByDizzyDoesNothing) {
    BossAI09 b;
    b.SetDizzy(true);
    b.Scout();
    EXPECT_FALSE(b.TargetCleared());
}

// ---- Dizzy: latches dizzy only when alive ----------------------------------

TEST(BossAI09Test, EnterDizzySetsDizzyWhenAlive) {
    BossAI09 b;
    EXPECT_FALSE(b.Dizzy());
    b.EnterDizzy();
    EXPECT_TRUE(b.Dizzy());
}

TEST(BossAI09Test, EnterDizzyGatedByDead) {
    BossAI09 b;
    b.SetDead(true);
    b.EnterDizzy();
    EXPECT_FALSE(b.Dizzy()); // dead gates the stun out
}

// ---- OnGameStateChange: wake latch -----------------------------------------

TEST(BossAI09Test, OnGameStateChangeWakesWhenRoomReady) {
    BossAI09 b;
    EXPECT_FALSE(b.Awake());
    b.OnGameStateChange(1, true);
    EXPECT_TRUE(b.Awake());
}

TEST(BossAI09Test, OnGameStateChangeIgnoresNonStartState) {
    BossAI09 b;
    b.OnGameStateChange(0, true); // game_state != 1
    EXPECT_FALSE(b.Awake());
    b.OnGameStateChange(2, true);
    EXPECT_FALSE(b.Awake());
}

TEST(BossAI09Test, OnGameStateChangeWaitsForRoomReady) {
    BossAI09 b;
    b.OnGameStateChange(1, false); // room not ready
    EXPECT_FALSE(b.Awake());
}

// ---- FixedUpdateTick: awake-clear (death side of the latch) -----------------

TEST(BossAI09Test, FixedUpdateClearsAwakeWhenDeadAndNotShooting) {
    BossAI09 b;
    b.OnGameStateChange(1, true); // awake = 1
    ASSERT_TRUE(b.Awake());
    b.SetDead(true);     // dead = 1
    b.SetShooting(false); // shooting = 0
    b.FixedUpdateTick();
    EXPECT_FALSE(b.Awake()); // awake(0x18) cleared
}

TEST(BossAI09Test, FixedUpdateKeepsAwakeWhenAlive) {
    BossAI09 b;
    b.SetAwake(true);
    b.SetDead(false); // alive: gate requires dead
    b.FixedUpdateTick();
    EXPECT_TRUE(b.Awake());
}

TEST(BossAI09Test, FixedUpdateKeepsAwakeWhenShooting) {
    BossAI09 b;
    b.SetAwake(true);
    b.SetDead(true);
    b.SetShooting(true); // shooting blocks the inner (shooting==0) branch
    b.FixedUpdateTick();
    EXPECT_TRUE(b.Awake());
}

TEST(BossAI09Test, FixedUpdateNoOpWhenNotAwake) {
    BossAI09 b;
    EXPECT_FALSE(b.Awake());
    b.SetDead(true);
    b.FixedUpdateTick();
    EXPECT_FALSE(b.Awake()); // stays false; outer gate requires awake!=0
}

TEST(BossAI09Test, FixedUpdateTakesNoDraw) {
    BossAI09 b;
    b.SetSeed(555);
    b.OnGameStateChange(1, true);
    b.SetDead(true);
    RGRandom ref = MakeRef(555);
    b.FixedUpdateTick();
    EXPECT_FALSE(b.Awake());
    // No RNG in FixedUpdate's brain path: streams aligned.
    EXPECT_EQ(b.Rng().Range(0, BossAI09::kRollCeiling),
              ref.Range(0, BossAI09::kRollCeiling));
}

// ---- BossAngry: angry flag, shoot_cd halved, anim speed --------------------

TEST(BossAI09Test, OnHurtEntersAngryBelowHalfHp) {
    BossAI09 b;
    b.SetShootCd(2.0F);
    b.OnHurt(301, 600); // > 50% -> calm
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 2.0F);
    EXPECT_FLOAT_EQ(b.AnimSpeed(), 1.0F);

    b.OnHurt(299, 600); // < 50% -> angry
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 1.0F); // 2.0 * 0.5
    EXPECT_FLOAT_EQ(b.AnimSpeed(), BossAI09::kAngryAnimSpeed);
}

TEST(BossAI09Test, OnHurtExactlyHalfIsNotAngry) {
    BossAI09 b;
    b.SetShootCd(2.0F);
    b.OnHurt(300, 600); // 0.5 is not < 0.5
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 2.0F);
}

TEST(BossAI09Test, OnHurtAngryHappensOnce) {
    BossAI09 b;
    b.SetShootCd(4.0F);
    b.OnHurt(100, 600); // angry, shoot_cd -> 2.0
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 2.0F);
    b.OnHurt(10, 600); // already angry: no second halving
    EXPECT_FLOAT_EQ(b.ShootCd(), 2.0F);
    EXPECT_FLOAT_EQ(b.AnimSpeed(), BossAI09::kAngryAnimSpeed);
}

TEST(BossAI09Test, OnHurtZeroMaxHpDoesNotEnterAngry) {
    BossAI09 b;
    b.SetShootCd(2.0F);
    b.OnHurt(0, 0); // guard against divide-by-zero
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 2.0F);
}

TEST(BossAI09Test, OnHurtTakesNoDraw) {
    BossAI09 b;
    b.SetSeed(321);
    b.SetShootCd(2.0F);
    RGRandom ref = MakeRef(321);
    b.OnHurt(1, 600); // angry, but no RNG involvement
    EXPECT_TRUE(b.Angry());
    EXPECT_EQ(b.Rng().Range(0, BossAI09::kRollCeiling),
              ref.Range(0, BossAI09::kRollCeiling));
}

// ---- full interleaved replay lockstep --------------------------------------

TEST(BossAI09Test, FullStreamReplayIsDeterministic) {
    auto run = [](int seed) {
        BossAI09 b;
        b.SetSeed(seed);
        b.SetCanShoot(true);
        std::vector<float> trace;
        for (int i = 0; i < 24; ++i) {
            const glm::vec2 d = b.RunReflection();
            trace.push_back(d.x);
            trace.push_back(d.y);
            trace.push_back(static_cast<float>(b.ShootReflection()));
        }
        return trace;
    };
    const auto a = run(2718);
    const auto b = run(2718);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_FLOAT_EQ(a[i], b[i]);
    }
}

// Interleaved draw COUNT/ORDER against a parallel reference: RunReflection (2
// floats) then ShootReflection (1 int), repeated, must consume draws in exact
// lockstep with a same-seeded RGRandom.
TEST(BossAI09Test, InterleavedDrawOrderMatchesReference) {
    BossAI09 b;
    b.SetSeed(8675309);
    b.SetCanShoot(true);
    RGRandom ref = MakeRef(8675309);

    for (int i = 0; i < 16; ++i) {
        const glm::vec2 d = b.RunReflection();
        const float rx = ref.Range(BossAI09::kWanderMin, BossAI09::kWanderMax);
        const float ry = ref.Range(BossAI09::kWanderMin, BossAI09::kWanderMax);
        const float len = std::sqrt(rx * rx + ry * ry);
        const glm::vec2 expected =
            len > 0.0F ? glm::vec2(rx / len, ry / len) : glm::vec2(0.0F, 0.0F);
        EXPECT_FLOAT_EQ(d.x, expected.x);
        EXPECT_FLOAT_EQ(d.y, expected.y);

        const int idx = b.ShootReflection();
        const int roll = ref.Range(0, BossAI09::kRollCeiling);
        int expectedIdx =
            roll / (BossAI09::kRollCeiling / BossAI09::kAttackCount) + 1;
        if (expectedIdx > BossAI09::kAttackCount) {
            expectedIdx = BossAI09::kAttackCount;
        }
        EXPECT_EQ(idx, expectedIdx);
    }
}

// NOLINTEND(readability-magic-numbers)
