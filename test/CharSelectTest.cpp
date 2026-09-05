#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "data/GameData.hpp"
#include "game/CharSelectModel.hpp"
#include "game/RunController.hpp"

// B5/B6 ENTRY FLOW: a pure hero-select model over the real 13-hero roster, real
// hero names from the i2 localization bundle, and the run's chosen hero held in
// RunState via RunController's pure setters (StartRun itself needs GL and is
// exercised by the headless smoke run with SK_START_CHAR / SK_CHAR).
namespace {

const char *kResourceRoot = RESOURCE_DIR;

TEST(CharSelectTest, ModelCyclesWithWrapAndSelectsById) {
    Game::CharSelectModel m({"c01", "c02", "c03"});
    EXPECT_EQ(m.Count(), 3u);
    EXPECT_EQ(m.Current(), "c01");
    m.Prev();
    EXPECT_EQ(m.Current(), "c03"); // wraps backwards
    m.Next();
    m.Next();
    EXPECT_EQ(m.Current(), "c02");
    EXPECT_TRUE(m.Select("c03"));
    EXPECT_EQ(m.Index(), 2u);
    EXPECT_FALSE(m.Select("c99")); // unknown: cursor unchanged
    EXPECT_EQ(m.Current(), "c03");
}

TEST(CharSelectTest, EmptyRosterIsSafe) {
    Game::CharSelectModel m(std::vector<std::string>{});
    EXPECT_EQ(m.Count(), 0u);
    m.Next();
    m.Prev();
    EXPECT_FALSE(m.Select("c01"));
}

TEST(CharSelectTest, RosterCarriesRealNames) {
    Game::GameData gd;
    gd.LoadAll(kResourceRoot);
    ASSERT_EQ(gd.Characters().size(), 13u);
    const Game::CharacterDef *knight = gd.FindCharacter("c01");
    const Game::CharacterDef *druid = gd.FindCharacter("c12");
    ASSERT_NE(knight, nullptr);
    ASSERT_NE(druid, nullptr);
    EXPECT_EQ(knight->name, "Knight"); // i2 Character1_name_skin0
    EXPECT_EQ(druid->name, "Druid");   // i2 Character12_name_skin0
    for (const Game::CharacterDef &c : gd.Characters()) {
        EXPECT_FALSE(c.name.empty()) << c.id;
    }
}

TEST(CharSelectTest, RunKeepsTheChosenHeroAcrossReset) {
    Game::RunController run(7);
    EXPECT_EQ(run.State().charId, "c01"); // default hero
    run.SetHero("c12");
    EXPECT_EQ(run.State().charId, "c12");
    run.ResetRunState(); // an EndScene restart replays the SAME hero
    EXPECT_EQ(run.State().charId, "c12");
    EXPECT_EQ(run.State().floorIndex, 0);
    EXPECT_FALSE(run.State().carried.has_value());
}

} // namespace
