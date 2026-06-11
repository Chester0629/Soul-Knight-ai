#include <gtest/gtest.h>

#include <vector>

#include "combat/BossAI07.hpp"
#include "data/RGRandom.hpp"

using Game::BossAI07;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

namespace {

// A parallel reference stream, same-seeded, to assert exact draw count+order.
RGRandom RefStream(int seed) {
    RGRandom r;
    r.SetRandomSeed(seed);
    return r;
}

} // namespace

// ---- FixedUpdate gate (BossAI07__FixedUpdate) ------------------------------

TEST(BossAI07Test, FixedUpdateGatedUntilAwake) {
    BossAI07 b;
    EXPECT_FALSE(b.Awake());
    EXPECT_FALSE(b.FixedUpdate()); // !awake -> no tick
}

TEST(BossAI07Test, FixedUpdateRunsWhenAwakeAndAlive) {
    BossAI07 b;
    EXPECT_TRUE(b.OnGameStateChange(1, true)); // wake gate
    EXPECT_TRUE(b.Awake());
    EXPECT_TRUE(b.FixedUpdate()); // awake && !dead -> owner runs EnemyUpdate tail
}

TEST(BossAI07Test, FixedUpdateClearsAwakeWhenDead) {
    BossAI07 b;
    b.OnGameStateChange(1, true);
    b.SetDead(true);
    EXPECT_FALSE(b.FixedUpdate()); // dead path
    EXPECT_FALSE(b.Awake());       // *(this+0x18) = 0
}

// ---- Scout gate (BossAI07__Scout) ------------------------------------------

TEST(BossAI07Test, ScoutClearsTargetWhenAliveAndNotDizzy) {
    BossAI07 b;
    EXPECT_TRUE(b.Scout());
    EXPECT_TRUE(b.TargetCleared());
}

TEST(BossAI07Test, ScoutGatedByDead) {
    BossAI07 b;
    b.SetDead(true);
    EXPECT_FALSE(b.Scout());
    EXPECT_FALSE(b.TargetCleared());
}

TEST(BossAI07Test, ScoutGatedByDizzy) {
    BossAI07 b;
    b.SetDizzy(true);
    EXPECT_FALSE(b.Scout());
    EXPECT_FALSE(b.TargetCleared());
}

TEST(BossAI07Test, ScoutDrawsNoRng) {
    // Scout takes no draw on any path; the stream must be untouched.
    BossAI07 b;
    b.SetSeed(123);
    RGRandom ref = RefStream(123);
    b.Scout();
    b.SetDead(true);
    b.Scout();
    // First subsequent draw must equal the reference's first draw.
    EXPECT_EQ(b.Rng().Range(0, 100), ref.Range(0, 100));
}

// ---- ShootReflection (BossAI07__ShootReflection) ---------------------------

TEST(BossAI07Test, ShootReflectionDispatchPathTakesNoDraw) {
    BossAI07 b;
    b.SetSeed(777);
    b.SetScoutRate(2.0F);
    b.SetCanShoot(true); // can_shoot && !dead && !dizzy -> StartAtk01, no draw
    RGRandom ref = RefStream(777);

    float delay = -1.0F;
    EXPECT_TRUE(b.ShootReflection(delay)); // dispatched
    EXPECT_FALSE(b.CanShoot());            // *(this+0x40) = 0
    EXPECT_FLOAT_EQ(delay, -1.0F);         // out param untouched on attack path

    // Stream untouched: next draw equals reference's first draw.
    EXPECT_FLOAT_EQ(b.Rng().Range(0.0F, 1.0F), ref.Range(0.0F, 1.0F));
}

TEST(BossAI07Test, ShootReflectionReInvokeDrawsOneFloatInRange) {
    BossAI07 b;
    b.SetSeed(2024);
    b.SetScoutRate(2.0F);
    b.SetCanShoot(false); // forces the re-Invoke (draw) branch
    RGRandom ref = RefStream(2024);

    float delay = 0.0F;
    EXPECT_FALSE(b.ShootReflection(delay)); // re-Invoke path

    // Exactly one float Range(scout_rate*0.5, scout_rate) was drawn, in order.
    const float expected = ref.Range(2.0F * 0.5F, 2.0F);
    EXPECT_FLOAT_EQ(delay, expected);
    EXPECT_GE(delay, 1.0F); // scout_rate*0.5
    EXPECT_LE(delay, 2.0F); // scout_rate (inclusive)
}

TEST(BossAI07Test, ShootReflectionGatedDeadFallsToDrawBranch) {
    // can_shoot set but dead -> skip dispatch, fall through to the draw branch.
    BossAI07 b;
    b.SetSeed(55);
    b.SetScoutRate(4.0F);
    b.SetCanShoot(true);
    b.SetDead(true);
    RGRandom ref = RefStream(55);

    float delay = 0.0F;
    EXPECT_FALSE(b.ShootReflection(delay)); // not dispatched (dead gate)
    EXPECT_TRUE(b.CanShoot());              // can_shoot NOT cleared on this path
    EXPECT_FLOAT_EQ(delay, ref.Range(4.0F * 0.5F, 4.0F));
}

TEST(BossAI07Test, ShootReflectionReInvokeUsesAngryHalvedScoutRateNote) {
    // The low bound is exactly scout_rate*0.5 (golden scalar), independent of
    // angry state (BossAngry halves shoot_cd, not scout_rate).
    BossAI07 b;
    b.SetSeed(9);
    b.SetScoutRate(3.0F);
    b.SetCanShoot(false);
    RGRandom ref = RefStream(9);
    float delay = 0.0F;
    b.ShootReflection(delay);
    EXPECT_FLOAT_EQ(delay, ref.Range(1.5F, 3.0F));
}

// ---- BossAngry (BossAI07__BossAngry) ---------------------------------------

TEST(BossAI07Test, BossAngrySetsFlagAndHalvesShootCd) {
    BossAI07 b;
    b.SetShootCd(2.0F);
    EXPECT_FALSE(b.Angry());
    b.BossAngry();
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 2.0F * BossAI07::kAngryShootCdScale); // 1.0
}

TEST(BossAI07Test, BossAngryHalvesShootCdEachCall) {
    // The decomp has no angry latch: both field writes run on every call, so a
    // second invocation halves shoot_cd again (2.0 -> 1.0 -> 0.5).
    BossAI07 b;
    b.SetShootCd(2.0F);
    b.BossAngry();
    EXPECT_FLOAT_EQ(b.ShootCd(), 1.0F); // 2.0 * 0.5
    b.BossAngry();                      // second call halves again
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 0.5F); // 1.0 * 0.5
}

TEST(BossAI07Test, BossAngryDrawsNoRng) {
    BossAI07 b;
    b.SetSeed(3);
    b.SetShootCd(2.0F);
    RGRandom ref = RefStream(3);
    b.BossAngry();
    EXPECT_EQ(b.Rng().Range(0, 50), ref.Range(0, 50)); // stream untouched
}

// ---- OnGameStateChange (BossAI07__OnGameStateChange) -----------------------

TEST(BossAI07Test, OnGameStateChangeWakesOnStartWhenRoomReady) {
    BossAI07 b;
    EXPECT_TRUE(b.OnGameStateChange(1, true));
    EXPECT_TRUE(b.Awake());
}

TEST(BossAI07Test, OnGameStateChangeIgnoresNonStartState) {
    BossAI07 b;
    EXPECT_FALSE(b.OnGameStateChange(0, true)); // game_state != 1 -> return
    EXPECT_FALSE(b.OnGameStateChange(2, true));
    EXPECT_FALSE(b.Awake());
}

TEST(BossAI07Test, OnGameStateChangeWaitsForRoomReady) {
    BossAI07 b;
    EXPECT_FALSE(b.OnGameStateChange(1, false)); // room not ready -> no wake
    EXPECT_FALSE(b.Awake());
}

// ---- InAtk01 (BossAI07__InAtk01) -------------------------------------------

TEST(BossAI07Test, InAtk01DrawsOneIntRangeMatchesReference) {
    BossAI07 b;
    b.SetSeed(4242);
    RGRandom ref = RefStream(4242);
    for (int i = 0; i < 64; ++i) {
        const int got = b.InAtk01();
        EXPECT_EQ(got, ref.Range(0, 100)); // one int draw, exact order
        EXPECT_GE(got, 0);
        EXPECT_LT(got, 100); // max exclusive
    }
}

// ---- CreateBullet1/4 roll (BossAI07__CreateBullet1 / CreateBullet4) ---------

TEST(BossAI07Test, CreateBulletRollDrawsOneIntRange0To20) {
    BossAI07 b;
    b.SetSeed(31337);
    RGRandom ref = RefStream(31337);
    for (int i = 0; i < 64; ++i) {
        const int got = b.CreateBulletRoll();
        EXPECT_EQ(got, ref.Range(0, 20)); // Range(0, 0x14), max exclusive
        EXPECT_GE(got, 0);
        EXPECT_LT(got, 20);
    }
}

// ---- CreateBullet2 ring count (BossAI07__CreateBullet2) --------------------

TEST(BossAI07Test, CreateBullet2RingCalmIsFour) {
    BossAI07 b;
    EXPECT_FALSE(b.Angry());
    EXPECT_EQ(b.CreateBullet2RingCount(), 4); // 8 >> 1
}

TEST(BossAI07Test, CreateBullet2RingAngryIsSix) {
    BossAI07 b;
    b.SetShootCd(2.0F);
    b.BossAngry();
    EXPECT_EQ(b.CreateBullet2RingCount(), 6); // 0xc >> 1 == 12 >> 1
}

TEST(BossAI07Test, CreateBullet2RingCountDrawsNoRng) {
    BossAI07 b;
    b.SetSeed(8);
    RGRandom ref = RefStream(8);
    (void)b.CreateBullet2RingCount();
    EXPECT_EQ(b.Rng().Range(0, 20), ref.Range(0, 20)); // pure scalar, no draw
}

// ---- Golden scalars --------------------------------------------------------

TEST(BossAI07Test, GoldenScalars) {
    EXPECT_FLOAT_EQ(BossAI07::kAngryShootCdScale, 0.5F);
    EXPECT_FLOAT_EQ(BossAI07::kReinvokeDelayLowScale, 0.5F);
    EXPECT_EQ(BossAI07::kInAtk01RollCeiling, 100);
    EXPECT_EQ(BossAI07::kCreateBulletRollCeiling, 20); // 0x14
    EXPECT_EQ(BossAI07::kBullet2RingCalm, 4);
    EXPECT_EQ(BossAI07::kBullet2RingAngry, 6);
    EXPECT_EQ(BossAI07::kGameStateStart, 1);
    EXPECT_EQ(BossAI07::kBossClipMinLen, 3);
}

// ---- Full interleaved stream replay (lockstep determinism) -----------------

TEST(BossAI07Test, FullStreamReplayIsLockstep) {
    // Interleave the only three drawing bodies (ShootReflection re-Invoke,
    // InAtk01, CreateBulletRoll) and assert identical replay AND exact match to
    // an independently-driven reference stream in the same order.
    auto run = [](int seed) {
        BossAI07 b;
        b.SetSeed(seed);
        b.SetScoutRate(2.0F);
        RGRandom ref;
        ref.SetRandomSeed(seed);
        std::vector<float> trace;
        for (int i = 0; i < 16; ++i) {
            b.SetCanShoot(false); // force the re-Invoke draw branch
            float delay = 0.0F;
            b.ShootReflection(delay);
            trace.push_back(delay);
            EXPECT_FLOAT_EQ(delay, ref.Range(1.0F, 2.0F));

            const int a = b.InAtk01();
            trace.push_back(static_cast<float>(a));
            EXPECT_EQ(a, ref.Range(0, 100));

            const int c = b.CreateBulletRoll();
            trace.push_back(static_cast<float>(c));
            EXPECT_EQ(c, ref.Range(0, 20));
        }
        return trace;
    };
    const auto first = run(2718);
    const auto second = run(2718);
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_FLOAT_EQ(first[i], second[i]);
    }
}

// NOLINTEND(readability-magic-numbers)
