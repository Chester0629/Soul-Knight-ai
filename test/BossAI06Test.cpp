#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/BossAI06.hpp"
#include "data/RGRandom.hpp"

using Game::BossAI06;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

namespace {

// A parallel reference stream seeded identically to the boss under test, so we can
// assert the exact draw count + order + values the brain pulls from RGRandom.
RGRandom Ref(int seed) {
    RGRandom r;
    r.SetRandomSeed(seed);
    return r;
}

} // namespace

// ---- angry-phase gate (GetHurt -> BossAngry) --------------------------------

TEST(BossAI06Test, EntersAngryBelowHalfHp) {
    BossAI06 b;
    b.OnHurt(301, 600, /*awake=*/true); // > 50% -> calm
    EXPECT_FALSE(b.Angry());
    b.OnHurt(299, 600, /*awake=*/true); // < 50% -> angry
    EXPECT_TRUE(b.Angry());
}

TEST(BossAI06Test, ExactlyHalfIsNotAngry) {
    BossAI06 b;
    b.OnHurt(300, 600, /*awake=*/true); // 0.5 is not < 0.5
    EXPECT_FALSE(b.Angry());
}

TEST(BossAI06Test, NotAwakeIgnoresHit) {
    BossAI06 b;
    b.OnHurt(1, 600, /*awake=*/false); // 0x18 == 0 -> ignored
    EXPECT_FALSE(b.Angry());
}

TEST(BossAI06Test, DeadIgnoresHit) {
    BossAI06 b;
    b.SetDead(true);
    b.OnHurt(1, 600, /*awake=*/true); // 0x38 != 0 -> ignored
    EXPECT_FALSE(b.Angry());
}

TEST(BossAI06Test, AngryLatchesOnce) {
    BossAI06 b;
    b.OnHurt(100, 600, /*awake=*/true);
    EXPECT_TRUE(b.Angry());
    b.OnHurt(10, 600, /*awake=*/true); // still < 50%, no re-fire
    EXPECT_TRUE(b.Angry());
}

TEST(BossAI06Test, ZeroMaxHpDoesNotEnterAngry) {
    BossAI06 b;
    b.OnHurt(0, 0, /*awake=*/true); // divide-by-zero guard
    EXPECT_FALSE(b.Angry());
}

TEST(BossAI06Test, OnHurtDrawsNoRng) {
    // GetHurt/BossAngry pull nothing from the stream: a roll afterwards must equal
    // the very first reference draw.
    BossAI06 b;
    b.SetSeed(123);
    RGRandom ref = Ref(123);
    b.OnHurt(1, 600, /*awake=*/true);
    EXPECT_TRUE(b.Angry());
    int roll = -1;
    b.SetCanShoot(true);
    ASSERT_TRUE(b.ShootReflectionRoll(roll));
    EXPECT_EQ(roll, ref.Range(0, BossAI06::kRollCeiling));
}

TEST(BossAI06Test, BossAngrySetsLatchOnly) {
    BossAI06 b;
    EXPECT_FALSE(b.Angry());
    b.BossAngry();
    EXPECT_TRUE(b.Angry());
}

// ---- RunReflection wander direction (Range(-1,1) x2) ------------------------

TEST(BossAI06Test, WanderDrawsTwoFloatsInOrder) {
    BossAI06 b;
    b.SetSeed(777);
    RGRandom ref = Ref(777);
    for (int i = 0; i < 32; ++i) {
        const float rx = ref.Range(BossAI06::kWanderMin, BossAI06::kWanderMax);
        const float ry = ref.Range(BossAI06::kWanderMin, BossAI06::kWanderMax);
        const float len = std::sqrt(rx * rx + ry * ry);
        const glm::vec2 expected =
            len > 0.0F ? glm::vec2(rx / len, ry / len) : glm::vec2(0.0F, 0.0F);
        const glm::vec2 got = b.WanderDirection();
        EXPECT_FLOAT_EQ(got.x, expected.x);
        EXPECT_FLOAT_EQ(got.y, expected.y);
    }
}

TEST(BossAI06Test, WanderIsNormalized) {
    BossAI06 b;
    b.SetSeed(99);
    for (int i = 0; i < 64; ++i) {
        const glm::vec2 d = b.WanderDirection();
        const float mag = std::sqrt(d.x * d.x + d.y * d.y);
        // Either a unit vector or the degenerate zero vector (both axes drew 0).
        EXPECT_TRUE(std::fabs(mag - 1.0F) < 1e-4F || mag == 0.0F);
    }
}

// ---- ShootReflection roll gates --------------------------------------------

TEST(BossAI06Test, ShootRollOnlyWhenAllGatesOpen) {
    BossAI06 b;
    b.SetSeed(5);
    b.SetCanShoot(true);
    // dead == false, dizzy == false by default -> gate open.
    int roll = -1;
    RGRandom ref = Ref(5);
    ASSERT_TRUE(b.ShootReflectionRoll(roll));
    EXPECT_EQ(roll, ref.Range(0, BossAI06::kRollCeiling));
    EXPECT_GE(roll, 0);
    EXPECT_LT(roll, BossAI06::kRollCeiling);
}

TEST(BossAI06Test, ShootRollGatedByCanShoot) {
    BossAI06 b;
    b.SetSeed(5);
    b.SetCanShoot(false); // 0x40 == 0 -> no draw
    int roll = -123;
    EXPECT_FALSE(b.ShootReflectionRoll(roll));
    EXPECT_EQ(roll, -123); // untouched

    // The gated-out path consumed no RNG: now open the gate and the first roll must
    // equal the first reference draw.
    b.SetCanShoot(true);
    RGRandom ref = Ref(5);
    ASSERT_TRUE(b.ShootReflectionRoll(roll));
    EXPECT_EQ(roll, ref.Range(0, BossAI06::kRollCeiling));
}

TEST(BossAI06Test, ShootRollGatedByDead) {
    BossAI06 b;
    b.SetSeed(5);
    b.SetCanShoot(true);
    b.SetDead(true); // 0x38 -> no draw
    int roll = 0;
    EXPECT_FALSE(b.ShootReflectionRoll(roll));
}

TEST(BossAI06Test, ShootRollGatedByDizzy) {
    BossAI06 b;
    b.SetSeed(5);
    b.SetCanShoot(true);
    b.SetDizzy(true); // 0xa1 -> no draw
    int roll = 0;
    EXPECT_FALSE(b.ShootReflectionRoll(roll));

    // Recover from dizzy: the gate reopens and the first roll equals the first
    // reference draw (gated-out path consumed nothing).
    b.SetDizzy(false);
    RGRandom ref = Ref(5);
    ASSERT_TRUE(b.ShootReflectionRoll(roll));
    EXPECT_EQ(roll, ref.Range(0, BossAI06::kRollCeiling));
}

// ---- Atk3CreateBullet fan angle (Range(0, 360)) -----------------------------

TEST(BossAI06Test, Atk3AngleMatchesReferenceStream) {
    BossAI06 b;
    b.SetSeed(31415);
    RGRandom ref = Ref(31415);
    for (int i = 0; i < 32; ++i) {
        const int a = b.Atk3CreateBulletAngle();
        EXPECT_EQ(a, ref.Range(0, BossAI06::kAtk3AngleCeiling));
        EXPECT_GE(a, 0);
        EXPECT_LT(a, BossAI06::kAtk3AngleCeiling);
    }
}

// ---- StartAtk03 / EndAtk02 field writes -------------------------------------

TEST(BossAI06Test, StartAtk03SetsAtk3Shooting) {
    BossAI06 b;
    EXPECT_FALSE(b.Atk3Shooting());
    b.StartAtk03(); // 0xf0 = 1
    EXPECT_TRUE(b.Atk3Shooting());
}

TEST(BossAI06Test, StartAtk03DrawsNoRng) {
    BossAI06 b;
    b.SetSeed(8);
    RGRandom ref = Ref(8);
    b.StartAtk03();
    EXPECT_EQ(b.Atk3CreateBulletAngle(), ref.Range(0, BossAI06::kAtk3AngleCeiling));
}

TEST(BossAI06Test, EndAtk02ClearsFlags) {
    BossAI06 b;
    int sink = 0;
    b.SetSeed(1);
    b.InAtk02(1, sink);    // sets atk2_shooting = 1
    EXPECT_TRUE(b.Atk2Shooting());
    b.EndAtk02();          // 0xf1 = 0, 0xf3 = 0
    EXPECT_FALSE(b.Atk2Shooting());
    EXPECT_FALSE(b.BallGroupRotation());
}

TEST(BossAI06Test, EndAtk02DrawsNoRng) {
    BossAI06 b;
    b.SetSeed(42);
    RGRandom ref = Ref(42);
    b.EndAtk02();
    EXPECT_EQ(b.Atk3CreateBulletAngle(), ref.Range(0, BossAI06::kAtk3AngleCeiling));
}

// ---- InAtk02 sub-phase switch (cases 1/2/4 draw; 3/default do not) ----------

TEST(BossAI06Test, InAtk02Case1WritesAndDraws) {
    BossAI06 b;
    b.SetSeed(2024);
    RGRandom ref = Ref(2024);
    int roll = -1;
    ASSERT_TRUE(b.InAtk02(1, roll));
    EXPECT_TRUE(b.Atk2Shooting());                       // 0xf3 = 1
    EXPECT_FLOAT_EQ(b.Atk2InvokeTime(), BossAI06::kAtk2InvokeTime); // 0xec = +1.0
    EXPECT_EQ(b.Atk2Value(), 1);
    EXPECT_EQ(roll, ref.Range(0, BossAI06::kInAtk02RollCeiling));
}

TEST(BossAI06Test, InAtk02Case2WritesNegInvokeAndDraws) {
    BossAI06 b;
    b.SetSeed(2024);
    RGRandom ref = Ref(2024);
    int roll = -1;
    ASSERT_TRUE(b.InAtk02(2, roll));
    EXPECT_FALSE(b.Atk2Shooting());                        // case 2 does not set 0xf3
    EXPECT_FLOAT_EQ(b.Atk2InvokeTime(), -BossAI06::kAtk2InvokeTime); // 0xec = -1.0
    EXPECT_EQ(roll, ref.Range(0, BossAI06::kInAtk02RollCeiling));
}

TEST(BossAI06Test, InAtk02Case3WritesNoDraw) {
    BossAI06 b;
    b.SetSeed(2024);
    RGRandom ref = Ref(2024);
    int roll = -777;
    EXPECT_FALSE(b.InAtk02(3, roll));                      // tail case: no draw
    EXPECT_EQ(roll, -777);                                 // untouched
    EXPECT_TRUE(b.Atk2Shooting());                         // 0xf3 = 1
    EXPECT_FLOAT_EQ(b.Atk2InvokeTime(), -BossAI06::kAtk2InvokeTime); // 0xec = -1.0
    // Stream untouched: the next draw must equal the first reference draw.
    EXPECT_EQ(b.Atk3CreateBulletAngle(),
              ref.Range(0, BossAI06::kAtk3AngleCeiling));
}

TEST(BossAI06Test, InAtk02Case4WritesNegInvokeAndDraws) {
    BossAI06 b;
    b.SetSeed(2024);
    RGRandom ref = Ref(2024);
    int roll = -1;
    ASSERT_TRUE(b.InAtk02(4, roll));
    EXPECT_FLOAT_EQ(b.Atk2InvokeTime(), -BossAI06::kAtk2InvokeTime); // 0xec = -1.0
    EXPECT_EQ(roll, ref.Range(0, BossAI06::kInAtk02RollCeiling));
}

TEST(BossAI06Test, InAtk02DefaultWritesNothingNoDraw) {
    BossAI06 b;
    b.SetSeed(2024);
    RGRandom ref = Ref(2024);
    int roll = -555;
    EXPECT_FALSE(b.InAtk02(0, roll));     // default: nothing
    EXPECT_EQ(roll, -555);
    EXPECT_FALSE(b.Atk2Shooting());
    EXPECT_FLOAT_EQ(b.Atk2InvokeTime(), 0.0F);
    // Stream untouched.
    EXPECT_EQ(b.Atk3CreateBulletAngle(),
              ref.Range(0, BossAI06::kAtk3AngleCeiling));
}

// ---- full interleaved replay (lockstep across the whole draw set) -----------

TEST(BossAI06Test, FullStreamReplayIsDeterministic) {
    // Interleave every drawing path; replay -> identical sequence + identical draw
    // count (any extra/missing/reordered draw would diverge).
    auto run = [](int seed) -> std::vector<float> {
        BossAI06 b;
        b.SetSeed(seed);
        b.SetCanShoot(true);
        std::vector<float> trace;
        int roll = 0;
        for (int i = 0; i < 16; ++i) {
            const glm::vec2 d = b.WanderDirection(); // 2 float draws
            trace.push_back(d.x);
            trace.push_back(d.y);
            EXPECT_TRUE(b.ShootReflectionRoll(roll)); // 1 int draw
            trace.push_back(static_cast<float>(roll));
            trace.push_back(static_cast<float>(b.Atk3CreateBulletAngle())); // 1 int
            // case 3 / default draw nothing -> must not perturb the stream
            int sink = 0;
            EXPECT_FALSE(b.InAtk02(3, sink));
            EXPECT_FALSE(b.InAtk02(0, sink));
            EXPECT_TRUE(b.InAtk02(1, roll)); // 1 int draw
            trace.push_back(static_cast<float>(roll));
        }
        return trace;
    };
    const auto a = run(2718);
    const auto c = run(2718);
    ASSERT_EQ(a.size(), c.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_FLOAT_EQ(a[i], c[i]);
    }
}

TEST(BossAI06Test, FullStreamMatchesParallelReference) {
    // Exact draw order against an independent same-seeded RGRandom: Wander(x,y),
    // ShootReflectionRoll, Atk3 angle, [case3 no draw], InAtk02(1).
    BossAI06 b;
    b.SetSeed(909);
    b.SetCanShoot(true);
    RGRandom ref = Ref(909);
    int roll = 0;
    int sink = 0;
    for (int i = 0; i < 12; ++i) {
        const glm::vec2 d = b.WanderDirection();
        const float rx = ref.Range(BossAI06::kWanderMin, BossAI06::kWanderMax);
        const float ry = ref.Range(BossAI06::kWanderMin, BossAI06::kWanderMax);
        const float len = std::sqrt(rx * rx + ry * ry);
        const glm::vec2 exp =
            len > 0.0F ? glm::vec2(rx / len, ry / len) : glm::vec2(0.0F, 0.0F);
        EXPECT_FLOAT_EQ(d.x, exp.x);
        EXPECT_FLOAT_EQ(d.y, exp.y);

        ASSERT_TRUE(b.ShootReflectionRoll(roll));
        EXPECT_EQ(roll, ref.Range(0, BossAI06::kRollCeiling));

        EXPECT_EQ(b.Atk3CreateBulletAngle(),
                  ref.Range(0, BossAI06::kAtk3AngleCeiling));

        EXPECT_FALSE(b.InAtk02(3, sink)); // no draw on ref either

        ASSERT_TRUE(b.InAtk02(1, roll));
        EXPECT_EQ(roll, ref.Range(0, BossAI06::kInAtk02RollCeiling));
    }
}

// NOLINTEND(readability-magic-numbers)
