#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/BossAI12.hpp"
#include "data/RGRandom.hpp"

using Game::BossAI12;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

namespace {

// Parallel reference stream: a same-seeded RGRandom we draw from independently
// to prove BossAI12 consumes draws in the EXACT count + order the decomp does.
RGRandom MakeRef(int seed) {
    RGRandom r;
    r.SetRandomSeed(seed);
    return r;
}

} // namespace

// ---- Scout gate (BossAI12__Scout, no RNG) ---------------------------------

TEST(BossAI12Test, ScoutClearsTargetWhenIdle) {
    BossAI12 b;
    EXPECT_TRUE(b.HasTarget());
    b.Scout(); // not dizzy, not dead -> clears target_obj(0x7c)
    EXPECT_FALSE(b.HasTarget());
}

TEST(BossAI12Test, ScoutGatedByDizzy) {
    BossAI12 b;
    b.OnDizzy(); // dizzy(0xa1) = 1
    EXPECT_TRUE(b.Dizzy());
    b.Scout(); // gated: dizzy -> no target clear
    EXPECT_TRUE(b.HasTarget());
}

TEST(BossAI12Test, ScoutGatedByDead) {
    BossAI12 b;
    b.SetDead(true);
    b.Scout(); // gated: dead -> no target clear
    EXPECT_TRUE(b.HasTarget());
}

TEST(BossAI12Test, ScoutDrawsNoRng) {
    BossAI12 b;
    b.SetSeed(123);
    RGRandom ref = MakeRef(123);
    b.Scout();
    // Stream untouched: the boss's next float draw equals the reference's first.
    EXPECT_FLOAT_EQ(b.Rng().Range(-1.0F, 1.0F), ref.Range(-1.0F, 1.0F));
}

// ---- RunReflection wander (BossAI12__RunReflection, 2 float draws) ---------

TEST(BossAI12Test, WanderDrawsTwoFloatsInOrder) {
    const int seed = 555;
    BossAI12 b;
    b.SetSeed(seed);
    RGRandom ref = MakeRef(seed);

    const glm::vec2 dir = b.WanderDirection();

    // Reference draws x then y from Range(-1f, 1f) (max inclusive), same order.
    const float rx = ref.Range(BossAI12::kWanderAxisMin, BossAI12::kWanderAxisMax);
    const float ry = ref.Range(BossAI12::kWanderAxisMin, BossAI12::kWanderAxisMax);
    const float len = std::sqrt(rx * rx + ry * ry);
    const float ex = (len > 0.0F) ? rx / len : 0.0F;
    const float ey = (len > 0.0F) ? ry / len : 0.0F;

    EXPECT_FLOAT_EQ(dir.x, ex);
    EXPECT_FLOAT_EQ(dir.y, ey);
}

TEST(BossAI12Test, WanderResultIsUnitOrZero) {
    BossAI12 b;
    b.SetSeed(99);
    for (int i = 0; i < 64; ++i) {
        const glm::vec2 d = b.WanderDirection();
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        // Either a unit vector or the degenerate zero (both axes drew 0).
        EXPECT_TRUE(std::fabs(len - 1.0F) < 1e-4F || len < 1e-6F);
    }
}

TEST(BossAI12Test, WanderConsumesExactlyTwoDraws) {
    const int seed = 4242;
    BossAI12 b;
    b.SetSeed(seed);
    RGRandom ref = MakeRef(seed);

    b.WanderDirection();          // boss: 2 draws
    ref.Range(-1.0F, 1.0F);       // ref:  burn 2 to realign
    ref.Range(-1.0F, 1.0F);

    // Now both streams are at draw #3 -> next int draw must match.
    EXPECT_EQ(b.Rng().Range(0, 100), ref.Range(0, 100));
}

// ---- ShootReflection roll (BossAI12__ShootReflection, gated 1 int draw) ----

TEST(BossAI12Test, ShootRollFiresWhenGateOpen) {
    const int seed = 7;
    BossAI12 b;
    b.SetSeed(seed);
    RGRandom ref = MakeRef(seed);

    int roll = -1;
    const bool fired = b.ShootReflectionRoll(/*canShoot=*/true,
                                             /*dead=*/false,
                                             /*dizzy=*/false, roll);
    EXPECT_TRUE(fired);
    EXPECT_EQ(roll, ref.Range(0, 100)); // single Range(0,100), max exclusive
    EXPECT_GE(roll, 0);
    EXPECT_LT(roll, BossAI12::kRollCeiling);
}

TEST(BossAI12Test, ShootRollGatedByNotCanShoot) {
    const int seed = 7;
    BossAI12 b;
    b.SetSeed(seed);
    RGRandom ref = MakeRef(seed);

    int roll = -1;
    const bool fired =
        b.ShootReflectionRoll(false, false, false, roll); // can_shoot(0x40)==0
    EXPECT_FALSE(fired);
    EXPECT_EQ(roll, -1); // outRoll untouched
    // No draw consumed: stream still aligned with the reference.
    EXPECT_EQ(b.Rng().Range(0, 100), ref.Range(0, 100));
}

TEST(BossAI12Test, ShootRollGatedByDead) {
    const int seed = 7;
    BossAI12 b;
    b.SetSeed(seed);
    RGRandom ref = MakeRef(seed);

    int roll = -1;
    const bool fired =
        b.ShootReflectionRoll(true, true, false, roll); // dead(0x38)!=0
    EXPECT_FALSE(fired);
    EXPECT_EQ(b.Rng().Range(0, 100), ref.Range(0, 100)); // no draw
}

TEST(BossAI12Test, ShootRollGatedByDizzy) {
    const int seed = 7;
    BossAI12 b;
    b.SetSeed(seed);
    RGRandom ref = MakeRef(seed);

    int roll = -1;
    const bool fired =
        b.ShootReflectionRoll(true, false, true, roll); // dizzy(0xa1)!=0
    EXPECT_FALSE(fired);
    EXPECT_EQ(b.Rng().Range(0, 100), ref.Range(0, 100)); // no draw
}

TEST(BossAI12Test, ShootRollDeterministic) {
    BossAI12 a;
    BossAI12 b;
    a.SetSeed(31337);
    b.SetSeed(31337);
    for (int i = 0; i < 32; ++i) {
        int ra = -1;
        int rb = -1;
        EXPECT_TRUE(a.ShootReflectionRoll(true, false, false, ra));
        EXPECT_TRUE(b.ShootReflectionRoll(true, false, false, rb));
        EXPECT_EQ(ra, rb);
    }
}

// ---- GetHurt -> BossAngry (BossAI12__GetHurt/BossAngry, no RNG) ------------

TEST(BossAI12Test, EntersAngryBelowHalfHp) {
    BossAI12 b;
    b.OnHurt(/*awake=*/true, 301, 600); // > 50% -> calm
    EXPECT_FALSE(b.Angry());
    b.OnHurt(true, 299, 600); // < 50% -> angry
    EXPECT_TRUE(b.Angry());
}

TEST(BossAI12Test, ExactlyHalfIsNotAngry) {
    BossAI12 b;
    b.OnHurt(true, 300, 600); // 0.5 is not < 0.5
    EXPECT_FALSE(b.Angry());
}

TEST(BossAI12Test, GetHurtGatedByNotAwake) {
    BossAI12 b;
    b.OnHurt(/*awake=*/false, 1, 600); // gated: !awake -> never angry
    EXPECT_FALSE(b.Angry());
}

TEST(BossAI12Test, GetHurtGatedByDead) {
    BossAI12 b;
    b.SetDead(true);
    b.OnHurt(true, 1, 600); // gated: dead -> never angry
    EXPECT_FALSE(b.Angry());
}

TEST(BossAI12Test, ZeroMaxHpDoesNotEnterAngry) {
    BossAI12 b;
    b.OnHurt(true, 0, 0); // divide-by-zero guard
    EXPECT_FALSE(b.Angry());
}

TEST(BossAI12Test, BossAngryHalvesShootCdOnce) {
    BossAI12 b;
    b.SetShootCd(2.0F);
    b.OnHurt(true, 100, 600); // angry: shoot_cd(0x3c) *= 0.5
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 2.0F * BossAI12::kAngryShootCdScale);

    b.OnHurt(true, 10, 600); // already angry -> no second halving
    EXPECT_FLOAT_EQ(b.ShootCd(), 1.0F);
}

TEST(BossAI12Test, GetHurtDrawsNoRng) {
    BossAI12 b;
    b.SetSeed(2024);
    RGRandom ref = MakeRef(2024);
    b.OnHurt(true, 100, 600); // angry transition draws nothing
    EXPECT_EQ(b.Rng().Range(0, 100), ref.Range(0, 100));
}

// ---- Dizzy (BossAI12__Dizzy, no RNG) --------------------------------------

TEST(BossAI12Test, DizzyLatchesWhenAlive) {
    BossAI12 b;
    b.OnDizzy();
    EXPECT_TRUE(b.Dizzy());
}

TEST(BossAI12Test, DizzyGatedByDead) {
    BossAI12 b;
    b.SetDead(true);
    b.OnDizzy(); // gated: dead -> no dizzy
    EXPECT_FALSE(b.Dizzy());
}

// ---- InAtk01 / InAtk03 angry-gated scalars (no RNG) -----------------------

TEST(BossAI12Test, InAtk01BulletCountCalmVsAngry) {
    BossAI12 b;
    EXPECT_EQ(b.InAtk01BulletCount(), BossAI12::kInAtk01BulletsCalm); // 1
    b.OnHurt(true, 1, 600);
    EXPECT_TRUE(b.Angry());
    EXPECT_EQ(b.InAtk01BulletCount(), BossAI12::kInAtk01BulletsAngry); // 2
}

TEST(BossAI12Test, InAtk03AngleStepCalmVsAngry) {
    BossAI12 b;
    // 180 / 5 = 36 degrees while calm.
    EXPECT_EQ(b.InAtk03AngleStep(), 180 / BossAI12::kInAtk03DivisorCalm);
    EXPECT_EQ(b.InAtk03AngleStep(), 36);
    b.OnHurt(true, 1, 600);
    // 180 / 6 = 30 degrees while angry.
    EXPECT_EQ(b.InAtk03AngleStep(), 180 / BossAI12::kInAtk03DivisorAngry);
    EXPECT_EQ(b.InAtk03AngleStep(), 30);
}

// ---- EndAtk04 cadence relax (no RNG) --------------------------------------

TEST(BossAI12Test, EndAtk04SubtractsOneSecond) {
    BossAI12 b;
    EXPECT_FLOAT_EQ(b.EndAtk04ShootCd(3.0F), 2.0F);
    EXPECT_FLOAT_EQ(b.EndAtk04ShootCd(1.0F), 0.0F);
}

// ---- Full lockstep replay across interleaved draws ------------------------

TEST(BossAI12Test, FullStreamReplayIsDeterministic) {
    // Mix gated + ungated calls and prove the consumed-draw sequence replays
    // bit-for-bit (gated calls consume nothing; ungated consume exactly their
    // documented count).
    auto run = [](int seed) {
        BossAI12 b;
        b.SetSeed(seed);
        std::vector<float> trace;
        for (int i = 0; i < 16; ++i) {
            // gated-out: no draw
            int gatedRoll = -1;
            b.ShootReflectionRoll(false, false, false, gatedRoll);
            // 2 float draws
            const glm::vec2 d = b.WanderDirection();
            trace.push_back(d.x);
            trace.push_back(d.y);
            // 1 int draw (gate open)
            int roll = -1;
            b.ShootReflectionRoll(true, false, false, roll);
            trace.push_back(static_cast<float>(roll));
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

TEST(BossAI12Test, InterleavedDrawsMatchReferenceStream) {
    const int seed = 8675309;
    BossAI12 b;
    b.SetSeed(seed);
    RGRandom ref = MakeRef(seed);

    // Wander (2 floats), then an open ShootRoll (1 int), against the reference.
    const glm::vec2 d = b.WanderDirection();
    const float rx = ref.Range(-1.0F, 1.0F);
    const float ry = ref.Range(-1.0F, 1.0F);
    const float len = std::sqrt(rx * rx + ry * ry);
    EXPECT_FLOAT_EQ(d.x, (len > 0.0F) ? rx / len : 0.0F);
    EXPECT_FLOAT_EQ(d.y, (len > 0.0F) ? ry / len : 0.0F);

    int roll = -1;
    EXPECT_TRUE(b.ShootReflectionRoll(true, false, false, roll));
    EXPECT_EQ(roll, ref.Range(0, 100));
}

// NOLINTEND(readability-magic-numbers)
