#include <gtest/gtest.h>

#include "combat/Gun006Paw.hpp"
#include "data/RGRandom.hpp"

using Game::Gun006Paw;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- StatAfterBuff: success-path stat += atk (317035) -----------------------

TEST(Gun006PawTest, StatAfterBuffAddsAtkToStat) {
    // FAITHFUL: *piVar3 = *piVar3 + atk(0xc). Pure int add, no clamp.
    EXPECT_EQ(Gun006Paw::StatAfterBuff(10, 5), 15);
    EXPECT_EQ(Gun006Paw::StatAfterBuff(0, 7), 7);
    EXPECT_EQ(Gun006Paw::StatAfterBuff(100, 0), 100);
}

TEST(Gun006PawTest, StatAfterBuffHasNoClamp) {
    // The decomp applies no clamp: a negative atk lowers the stat exactly as the
    // raw `*piVar3 + atk` would.
    EXPECT_EQ(Gun006Paw::StatAfterBuff(3, -8), -5);
}

// ---- SwordDmgFromTarget: success-path pass-through copy (317062) -------------

TEST(Gun006PawTest, SwordDmgFromTargetIsPassThrough) {
    // FAITHFUL: sword[0x20] = *(stat-source + 0x40). The sword damage BECOMES the
    // supplied source-stat value verbatim.
    EXPECT_EQ(Gun006Paw::SwordDmgFromTarget(42), 42);
    EXPECT_EQ(Gun006Paw::SwordDmgFromTarget(0), 0);
    EXPECT_EQ(Gun006Paw::SwordDmgFromTarget(-1), -1);
}

// ---- FallbackSwordDmg: no-resolve path sword.dmg = atk + 3 (317074) ----------

TEST(Gun006PawTest, FallbackSwordDmgIsAtkPlusThree) {
    // FAITHFUL: sword[0x20] = atk(0xc) + 3.
    EXPECT_EQ(Gun006Paw::FallbackSwordDmg(0), 3);
    EXPECT_EQ(Gun006Paw::FallbackSwordDmg(7), 10);
    EXPECT_EQ(Gun006Paw::FallbackSwordDmg(-3), 0);
}

TEST(Gun006PawTest, FallbackBonusMatchesNamedConstant) {
    // The +3 is the recovered immediate at 317074; assert the math tracks it
    // (this exercises the relationship, not a TODO[verify] magic value).
    const int atk = 12;
    EXPECT_EQ(Gun006Paw::FallbackSwordDmg(atk), atk + Gun006Paw::kFallbackDmgBonus);
}

// ---- RNG determinism: Gun006Paw's modelled math draws ZERO from the stream ---

TEST(Gun006PawTest, BuffMathConsumesNoRandomDraws) {
    // Gun006Paw is deterministic (no rg_random in any body). A parallel
    // same-seeded stream must stay in lockstep (un-advanced) after exercising
    // every modelled method.
    Gun006Paw weapon;
    weapon.SetSeed(135790);
    RGRandom reference;
    reference.SetRandomSeed(135790);

    for (int i = 0; i < 16; ++i) {
        (void)Gun006Paw::StatAfterBuff(i, i + 1);
        (void)Gun006Paw::SwordDmgFromTarget(i * 2);
        (void)Gun006Paw::FallbackSwordDmg(i);
    }

    // The weapon stream must be byte-for-byte where it started: its next draw
    // equals the reference's first draw (no hidden advance happened).
    EXPECT_EQ(weapon.Rng().Range(0, 1000000), reference.Range(0, 1000000));
}

// NOLINTEND(readability-magic-numbers)
