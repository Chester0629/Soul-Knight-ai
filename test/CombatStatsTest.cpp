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

// NOLINTEND(readability-magic-numbers)
