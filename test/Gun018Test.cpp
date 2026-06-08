#include <gtest/gtest.h>

#include <vector>

#include "combat/Gun018.hpp"
#include "data/RGRandom.hpp"

using Game::Gun018;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- ctor immediates: the directly recoverable pellet config ----------------

TEST(Gun018Test, PelletConfigMatchesCtorImmediates) {
    Gun018 g;
    EXPECT_EQ(g.BulletCount(), 3);          // 965254 this+0x8c = 3
    EXPECT_FLOAT_EQ(g.BulletSpeed(), 20.0F); // 965255 this+0x90 = 20.0f
    EXPECT_EQ(g.BulletDeviation(), 20);      // 965257 this+0x9c = 0x14
    EXPECT_EQ(Gun018::kBulletAngle, 20);     // 965256 this+0x98 = 0x14
    EXPECT_FLOAT_EQ(Gun018::kBaseField0x78, 2.0F);
    EXPECT_FLOAT_EQ(Gun018::kBaseField0x80, 1.0F);
}

// ---- AttackSpread: ADDITIVE base + recoil (distinct from other guns) --------

TEST(Gun018Test, AttackSpreadIsAdditive) {
    // FAITHFUL: 965296-965297 fVar4 = baseAngle; fVar4 = fVar4 + recoil.
    EXPECT_FLOAT_EQ(Gun018::AttackSpread(20.0F, 5.0F), 25.0F);
    EXPECT_FLOAT_EQ(Gun018::AttackSpread(0.0F, 7.5F), 7.5F);
    EXPECT_FLOAT_EQ(Gun018::AttackSpread(12.0F, 0.0F), 12.0F);
    // No clamp: a negative recoil can shrink the span below the base angle.
    EXPECT_FLOAT_EQ(Gun018::AttackSpread(10.0F, -4.0F), 6.0F);
}

// ---- PelletGatePasses: -c <= c  <=>  c >= 0 ---------------------------------

TEST(Gun018Test, PelletGateOpenForNonNegativeCounter) {
    EXPECT_TRUE(Gun018::PelletGatePasses(0));   // -0 <= 0
    EXPECT_TRUE(Gun018::PelletGatePasses(1));   // -1 <= 1
    EXPECT_TRUE(Gun018::PelletGatePasses(100));
    EXPECT_FALSE(Gun018::PelletGatePasses(-1)); //  1 <= -1 is false
    EXPECT_FALSE(Gun018::PelletGatePasses(-50));
}

// ---- Attack scatter: ONE float draw (max-INCLUSIVE), order-locked ----------

TEST(Gun018Test, AttackScatterDrawsOneInclusiveFloat) {
    // FAITHFUL: 965302 RGRandom::Range(-spread, +spread). A parallel same-seeded
    // stream must produce the identical float for the identical span.
    Gun018 weapon;
    weapon.SetSeed(424242);
    RGRandom reference;
    reference.SetRandomSeed(424242);

    const float spread = Gun018::AttackSpread(20.0F, 5.0F); // 25.0f
    const float got = weapon.AttackScatter(20.0F, 5.0F);
    const float want = reference.Range(-spread, spread); // Range(float): one draw
    EXPECT_FLOAT_EQ(got, want);

    // Exactly one draw consumed: next draws on both streams must still agree.
    EXPECT_EQ(weapon.Rng().Range(0, 1000000), reference.Range(0, 1000000));
}

// ---- CreateBullet scatter: ONE int draw (max-EXCLUSIVE) when gate open ------

TEST(Gun018Test, CreateBulletScatterDrawsOneExclusiveIntWhenGateOpen) {
    // FAITHFUL: 965324 RGRandom::Range(-deviation, +deviation), gated on c >= 0.
    Gun018 weapon;
    weapon.SetSeed(31337);
    RGRandom reference;
    reference.SetRandomSeed(31337);

    const int got = weapon.CreateBulletScatter(0); // gate open
    const int want = reference.Range(-Gun018::kBulletDeviation,
                                     Gun018::kBulletDeviation);
    EXPECT_EQ(got, want);

    // Exactly one int draw consumed when the gate is open.
    EXPECT_EQ(weapon.Rng().Range(0, 1000000), reference.Range(0, 1000000));
}

TEST(Gun018Test, CreateBulletScatterDrawsZeroWhenGateClosed) {
    // When the counter is negative the decomp skips the `if` body entirely:
    // ZERO RGRandom draws. The weapon stream must stay byte-for-byte un-advanced.
    Gun018 weapon;
    weapon.SetSeed(555);
    RGRandom reference;
    reference.SetRandomSeed(555);

    const int got = weapon.CreateBulletScatter(-1); // gate closed
    EXPECT_EQ(got, 0);

    // No advance happened: the weapon's next draw equals the reference's first.
    EXPECT_EQ(weapon.Rng().Range(0, 1000000), reference.Range(0, 1000000));
}

// ---- Full-pull lockstep: count + order across Attack then N pellets ---------

TEST(Gun018Test, FullPullDrawCountAndOrderMatchReference) {
    // One pull: Attack draws ONE float, then the owner fires kBulletCount pellets
    // each with the counter open -> one int draw apiece. The exact draw sequence
    // (1 float + 3 ints) must match a parallel same-seeded reference stream.
    Gun018 weapon;
    weapon.SetSeed(909090);
    RGRandom reference;
    reference.SetRandomSeed(909090);

    const float aSpread = Gun018::AttackSpread(20.0F, 2.0F); // 22.0f
    const std::vector<float> floats = [&]() -> std::vector<float> {
        std::vector<float> out;
        out.push_back(weapon.AttackScatter(20.0F, 2.0F));
        return out;
    }();
    EXPECT_FLOAT_EQ(floats.front(), reference.Range(-aSpread, aSpread));

    for (int i = 0; i < Gun018::kBulletCount; ++i) {
        const int pellet = weapon.CreateBulletScatter(i); // counter i >= 0
        EXPECT_EQ(pellet, reference.Range(-Gun018::kBulletDeviation,
                                          Gun018::kBulletDeviation));
    }

    // After 1 float + 3 int draws the two streams remain in lockstep.
    EXPECT_EQ(weapon.Rng().Range(0, 1000000), reference.Range(0, 1000000));
}

// NOLINTEND(readability-magic-numbers)
