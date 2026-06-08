#include <gtest/gtest.h>

#include "combat/CombatStats.hpp"

using Game::CharacterDef;
using Game::CombatStats;

namespace {
CombatStats Make(int hp, int armor, int energy) {
    CombatStats s;
    s.hp = s.maxHp = hp;
    s.armor = s.maxArmor = armor;
    s.energy = s.maxEnergy = energy;
    return s;
}
} // namespace

// NOLINTBEGIN(readability-magic-numbers)

TEST(CombatStatsTest, ArmorAbsorbsBeforeHp) {
    auto s = Make(6, 3, 0);
    s.TakeDamage(2);
    EXPECT_EQ(s.armor, 1);
    EXPECT_EQ(s.hp, 6);
}

TEST(CombatStatsTest, DamageOverflowsArmorIntoHp) {
    auto s = Make(6, 3, 0);
    s.TakeDamage(5); // armor 3 -> 0, 2 overflow -> hp 6 -> 4
    EXPECT_EQ(s.armor, 0);
    EXPECT_EQ(s.hp, 4);
}

TEST(CombatStatsTest, NoArmorHitsHpDirectly) {
    auto s = Make(6, 0, 0);
    s.TakeDamage(2);
    EXPECT_EQ(s.hp, 4);
}

TEST(CombatStatsTest, DeathWhenHpReachesZero) {
    auto s = Make(3, 0, 0);
    s.TakeDamage(5);
    EXPECT_EQ(s.hp, 0);
    EXPECT_TRUE(s.IsDead());
}

TEST(CombatStatsTest, NonPositiveDamageIsNoOp) {
    auto s = Make(6, 3, 0);
    s.TakeDamage(0);
    s.TakeDamage(-4);
    EXPECT_EQ(s.armor, 3);
    EXPECT_EQ(s.hp, 6);
}

TEST(CombatStatsTest, HealClampsToMax) {
    auto s = Make(6, 0, 0);
    s.hp = 2;
    s.Heal(10);
    EXPECT_EQ(s.hp, 6);
}

TEST(CombatStatsTest, ArmorRestoreClampsToMax) {
    auto s = Make(6, 3, 0);
    s.armor = 0;
    s.AddArmor(10);
    EXPECT_EQ(s.armor, 3);
}

TEST(CombatStatsTest, EnergySpendGating) {
    auto s = Make(6, 0, 100);
    EXPECT_TRUE(s.SpendEnergy(30));
    EXPECT_EQ(s.energy, 70);
    EXPECT_FALSE(s.SpendEnergy(100));
    EXPECT_EQ(s.energy, 70);
}

TEST(CombatStatsTest, FromCharacterCopiesStats) {
    CharacterDef c;
    c.maxHp = 6;
    c.hp = 6;
    c.maxArmor = 3;
    c.armor = 3;
    c.maxEnergy = 130;
    c.energy = 130;
    const auto s = CombatStats::FromCharacter(c);
    EXPECT_EQ(s.maxHp, 6);
    EXPECT_EQ(s.hp, 6);
    EXPECT_EQ(s.armor, 3);
    EXPECT_EQ(s.maxEnergy, 130);
    EXPECT_EQ(s.energy, 130);
}

// ---- Deepen: RestoreHealth / RestoreArmor clamp rules --------------------

TEST(CombatStatsTest, RestoreHealthDamagePathFloorsAtZero) {
    // FAITHFUL: RoleAttribute__RestoreHealth @ game_full.c:432288 -- negative
    // amount that overshoots floors hp at 0.
    auto s = Make(3, 0, 0);
    s.RestoreHealth(-5);
    EXPECT_EQ(s.hp, 0);
}

TEST(CombatStatsTest, RestoreHealthHealPathDoesNotCapAtMax) {
    // The decomp's positive branch does NOT clamp to max_hp (that pop-up path is
    // owner-side); RestoreHealth therefore over-heals past maxHp by design.
    auto s = Make(6, 0, 0);
    s.hp = 4;
    s.RestoreHealth(10);
    EXPECT_EQ(s.hp, 14); // unclamped, exactly as RestoreHealth (cf. Heal which caps)
}

TEST(CombatStatsTest, HealStillCapsToMaxAfterDeepen) {
    // Existing Heal() contract is preserved and distinct from RestoreHealth.
    auto s = Make(6, 0, 0);
    s.hp = 4;
    s.Heal(10);
    EXPECT_EQ(s.hp, 6);
}

TEST(CombatStatsTest, RestoreArmorZeroIsNoOp) {
    // FAITHFUL: RestoreArmor @ game_full.c:432491 early-returns on amount == 0.
    auto s = Make(6, 3, 0);
    s.RestoreArmor(0);
    EXPECT_EQ(s.armor, 3);
}

TEST(CombatStatsTest, RestoreArmorGainCapsToMax) {
    auto s = Make(6, 5, 0);
    s.armor = 0;
    s.RestoreArmor(10); // amount >= 1 -> cap to maxArmor
    EXPECT_EQ(s.armor, 5);
}

TEST(CombatStatsTest, RestoreArmorChipFloorsAtZero) {
    auto s = Make(6, 3, 0);
    s.RestoreArmor(-10); // amount < 1 -> floor at 0
    EXPECT_EQ(s.armor, 0);
}

// ---- Deepen: ArmorReloadTick (RoleAttributePlayer__ArmorReload) ----------

TEST(CombatStatsTest, ArmorReloadAccumulatesUntilThreshold) {
    // FAITHFUL: ArmorReload @ game_full.c:432354 -- +1 once armorTime reaches
    // armorLoad + armorRate, then armorTime resets to armorLoad (NOT zero).
    auto s = Make(6, 5, 0);
    s.armor = 0;
    s.armorLoad = 1.0F;
    s.armorRate = 0.5F; // threshold = 1.5s
    EXPECT_FALSE(s.ArmorReloadTick(1.0F));
    EXPECT_EQ(s.armor, 0);
    EXPECT_TRUE(s.ArmorReloadTick(0.5F)); // total 1.5 -> grant
    EXPECT_EQ(s.armor, 1);
    EXPECT_FLOAT_EQ(s.armorTime, 1.0F); // reset to armorLoad, not 0
}

TEST(CombatStatsTest, ArmorReloadStopsAtMaxArmor) {
    auto s = Make(6, 3, 0);
    s.armor = 3; // already full -> guard returns false, no accumulation
    s.armorLoad = 0.0F;
    s.armorRate = 0.0F;
    EXPECT_FALSE(s.ArmorReloadTick(5.0F));
    EXPECT_EQ(s.armor, 3);
    EXPECT_FLOAT_EQ(s.armorTime, 0.0F);
}

// ---- Deepen: EnergyReloadTick (RoleAttributePlayer__EnergyReLoad) --------

TEST(CombatStatsTest, EnergyReloadGrantsAtTwoSeconds) {
    // FAITHFUL: EnergyReLoad @ game_full.c:432415 -- +1 every 2.0s, reset to 0.
    auto s = Make(6, 0, 100);
    s.energy = 0;
    EXPECT_FALSE(s.EnergyReloadTick(1.5F));
    EXPECT_EQ(s.energy, 0);
    EXPECT_TRUE(s.EnergyReloadTick(0.5F)); // total 2.0 -> grant
    EXPECT_EQ(s.energy, 1);
    EXPECT_FLOAT_EQ(s.energyTime, 0.0F); // reset to 0, unlike armor
    EXPECT_FLOAT_EQ(CombatStats::kEnergyReloadInterval, 2.0F);
}

TEST(CombatStatsTest, EnergyReloadStopsAtMaxEnergy) {
    auto s = Make(6, 0, 5);
    s.energy = 5; // full -> guard returns false
    EXPECT_FALSE(s.EnergyReloadTick(10.0F));
    EXPECT_EQ(s.energy, 5);
}

// ---- Deepen: ChangeSpeed / SpeedBack (additive buff + exact revert) -------

TEST(CombatStatsTest, ChangeSpeedThenSpeedBackIsExactInverse) {
    // FAITHFUL: ChangeSpeed @ 432166 (+= value, memo +0x2c) / SpeedBack @ 432274
    // (-= memo). Round-trip must restore the original speed exactly.
    CombatStats s;
    s.speed = 4.0F;
    s.ChangeSpeed(-1.5F); // slow debuff
    EXPECT_FLOAT_EQ(s.speed, 2.5F);
    EXPECT_FLOAT_EQ(s.speedChangeValue, -1.5F);
    s.SpeedBack();
    EXPECT_FLOAT_EQ(s.speed, 4.0F);
}

TEST(CombatStatsTest, FromCharacterCopiesSpeed) {
    CharacterDef c;
    c.maxHp = 6;
    c.speed = 3.5F;
    c.speedRate = 0.25F;
    const auto s = CombatStats::FromCharacter(c);
    EXPECT_FLOAT_EQ(s.speed, 3.5F);
    EXPECT_FLOAT_EQ(s.speedRate, 0.25F);
}

// ---- Deepen: recovered ctor default-stat tables --------------------------

TEST(CombatStatsTest, WeaponDefaultStatsMatchCtor) {
    // FAITHFUL: RGWeapon___ctor @ game_full.c:431736.
    using W = Game::WeaponDefaultStats;
    EXPECT_EQ(W::kAtk, 20);
    EXPECT_EQ(W::kThroughCount, 1);
    EXPECT_FLOAT_EQ(W::kRepel, 3.0F);
    EXPECT_FLOAT_EQ(W::kBulletSpeed, 24.0F);
    EXPECT_EQ(W::kDeviation, 5);
    EXPECT_EQ(W::kConsume, 1);
    EXPECT_FLOAT_EQ(W::kWeaponSpeed, 1.0F);
    EXPECT_FLOAT_EQ(W::kAtkMoveSpeed, 0.1F);
    EXPECT_TRUE(W::kActivate);
}

TEST(CombatStatsTest, EnemyWeaponDefaultStatsMatchCtor) {
    // FAITHFUL: RGEWeapon___ctor @ game_full.c:474299.
    using E = Game::EnemyWeaponDefaultStats;
    EXPECT_EQ(E::kThroughCount, 1);
    EXPECT_FLOAT_EQ(E::kRepel, 3.0F);
    EXPECT_FLOAT_EQ(E::kBulletSpeed, 24.0F);
    EXPECT_EQ(E::kDeviation, 5);
    EXPECT_EQ(E::kConsume, 1);
    EXPECT_TRUE(E::kActivate);
}

// NOLINTEND(readability-magic-numbers)
