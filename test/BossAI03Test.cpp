#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/BossAI03.hpp"

using Game::BossAI03;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

namespace {

// Parallel reference RGRandom seeded identically to the boss-under-test. Used to
// prove the boss draws the EXACT same values in the EXACT same order/count as a
// hand-rolled reference stream (RNG-determinism lockstep).
RGRandom RefStream(int seed) {
    RGRandom r;
    r.SetRandomSeed(seed);
    return r;
}

} // namespace

// ---- BossAngry (hp/max_hp < 0.5, once) -------------------------------------

TEST(BossAI03Test, EntersAngryBelowHalfHp) {
    BossAI03 b;
    b.OnHurt(301, 600); // > 50% -> still calm
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.AnimSpeed(), 1.0F);

    b.OnHurt(299, 600); // < 50% -> angry
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.AnimSpeed(), BossAI03::kAngryAnimSpeed); // set_speed(1.2)
}

TEST(BossAI03Test, ExactlyHalfIsNotAngry) {
    BossAI03 b;
    b.OnHurt(300, 600); // 0.5 is not < 0.5
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.AnimSpeed(), 1.0F);
}

TEST(BossAI03Test, AngryTransitionHappensOnce) {
    BossAI03 b;
    b.OnHurt(100, 600);
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.AnimSpeed(), BossAI03::kAngryAnimSpeed);
    b.OnHurt(10, 600); // still below 50%, no re-fire
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.AnimSpeed(), BossAI03::kAngryAnimSpeed);
}

TEST(BossAI03Test, ZeroMaxHpDoesNotEnterAngry) {
    BossAI03 b;
    b.OnHurt(0, 0); // guard against divide-by-zero
    EXPECT_FALSE(b.Angry());
}

TEST(BossAI03Test, OnHurtTakesNoDraw) {
    // BossAngry performs no RNG draw: a same-seeded follow-up roll must be
    // unchanged whether or not OnHurt fired.
    BossAI03 quiet;
    BossAI03 hurt;
    quiet.SetSeed(909);
    hurt.SetSeed(909);
    hurt.OnHurt(1, 600);
    EXPECT_TRUE(hurt.Angry());
    RGRandom ref = RefStream(909);
    // EndAtk01 always draws once; both must equal the reference's first draw.
    EXPECT_EQ(quiet.EndAtk01(), ref.Range(0, BossAI03::kRollCeiling));
    RGRandom ref2 = RefStream(909);
    EXPECT_EQ(hurt.EndAtk01(), ref2.Range(0, BossAI03::kRollCeiling));
}

// ---- Dizzy / ChildDead gates (no draw) -------------------------------------

TEST(BossAI03Test, DizzyLatchesOnlyWhenAlive) {
    BossAI03 b;
    EXPECT_TRUE(b.DizzyHit()); // alive -> latch
    EXPECT_TRUE(b.Dizzy());

    BossAI03 dead;
    dead.SetDead(true);
    EXPECT_FALSE(dead.DizzyHit()); // dead -> gated out
    EXPECT_FALSE(dead.Dizzy());
}

TEST(BossAI03Test, DizzyTakesNoDraw) {
    BossAI03 b;
    b.SetSeed(13);
    b.DizzyHit();
    RGRandom ref = RefStream(13);
    // Dizzy drew nothing, so WanderDirection's two draws are still the first two.
    const glm::vec2 w = b.WanderDirection();
    const float rx = ref.Range(BossAI03::kWanderMin, BossAI03::kWanderMax);
    const float ry = ref.Range(BossAI03::kWanderMin, BossAI03::kWanderMax);
    const float len = std::sqrt(rx * rx + ry * ry);
    EXPECT_FLOAT_EQ(w.x, len > 0.0F ? rx / len : 0.0F);
    EXPECT_FLOAT_EQ(w.y, len > 0.0F ? ry / len : 0.0F);
}

TEST(BossAI03Test, ChildDeadClearsShootingNoDraw) {
    BossAI03 b;
    b.SetSeed(55);
    b.StartAtk02();
    EXPECT_TRUE(b.Shooting());
    b.ChildDead();
    EXPECT_FALSE(b.Shooting()); // shooting(0x80) = 0
    // No draw: EndAtk01's roll still equals the reference's first draw.
    RGRandom ref = RefStream(55);
    EXPECT_EQ(b.EndAtk01(), ref.Range(0, BossAI03::kRollCeiling));
}

// ---- RunReflection: two Range(-1,1) float draws, in order ------------------

TEST(BossAI03Test, WanderDrawsTwoFloatsInOrder) {
    BossAI03 b;
    b.SetSeed(2024);
    RGRandom ref = RefStream(2024);
    const glm::vec2 w = b.WanderDirection();
    const float rx = ref.Range(BossAI03::kWanderMin, BossAI03::kWanderMax);
    const float ry = ref.Range(BossAI03::kWanderMin, BossAI03::kWanderMax);
    const float len = std::sqrt(rx * rx + ry * ry);
    ASSERT_GT(len, 0.0F);
    EXPECT_FLOAT_EQ(w.x, rx / len);
    EXPECT_FLOAT_EQ(w.y, ry / len);
    // Unit length (normalized).
    EXPECT_NEAR(std::sqrt(w.x * w.x + w.y * w.y), 1.0F, 1e-5F);
}

TEST(BossAI03Test, WanderIsDeterministic) {
    BossAI03 a;
    BossAI03 b;
    a.SetSeed(777);
    b.SetSeed(777);
    for (int i = 0; i < 32; ++i) {
        const glm::vec2 wa = a.WanderDirection();
        const glm::vec2 wb = b.WanderDirection();
        EXPECT_FLOAT_EQ(wa.x, wb.x);
        EXPECT_FLOAT_EQ(wa.y, wb.y);
    }
}

// ---- ShootReflection: GATED single Range(0,100) draw -----------------------

TEST(BossAI03Test, ChooseAttackGatedWhenCannotShoot) {
    BossAI03 b;
    b.SetSeed(404);
    // can_shoot defaults to false -> gated out, NO draw, returns kNoAttack.
    EXPECT_EQ(b.ChooseAttack(), BossAI03::kNoAttack);
    EXPECT_EQ(b.AtkIndex(), BossAI03::kNoAttack);
    // Stream untouched: first real draw equals reference's first draw.
    RGRandom ref = RefStream(404);
    EXPECT_EQ(b.EndAtk01(), ref.Range(0, BossAI03::kRollCeiling));
}

TEST(BossAI03Test, ChooseAttackGatedWhenDead) {
    BossAI03 b;
    b.SetSeed(101);
    b.EndAtk01();      // can_shoot -> true (consumes one ref draw)
    b.SetDead(true);   // dead gate
    RGRandom ref = RefStream(101);
    ref.Range(0, BossAI03::kRollCeiling); // mirror EndAtk01's draw
    EXPECT_EQ(b.ChooseAttack(), BossAI03::kNoAttack); // gated -> no draw
    // Next genuine draw still matches the reference (stream not advanced).
    EXPECT_EQ(b.EndAtk01(), ref.Range(0, BossAI03::kRollCeiling));
}

TEST(BossAI03Test, ChooseAttackGatedWhenDizzy) {
    BossAI03 b;
    b.SetSeed(220);
    b.EndAtk01();    // arm can_shoot
    b.DizzyHit();    // dizzy gate
    RGRandom ref = RefStream(220);
    ref.Range(0, BossAI03::kRollCeiling); // mirror EndAtk01
    EXPECT_EQ(b.ChooseAttack(), BossAI03::kNoAttack); // gated -> no draw
    EXPECT_EQ(b.EndAtk01(), ref.Range(0, BossAI03::kRollCeiling));
}

TEST(BossAI03Test, ChooseAttackDrawsWhenUngated) {
    BossAI03 b;
    b.SetSeed(303);
    b.EndAtk01(); // can_shoot -> true (one ref draw consumed)
    RGRandom ref = RefStream(303);
    ref.Range(0, BossAI03::kRollCeiling); // mirror EndAtk01

    const int roll = ref.Range(0, BossAI03::kRollCeiling); // the gated draw
    int expected = roll / (BossAI03::kRollCeiling / BossAI03::kAttackCount) + 1;
    if (expected > BossAI03::kAttackCount) {
        expected = BossAI03::kAttackCount;
    }
    const int idx = b.ChooseAttack();
    EXPECT_EQ(idx, expected);
    EXPECT_GE(idx, 1);
    EXPECT_LE(idx, BossAI03::kAttackCount);
    EXPECT_EQ(b.AtkIndex(), idx);
}

TEST(BossAI03Test, ChooseAttackCoversAllFiveBuckets) {
    BossAI03 b;
    b.SetSeed(7);
    b.EndAtk01(); // arm can_shoot once; it stays armed
    std::vector<int> seen(BossAI03::kAttackCount + 1, 0);
    for (int i = 0; i < 600; ++i) {
        const int idx = b.ChooseAttack();
        ASSERT_GE(idx, 1);
        ASSERT_LE(idx, BossAI03::kAttackCount);
        seen[static_cast<std::size_t>(idx)]++;
    }
    for (int idx = 1; idx <= BossAI03::kAttackCount; ++idx) {
        EXPECT_GT(seen[static_cast<std::size_t>(idx)], 0);
    }
}

// ---- attack START hooks: exact field writes, no draws ----------------------

TEST(BossAI03Test, StartAtk02SetsLockAndShooting) {
    BossAI03 b;
    b.SetSeed(1);
    b.StartAtk02();
    EXPECT_TRUE(b.WeaponLockTarget()); // 0x1c = 1
    EXPECT_TRUE(b.Shooting());         // 0x80 = 1
    RGRandom ref = RefStream(1);
    EXPECT_EQ(b.EndAtk01(), ref.Range(0, BossAI03::kRollCeiling)); // no draw before
}

TEST(BossAI03Test, StartAtk03SetsShootingOnly) {
    BossAI03 b;
    b.StartAtk03();
    EXPECT_TRUE(b.Shooting());           // 0x80 = 1
    EXPECT_FALSE(b.WeaponLockTarget());  // untouched
}

TEST(BossAI03Test, StartAtk04SetsShootingAndClearsAtk4Index) {
    BossAI03 b;
    b.StartAtk04();
    EXPECT_TRUE(b.Shooting()); // 0x80 = 1
    EXPECT_EQ(b.Atk4Index(), 0); // 0xdc = 0
}

TEST(BossAI03Test, StartAtk05SetsShootingOnly) {
    BossAI03 b;
    b.StartAtk05();
    EXPECT_TRUE(b.Shooting()); // 0x80 = 1
}

TEST(BossAI03Test, StartHooksTakeNoDraw) {
    BossAI03 b;
    b.SetSeed(42);
    b.StartAtk02();
    b.StartAtk03();
    b.StartAtk04();
    b.StartAtk05();
    RGRandom ref = RefStream(42);
    // First genuine draw (EndAtk01) must still be the reference's first draw.
    EXPECT_EQ(b.EndAtk01(), ref.Range(0, BossAI03::kRollCeiling));
}

// ---- InAtk02 / InAtk05: bullet-release frame -------------------------------

TEST(BossAI03Test, InAtk02ClearsShooting) {
    BossAI03 b;
    b.StartAtk02();
    EXPECT_TRUE(b.Shooting());
    b.InAtk02();
    EXPECT_FALSE(b.Shooting()); // 0x80 = 0
}

TEST(BossAI03Test, InAtk05WritesNoStateNoDraw) {
    BossAI03 b;
    b.SetSeed(88);
    b.StartAtk05();
    EXPECT_TRUE(b.Shooting());
    b.InAtk05();
    EXPECT_TRUE(b.Shooting()); // unchanged: decomp writes nothing here
    RGRandom ref = RefStream(88);
    EXPECT_EQ(b.EndAtk01(), ref.Range(0, BossAI03::kRollCeiling)); // no draw
}

// ---- EndAtk01: writes + single follow-up Range(0,100) ----------------------

TEST(BossAI03Test, EndAtk01WritesAndDrawsOnce) {
    BossAI03 b;
    b.SetSeed(2718);
    b.StartAtk02(); // set lock so we can see EndAtk01 clear it
    EXPECT_TRUE(b.WeaponLockTarget());
    RGRandom ref = RefStream(2718);
    const int got = b.EndAtk01();
    EXPECT_FALSE(b.WeaponLockTarget()); // 0x1c = 0
    EXPECT_TRUE(b.CanShoot());          // 0x40 = 1
    EXPECT_EQ(got, ref.Range(0, BossAI03::kRollCeiling)); // exactly one draw
    EXPECT_GE(got, 0);
    EXPECT_LT(got, BossAI03::kRollCeiling);
}

// ---- EndAtk02: writes always; follow-up roll GATED on angry -----------------

TEST(BossAI03Test, EndAtk02CalmTakesNoDraw) {
    BossAI03 b;
    b.SetSeed(31337);
    b.StartAtk02();
    EXPECT_TRUE(b.WeaponLockTarget());
    const int got = b.EndAtk02(); // calm: no draw
    EXPECT_FALSE(b.WeaponLockTarget()); // 0x1c = 0
    EXPECT_TRUE(b.CanShoot());          // 0x40 = 1
    EXPECT_EQ(got, -1);                 // calm sentinel, no roll
    // Stream untouched: next draw equals reference's first draw.
    RGRandom ref = RefStream(31337);
    EXPECT_EQ(b.EndAtk01(), ref.Range(0, BossAI03::kRollCeiling));
}

TEST(BossAI03Test, EndAtk02AngryDrawsOnce) {
    BossAI03 b;
    b.SetSeed(31337);
    b.OnHurt(1, 600); // angry (no draw)
    EXPECT_TRUE(b.Angry());
    RGRandom ref = RefStream(31337);
    const int got = b.EndAtk02(); // angry: one draw
    EXPECT_FALSE(b.WeaponLockTarget());
    EXPECT_TRUE(b.CanShoot());
    EXPECT_EQ(got, ref.Range(0, BossAI03::kRollCeiling));
    EXPECT_GE(got, 0);
    EXPECT_LT(got, BossAI03::kRollCeiling);
}

// ---- Full interleaved replay = identical sequence (lockstep) ----------------

TEST(BossAI03Test, FullStreamReplayIsDeterministic) {
    // Interleave every drawing method and replay -> identical sequence. can_shoot
    // is armed once (EndAtk01) so ChooseAttack stays ungated; angry is set once
    // (OnHurt) so EndAtk02 always draws. Per iteration the draw shape is fixed:
    //   WanderDirection (2 float) -> EndAtk01 (1 int) -> ChooseAttack (1 int) ->
    //   EndAtk02 (1 int, angry). No dizzy/dead toggling inside the loop.
    auto run = [](int seed) {
        BossAI03 b;
        b.SetSeed(seed);
        b.OnHurt(1, 600); // angry, no draw
        std::vector<float> trace;
        for (int i = 0; i < 24; ++i) {
            const glm::vec2 w = b.WanderDirection(); // 2 draws
            trace.push_back(w.x);
            trace.push_back(w.y);
            trace.push_back(static_cast<float>(b.EndAtk01()));    // 1 draw, arms can_shoot
            trace.push_back(static_cast<float>(b.ChooseAttack())); // 1 draw (ungated)
            trace.push_back(static_cast<float>(b.EndAtk02()));     // 1 draw (angry)
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

TEST(BossAI03Test, FullStreamMatchesHandRolledReference) {
    // Stronger than determinism: prove the boss's draw COUNT and ORDER match a
    // parallel reference stream exactly across an interleaved sequence.
    const int seed = 9001;
    BossAI03 b;
    b.SetSeed(seed);
    b.OnHurt(1, 600); // angry (no draw)
    RGRandom ref = RefStream(seed);
    for (int i = 0; i < 24; ++i) {
        const glm::vec2 w = b.WanderDirection();
        const float rx = ref.Range(BossAI03::kWanderMin, BossAI03::kWanderMax);
        const float ry = ref.Range(BossAI03::kWanderMin, BossAI03::kWanderMax);
        const float len = std::sqrt(rx * rx + ry * ry);
        EXPECT_FLOAT_EQ(w.x, len > 0.0F ? rx / len : 0.0F);
        EXPECT_FLOAT_EQ(w.y, len > 0.0F ? ry / len : 0.0F);

        EXPECT_EQ(b.EndAtk01(), ref.Range(0, BossAI03::kRollCeiling));

        const int roll = ref.Range(0, BossAI03::kRollCeiling);
        int expected = roll / (BossAI03::kRollCeiling / BossAI03::kAttackCount) + 1;
        if (expected > BossAI03::kAttackCount) {
            expected = BossAI03::kAttackCount;
        }
        EXPECT_EQ(b.ChooseAttack(), expected);

        EXPECT_EQ(b.EndAtk02(), ref.Range(0, BossAI03::kRollCeiling));
    }
}

// ---- StopAttack: brain bookkeeping only ------------------------------------

TEST(BossAI03Test, StopAttackResetsBucketNoDraw) {
    BossAI03 b;
    b.SetSeed(11);
    b.EndAtk01();        // arm can_shoot (one ref draw)
    b.ChooseAttack();    // sets a bucket (one ref draw)
    EXPECT_NE(b.AtkIndex(), BossAI03::kNoAttack);
    b.StopAttack();
    EXPECT_EQ(b.AtkIndex(), BossAI03::kNoAttack);
    RGRandom ref = RefStream(11);
    ref.Range(0, BossAI03::kRollCeiling); // EndAtk01
    ref.Range(0, BossAI03::kRollCeiling); // ChooseAttack
    // StopAttack drew nothing: next draw still matches.
    EXPECT_EQ(b.EndAtk01(), ref.Range(0, BossAI03::kRollCeiling));
}

// NOLINTEND(readability-magic-numbers)
