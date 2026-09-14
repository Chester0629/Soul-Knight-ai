#include <fstream>
#include <string>
#include <vector>

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
// RGMusicManager__PlayEffect(this, 7) (game_typed.c:78114-78126). Boss show-up
// cue; GameScene schedules it WaveScheduler::kShowUpEffectDelayMs after every
// wave part >= 1 (T-29, game_typed.c:78030-78082).
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

// --- P6.7: BgmFor is keyed on the LEVEL's biome, not the chapter -----------
//
// Every expected clip below is the value audio.json's bgm.by_biome actually
// carries, generated one row per res/map_manager/map_<V><N>.prefab
// MapManager.bgm_clip (variant letter -> biome: A 1-forest, B 1-ice, C 2-castle,
// D 2-graveyard, E 3-alien, F 3-volcano). The three biomes whose "high" slot is
// "" ship no bgm_<N>High clip in rgsound/bgm, so BgmFor(biome, true) must fall
// back to the "low" clip for them.
//
// This is the defect P6.7 fixes: the old BgmFor(chapter, ...) went through
// ChapterBiome(), which only ever names ONE of each chapter's TWO variants
// (1-ice, 2-castle, 3-alien), so a run on an A/D/F floor played the wrong loop.
namespace {
struct BiomeBgmRow {
    const char *biome;
    const char *low;
    const char *high; ///< "" when the source ships no High variant.
};
// All SIX biomes present in Resources/data/audio.json bgm.by_biome.
const BiomeBgmRow kBiomeBgm[] = {
    {"1-forest", "bgm_1Low.ogg", "bgm_1High.ogg"},
    {"1-ice", "bgm_5Low.ogg", ""},
    {"2-castle", "bgm_4Low.ogg", ""},
    {"2-graveyard", "bgm_2Low.ogg", "bgm_2High.ogg"},
    {"3-alien", "bgm_3Low.ogg", "bgm_3High.ogg"},
    {"3-volcano", "bgm_6Low.ogg", ""},
};
} // namespace

TEST(AudioTableTest, BgmForEveryBiomeLowResolves) {
    GameData gd;
    gd.LoadAll(kResourceRoot);
    if (!gd.HasAudio()) {
        GTEST_SKIP();
    }
    for (const BiomeBgmRow &r : kBiomeBgm) {
        const std::string &low = gd.BgmFor(r.biome, false);
        EXPECT_EQ(low, r.low) << "biome " << r.biome;
        EXPECT_TRUE(ClipExists(low)) << "biome " << r.biome << " -> " << low;
    }
}

TEST(AudioTableTest, BgmForEveryBiomeHighResolvesOrFallsBackToLow) {
    GameData gd;
    gd.LoadAll(kResourceRoot);
    if (!gd.HasAudio()) {
        GTEST_SKIP();
    }
    for (const BiomeBgmRow &r : kBiomeBgm) {
        const std::string expected = (r.high[0] != '\0') ? r.high : r.low;
        const std::string &high = gd.BgmFor(r.biome, true);
        EXPECT_EQ(high, expected) << "biome " << r.biome;
        EXPECT_TRUE(ClipExists(high)) << "biome " << r.biome << " -> " << high;
    }
}

// The six biomes are NOT interchangeable per chapter: each chapter's two
// variants carry DIFFERENT clips, which is exactly what keying on the chapter
// number could never express.
TEST(AudioTableTest, BgmDiffersBetweenTheTwoVariantsOfEachChapter) {
    GameData gd;
    gd.LoadAll(kResourceRoot);
    if (!gd.HasAudio()) {
        GTEST_SKIP();
    }
    EXPECT_NE(gd.BgmFor("1-forest", false), gd.BgmFor("1-ice", false));
    EXPECT_NE(gd.BgmFor("2-castle", false), gd.BgmFor("2-graveyard", false));
    EXPECT_NE(gd.BgmFor("3-alien", false), gd.BgmFor("3-volcano", false));
}

// An unknown biome key (e.g. levels.json's G1 "end" floor, whose map_G1.prefab
// has `bgm_clip: {fileID: 0}` -- no clip at all) yields "", which AudioPlayer
// treats as "keep playing whatever is playing".
TEST(AudioTableTest, BgmForUnknownBiomeIsEmpty) {
    GameData gd;
    gd.LoadAll(kResourceRoot);
    if (!gd.HasAudio()) {
        GTEST_SKIP();
    }
    EXPECT_TRUE(gd.BgmFor("end", false).empty());
    EXPECT_TRUE(gd.BgmFor("not-a-biome", true).empty());
}

// --- T-35: the ui_effects table (clips GameScene plays directly) -----------
TEST(AudioTableTest, UiEffectClipsResolve) {
    GameData gd;
    gd.LoadAll(kResourceRoot);
    if (!gd.HasAudio()) {
        GTEST_SKIP();
    }
    // Every key GameScene.cpp looks up must exist in the table -- otherwise the
    // call site silently falls through to the single documented literal.
    for (const char *key : {"chest_open", "player_hurt", "enemy_hurt", "player_death",
                            "enemy_death", "boss_death"}) {
        const std::string &clip = gd.UiEffectClip(key);
        EXPECT_FALSE(clip.empty()) << "ui_effects." << key << " missing from audio.json";
        EXPECT_TRUE(ClipExists(clip)) << "ui_effects." << key << " -> " << clip;
    }
    // FAITHFUL row: res/item/chest01.prefab RGChest.audio_clip.
    EXPECT_EQ(gd.UiEffectClip("chest_open"), "fx_chest_open.wav");
    EXPECT_TRUE(gd.UiEffectClip("no_such_ui_effect").empty());
}

// EXHAUSTIVE: every clip the loaded AudioTable references must exist under
// Resources/audio -- walked programmatically over the parsed struct (effects,
// bgm menu/menu_button/room, bgm.by_biome low+high for ALL biomes, weapon_shoot,
// enemy_gun_shoot, ui_effects), so a new table/row cannot slip past this gate by
// simply not being hand-listed here. Mirrors the same contract
// tools/validate_data.py enforces on the data side.
TEST(AudioTableTest, EveryReferencedClipFileExists) {
    GameData gd;
    gd.LoadAll(kResourceRoot);
    if (!gd.HasAudio()) {
        GTEST_SKIP();
    }
    const Game::AudioTable &t = gd.Audio();
    int checked = 0;
    auto check = [&checked](const std::string &what, const std::string &clip) {
        if (clip.empty()) {
            return; // an intentionally-absent slot (e.g. a biome with no High variant)
        }
        ++checked;
        EXPECT_TRUE(ClipExists(clip)) << what << " -> " << clip;
    };

    for (const auto &kv : t.effects) {
        check("effects[" + std::to_string(kv.first) + "]", kv.second);
    }
    check("bgm.menu", t.bgmMenu);
    check("bgm.menu_button", t.bgmMenuButton);
    check("bgm.room", t.bgmRoom);
    for (const auto &kv : t.bgmByBiome) {
        check("bgm.by_biome." + kv.first + ".low", kv.second.low);
        check("bgm.by_biome." + kv.first + ".high", kv.second.high);
    }
    for (const auto &kv : t.weaponShoot) {
        check("weapon_shoot." + kv.first, kv.second);
    }
    for (const auto &kv : t.enemyGunShoot) {
        check("enemy_gun_shoot." + kv.first, kv.second);
    }
    for (const auto &kv : t.uiEffects) {
        check("ui_effects." + kv.first, kv.second);
    }

    // Guard the guard: if a future refactor empties the table, this test must not
    // silently pass by checking nothing. 11 effects + 3 bgm singles + 6 biomes +
    // 193 weapon_shoot + 9 enemy_gun_shoot + 6 ui_effects is well over 200 rows.
    EXPECT_GT(checked, 200) << "audio table looks empty -- only " << checked
                            << " clip references walked";
    EXPECT_FALSE(t.bgmByBiome.empty());
    EXPECT_FALSE(t.uiEffects.empty());
}

// NOLINTEND(readability-magic-numbers)
