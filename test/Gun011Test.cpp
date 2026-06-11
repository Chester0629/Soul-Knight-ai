#include <gtest/gtest.h>

#include "combat/Gun011.hpp"
#include "data/RGRandom.hpp"

using Game::Gun011;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- ScatterHalfAngle: base + base*recoil = base*(1+recoil), ZERO draws ------

TEST(Gun011Test, ScatterHalfAngleIsBasePlusBaseTimesRecoil) {
    // FAITHFUL: Attack @ 964211 -- fVar4 = fVar4 + fVar4 * recoil.
    EXPECT_FLOAT_EQ(Gun011::ScatterHalfAngle(10.0F, 0.5F), 15.0F); // 10 + 10*0.5
    EXPECT_FLOAT_EQ(Gun011::ScatterHalfAngle(8.0F, 0.0F), 8.0F);   // no recoil
    EXPECT_FLOAT_EQ(Gun011::ScatterHalfAngle(8.0F, 1.0F), 16.0F);  // double
}

TEST(Gun011Test, ScatterHalfAngleNegativeRecoilTightens) {
    // The decomp applies no clamp: a negative recoil shrinks the spread, exactly
    // as base + base*recoil divides it down.
    EXPECT_FLOAT_EQ(Gun011::ScatterHalfAngle(10.0F, -0.25F), 7.5F);
}

TEST(Gun011Test, ScatterHalfAngleZeroBaseIsZero) {
    EXPECT_FLOAT_EQ(Gun011::ScatterHalfAngle(0.0F, 3.0F), 0.0F);
}

// ---- ScatterAngle: ONE max-inclusive float draw, symmetric about 0 ----------

TEST(Gun011Test, ScatterAngleMatchesParallelStreamSingleDraw) {
    // FAITHFUL: Attack @ 964217 -- RGRandom__Range(this+0x60, -half, half) is the
    // max-INCLUSIVE float overload, EXACTLY ONE draw. A parallel same-seeded
    // RGRandom drawing Range(-half, half) once must produce the same value.
    const float half = Gun011::ScatterHalfAngle(12.0F, 0.5F); // 18.0
    Gun011 weapon;
    weapon.SetSeed(424242);
    RGRandom reference;
    reference.SetRandomSeed(424242);

    const float got = weapon.ScatterAngle(half);
    const float want = reference.Range(-half, half);
    EXPECT_FLOAT_EQ(got, want);

    // Exactly one draw was consumed: the streams remain in lockstep afterwards.
    EXPECT_EQ(weapon.Rng().Range(0, 1000000), reference.Range(0, 1000000));
}

TEST(Gun011Test, ScatterAngleStaysWithinHalfBound) {
    // Range(-half, half) is symmetric: the drawn angle never leaves [-half, half].
    Gun011 weapon;
    weapon.SetSeed(7);
    const float half = 20.0F;
    for (int i = 0; i < 64; ++i) {
        const float a = weapon.ScatterAngle(half);
        EXPECT_GE(a, -half);
        EXPECT_LE(a, half);
    }
}

// ---- Attack: chains half-angle (0 draws) + scatter (1 draw) ------------------

TEST(Gun011Test, AttackConsumesExactlyOneDraw) {
    // FAITHFUL: Gun011__Attack @ 964176. The half-angle math takes no draw; the
    // single Range(-half,+half) is the only RNG consumption per shot.
    Gun011 weapon;
    weapon.SetSeed(99);
    RGRandom reference;
    reference.SetRandomSeed(99);

    const float baseAngle = 9.0F;
    const float recoil = 0.5F;
    const float half = Gun011::ScatterHalfAngle(baseAngle, recoil); // 13.5

    const float got = weapon.Attack(baseAngle, recoil);
    const float want = reference.Range(-half, half); // exactly one draw
    EXPECT_FLOAT_EQ(got, want);

    // Lockstep confirms Attack drew once and only once.
    EXPECT_EQ(weapon.Rng().Range(0, 1000000), reference.Range(0, 1000000));
}

// ---- CreateEndShootBullet: identical formula + identical single draw --------

TEST(Gun011Test, EndShootMatchesAttackFormulaAndDrawCount) {
    // The two decomp bodies are byte-for-byte identical: same base+base*recoil
    // half-angle and the same single Range(-half,+half) draw. Two same-seeded
    // weapons -- one calling Attack, one CreateEndShootBullet -- must agree.
    Gun011 viaAttack;
    viaAttack.SetSeed(31337);
    Gun011 viaEndShoot;
    viaEndShoot.SetSeed(31337);

    const float baseAngle = 14.0F;
    const float recoil = 0.25F;

    const float a = viaAttack.Attack(baseAngle, recoil);
    const float e = viaEndShoot.CreateEndShootBullet(baseAngle, recoil);
    EXPECT_FLOAT_EQ(a, e);

    // Both consumed exactly one draw -> still in lockstep.
    EXPECT_EQ(viaAttack.Rng().Range(0, 1000000),
              viaEndShoot.Rng().Range(0, 1000000));
}

TEST(Gun011Test, EndShootSingleDrawAgainstParallelStream) {
    Gun011 weapon;
    weapon.SetSeed(2024);
    RGRandom reference;
    reference.SetRandomSeed(2024);

    const float baseAngle = 6.0F;
    const float recoil = 1.0F;
    const float half = Gun011::ScatterHalfAngle(baseAngle, recoil); // 12.0

    const float got = weapon.CreateEndShootBullet(baseAngle, recoil);
    const float want = reference.Range(-half, half);
    EXPECT_FLOAT_EQ(got, want);
    EXPECT_EQ(weapon.Rng().Range(0, 1000000), reference.Range(0, 1000000));
}

// NOLINTEND(readability-magic-numbers)
