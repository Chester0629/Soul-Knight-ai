#include <gtest/gtest.h>

#include <string>

#include "game/RunController.hpp"
#include "game/RunState.hpp"

// B4 RESULTS SCREEN: the pure data behind the Statements-scene port -- the
// RGGameProcess counters (kill_count / start_time), the level_list label and
// the clock text -- and the controller's accumulate / reset contract. The
// EndScene rendering itself needs GL and is proven by the headless smoke run
// (SK_FORCE_DIE + SK_SHOT).
namespace {

TEST(RunStatsTest, LevelLabelIsTheOriginalLevelList) {
    // FAITHFUL @ Statements.unity UIStatement.level_list (16 entries).
    EXPECT_STREQ(Game::LevelLabel(0), "1-1");
    EXPECT_STREQ(Game::LevelLabel(4), "1-5");
    EXPECT_STREQ(Game::LevelLabel(5), "2-1");
    EXPECT_STREQ(Game::LevelLabel(9), "2-5");
    EXPECT_STREQ(Game::LevelLabel(10), "3-1");
    EXPECT_STREQ(Game::LevelLabel(15), "3-6");
    // Out of range clamps to the ends (the port keeps running past the 16 cap).
    EXPECT_STREQ(Game::LevelLabel(-3), "1-1");
    EXPECT_STREQ(Game::LevelLabel(40), "3-6");
}

TEST(RunStatsTest, ClockIsHoursMinutesSeconds) {
    EXPECT_EQ(Game::FormatRunClock(0.0F), "0:00:00");
    EXPECT_EQ(Game::FormatRunClock(999.0F), "0:00:00"); // truncates
    EXPECT_EQ(Game::FormatRunClock(61000.0F), "0:01:01");
    EXPECT_EQ(Game::FormatRunClock(749000.0F), "0:12:29"); // the scene's placeholder
    EXPECT_EQ(Game::FormatRunClock(3600000.0F + 5000.0F), "1:00:05");
    EXPECT_EQ(Game::FormatRunClock(-5.0F), "0:00:00");
}

TEST(RunStatsTest, ControllerAccumulatesKillsAndTime) {
    Game::RunController run(11);
    EXPECT_EQ(run.State().stats.kills, 0);
    EXPECT_FLOAT_EQ(run.State().stats.timeMs, 0.0F);
    EXPECT_EQ(run.State().outcome, Game::RunState::Outcome::None);

    run.RecordKill();
    run.RecordKill();
    run.RecordTime(16.0F);
    run.RecordTime(16.0F);
    EXPECT_EQ(run.State().stats.kills, 2);
    EXPECT_FLOAT_EQ(run.State().stats.timeMs, 32.0F);
    // Counters are run-long: advancing a floor keeps them.
    run.AdvanceFloor(Game::PlayerContinuation{});
    run.RecordKill();
    EXPECT_EQ(run.State().stats.kills, 3);
    EXPECT_EQ(run.State().floorIndex, 1);
    // Coins / gems are untouched until the economy is ported: real zeros.
    EXPECT_EQ(run.State().stats.coins, 0);
    EXPECT_EQ(run.State().stats.gemsThisRun, 0);
}

TEST(RunStatsTest, ResetClearsStatsAndOutcomeButKeepsHero) {
    Game::RunController run(11);
    run.SetHero("c07");
    run.RecordKill();
    run.RecordTime(500.0F);
    run.ResetRunState();
    EXPECT_EQ(run.State().stats.kills, 0);
    EXPECT_FLOAT_EQ(run.State().stats.timeMs, 0.0F);
    EXPECT_EQ(run.State().outcome, Game::RunState::Outcome::None);
    EXPECT_EQ(run.State().charId, "c07");
}

} // namespace
