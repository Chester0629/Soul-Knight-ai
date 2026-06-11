#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/BossAI02.hpp"

using Game::BossAI02;

// NOLINTBEGIN(readability-magic-numbers)

// ---- angry phase (BossAngry) ------------------------------------------------

TEST(BossAI02Test, EntersAngryBelowHalfHp) {
    BossAI02 b;
    b.OnHurt(301, 600); // > 50% -> still calm
    EXPECT_FALSE(b.Angry());

    b.OnHurt(299, 600); // < 50% -> angry
    EXPECT_TRUE(b.Angry());
}

TEST(BossAI02Test, ExactlyHalfIsNotAngry) {
    BossAI02 b;
    b.OnHurt(300, 600); // 0.5 is not < 0.5
    EXPECT_FALSE(b.Angry());
}

TEST(BossAI02Test, AngryAppliesSpeedAndAnimEffectsOnce) {
    BossAI02 b;
    EXPECT_FLOAT_EQ(b.SpeedBonus(), 0.0F);
    EXPECT_FLOAT_EQ(b.AnimSpeed(), 1.0F);

    b.OnHurt(100, 600); // angry
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.SpeedBonus(), BossAI02::kAngrySpeedBonus); // +0.2 once
    EXPECT_FLOAT_EQ(b.AnimSpeed(), BossAI02::kAngryAnimSpeed);   // 1.2

    b.OnHurt(10, 600); // still below 50%, must not double-apply
    EXPECT_FLOAT_EQ(b.SpeedBonus(), BossAI02::kAngrySpeedBonus);
    EXPECT_FLOAT_EQ(b.AnimSpeed(), BossAI02::kAngryAnimSpeed);
}

TEST(BossAI02Test, ZeroMaxHpDoesNotEnterAngry) {
    BossAI02 b;
    b.OnHurt(0, 0); // guard against divide-by-zero
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.SpeedBonus(), 0.0F);
}

// ---- attack selection (ShootReflection) -------------------------------------

TEST(BossAI02Test, ChooseAttackReturnsBucketInRange) {
    BossAI02 b;
    b.SetSeed(4242);
    for (int i = 0; i < 64; ++i) {
        const int idx = b.ChooseAttack();
        // Bucket index in [0, kAttackCount); the decomp persists no atk_index,
        // so ChooseAttack only returns the roll bucket (mirrors BossAI01).
        EXPECT_GE(idx, 0);
        EXPECT_LT(idx, BossAI02::kAttackCount);
    }
}

TEST(BossAI02Test, ChooseAttackIsDeterministic) {
    BossAI02 a;
    BossAI02 b;
    a.SetSeed(4242);
    b.SetSeed(4242);
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(a.ChooseAttack(), b.ChooseAttack()); // same seed -> same out
    }
}

TEST(BossAI02Test, DifferentSeedsDiverge) {
    BossAI02 a;
    BossAI02 b;
    a.SetSeed(1);
    b.SetSeed(987654);
    bool diverged = false;
    for (int i = 0; i < 64; ++i) {
        if (a.ChooseAttack() != b.ChooseAttack()) {
            diverged = true;
        }
    }
    EXPECT_TRUE(diverged);
}

TEST(BossAI02Test, ChooseAttackCoversAllFour) {
    BossAI02 b;
    b.SetSeed(7);
    std::vector<int> seen(BossAI02::kAttackCount, 0);
    for (int i = 0; i < 600; ++i) {
        seen[static_cast<std::size_t>(b.ChooseAttack())]++;
    }
    for (int idx = 0; idx < BossAI02::kAttackCount; ++idx) {
        EXPECT_GT(seen[static_cast<std::size_t>(idx)], 0); // every atk reachable
    }
}

// ---- wander (RunReflection) -------------------------------------------------

TEST(BossAI02Test, WanderDirectionIsUnitOrZero) {
    BossAI02 b;
    b.SetSeed(2024);
    for (int i = 0; i < 64; ++i) {
        const glm::vec2 d = b.WanderDirection();
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        // Normalized: length is ~1, or exactly 0 in the degenerate (0,0) case.
        EXPECT_TRUE(std::fabs(len - 1.0F) < 1e-4F || len == 0.0F);
    }
}

TEST(BossAI02Test, WanderDirectionIsDeterministic) {
    BossAI02 a;
    BossAI02 b;
    a.SetSeed(99);
    b.SetSeed(99);
    for (int i = 0; i < 32; ++i) {
        const glm::vec2 da = a.WanderDirection();
        const glm::vec2 db = b.WanderDirection();
        EXPECT_FLOAT_EQ(da.x, db.x);
        EXPECT_FLOAT_EQ(da.y, db.y);
    }
}

// ---- per-attack state writes ------------------------------------------------

TEST(BossAI02Test, InAtk01SetsShootingAndClearsLock) {
    BossAI02 b;
    b.InAtk01(); // decomp: shooting(0x80)=1, weapon_lock_target(0x1c)=0
    EXPECT_TRUE(b.Shooting());
    EXPECT_FALSE(b.WeaponLockTarget());
}

TEST(BossAI02Test, EndAtk02ClearsShootingAndArms) {
    BossAI02 b;
    b.SetSeed(5);
    b.InAtk02();
    EXPECT_TRUE(b.Shooting());
    EXPECT_FALSE(b.CanShoot());
    b.EndAtk02();
    EXPECT_FALSE(b.Shooting());
    EXPECT_TRUE(b.CanShoot());
}

TEST(BossAI02Test, EndAtk03ClearsLockAndArms) {
    BossAI02 b;
    b.SetSeed(5);
    b.EndAtk03();
    EXPECT_FALSE(b.WeaponLockTarget());
    EXPECT_TRUE(b.CanShoot());
}

TEST(BossAI02Test, EndAtk04ClearsLockAndArms) {
    BossAI02 b;
    b.EndAtk04();
    EXPECT_FALSE(b.WeaponLockTarget());
    EXPECT_TRUE(b.CanShoot());
}

// ---- per-attack RNG draws (bounds + determinism) ----------------------------

TEST(BossAI02Test, InAtk02BulletAngleInRange) {
    BossAI02 b;
    b.SetSeed(2024);
    for (int i = 0; i < 64; ++i) {
        const int angle = b.InAtk02();
        EXPECT_GE(angle, BossAI02::kBulletAngleMin);
        EXPECT_LT(angle, BossAI02::kBulletAngleMax); // max EXCLUSIVE
    }
}

TEST(BossAI02Test, EndAtk03RollInRange) {
    BossAI02 b;
    b.SetSeed(2024);
    for (int i = 0; i < 64; ++i) {
        const int r = b.EndAtk03();
        EXPECT_GE(r, 0);
        EXPECT_LT(r, BossAI02::kEndAtk03RollCeiling); // max EXCLUSIVE
    }
}

TEST(BossAI02Test, InAtk04RollInRange) {
    BossAI02 b;
    b.SetSeed(2024);
    for (int i = 0; i < 64; ++i) {
        const int r = b.InAtk04();
        EXPECT_GE(r, 0);
        EXPECT_LT(r, BossAI02::kInAtk04RollCeiling); // max EXCLUSIVE
    }
}

// ---- full-stream replay determinism (lockstep) ------------------------------

TEST(BossAI02Test, FullStreamReplayIsDeterministic) {
    // Interleave every RNG-drawing path and replay -> identical sequence.
    auto run = [](int seed) {
        BossAI02 b;
        b.SetSeed(seed);
        std::vector<float> trace;
        for (int i = 0; i < 16; ++i) {
            trace.push_back(static_cast<float>(b.ChooseAttack()));
            const glm::vec2 d = b.WanderDirection();
            trace.push_back(d.x);
            trace.push_back(d.y);
            trace.push_back(static_cast<float>(b.InAtk02()));
            trace.push_back(static_cast<float>(b.EndAtk03()));
            trace.push_back(static_cast<float>(b.InAtk04()));
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

TEST(BossAI02Test, DrawOrderMatchesDecompWithinAtk02) {
    // InAtk02 draws exactly one int before EndAtk02 (which draws none); a second
    // InAtk02 must draw the NEXT stream value, proving draw count/order.
    BossAI02 ref;
    ref.SetSeed(123);
    const int first = ref.InAtk02();
    ref.EndAtk02(); // no RNG draw
    const int second = ref.InAtk02();

    BossAI02 chk;
    chk.SetSeed(123);
    EXPECT_EQ(chk.Rng().Range(BossAI02::kBulletAngleMin,
                              BossAI02::kBulletAngleMax),
              first);
    EXPECT_EQ(chk.Rng().Range(BossAI02::kBulletAngleMin,
                              BossAI02::kBulletAngleMax),
              second);
}

// NOLINTEND(readability-magic-numbers)
