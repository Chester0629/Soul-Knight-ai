#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/BossAI08.hpp"

using Game::BossAI08;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

namespace {

// A same-seeded reference RGRandom drawn in lockstep with the brain, used to
// prove draw COUNT and ORDER match the decomp exactly.
RGRandom Ref(int seed) {
    RGRandom r;
    r.SetRandomSeed(seed);
    return r;
}

} // namespace

// ---- Scout gate (no RNG on any path) ---------------------------------------

TEST(BossAI08Test, ScoutClearsTargetWhenAliveAndNotDizzy) {
    BossAI08 b;
    b.SetSeed(1);
    EXPECT_TRUE(b.Scout(/*dead=*/false, /*dizzy=*/false));
    EXPECT_TRUE(b.TargetCleared());
}

TEST(BossAI08Test, ScoutGatedWhenDead) {
    BossAI08 b;
    b.SetSeed(1);
    EXPECT_FALSE(b.Scout(/*dead=*/true, /*dizzy=*/false));
    EXPECT_FALSE(b.TargetCleared());
}

TEST(BossAI08Test, ScoutGatedWhenDizzy) {
    BossAI08 b;
    b.SetSeed(1);
    EXPECT_FALSE(b.Scout(/*dead=*/false, /*dizzy=*/true));
    EXPECT_FALSE(b.TargetCleared());
}

TEST(BossAI08Test, ScoutDeadTakesPriorityOverDizzy) {
    // Decomp: dizzy is only read when dead==0; either latch gates the clear.
    BossAI08 b;
    b.SetSeed(1);
    EXPECT_FALSE(b.Scout(/*dead=*/true, /*dizzy=*/true));
    EXPECT_FALSE(b.TargetCleared());
}

TEST(BossAI08Test, ScoutDrawsNoRng) {
    // Scout (any gate result) must not advance the deterministic stream.
    BossAI08 b;
    b.SetSeed(7777);
    RGRandom ref = Ref(7777);
    b.Scout(false, false);
    b.Scout(true, false);
    b.Scout(false, true);
    // The very next draw must equal the reference's FIRST draw (stream untouched).
    EXPECT_EQ(b.ChooseAttack(true, false, false), ref.Range(0, 100));
}

// ---- RunReflection: exactly two Range(-1,1) float draws, in order ----------

TEST(BossAI08Test, WanderConsumesTwoFloatDrawsInOrder) {
    BossAI08 b;
    b.SetSeed(2024);
    RGRandom ref = Ref(2024);
    const float rx = ref.Range(-1.0F, 1.0F);
    const float ry = ref.Range(-1.0F, 1.0F);
    const float len = std::sqrt(rx * rx + ry * ry);
    const glm::vec2 expected =
        len > 0.0F ? glm::vec2(rx / len, ry / len) : glm::vec2(0.0F, 0.0F);

    const glm::vec2 got = b.WanderDirection();
    EXPECT_FLOAT_EQ(got.x, expected.x);
    EXPECT_FLOAT_EQ(got.y, expected.y);

    // After exactly two draws, brain and ref must remain in lockstep.
    EXPECT_FLOAT_EQ(b.Rng().Range(-1.0F, 1.0F), ref.Range(-1.0F, 1.0F));
}

TEST(BossAI08Test, WanderDirectionIsUnitLengthOrZero) {
    BossAI08 b;
    b.SetSeed(55);
    for (int i = 0; i < 64; ++i) {
        const glm::vec2 d = b.WanderDirection();
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        const bool unit = std::fabs(len - 1.0F) < 1e-4F;
        const bool zero = len < 1e-6F;
        EXPECT_TRUE(unit || zero);
    }
}

TEST(BossAI08Test, WanderIsDeterministic) {
    BossAI08 a;
    BossAI08 b;
    a.SetSeed(31337);
    b.SetSeed(31337);
    for (int i = 0; i < 32; ++i) {
        const glm::vec2 da = a.WanderDirection();
        const glm::vec2 db = b.WanderDirection();
        EXPECT_FLOAT_EQ(da.x, db.x);
        EXPECT_FLOAT_EQ(da.y, db.y);
    }
}

// ---- ShootReflection: single Range(0,100), gated (gated path = no draw) -----

TEST(BossAI08Test, ChooseAttackDrawsWhenGateOpen) {
    BossAI08 b;
    b.SetSeed(909);
    RGRandom ref = Ref(909);
    const int roll = b.ChooseAttack(/*canShoot=*/true, /*dead=*/false,
                                    /*dizzy=*/false);
    EXPECT_EQ(roll, ref.Range(0, 100)); // exact same draw
    EXPECT_GE(roll, 0);
    EXPECT_LT(roll, BossAI08::kRollCeiling); // Range int is max-exclusive
}

TEST(BossAI08Test, ChooseAttackGatedByCanShootTakesNoDraw) {
    BossAI08 b;
    b.SetSeed(4242);
    RGRandom ref = Ref(4242);
    EXPECT_EQ(b.ChooseAttack(/*canShoot=*/false, false, false), -1);
    // Stream untouched: next open roll equals ref's FIRST draw.
    EXPECT_EQ(b.ChooseAttack(true, false, false), ref.Range(0, 100));
}

TEST(BossAI08Test, ChooseAttackGatedByDeadTakesNoDraw) {
    BossAI08 b;
    b.SetSeed(4242);
    RGRandom ref = Ref(4242);
    EXPECT_EQ(b.ChooseAttack(/*canShoot=*/true, /*dead=*/true, false), -1);
    EXPECT_EQ(b.ChooseAttack(true, false, false), ref.Range(0, 100));
}

TEST(BossAI08Test, ChooseAttackGatedByDizzyTakesNoDraw) {
    BossAI08 b;
    b.SetSeed(4242);
    RGRandom ref = Ref(4242);
    EXPECT_EQ(b.ChooseAttack(/*canShoot=*/true, false, /*dizzy=*/true), -1);
    EXPECT_EQ(b.ChooseAttack(true, false, false), ref.Range(0, 100));
}

TEST(BossAI08Test, ChooseAttackUnseededTakesNoDraw) {
    // rg_random == null in the decomp -> no draw (rg_random(0xc) != 0 gate).
    BossAI08 b; // not seeded
    EXPECT_FALSE(b.Seeded());
    EXPECT_EQ(b.ChooseAttack(true, false, false), -1);
}

TEST(BossAI08Test, ChooseAttackIsDeterministic) {
    BossAI08 a;
    BossAI08 b;
    a.SetSeed(13);
    b.SetSeed(13);
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(a.ChooseAttack(true, false, false),
                  b.ChooseAttack(true, false, false));
    }
}

// ---- BossAngry: hp/max_hp < 0.5 once; angry + shoot_cd *= 0.5 --------------

TEST(BossAI08Test, EntersAngryBelowHalfHp) {
    BossAI08 b;
    b.SetShootCd(2.0F);
    b.OnHurt(/*awake=*/true, /*dead=*/false, 301, 600); // > 50% -> calm
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 2.0F);

    b.OnHurt(true, false, 299, 600); // < 50% -> angry, shoot_cd halved
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 1.0F);
}

TEST(BossAI08Test, ExactlyHalfIsNotAngry) {
    BossAI08 b;
    b.SetShootCd(2.0F);
    b.OnHurt(true, false, 300, 600); // 0.5 is not < 0.5
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 2.0F);
}

TEST(BossAI08Test, AngryTransitionHappensOnce) {
    BossAI08 b;
    b.SetShootCd(2.0F);
    b.OnHurt(true, false, 100, 600); // angry, shoot_cd -> 1.0
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 1.0F);
    b.OnHurt(true, false, 10, 600); // already angry: no second halving
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 1.0F);
}

TEST(BossAI08Test, HurtBeforeAwakeIsIgnored) {
    BossAI08 b;
    b.SetShootCd(2.0F);
    b.OnHurt(/*awake=*/false, false, 1, 600); // awake(0x18) gate
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 2.0F);
}

TEST(BossAI08Test, HurtWhenDeadIsIgnored) {
    BossAI08 b;
    b.SetShootCd(2.0F);
    b.OnHurt(true, /*dead=*/true, 1, 600); // dead(0x38) gate
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 2.0F);
}

TEST(BossAI08Test, ZeroMaxHpDoesNotEnterAngry) {
    BossAI08 b;
    b.SetShootCd(2.0F);
    b.OnHurt(true, false, 0, 0); // guard against divide-by-zero
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCd(), 2.0F);
}

TEST(BossAI08Test, OnHurtDrawsNoRng) {
    BossAI08 b;
    b.SetSeed(321);
    b.SetShootCd(2.0F);
    RGRandom ref = Ref(321);
    b.OnHurt(true, false, 1, 600); // angry path
    EXPECT_TRUE(b.Angry());
    EXPECT_EQ(b.ChooseAttack(true, false, false), ref.Range(0, 100));
}

// ---- Weapon-lock state writes (no RNG) -------------------------------------

TEST(BossAI08Test, StartAtk02SetsWeaponLock) {
    BossAI08 b;
    EXPECT_FALSE(b.WeaponLockTarget());
    b.StartAtk02();
    EXPECT_TRUE(b.WeaponLockTarget()); // weapon_lock_target(0x1c) = 1
}

TEST(BossAI08Test, TrunWeaponLockClearsWeaponLock) {
    BossAI08 b;
    b.StartAtk02();
    EXPECT_TRUE(b.WeaponLockTarget());
    b.TrunWeaponLock();
    EXPECT_FALSE(b.WeaponLockTarget()); // weapon_lock_target(0x1c) = 0
}

TEST(BossAI08Test, WeaponLockWritesDrawNoRng) {
    BossAI08 b;
    b.SetSeed(99);
    RGRandom ref = Ref(99);
    b.StartAtk02();
    b.TrunWeaponLock();
    EXPECT_EQ(b.ChooseAttack(true, false, false), ref.Range(0, 100));
}

// ---- Golden scalars re-derived from the decomp -----------------------------

TEST(BossAI08Test, GoldenScalars) {
    EXPECT_FLOAT_EQ(BossAI08::kAngryHpFraction, 0.5F);   // hp/max_hp < 0.5
    EXPECT_FLOAT_EQ(BossAI08::kAngryShootCdScale, 0.5F); // shoot_cd *= 0.5
    EXPECT_FLOAT_EQ(BossAI08::kAngryAnimSpeed, 1.2F);    // 0x3f99999a
    EXPECT_EQ(BossAI08::kRollCeiling, 100);              // Range(0,100)
    EXPECT_FLOAT_EQ(BossAI08::kWanderMin, -1.0F);        // 0xbf800000
    EXPECT_FLOAT_EQ(BossAI08::kWanderMax, 1.0F);         // 0x3f800000
}

// ---- Full interleaved stream replay (lockstep across all draw points) ------

TEST(BossAI08Test, FullStreamReplayIsDeterministic) {
    auto run = [](int seed) {
        BossAI08 b;
        b.SetSeed(seed);
        b.SetShootCd(2.0F);
        std::vector<float> trace;
        for (int i = 0; i < 16; ++i) {
            const glm::vec2 d = b.WanderDirection(); // 2 float draws
            trace.push_back(d.x);
            trace.push_back(d.y);
            // open gate -> 1 int draw; gated calls in between take none.
            trace.push_back(
                static_cast<float>(b.ChooseAttack(false, false, false))); // gated
            trace.push_back(
                static_cast<float>(b.ChooseAttack(true, false, false)));  // draw
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

// Cross-check the interleaved sequence against an independent reference stream:
// proves the EXACT draw count+order (2 floats then 1 int per iteration, gated
// calls consuming nothing).
TEST(BossAI08Test, InterleavedDrawOrderMatchesReferenceStream) {
    BossAI08 b;
    b.SetSeed(8080);
    RGRandom ref = Ref(8080);
    for (int i = 0; i < 16; ++i) {
        const float ex = ref.Range(-1.0F, 1.0F);
        const float ey = ref.Range(-1.0F, 1.0F);
        const float len = std::sqrt(ex * ex + ey * ey);
        const glm::vec2 expected =
            len > 0.0F ? glm::vec2(ex / len, ey / len) : glm::vec2(0.0F, 0.0F);
        const glm::vec2 got = b.WanderDirection();
        EXPECT_FLOAT_EQ(got.x, expected.x);
        EXPECT_FLOAT_EQ(got.y, expected.y);

        EXPECT_EQ(b.ChooseAttack(true, false, true), -1); // gated: no draw
        EXPECT_EQ(b.ChooseAttack(true, false, false), ref.Range(0, 100));
    }
}

// NOLINTEND(readability-magic-numbers)
