#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <cmath>
#include <vector>

#include "combat/BossAI14.hpp"
#include "data/RGRandom.hpp"

using Game::BossAI14;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

namespace {

// A reference RGRandom seeded identically to the brain, used to predict the
// EXACT draw sequence (count + order + min/max args) the brain must consume.
RGRandom RefSeeded(int seed) {
    RGRandom r;
    r.SetRandomSeed(seed);
    return r;
}

// Reference normalization matching BossAI14::RunReflectionHeading().
glm::vec2 NormRef(float x, float y) {
    const glm::vec2 v{x, y};
    const float len = glm::length(v);
    if (len <= 0.0F) {
        return glm::vec2{0.0F, 0.0F};
    }
    return v / len;
}

constexpr int kSeed = 90014;

} // namespace

// ---- ctor field seed -------------------------------------------------------

TEST(BossAI14Test, ConstructSeedsRootAndCanHit) {
    BossAI14 b;
    EXPECT_FALSE(b.Root());
    EXPECT_FALSE(b.CanHit());
    b.Construct(); // *(u16*)(p+0xe5) = 0x101 -> root=1, can_hit=1
    EXPECT_TRUE(b.Root());
    EXPECT_TRUE(b.CanHit());
}

// ---- Scout gate (no draw) --------------------------------------------------

TEST(BossAI14Test, ScoutClearsTargetOnlyWhenAliveAndNotDizzy) {
    BossAI14 b;
    EXPECT_TRUE(b.ScoutClearsTarget()); // alive, not dizzy

    b.SetDead(true);
    EXPECT_FALSE(b.ScoutClearsTarget()); // dead gate

    b.SetDead(false);
    EXPECT_TRUE(b.ApplyDizzy());
    EXPECT_FALSE(b.ScoutClearsTarget()); // dizzy gate
}

// ---- RunReflection: root mode = one float draw -----------------------------

TEST(BossAI14Test, RunReflectionRootDelayMatchesSingleFloatDraw) {
    BossAI14 b;
    b.SetSeed(kSeed);
    b.Construct(); // root = true

    RGRandom ref = RefSeeded(kSeed);
    const float scoutRate = 1.5F;
    for (int i = 0; i < 32; ++i) {
        const float expected =
            ref.Range(scoutRate * BossAI14::kRootDelayLoFactor, scoutRate);
        const float got = b.RunReflectionRootDelay(scoutRate);
        EXPECT_FLOAT_EQ(got, expected);          // value + draw order
        EXPECT_GE(got, scoutRate * 0.5F);
        EXPECT_LE(got, scoutRate);
    }
}

// ---- RunReflection: non-root mode = two float draws (x then y) --------------

TEST(BossAI14Test, RunReflectionHeadingDrawsTwoFloatsInXThenYOrder) {
    BossAI14 b;
    b.SetSeed(kSeed);
    // root defaults false (no Construct) -> heading branch

    RGRandom ref = RefSeeded(kSeed);
    for (int i = 0; i < 32; ++i) {
        const float x = ref.Range(BossAI14::kHeadingMin, BossAI14::kHeadingMax);
        const float y = ref.Range(BossAI14::kHeadingMin, BossAI14::kHeadingMax);
        const glm::vec2 dir = b.RunReflectionHeading();
        const glm::vec2 want = NormRef(x, y); // exact same draw order + normalize
        EXPECT_FLOAT_EQ(dir.x, want.x);
        EXPECT_FLOAT_EQ(dir.y, want.y);

        const float len = glm::length(dir);
        EXPECT_TRUE(len == 0.0F || std::abs(len - 1.0F) < 1e-4F);
    }
}

// ---- ShootReflection: the single Range(0,100) roll + gates ------------------

TEST(BossAI14Test, ShootReflectionDrawsOneIntWhenGateOpen) {
    BossAI14 b;
    b.SetSeed(kSeed);
    b.SetCanShoot(true); // gate: can_shoot && !dead && !dizzy

    RGRandom ref = RefSeeded(kSeed);
    for (int i = 0; i < 64; ++i) {
        const int roll = ref.Range(0, BossAI14::kRollCeiling); // mirror the draw
        const int idx = b.ShootReflectionChoose();

        int expected = roll / (BossAI14::kRollCeiling / BossAI14::kAttackCount) + 1;
        if (expected > BossAI14::kAttackCount) {
            expected = BossAI14::kAttackCount;
        }
        EXPECT_EQ(idx, expected);          // exact bucket of the exact roll
        EXPECT_GE(idx, 1);
        EXPECT_LE(idx, BossAI14::kAttackCount);
        // ShootReflection returns the bucket as a pure value: BossAI14 has no
        // atk_index field and the decomp writes none (vtable[0x10c] jumptable).
    }
}

TEST(BossAI14Test, ShootReflectionGatedPathsConsumeNoDraw) {
    // Each gated-out reason must leave the stream untouched: the first REAL draw
    // afterward must equal the reference's FIRST draw (its bucket index).
    RGRandom ref = RefSeeded(kSeed);
    const int firstRef = ref.Range(0, BossAI14::kRollCeiling);
    const auto expectedBucket = [](int roll) {
        int e = roll / (BossAI14::kRollCeiling / BossAI14::kAttackCount) + 1;
        return e > BossAI14::kAttackCount ? BossAI14::kAttackCount : e;
    };

    // can_shoot == false -> no draw
    {
        BossAI14 b;
        b.SetSeed(kSeed);
        EXPECT_FALSE(b.CanShoot());
        EXPECT_EQ(b.ShootReflectionChoose(), BossAI14::kNoAttack);
        b.SetCanShoot(true);
        const int probe = b.ShootReflectionChoose(); // first real draw now
        EXPECT_EQ(probe, expectedBucket(firstRef));  // stream un-advanced
    }
    // dead == true -> no draw
    {
        BossAI14 b;
        b.SetSeed(kSeed);
        b.SetCanShoot(true);
        b.SetDead(true);
        EXPECT_EQ(b.ShootReflectionChoose(), BossAI14::kNoAttack);
        b.SetDead(false);
        EXPECT_EQ(b.ShootReflectionChoose(), expectedBucket(firstRef));
    }
    // dizzy == true -> no draw (probe via a sibling that never set dizzy, since
    // there is no public un-dizzy; both share the same seed so the first real
    // draw must be the reference's first roll).
    {
        BossAI14 b;
        b.SetSeed(kSeed);
        b.SetCanShoot(true);
        EXPECT_TRUE(b.ApplyDizzy());
        EXPECT_EQ(b.ShootReflectionChoose(), BossAI14::kNoAttack);

        BossAI14 sib;
        sib.SetSeed(kSeed);
        sib.SetCanShoot(true);
        EXPECT_EQ(sib.ShootReflectionChoose(), expectedBucket(firstRef));
    }
}

TEST(BossAI14Test, ShootReflectionCoversAllSixBuckets) {
    BossAI14 b;
    b.SetSeed(7);
    b.SetCanShoot(true);
    std::vector<int> seen(BossAI14::kAttackCount + 1, 0);
    for (int i = 0; i < 1200; ++i) {
        seen[static_cast<std::size_t>(b.ShootReflectionChoose())]++;
    }
    for (int idx = 1; idx <= BossAI14::kAttackCount; ++idx) {
        EXPECT_GT(seen[static_cast<std::size_t>(idx)], 0); // every atk reachable
    }
    EXPECT_EQ(seen[0], 0); // gate open -> never the idle sentinel
}

TEST(BossAI14Test, ShootReflectionIsDeterministic) {
    BossAI14 a;
    BossAI14 b;
    a.SetSeed(31337);
    b.SetSeed(31337);
    a.SetCanShoot(true);
    b.SetCanShoot(true);
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(a.ShootReflectionChoose(), b.ShootReflectionChoose());
    }
}

// ---- BossAngry: one-shot enrage, no draw -----------------------------------

TEST(BossAI14Test, BossAngryHalvesShootCdOnceAndLatches) {
    BossAI14 b;
    EXPECT_FALSE(b.Angry());
    const float halved = b.BossAngry(2.0F);
    EXPECT_FLOAT_EQ(halved, 1.0F); // 2.0 * 0.5
    EXPECT_TRUE(b.Angry());
    // second call: latched -> no further halving (returns its arg unchanged)
    const float again = b.BossAngry(1.0F);
    EXPECT_FLOAT_EQ(again, 1.0F);
}

TEST(BossAI14Test, BossAngryConstantsMatchDecomp) {
    EXPECT_FLOAT_EQ(BossAI14::kAngryShootCdScale, 0.5F);  // *(p+0x3c) *= 0.5
    EXPECT_FLOAT_EQ(BossAI14::kAngryAnimSpeed, 1.2F);     // 0x3f99999a
}

TEST(BossAI14Test, BossAngryTakesNoDraw) {
    // angry path must not consume the stream: a draw after it equals the first
    // reference draw.
    BossAI14 b;
    b.SetSeed(kSeed);
    b.SetCanShoot(true);
    b.BossAngry(2.0F);

    RGRandom ref = RefSeeded(kSeed);
    const int firstRef = ref.Range(0, BossAI14::kRollCeiling);
    int expected = firstRef / (BossAI14::kRollCeiling / BossAI14::kAttackCount) + 1;
    if (expected > BossAI14::kAttackCount) {
        expected = BossAI14::kAttackCount;
    }
    EXPECT_EQ(b.ShootReflectionChoose(), expected);
}

// ---- Dizzy gate ------------------------------------------------------------

TEST(BossAI14Test, DizzyLatchesOnlyWhenAlive) {
    BossAI14 b;
    EXPECT_TRUE(b.ApplyDizzy());  // alive -> latches
    EXPECT_TRUE(b.Dizzy());
    EXPECT_FALSE(b.ApplyDizzy()); // already dizzy -> no re-latch

    BossAI14 dead;
    dead.SetDead(true);
    EXPECT_FALSE(dead.ApplyDizzy()); // dead gate blocks the latch
    EXPECT_FALSE(dead.Dizzy());
}

// ---- GetForce immunity gate ------------------------------------------------

TEST(BossAI14Test, AcceptsForceBlockedByRootOrInAtk03) {
    BossAI14 b;
    EXPECT_TRUE(b.AcceptsForce()); // !inAtk03 && !root

    b.SetRoot(true);
    EXPECT_FALSE(b.AcceptsForce()); // root immunity

    b.SetRoot(false);
    b.SetInAtk03(true);
    EXPECT_FALSE(b.AcceptsForce()); // inAtk03 immunity

    // The inAtk03(0xed) latch is owned by the InAtk03/Attacking03 coroutine, not
    // by EndAtk03 (which only lowers the animator bool); clearing it ends immunity.
    b.SetInAtk03(false);
    EXPECT_FALSE(b.InAtk03());
    EXPECT_TRUE(b.AcceptsForce());
}

// ---- Full interleaved replay -----------------------------------------------

TEST(BossAI14Test, FullStreamReplayIsDeterministic) {
    auto run = [](int seed) {
        BossAI14 b;
        b.SetSeed(seed);
        b.SetCanShoot(true);
        std::vector<float> trace;
        for (int i = 0; i < 24; ++i) {
            const glm::vec2 h = b.RunReflectionHeading();   // 2 float draws
            trace.push_back(h.x);
            trace.push_back(h.y);
            trace.push_back(static_cast<float>(b.ShootReflectionChoose())); // 1 int
            trace.push_back(b.RunReflectionRootDelay(1.0F)); // 1 float
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

// NOLINTEND(readability-magic-numbers)
