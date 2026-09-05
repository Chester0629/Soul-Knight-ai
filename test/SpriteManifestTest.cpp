#include <gtest/gtest.h>

#include <string>

#include "data/GameData.hpp"

// SPRITE MANIFEST (fix 2a): entities load their REAL per-species animation frames
// from sprites.json -- the prefab -> Animator -> clip -> frame chain resolved from
// the AssetRipper project (the raw 1.07 dump lost the sprite refs, fileID: 0).
// Ground truth below was resolved by hand: e_boar01's run clip references the
// sprite guids that are enemy27_0..3 (enemy art is atlas-named, never by species);
// c01's Animator default (idle) state is frames c01_4..7, not the 0..8 the old
// hardcoded cycle used.
namespace {

const char *kResourceRoot = RESOURCE_DIR;

Game::GameData LoadGameData() {
    Game::GameData gd;
    gd.LoadAll(kResourceRoot);
    return gd;
}

TEST(SpriteManifestTest, ManifestLoadsEnemiesBossesAndCharacters) {
    const Game::GameData gd = LoadGameData();
    EXPECT_NE(gd.FindSprites("e_boar01"), nullptr);
    EXPECT_NE(gd.FindSprites("e_hammer"), nullptr);
    EXPECT_NE(gd.FindSprites("boss01"), nullptr);
    EXPECT_NE(gd.FindSprites("c01"), nullptr);
    EXPECT_NE(gd.FindSprites("c12"), nullptr);
}

TEST(SpriteManifestTest, BoarRunIsTheEnemy27Atlas) {
    const Game::GameData gd = LoadGameData();
    const auto frames = gd.SpriteFrames("e_boar01", "R", "run");
    ASSERT_EQ(frames.size(), 4u);
    EXPECT_EQ(frames[0], "R/sprites/enemy27_0.png");
    EXPECT_EQ(frames[1], "R/sprites/enemy27_1.png");
    EXPECT_EQ(frames[3], "R/sprites/enemy27_3.png");
}

TEST(SpriteManifestTest, KnightIdleIsFrames4To7) {
    const Game::GameData gd = LoadGameData();
    const Game::SpriteSet *s = gd.FindSprites("c01");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->defaultState, "ide");
    const auto frames = gd.SpriteFrames("c01", "R"); // default (idle) state
    ASSERT_EQ(frames.size(), 4u);
    EXPECT_EQ(frames[0], "R/sprites/c01_4.png");
    EXPECT_EQ(frames[3], "R/sprites/c01_7.png");
}

TEST(SpriteManifestTest, FrameIntervalComesFromClipSampleRate) {
    const Game::GameData gd = LoadGameData();
    EXPECT_EQ(gd.SpriteIntervalMs("e_boar01", 120), 125); // 8 fps
    EXPECT_EQ(gd.SpriteIntervalMs("boss01", 150), 83);    // 12 fps
    EXPECT_EQ(gd.SpriteIntervalMs("no_such_entity", 120), 120);
}

TEST(SpriteManifestTest, EmptyDefaultStateFallsBackDeterministically) {
    const Game::GameData gd = LoadGameData();
    // e_battery01's Animator default state has no body frames; the loader must
    // still yield a non-empty frame set, and the same one every call.
    const auto a = gd.SpriteFrames("e_battery01", "R");
    EXPECT_FALSE(a.empty());
    EXPECT_EQ(a, gd.SpriteFrames("e_battery01", "R"));
}

TEST(SpriteManifestTest, UnknownEntityHasNoFrames) {
    const Game::GameData gd = LoadGameData();
    EXPECT_EQ(gd.FindSprites("no_such_entity"), nullptr);
    EXPECT_TRUE(gd.SpriteFrames("no_such_entity", "R").empty());
}

} // namespace
