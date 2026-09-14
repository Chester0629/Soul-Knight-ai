#include <fstream>

#include <gtest/gtest.h>

#include "data/GameData.hpp"

using Game::GameData;

namespace {
// RESOURCE_DIR is injected by CMake (points at the repo's Resources/ folder).
const char *kResourceRoot = RESOURCE_DIR;

bool ClipExists(const std::string &clip) {
    if (clip.empty()) {
        return false;
    }
    std::ifstream f(std::string(kResourceRoot) + "/audio/" + clip, std::ios::binary);
    return f.good();
}
} // namespace

// NOLINTBEGIN(readability-magic-numbers)

TEST(AudioTableTest, LoadsAudioJson) {
    GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    if (!gd.HasAudio()) {
        GTEST_SKIP() << "audio.json absent (checkout without the sibling _reverse "
                        "dump / Resources/audio -- run tools/extract_audio.py + "
                        "tools/build_gamedata.py)";
    }
}

// FAITHFUL @ RGMusicManager.prefab effect_list[7] / EnemyMaker.ShowUpEffect ->
// RGMusicManager__PlayEffect(this, 7) (game_typed.c). Boss show-up cue.
TEST(AudioTableTest, EffectIndex7IsBossShowUp) {
    GameData gd;
    gd.LoadAll(kResourceRoot);
    if (!gd.HasAudio()) {
        GTEST_SKIP();
    }
    const std::string &clip = gd.FindEffectClip(7);
    EXPECT_EQ(clip, "fx_show_up.wav");
    EXPECT_TRUE(ClipExists(clip));
}

// FAITHFUL @ res/weapon/ring_0.prefab RGWeapon(-derived).audio_clip.
TEST(AudioTableTest, WeaponShootClipResolves) {
    GameData gd;
    gd.LoadAll(kResourceRoot);
    if (!gd.HasAudio()) {
        GTEST_SKIP();
    }
    const std::string &clip = gd.WeaponShootClip("weapon_000");
    EXPECT_FALSE(clip.empty()) << "weapon_000 should carry an audio_clip (193/194 weapons do)";
    EXPECT_TRUE(ClipExists(clip));
}

// FAITHFUL @ Assets/_RGScene/Title.unity UITitle.bgm_clip, which is the SAME
// clip as res/map_manager/map_A1.prefab MapManager.bgm_clip (bgm_1Low.ogg).
TEST(AudioTableTest, MenuBgmResolves) {
    GameData gd;
    gd.LoadAll(kResourceRoot);
    if (!gd.HasAudio()) {
        GTEST_SKIP();
    }
    EXPECT_EQ(gd.BgmMenu(), "bgm_1Low.ogg");
    EXPECT_TRUE(ClipExists(gd.BgmMenu()));
}

// FAITHFUL @ ChapterBiome(1) == "1-ice" -> res/map_manager/map_B*.prefab
// MapManager.bgm_clip (bgm_5Low.ogg; no bgm_5High source clip exists, so the
// "high" request still falls back to "low" -- see GameData::BgmFor).
TEST(AudioTableTest, ChapterBgmFallsBackToLowWithNoHighVariant) {
    GameData gd;
    gd.LoadAll(kResourceRoot);
    if (!gd.HasAudio()) {
        GTEST_SKIP();
    }
    const std::string &low = gd.BgmFor(1, false);
    const std::string &high = gd.BgmFor(1, true);
    EXPECT_EQ(low, "bgm_5Low.ogg");
    EXPECT_EQ(high, "bgm_5Low.ogg"); // no bgm_5High clip shipped -- falls back
    EXPECT_TRUE(ClipExists(low));
}

// Every clip audio.json references must exist under Resources/audio (the same
// contract tools/validate_data.py enforces on the data side).
TEST(AudioTableTest, EveryReferencedClipFileExists) {
    GameData gd;
    gd.LoadAll(kResourceRoot);
    if (!gd.HasAudio()) {
        GTEST_SKIP();
    }
    for (int i = 0; i < 11; ++i) {
        const std::string &clip = gd.FindEffectClip(i);
        if (!clip.empty()) {
            EXPECT_TRUE(ClipExists(clip)) << "effect[" << i << "] -> " << clip;
        }
    }
    EXPECT_TRUE(ClipExists(gd.WeaponShootClip("ring_0")));
    EXPECT_TRUE(ClipExists(gd.BgmMenu()));
    EXPECT_TRUE(ClipExists(gd.BgmMenuButtonClip()));
    EXPECT_TRUE(ClipExists(gd.BgmRoomClip()));
}

// NOLINTEND(readability-magic-numbers)
