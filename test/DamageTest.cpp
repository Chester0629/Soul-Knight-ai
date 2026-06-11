#include <gtest/gtest.h>

#include "combat/CombatStats.hpp"
#include "combat/Damage.hpp"
#include "data/RGRandom.hpp"

using Game::CombatStats;
using Game::RGRandom;
using Game::Combat::AttackerInput;
using Game::Combat::Defender;
using Game::Combat::HitResult;

namespace {

// Mirror the RGRandom golden vector for seed 12345, Range(0,100):
//   {16, 56, 99, 70, 89, 41, 69, 90, 90, 66}. ResolveHit draws one of these per
// call (the crit roll), so we can assert EXACTLY which hits crit.
constexpr int kSeed = 12345;

CombatStats Make(int hp, int armor) {
    CombatStats s;
    s.hp = s.maxHp = hp;
    s.armor = s.maxArmor = armor;
    return s;
}

AttackerInput Atk(int dmg, int critical, float critFactor, float repel) {
    AttackerInput a;
    a.baseDamage = dmg;
    a.critical = critical;
    a.critFactor = critFactor;
    a.repelInputMagnitude = repel;
    return a;
}

} // namespace

// NOLINTBEGIN(readability-magic-numbers)

// --- Attacker side: crit roll determinism ------------------------------------

TEST(DamageTest, CritRollIsDeterministicForSeed) {
    // First roll for seed 12345 is 16. critical=20 -> 16 < 20 -> CRIT.
    RGRandom r;
    r.SetRandomSeed(kSeed);
    const HitResult res = Game::Combat::ResolveHit(
        Atk(10, 20, 2.0F, 0.0F), r, Defender::ENEMY);
    EXPECT_TRUE(res.isCrit);
    EXPECT_EQ(res.finalDamage, 20); // 10 * 2.0 critFactor
}

TEST(DamageTest, NonCritWhenRollAtOrAboveCritical) {
    // First roll is 16. critical=16 -> 16 < 16 is FALSE -> non-crit (crit is
    // strictly roll < critical, faithful to "critical <= roll => normal").
    RGRandom r;
    r.SetRandomSeed(kSeed);
    const HitResult res = Game::Combat::ResolveHit(
        Atk(10, 16, 2.0F, 0.0F), r, Defender::ENEMY);
    EXPECT_FALSE(res.isCrit);
    EXPECT_EQ(res.finalDamage, 10);
}

TEST(DamageTest, SameSeedSameResolutionSequence) {
    RGRandom a;
    RGRandom b;
    a.SetRandomSeed(kSeed);
    b.SetRandomSeed(kSeed);
    for (int i = 0; i < 10; ++i) {
        const HitResult ra =
            Game::Combat::ResolveHit(Atk(7, 50, 2.0F, 3.0F), a, Defender::PLAYER);
        const HitResult rb =
            Game::Combat::ResolveHit(Atk(7, 50, 2.0F, 3.0F), b, Defender::PLAYER);
        EXPECT_EQ(ra.finalDamage, rb.finalDamage) << "hit " << i;
        EXPECT_EQ(ra.isCrit, rb.isCrit) << "hit " << i;
        EXPECT_FLOAT_EQ(ra.repelMagnitude, rb.repelMagnitude) << "hit " << i;
    }
}

TEST(DamageTest, CritRollMatchesGoldenVectorExactly) {
    // Golden rolls for 12345: {16,56,99,70,89,41,69,90,90,66}. With critical=70,
    // crit lands iff roll < 70: 16<70 T, 56<70 T, 99 F, 70 F (==), 89 F, 41 T,
    // 69 T, 90 F, 90 F, 66 T.
    const bool expected[10] = {true, true, false, false, false,
                               true, true, false, false, true};
    RGRandom r;
    r.SetRandomSeed(kSeed);
    for (int i = 0; i < 10; ++i) {
        const HitResult res =
            Game::Combat::ResolveHit(Atk(5, 70, 2.0F, 0.0F), r, Defender::ENEMY);
        EXPECT_EQ(res.isCrit, expected[i]) << "roll index " << i;
    }
}

TEST(DamageTest, CritRollAlwaysConsumesOneDraw) {
    // Even at critical == 0 (never crits) the roll must still be drawn so the
    // stream stays in lockstep with the binary. Verify by comparing a parallel
    // raw RGRandom advanced the same number of times.
    RGRandom resolver;
    RGRandom raw;
    resolver.SetRandomSeed(kSeed);
    raw.SetRandomSeed(kSeed);
    for (int i = 0; i < 5; ++i) {
        const HitResult res = Game::Combat::ResolveHit(
            Atk(3, 0, 2.0F, 0.0F), resolver, Defender::ENEMY);
        EXPECT_FALSE(res.isCrit);
        raw.Range(0, 100); // one matching draw
    }
    // Both streams must now be at the same position.
    EXPECT_EQ(resolver.Range(0, 100), raw.Range(0, 100));
}

// --- Attacker side: damage factor (ice + co-op) ------------------------------

TEST(DamageTest, IceCoopHalvesDamage) {
    RGRandom r;
    r.SetRandomSeed(kSeed); // first roll 16; critical 0 -> non-crit
    AttackerInput a = Atk(10, 0, 2.0F, 0.0F);
    a.iceCoopHalving = true;
    const HitResult res = Game::Combat::ResolveHit(a, r, Defender::ENEMY);
    EXPECT_EQ(res.finalDamage, 5); // (int)(10 * 0.5)
}

TEST(DamageTest, CritFactorAppliesAfterIceFactor) {
    // base 10, ice 0.5 -> 5, crit *2.0 -> 10. roll 16 < critical 20 -> crit.
    RGRandom r;
    r.SetRandomSeed(kSeed);
    AttackerInput a = Atk(10, 20, 2.0F, 0.0F);
    a.iceCoopHalving = true;
    const HitResult res = Game::Combat::ResolveHit(a, r, Defender::ENEMY);
    EXPECT_TRUE(res.isCrit);
    EXPECT_EQ(res.finalDamage, 10); // (int)((int)(10*0.5) * 2.0)
}

// --- Attacker side: repel doubling + hard caps (28 enemy / 30 player) --------

TEST(DamageTest, RepelDoubledOnCritThenCappedEnemy28) {
    // roll 16 < critical 100 -> crit. repel 20 -> doubled 40 -> capped to 28.
    RGRandom r;
    r.SetRandomSeed(kSeed);
    const HitResult res = Game::Combat::ResolveHit(
        Atk(5, 100, 2.0F, 20.0F), r, Defender::ENEMY);
    EXPECT_TRUE(res.isCrit);
    EXPECT_FLOAT_EQ(res.repelMagnitude, 28.0F);
}

TEST(DamageTest, RepelDoubledOnCritThenCappedPlayer30) {
    // crit, repel 20 -> doubled 40 -> capped to 30 (player cap).
    RGRandom r;
    r.SetRandomSeed(kSeed);
    const HitResult res = Game::Combat::ResolveHit(
        Atk(5, 100, 2.0F, 20.0F), r, Defender::PLAYER);
    EXPECT_TRUE(res.isCrit);
    EXPECT_FLOAT_EQ(res.repelMagnitude, 30.0F);
}

TEST(DamageTest, NonCritRepelNotDoubledAndUnderCapPassesThrough) {
    // roll 16; critical 0 -> non-crit. repel 12 stays 12 (< 28 cap).
    RGRandom r;
    r.SetRandomSeed(kSeed);
    const HitResult res = Game::Combat::ResolveHit(
        Atk(5, 0, 2.0F, 12.0F), r, Defender::ENEMY);
    EXPECT_FALSE(res.isCrit);
    EXPECT_FLOAT_EQ(res.repelMagnitude, 12.0F);
}

TEST(DamageTest, NonCritRepelStillCappedWhenOverLimit) {
    // non-crit but raw repel 35 > 30 player cap -> clamped to 30 (cap applies
    // regardless of crit).
    RGRandom r;
    r.SetRandomSeed(kSeed);
    const HitResult res = Game::Combat::ResolveHit(
        Atk(5, 0, 2.0F, 35.0F), r, Defender::PLAYER);
    EXPECT_FALSE(res.isCrit);
    EXPECT_FLOAT_EQ(res.repelMagnitude, 30.0F);
}

// --- Defender PLAYER: armor absorb then overflow -----------------------------

TEST(DamageTest, PlayerArmorAbsorbsThenOverflowsToHp) {
    CombatStats p = Make(6, 3);
    HitResult res;
    res.finalDamage = 5; // armor 3 -> 0, 2 overflow -> hp 6 -> 4
    const bool dead = Game::Combat::ApplyToPlayer(p, res, true);
    EXPECT_EQ(p.armor, 0);
    EXPECT_EQ(p.hp, 4);
    EXPECT_FALSE(dead);
}

TEST(DamageTest, PlayerArmorFullyAbsorbsSmallHit) {
    CombatStats p = Make(6, 3);
    HitResult res;
    res.finalDamage = 2; // armor 3 -> 1, hp untouched
    const bool dead = Game::Combat::ApplyToPlayer(p, res, true);
    EXPECT_EQ(p.armor, 1);
    EXPECT_EQ(p.hp, 6);
    EXPECT_FALSE(dead);
}

TEST(DamageTest, PlayerIFrameGateIgnoresHit) {
    CombatStats p = Make(6, 3);
    HitResult res;
    res.finalDamage = 5;
    const bool dead = Game::Combat::ApplyToPlayer(p, res, /*canHurt=*/false);
    EXPECT_EQ(p.armor, 3); // untouched
    EXPECT_EQ(p.hp, 6);
    EXPECT_FALSE(dead);
}

// --- Defender ENEMY: no armor, straight subtraction --------------------------

TEST(DamageTest, EnemyTakesStraightHpNoArmor) {
    CombatStats e = Make(10, 5); // armor present but MUST be ignored
    HitResult res;
    res.finalDamage = 4;
    const bool dead = Game::Combat::ApplyToEnemy(e, res, true);
    EXPECT_EQ(e.armor, 5); // enemy armor never consumed
    EXPECT_EQ(e.hp, 6);    // 10 - 4
    EXPECT_FALSE(dead);
}

TEST(DamageTest, EnemyGateIgnoresHitWhenNotHurtable) {
    CombatStats e = Make(10, 0);
    HitResult res;
    res.finalDamage = 4;
    const bool dead = Game::Combat::ApplyToEnemy(e, res, /*canHurt=*/false);
    EXPECT_EQ(e.hp, 10);
    EXPECT_FALSE(dead);
}

TEST(DamageTest, EnemyHpCanGoNegativeBeforeDeathCheck) {
    // SyncGetHurt does a straight, unclamped subtraction (no std::max). HP may
    // dip below zero; death is still "hp <= 0".
    CombatStats e = Make(3, 0);
    HitResult res;
    res.finalDamage = 5;
    const bool dead = Game::Combat::ApplyToEnemy(e, res, true);
    EXPECT_EQ(e.hp, -2); // unclamped, faithful to SyncGetHurt
    EXPECT_TRUE(dead);
}

// --- Death at exactly hp == 0 (not < 0) --------------------------------------

TEST(DamageTest, PlayerDeathAtExactlyZeroHp) {
    CombatStats p = Make(5, 0);
    HitResult res;
    res.finalDamage = 5; // hp 5 -> 0 (clamped, player path)
    const bool dead = Game::Combat::ApplyToPlayer(p, res, true);
    EXPECT_EQ(p.hp, 0);
    EXPECT_TRUE(dead); // death is hp <= 0, fires at exactly 0
}

TEST(DamageTest, PlayerSurvivesAtOneHp) {
    CombatStats p = Make(5, 0);
    HitResult res;
    res.finalDamage = 4; // hp 5 -> 1
    const bool dead = Game::Combat::ApplyToPlayer(p, res, true);
    EXPECT_EQ(p.hp, 1);
    EXPECT_FALSE(dead);
}

TEST(DamageTest, EnemyDeathAtExactlyZeroHp) {
    CombatStats e = Make(4, 0);
    HitResult res;
    res.finalDamage = 4; // hp 4 -> 0
    const bool dead = Game::Combat::ApplyToEnemy(e, res, true);
    EXPECT_EQ(e.hp, 0);
    EXPECT_TRUE(dead); // hp <= 0 (not strictly < 0)
}

TEST(DamageTest, EnemySurvivesAtOneHp) {
    CombatStats e = Make(4, 0);
    HitResult res;
    res.finalDamage = 3; // hp 4 -> 1
    const bool dead = Game::Combat::ApplyToEnemy(e, res, true);
    EXPECT_EQ(e.hp, 1);
    EXPECT_FALSE(dead);
}

// --- End-to-end: ResolveHit -> defender, both paths --------------------------

TEST(DamageTest, EndToEndPlayerCritKill) {
    // roll 16 < critical 50 -> crit; base 4 * critFactor 2 = 8. Player hp 6,
    // armor 0 -> dead.
    RGRandom r;
    r.SetRandomSeed(kSeed);
    const HitResult res = Game::Combat::ResolveHit(
        Atk(4, 50, 2.0F, 0.0F), r, Defender::PLAYER);
    EXPECT_TRUE(res.isCrit);
    EXPECT_EQ(res.finalDamage, 8);
    CombatStats p = Make(6, 0);
    EXPECT_TRUE(Game::Combat::ApplyToPlayer(p, res, true));
    EXPECT_EQ(p.hp, 0);
}

TEST(DamageTest, EndToEndEnemyNonCritChip) {
    // critical 0 -> never crit; base 3 straight to enemy hp.
    RGRandom r;
    r.SetRandomSeed(kSeed);
    const HitResult res = Game::Combat::ResolveHit(
        Atk(3, 0, 2.0F, 1.0F), r, Defender::ENEMY);
    EXPECT_FALSE(res.isCrit);
    EXPECT_EQ(res.finalDamage, 3);
    CombatStats e = Make(10, 9); // armor must be ignored
    EXPECT_FALSE(Game::Combat::ApplyToEnemy(e, res, true));
    EXPECT_EQ(e.hp, 7);
    EXPECT_EQ(e.armor, 9);
}

// --- CombatStats additive helpers (blast-radius safety) ----------------------

TEST(DamageTest, CombatStatsApplyPlayerMatchesTakeDamage) {
    CombatStats a = Make(6, 3);
    CombatStats b = Make(6, 3);
    a.TakeDamage(5);
    const bool dead = b.ApplyPlayerDamage(5);
    EXPECT_EQ(a.hp, b.hp);
    EXPECT_EQ(a.armor, b.armor);
    EXPECT_EQ(dead, b.IsDead());
}

// NOLINTEND(readability-magic-numbers)
