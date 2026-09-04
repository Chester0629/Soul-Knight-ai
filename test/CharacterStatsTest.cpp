#include <gtest/gtest.h>

#include "data/GameData.hpp"

// PER-CHARACTER RE-KEY (B2-P2): each playable character (c01..c13) now carries its
// REAL base stats from the c##.prefab RoleAttributePlayer components, replacing the
// old single shared player_template. Ground-truth values are read directly from
// _reverse/.../ExportedProject/Assets/res/player/c##.prefab.
namespace {

const char *kResourceRoot = RESOURCE_DIR;

Game::GameData LoadGameData() {
    Game::GameData gd;
    gd.LoadAll(kResourceRoot);
    return gd;
}

TEST(CharacterStatsTest, RosterHasThirteenCharacters) {
    const Game::GameData gd = LoadGameData();
    EXPECT_EQ(gd.Characters().size(), 13u);
}

TEST(CharacterStatsTest, KnightC01IsFaithful) {
    const Game::GameData gd = LoadGameData();
    const Game::CharacterDef *c = gd.FindCharacter("c01");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->maxHp, 6);
    EXPECT_EQ(c->maxArmor, 5);
    EXPECT_EQ(c->maxEnergy, 180);
    EXPECT_EQ(c->atk, 4);               // FAITHFUL: c01 atk (was a hand-patched 5)
    EXPECT_FLOAT_EQ(c->skillCd, 10.0f); // FAITHFUL: c01 skill_cd (was 2.5)
}

TEST(CharacterStatsTest, DruidC12IsFaithful) {
    const Game::GameData gd = LoadGameData();
    const Game::CharacterDef *c = gd.FindCharacter("c12");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->maxArmor, 3);
    EXPECT_EQ(c->maxEnergy, 130);
    EXPECT_FLOAT_EQ(c->skillCd, 2.5f);
}

TEST(CharacterStatsTest, CharactersHaveDistinctStats) {
    const Game::GameData gd = LoadGameData();
    const Game::CharacterDef *c10 = gd.FindCharacter("c10");
    const Game::CharacterDef *c08 = gd.FindCharacter("c08");
    ASSERT_NE(c10, nullptr);
    ASSERT_NE(c08, nullptr);
    EXPECT_EQ(c10->maxHp, 11);   // glass-cannon HP
    EXPECT_EQ(c10->maxArmor, 1);
    EXPECT_EQ(c08->maxArmor, 8); // heavy-armor character
}

TEST(CharacterStatsTest, PlayerTemplateIsFaithfulC01) {
    const Game::GameData gd = LoadGameData();
    // The shared default template is re-pointed at c01 (Knight) -- faithful default.
    EXPECT_EQ(gd.PlayerTemplate().maxArmor, 5);
    EXPECT_EQ(gd.PlayerTemplate().maxEnergy, 180);
    EXPECT_EQ(gd.PlayerTemplate().atk, 4);
    EXPECT_FLOAT_EQ(gd.PlayerTemplate().skillCd, 10.0f);
}

TEST(CharacterStatsTest, UnknownCharacterIsNull) {
    const Game::GameData gd = LoadGameData();
    EXPECT_EQ(gd.FindCharacter("c99"), nullptr);
}

} // namespace
