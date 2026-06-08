#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <cmath>
#include <vector>

#include "combat/BossAI10.hpp"
#include "data/RGRandom.hpp"

using Game::BossAI10;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

namespace {

// Reference normalization matching BossAI10::RunReflectionDirection().
glm::vec2 NormRef(float x, float y) {
    const glm::vec2 v{x, y};
    const float len = glm::length(v);
    if (len <= 0.0F) {
        return glm::vec2{0.0F, 0.0F};
    }
    return v / len;
}

} // namespace

// ---- RunReflection: two ordered float Range(-1,1) draws, normalized ----------

TEST(BossAI10Test, RunReflectionDrawsTwoFloatsXThenY) {
    // Parallel reference stream from a same-seeded RGRandom: x then y, max-inclusive.
    BossAI10 b;
    RGRandom ref;
    b.SetSeed(12345);
    ref.SetRandomSeed(12345);
    for (int i = 0; i < 32; ++i) {
        const float x = ref.Range(BossAI10::kDirMin, BossAI10::kDirMax);
        const float y = ref.Range(BossAI10::kDirMin, BossAI10::kDirMax);
        const glm::vec2 got = b.RunReflectionDirection();
        const glm::vec2 want = NormRef(x, y);
        EXPECT_FLOAT_EQ(got.x, want.x);
        EXPECT_FLOAT_EQ(got.y, want.y);
    }
}

TEST(BossAI10Test, RunReflectionDirectionIsUnitOrZero) {
    BossAI10 b;
    b.SetSeed(777);
    for (int i = 0; i < 64; ++i) {
        const glm::vec2 d = b.RunReflectionDirection();
        const float len = glm::length(d);
        // Either a unit vector or the (0,0) degenerate case.
        EXPECT_TRUE(len == 0.0F || std::abs(len - 1.0F) < 1e-4F);
    }
}

TEST(BossAI10Test, RunReflectionConsumesExactlyTwoDrawsPerCall) {
    // Interleave RunReflection with a probe draw and verify lockstep ordering:
    // after N RunReflection calls the boss stream equals a reference that drew
    // exactly 2 floats per call.
    BossAI10 b;
    RGRandom ref;
    b.SetSeed(2024);
    ref.SetRandomSeed(2024);
    for (int i = 0; i < 10; ++i) {
        b.RunReflectionDirection();
        ref.Range(BossAI10::kDirMin, BossAI10::kDirMax);
        ref.Range(BossAI10::kDirMin, BossAI10::kDirMax);
    }
    // Streams are now in lockstep: the next draw must match exactly.
    EXPECT_FLOAT_EQ(b.Rng().Range(BossAI10::kDirMin, BossAI10::kDirMax),
                    ref.Range(BossAI10::kDirMin, BossAI10::kDirMax));
}

// ---- ShootReflection: ONE int Range(0,100) draw, gated -----------------------

TEST(BossAI10Test, ShootReflectionDrawsRange0To100WhenActive) {
    BossAI10 b;
    RGRandom ref;
    b.SetSeed(99);
    ref.SetRandomSeed(99);
    b.SetCanShoot(true); // gate open: !dead, !dizzy by default
    for (int i = 0; i < 32; ++i) {
        const int roll = ref.Range(0, BossAI10::kRollCeiling);
        int want = roll / (BossAI10::kRollCeiling / BossAI10::kAttackCount) + 1;
        if (want > BossAI10::kAttackCount) {
            want = BossAI10::kAttackCount;
        }
        const int got = b.ShootReflectionChooseAttack();
        EXPECT_EQ(got, want);
    }
}

TEST(BossAI10Test, ShootReflectionGatedByCanShootTakesNoDraw) {
    // can_shoot == false -> no draw, no field written, stream untouched.
    BossAI10 b;
    RGRandom ref;
    b.SetSeed(4242);
    ref.SetRandomSeed(4242);
    b.SetCanShoot(false);
    EXPECT_EQ(b.ShootReflectionChooseAttack(), BossAI10::kNoAttack);
    // Stream must be pristine: the next draw equals the reference's FIRST draw.
    EXPECT_EQ(b.Rng().Range(0, BossAI10::kRollCeiling),
              ref.Range(0, BossAI10::kRollCeiling));
}

TEST(BossAI10Test, ShootReflectionGatedByDeadTakesNoDraw) {
    BossAI10 b;
    RGRandom ref;
    b.SetSeed(4242);
    ref.SetRandomSeed(4242);
    b.SetCanShoot(true);
    b.SetDead(true); // dead latch blocks the draw
    EXPECT_EQ(b.ShootReflectionChooseAttack(), BossAI10::kNoAttack);
    EXPECT_EQ(b.Rng().Range(0, BossAI10::kRollCeiling),
              ref.Range(0, BossAI10::kRollCeiling));
}

TEST(BossAI10Test, ShootReflectionGatedByDizzyTakesNoDraw) {
    BossAI10 b;
    RGRandom ref;
    b.SetSeed(4242);
    ref.SetRandomSeed(4242);
    b.SetCanShoot(true);
    b.SetDizzy(true); // dizzy latch blocks the draw
    EXPECT_EQ(b.ShootReflectionChooseAttack(), BossAI10::kNoAttack);
    EXPECT_EQ(b.Rng().Range(0, BossAI10::kRollCeiling),
              ref.Range(0, BossAI10::kRollCeiling));
}

TEST(BossAI10Test, ShootReflectionIndexAlwaysInRange) {
    BossAI10 b;
    b.SetSeed(7);
    b.SetCanShoot(true);
    for (int i = 0; i < 600; ++i) {
        const int idx = b.ShootReflectionChooseAttack();
        EXPECT_GE(idx, 1);
        EXPECT_LE(idx, BossAI10::kAttackCount);
    }
}

TEST(BossAI10Test, ShootReflectionCoversAllFiveBuckets) {
    BossAI10 b;
    b.SetSeed(31337);
    b.SetCanShoot(true);
    std::vector<int> seen(BossAI10::kAttackCount + 1, 0);
    for (int i = 0; i < 1000; ++i) {
        seen[static_cast<std::size_t>(b.ShootReflectionChooseAttack())]++;
    }
    for (int idx = 1; idx <= BossAI10::kAttackCount; ++idx) {
        EXPECT_GT(seen[static_cast<std::size_t>(idx)], 0);
    }
}

// ---- GetHurt -> BossAngry transition (field 0xCC, shoot_cd 0x3C halving) ------

TEST(BossAI10Test, EntersAngryBelowHalfHp) {
    BossAI10 b;
    b.SetAwake(true);
    const float cd0 = 2.0F;
    float cd = b.OnHurt(301, 600, cd0); // > 50% -> calm
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(cd, cd0);

    cd = b.OnHurt(299, 600, cd); // < 50% -> angry, shoot_cd halves
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(cd, cd0 * BossAI10::kAngryShootCdScale);
}

TEST(BossAI10Test, ExactlyHalfIsNotAngry) {
    BossAI10 b;
    b.SetAwake(true);
    b.OnHurt(300, 600, 2.0F); // 0.5 is not < 0.5
    EXPECT_FALSE(b.Angry());
}

TEST(BossAI10Test, AngryFiresOnlyOnce) {
    BossAI10 b;
    b.SetAwake(true);
    float cd = b.OnHurt(100, 600, 2.0F); // angry
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(cd, 1.0F); // halved once
    cd = b.OnHurt(10, 600, cd); // still below 50% -> no second halving
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(cd, 1.0F); // unchanged: BossAngry guarded by 0xCC
}

TEST(BossAI10Test, GetHurtGatedWhenNotAwake) {
    BossAI10 b; // awake == false by default
    const float cd = b.OnHurt(1, 600, 2.0F);
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(cd, 2.0F); // gated out: no angry, no cd change
}

TEST(BossAI10Test, GetHurtGatedWhenDead) {
    BossAI10 b;
    b.SetAwake(true);
    b.SetDead(true);
    const float cd = b.OnHurt(1, 600, 2.0F);
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(cd, 2.0F);
}

TEST(BossAI10Test, GetHurtDoesNotClampHpBeforeRatio) {
    // The decomp's heal+clamp is atk03-animator-gated owner-side state the brain
    // does not model; OnHurt uses the post-damage hp/max_hp ratio directly. So
    // hp above max_hp gives ratio > 1.0 (>= 0.5) -> still not angry.
    BossAI10 b;
    b.SetAwake(true);
    b.OnHurt(900, 600, 2.0F); // 900/600 == 1.5 >= 0.5 -> not angry
    EXPECT_FALSE(b.Angry());
}

TEST(BossAI10Test, GetHurtZeroMaxHpGuards) {
    BossAI10 b;
    b.SetAwake(true);
    const float cd = b.OnHurt(0, 0, 2.0F); // divide-by-zero guard
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(cd, 2.0F);
}

// ---- CreateIcicle chain (atk_4_count 0xD0, angry 0xCC cap) -------------------

TEST(BossAI10Test, IcicleChainDefaultCapIsThree) {
    BossAI10 b; // angry == false -> cap 3
    b.StartIcicleChain();
    // count 1 -> delay 0.25, count 2 -> 0.50, count 3 -> end (< 0).
    EXPECT_FLOAT_EQ(b.CreateIcicleStep(), 1.0F * BossAI10::kIcicleInvokeStep);
    EXPECT_EQ(b.Atk4Count(), 1);
    EXPECT_FLOAT_EQ(b.CreateIcicleStep(), 2.0F * BossAI10::kIcicleInvokeStep);
    EXPECT_EQ(b.Atk4Count(), 2);
    EXPECT_LT(b.CreateIcicleStep(), 0.0F); // count == 3 == cap -> chain ends
    EXPECT_EQ(b.Atk4Count(), 3);
}

TEST(BossAI10Test, IcicleChainAngryCapIsFour) {
    BossAI10 b;
    b.SetAngry(true); // angry (0xCC) -> cap 4
    b.StartIcicleChain();
    EXPECT_FLOAT_EQ(b.CreateIcicleStep(), 1.0F * BossAI10::kIcicleInvokeStep);
    EXPECT_FLOAT_EQ(b.CreateIcicleStep(), 2.0F * BossAI10::kIcicleInvokeStep);
    EXPECT_FLOAT_EQ(b.CreateIcicleStep(), 3.0F * BossAI10::kIcicleInvokeStep);
    EXPECT_LT(b.CreateIcicleStep(), 0.0F); // count == 4 == cap -> ends
    EXPECT_EQ(b.Atk4Count(), 4);
}

TEST(BossAI10Test, StartIcicleChainResetsCounter) {
    BossAI10 b;
    b.CreateIcicleStep();
    b.CreateIcicleStep();
    EXPECT_GT(b.Atk4Count(), 0);
    b.StartIcicleChain();
    EXPECT_EQ(b.Atk4Count(), BossAI10::kNoAttack);
}

TEST(BossAI10Test, IcicleChainTakesNoDraw) {
    // CreateIcicle does no RNG draw: the stream stays pristine.
    BossAI10 b;
    RGRandom ref;
    b.SetSeed(555);
    ref.SetRandomSeed(555);
    b.StartIcicleChain();
    for (int i = 0; i < 5; ++i) {
        b.CreateIcicleStep();
    }
    EXPECT_EQ(b.Rng().Range(0, 100), ref.Range(0, 100));
}

// ---- Dizzy / Scout / OnGameStateChange gates ---------------------------------

TEST(BossAI10Test, DizzyLatchesWhenAlive) {
    BossAI10 b;
    EXPECT_FALSE(b.Dizzy());
    EXPECT_TRUE(b.ApplyDizzy());
    EXPECT_TRUE(b.Dizzy());
}

TEST(BossAI10Test, DizzyDoesNotLatchWhenDead) {
    BossAI10 b;
    b.SetDead(true);
    EXPECT_FALSE(b.ApplyDizzy());
    EXPECT_FALSE(b.Dizzy());
}

TEST(BossAI10Test, ScoutActiveGatedByDizzyAndDead) {
    BossAI10 b;
    EXPECT_TRUE(b.ScoutActive());
    b.SetDizzy(true);
    EXPECT_FALSE(b.ScoutActive());
    b.SetDizzy(false);
    b.SetDead(true);
    EXPECT_FALSE(b.ScoutActive());
}

TEST(BossAI10Test, OnGameStateChangeWakesOnRoomStart) {
    BossAI10 b;
    EXPECT_FALSE(b.Awake());
    // Wrong state -> no wake.
    EXPECT_FALSE(b.OnGameStateChange(0, true));
    EXPECT_FALSE(b.Awake());
    // Right state but room not ready -> no wake.
    EXPECT_FALSE(b.OnGameStateChange(1, false));
    EXPECT_FALSE(b.Awake());
    // Right state + room ready -> wake.
    EXPECT_TRUE(b.OnGameStateChange(1, true));
    EXPECT_TRUE(b.Awake());
}

// ---- full-stream replay determinism (lockstep) -------------------------------

TEST(BossAI10Test, FullStreamReplayIsDeterministic) {
    auto run = [](int seed) {
        BossAI10 b;
        b.SetSeed(seed);
        b.SetCanShoot(true);
        std::vector<float> trace;
        for (int i = 0; i < 24; ++i) {
            const glm::vec2 d = b.RunReflectionDirection(); // 2 float draws
            trace.push_back(d.x);
            trace.push_back(d.y);
            trace.push_back(static_cast<float>(b.ShootReflectionChooseAttack())); // 1 int draw
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

TEST(BossAI10Test, DifferentSeedsDiverge) {
    BossAI10 a;
    BossAI10 b;
    a.SetSeed(1);
    b.SetSeed(987654);
    a.SetCanShoot(true);
    b.SetCanShoot(true);
    bool diverged = false;
    for (int i = 0; i < 64; ++i) {
        if (a.ShootReflectionChooseAttack() != b.ShootReflectionChooseAttack()) {
            diverged = true;
        }
    }
    EXPECT_TRUE(diverged);
}

// NOLINTEND(readability-magic-numbers)
