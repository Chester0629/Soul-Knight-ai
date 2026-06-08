#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <vector>

#include "combat/BossAI11.hpp"
#include "data/RGRandom.hpp"

using Game::BossAI11;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

namespace {

// A parallel, same-seeded reference stream so every test can assert the EXACT
// draw count + order BossAI11 takes (stream lockstep with the original decomp).
RGRandom RefStream(int seed) {
    RGRandom r;
    r.SetRandomSeed(seed);
    return r;
}

} // namespace

// ---- FixedUpdate: movement gate + the single field write -------------------

TEST(BossAI11Test, FixedUpdateGatedWhenNotAwake) {
    BossAI11 b;
    // awake = false -> the whole body is skipped.
    EXPECT_FALSE(b.FixedUpdate());
    EXPECT_FALSE(b.Awake());
}

TEST(BossAI11Test, FixedUpdateGatedWhileShooting) {
    BossAI11 b;
    b.SetAwake(true);
    b.SetShooting(true);
    // awake && shooting -> move block skipped, no field write.
    EXPECT_FALSE(b.FixedUpdate());
    EXPECT_TRUE(b.Awake()); // not cleared
}

TEST(BossAI11Test, FixedUpdateClearsAwakeWhenDead) {
    BossAI11 b;
    b.SetAwake(true);
    b.SetShooting(false);
    b.SetDead(true);
    // awake && !shooting && dead -> the one decomp field write: awake = false.
    EXPECT_TRUE(b.FixedUpdate());
    EXPECT_FALSE(b.Awake());
}

TEST(BossAI11Test, FixedUpdateMovePathWhenAliveAndNotShooting) {
    BossAI11 b;
    b.SetAwake(true);
    b.SetShooting(false);
    b.SetDead(false);
    EXPECT_TRUE(b.FixedUpdate()); // move path runs
    EXPECT_TRUE(b.Awake());       // alive: awake untouched
}

// ---- Scout: target-clear gate ----------------------------------------------

TEST(BossAI11Test, ScoutClearsTargetWhenAliveAndNotDizzy) {
    BossAI11 b;
    b.SetTarget(true);
    EXPECT_TRUE(b.Scout());
    EXPECT_FALSE(b.HasTarget()); // target_obj = null
}

TEST(BossAI11Test, ScoutGatedWhenDizzy) {
    BossAI11 b;
    b.SetTarget(true);
    EXPECT_TRUE(b.MakeDizzy());
    EXPECT_FALSE(b.Scout());     // gate closed
    EXPECT_TRUE(b.HasTarget());  // target untouched
}

TEST(BossAI11Test, ScoutGatedWhenDead) {
    BossAI11 b;
    b.SetTarget(true);
    b.SetDead(true);
    EXPECT_FALSE(b.Scout());
    EXPECT_TRUE(b.HasTarget());
}

// ---- RunReflection: two Range(-1,1) draws, in order ------------------------

TEST(BossAI11Test, WanderDrawsTwoFloatsInOrder) {
    const int seed = 1234;
    BossAI11 b;
    b.SetSeed(seed);
    RGRandom ref = RefStream(seed);

    const glm::vec2 dir = b.WanderDirection();
    // Reference: exactly two Range(-1,1) draws, x then y.
    const float rx = ref.Range(BossAI11::kWanderMin, BossAI11::kWanderMax);
    const float ry = ref.Range(BossAI11::kWanderMin, BossAI11::kWanderMax);
    const float len = std::sqrt(rx * rx + ry * ry);
    const glm::vec2 expected =
        len > 0.0F ? glm::vec2(rx / len, ry / len) : glm::vec2(0.0F, 0.0F);

    EXPECT_FLOAT_EQ(dir.x, expected.x);
    EXPECT_FLOAT_EQ(dir.y, expected.y);
    // Brain and reference consumed the same number of draws: a subsequent draw
    // from each must match (proves count == 2, no extra/missing draw).
    EXPECT_EQ(b.Rng().Range(0, 1000000), ref.Range(0, 1000000));
}

TEST(BossAI11Test, WanderDirectionIsUnitOrZero) {
    BossAI11 b;
    b.SetSeed(99);
    for (int i = 0; i < 64; ++i) {
        const glm::vec2 d = b.WanderDirection();
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        // Normalized: length 1, or exactly 0 in the degenerate both-zero case.
        EXPECT_TRUE(std::fabs(len - 1.0F) < 1e-4F || len == 0.0F);
    }
}

TEST(BossAI11Test, WanderIsDeterministic) {
    BossAI11 a;
    BossAI11 b;
    a.SetSeed(31337);
    b.SetSeed(31337);
    for (int i = 0; i < 32; ++i) {
        const glm::vec2 da = a.WanderDirection();
        const glm::vec2 db = b.WanderDirection();
        EXPECT_FLOAT_EQ(da.x, db.x);
        EXPECT_FLOAT_EQ(da.y, db.y);
    }
}

// ---- ShootReflection: gated single Range(0,100) draw -----------------------

TEST(BossAI11Test, ShootReflectionDrawsWhenGateOpen) {
    const int seed = 555;
    BossAI11 b;
    b.SetSeed(seed);
    b.SetCanShoot(true); // gate open: can_shoot && !dead && !dizzy
    RGRandom ref = RefStream(seed);

    const int roll = b.ShootReflection();
    const int expected = ref.Range(0, BossAI11::kRollCeiling);
    EXPECT_EQ(roll, expected);
    EXPECT_GE(roll, 0);
    EXPECT_LT(roll, BossAI11::kRollCeiling);
    // Same draw count: next draw from both streams matches.
    EXPECT_EQ(b.Rng().Range(0, 1000000), ref.Range(0, 1000000));
}

TEST(BossAI11Test, ShootReflectionNoDrawWhenCannotShoot) {
    const int seed = 777;
    BossAI11 b;
    b.SetSeed(seed);
    b.SetCanShoot(false); // gate closed at can_shoot
    RGRandom ref = RefStream(seed);

    EXPECT_EQ(b.ShootReflection(), -1); // sentinel: no draw
    // Stream untouched: brain's next draw equals reference's FIRST draw.
    EXPECT_EQ(b.Rng().Range(0, 1000000), ref.Range(0, 1000000));
}

TEST(BossAI11Test, ShootReflectionNoDrawWhenDead) {
    const int seed = 888;
    BossAI11 b;
    b.SetSeed(seed);
    b.SetCanShoot(true);
    b.SetDead(true); // gate closed at dead
    RGRandom ref = RefStream(seed);

    EXPECT_EQ(b.ShootReflection(), -1);
    EXPECT_EQ(b.Rng().Range(0, 1000000), ref.Range(0, 1000000)); // no draw taken
}

TEST(BossAI11Test, ShootReflectionNoDrawWhenDizzy) {
    const int seed = 909;
    BossAI11 b;
    b.SetSeed(seed);
    b.SetCanShoot(true);
    EXPECT_TRUE(b.MakeDizzy()); // gate closed at dizzy
    RGRandom ref = RefStream(seed);

    EXPECT_EQ(b.ShootReflection(), -1);
    EXPECT_EQ(b.Rng().Range(0, 1000000), ref.Range(0, 1000000)); // no draw taken
}

// ---- GetHurt -> BossAngry (hp/max_hp < 0.5, once) --------------------------

TEST(BossAI11Test, GetHurtIgnoredWhenNotAwake) {
    BossAI11 b;
    b.SetShootCd(2.0F);
    // awake == false -> hit ignored, no angry, shoot_cd untouched.
    b.OnHurt(1, 600);
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 2.0F);
}

TEST(BossAI11Test, GetHurtIgnoredWhenDead) {
    BossAI11 b;
    b.SetAwake(true);
    b.SetDead(true);
    b.SetShootCd(2.0F);
    b.OnHurt(1, 600);
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 2.0F);
}

TEST(BossAI11Test, GetHurtEntersAngryBelowHalf) {
    BossAI11 b;
    b.SetAwake(true);
    b.SetShootCd(2.0F);
    b.OnHurt(301, 600); // > 50% -> calm
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 2.0F);

    b.OnHurt(299, 600); // < 50% -> angry: shoot_cd *= 0.5
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 1.0F);
    EXPECT_FLOAT_EQ(b.AnimSpeed(), BossAI11::kAngryAnimSpeed);
}

TEST(BossAI11Test, GetHurtExactlyHalfIsNotAngry) {
    BossAI11 b;
    b.SetAwake(true);
    b.SetShootCd(2.0F);
    b.OnHurt(300, 600); // 0.5 is not < 0.5
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 2.0F);
}

TEST(BossAI11Test, BossAngryHappensOnce) {
    BossAI11 b;
    b.SetAwake(true);
    b.SetShootCd(2.0F);
    b.OnHurt(100, 600); // angry: 2.0 * 0.5 = 1.0
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 1.0F);
    b.OnHurt(10, 600);  // still < 50% but already angry -> no second halving
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 1.0F);
}

TEST(BossAI11Test, GetHurtZeroMaxHpDoesNotEnterAngry) {
    BossAI11 b;
    b.SetAwake(true);
    b.SetShootCd(2.0F);
    b.OnHurt(0, 0); // divide guard
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 2.0F);
}

TEST(BossAI11Test, GetHurtTakesNoRngDraw) {
    const int seed = 4242;
    BossAI11 b;
    b.SetSeed(seed);
    b.SetAwake(true);
    b.SetShootCd(2.0F);
    RGRandom ref = RefStream(seed);
    b.OnHurt(1, 600); // angry transition: must NOT touch the stream
    EXPECT_TRUE(b.Angry());
    EXPECT_EQ(b.Rng().Range(0, 1000000), ref.Range(0, 1000000));
}

// ---- InAtk03 / IntAtkIn / Dizzy field writes -------------------------------

TEST(BossAI11Test, InAtk03SetsWeaponLockTarget) {
    BossAI11 b;
    EXPECT_FALSE(b.WeaponLockTarget());
    b.InAtk03();
    EXPECT_TRUE(b.WeaponLockTarget()); // weapon_lock_target(0x1c) = 1
}

TEST(BossAI11Test, InAtk03TakesNoRngDraw) {
    const int seed = 13;
    BossAI11 b;
    b.SetSeed(seed);
    RGRandom ref = RefStream(seed);
    b.InAtk03();
    EXPECT_EQ(b.Rng().Range(0, 1000000), ref.Range(0, 1000000));
}

TEST(BossAI11Test, IntAtkInGatedOnAngry) {
    BossAI11 b;
    b.SetAwake(true);
    b.SetShootCd(2.0F);
    EXPECT_FALSE(b.IntAtkIn()); // calm: extra-spawn branch not taken
    b.OnHurt(1, 600);           // -> angry
    EXPECT_TRUE(b.IntAtkIn());  // angry: extra-spawn branch taken
}

TEST(BossAI11Test, DizzyLatchesWhenAlive) {
    BossAI11 b;
    EXPECT_FALSE(b.Dizzy());
    EXPECT_TRUE(b.MakeDizzy());
    EXPECT_TRUE(b.Dizzy()); // dizzy(0xa1) = 1
}

TEST(BossAI11Test, DizzyGatedWhenDead) {
    BossAI11 b;
    b.SetDead(true);
    EXPECT_FALSE(b.MakeDizzy()); // !dead gate closed
    EXPECT_FALSE(b.Dizzy());
}

// ---- Full interleaved replay (lockstep determinism) ------------------------

TEST(BossAI11Test, FullStreamReplayIsDeterministic) {
    // Interleave the only two RNG-consuming bodies (RunReflection 2x float,
    // ShootReflection 1x int) and replay -> identical sequence (lockstep).
    auto run = [](int seed) {
        BossAI11 b;
        b.SetSeed(seed);
        b.SetCanShoot(true);
        std::vector<float> trace;
        for (int i = 0; i < 24; ++i) {
            const glm::vec2 d = b.WanderDirection();
            trace.push_back(d.x);
            trace.push_back(d.y);
            trace.push_back(static_cast<float>(b.ShootReflection()));
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

TEST(BossAI11Test, ReplayMatchesParallelReferenceStream) {
    // Drive the brain through its RNG bodies and a same-seeded raw RGRandom in
    // exact lockstep: every draw count + order must line up.
    const int seed = 6060;
    BossAI11 b;
    b.SetSeed(seed);
    b.SetCanShoot(true);
    RGRandom ref = RefStream(seed);
    for (int i = 0; i < 16; ++i) {
        b.WanderDirection();
        ref.Range(BossAI11::kWanderMin, BossAI11::kWanderMax); // x
        ref.Range(BossAI11::kWanderMin, BossAI11::kWanderMax); // y
        b.ShootReflection();
        ref.Range(0, BossAI11::kRollCeiling);
    }
    // After identical draw sequences the streams are byte-aligned.
    EXPECT_EQ(b.Rng().Range(0, 1000000), ref.Range(0, 1000000));
}

// NOLINTEND(readability-magic-numbers)
