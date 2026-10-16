#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "data/GameData.hpp"
#include "game/RenderLayout.hpp"

// Slice 4 (characters): the prefab-derived rig data (shadow / lock ring / hand mount / held weapon) and the
// Animator clip families. FAITHFUL sources: res/player/c##.prefab + res/enemy/**.prefab children `shadow`,
// `shadow_lock`, `h1`; res/weapon/weapon_NNN.prefab children `w`, `gun_point`; res/player_anim controllers.
namespace {

const char *kResourceRoot = RESOURCE_DIR;

bool FileExists(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    return f.good();
}

TEST(ActorRigDataTest, KnightRigMatchesTheC01Prefab) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::CharacterDef *c01 = gd.FindCharacter("c01");
    ASSERT_NE(c01, nullptr);
    const Game::ActorRigDef &r = c01->rig;
    // child `shadow`: shadow3, (0, -0.05), layer 3 order 1, enabled, white m_Color.
    ASSERT_TRUE(r.shadow.present);
    EXPECT_EQ(r.shadow.sprite, "shadow3.png");
    EXPECT_FLOAT_EQ(r.shadow.offsetX, 0.0F);
    EXPECT_FLOAT_EQ(r.shadow.offsetY, -0.05F);
    EXPECT_EQ(r.shadow.sortLayer, 3);
    EXPECT_EQ(r.shadow.sortOrder, 1);
    EXPECT_TRUE(r.shadow.enabled);
    EXPECT_FLOAT_EQ(r.shadow.pivotX, 0.5F);
    EXPECT_FLOAT_EQ(r.shadow.pivotY, 0.0F);
    // child `shadow_lock`: the lock ring, disabled in the prefab, green m_Color (60, 233, 75)/255 (== the
    // skill-ready colour RGController$$UpdateShadowLock sets), layer 3 order 0 (under the shadow).
    ASSERT_TRUE(r.ring.present);
    EXPECT_EQ(r.ring.sprite, "shadow_lock.png");
    EXPECT_FALSE(r.ring.enabled);
    EXPECT_NEAR(r.ring.color[0], 60.0F / 255.0F, 1e-5F);
    EXPECT_NEAR(r.ring.color[1], 233.0F / 255.0F, 1e-5F);
    EXPECT_NEAR(r.ring.color[2], 75.0F / 255.0F, 1e-5F);
    EXPECT_FLOAT_EQ(r.ring.color[3], 1.0F);
    EXPECT_EQ(r.ring.sortLayer, 3);
    EXPECT_EQ(r.ring.sortOrder, 0);
    // hand mount h1 (0, 0.5) with RGHand.target_layer 6 (above the body's layer 5).
    ASSERT_TRUE(r.hasHand);
    EXPECT_FLOAT_EQ(r.handX, 0.0F);
    EXPECT_FLOAT_EQ(r.handY, 0.5F);
    EXPECT_EQ(r.handLayer, 6);
    EXPECT_GT(r.handLayer, Game::RenderLayout::kLayerCharacter);
}

TEST(ActorRigDataTest, EveryHeroHasAHandAndARing) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    ASSERT_EQ(gd.Characters().size(), 13U);
    for (const Game::CharacterDef &c : gd.Characters()) {
        EXPECT_TRUE(c.rig.hasHand) << c.id;
        EXPECT_TRUE(c.rig.shadow.present) << c.id;
        EXPECT_TRUE(c.rig.ring.present) << c.id;
        EXPECT_FALSE(c.rig.ring.enabled) << c.id; // the game turns it on, no prefab ships it on
        // the green ring tint: c01 is exactly (60, 233, 75)/255; the other prefabs differ by < 0.003 per channel
        EXPECT_NEAR(c.rig.ring.color[0], 60.0F / 255.0F, 3e-3F) << c.id;
        EXPECT_NEAR(c.rig.ring.color[1], 233.0F / 255.0F, 3e-3F) << c.id;
        EXPECT_NEAR(c.rig.ring.color[2], 75.0F / 255.0F, 3e-3F) << c.id;
    }
    // c08 holds its weapon on the layer BEHIND the body (h1 target_layer 4) at (0.4, 0.6).
    const Game::CharacterDef *c08 = gd.FindCharacter("c08");
    ASSERT_NE(c08, nullptr);
    EXPECT_EQ(c08->rig.handLayer, 4);
    EXPECT_FLOAT_EQ(c08->rig.handX, 0.4F);
    EXPECT_FLOAT_EQ(c08->rig.handY, 0.6F);
}

TEST(ActorRigDataTest, EnemyRingIsRedAndShadowFollowsThePrefab) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    std::size_t withShadow = 0;
    std::size_t withRing = 0;
    for (const Game::EnemyDef &e : gd.Enemies()) {
        if (e.rig.ring.present) {
            ++withRing;
            // enemy shadow_lock m_Color (0.9118, 0.2346, 0.2346): red, alpha 1; disabled until locked.
            EXPECT_NEAR(e.rig.ring.color[0], 0.911765F, 1e-4F) << e.id;
            EXPECT_NEAR(e.rig.ring.color[1], 0.234645F, 1e-4F) << e.id;
            EXPECT_NEAR(e.rig.ring.color[2], 0.234645F, 1e-4F) << e.id;
            EXPECT_FLOAT_EQ(e.rig.ring.color[3], 1.0F) << e.id;
            EXPECT_FALSE(e.rig.ring.enabled) << e.id;
        }
        if (e.rig.shadow.present) {
            ++withShadow;
            EXPECT_TRUE(e.rig.shadow.enabled) << e.id;
            EXPECT_EQ(e.rig.shadow.sortLayer, 3) << e.id;
            EXPECT_TRUE(e.rig.shadow.sprite == "shadow3.png" || e.rig.shadow.sprite == "shadow1.png") << e.id;
        }
    }
    EXPECT_EQ(withRing, 146U);   // every species prefab ships the ring child
    EXPECT_EQ(withShadow, 143U); // all but boss11, mineral_crystal, mineral_gold
    for (const char *id : {"boss11", "mineral_crystal", "mineral_gold"}) {
        const Game::EnemyDef *e = gd.FindEnemy(id);
        ASSERT_NE(e, nullptr) << id;
        EXPECT_FALSE(e->rig.shadow.present) << id;
    }
    // e_slime01: the plain shadow3, scale 1; boss01 draws its shadow / ring at scale 1.5 (the body stays 1).
    const Game::EnemyDef *slime = gd.FindEnemy("e_slime01");
    ASSERT_NE(slime, nullptr);
    EXPECT_EQ(slime->rig.shadow.sprite, "shadow3.png");
    EXPECT_FLOAT_EQ(slime->rig.shadow.scaleX, 1.0F);
    const Game::EnemyDef *boss = gd.FindEnemy("boss01");
    ASSERT_NE(boss, nullptr);
    EXPECT_FLOAT_EQ(boss->rig.shadow.scaleX, 1.5F);
    EXPECT_FLOAT_EQ(boss->rig.ring.scaleX, 1.5F);
    EXPECT_FLOAT_EQ(boss->bodyScale, 1.0F);
}

TEST(ActorRigDataTest, EveryRigSpriteExistsOnDisk) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    std::set<std::string> sprites;
    for (const Game::EnemyDef &e : gd.Enemies()) {
        sprites.insert(e.rig.shadow.sprite);
        sprites.insert(e.rig.ring.sprite);
    }
    for (const Game::CharacterDef &c : gd.Characters()) {
        sprites.insert(c.rig.shadow.sprite);
        sprites.insert(c.rig.ring.sprite);
    }
    for (const Game::WeaponDef &w : gd.Weapons()) {
        sprites.insert(w.held.sprite.sprite);
    }
    sprites.erase("");
    ASSERT_FALSE(sprites.empty());
    for (const std::string &s : sprites) {
        EXPECT_TRUE(FileExists(std::string(kResourceRoot) + "/sprites/" + s)) << s;
    }
}

TEST(ActorRigDataTest, EveryWeaponPrefabHasAHeldSpriteAndAFiniteGunPoint) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    ASSERT_EQ(gd.Weapons().size(), 194U);
    for (const Game::WeaponDef &w : gd.Weapons()) {
        EXPECT_TRUE(w.held.present) << w.id;
        EXPECT_TRUE(w.held.hasGunPoint) << w.id;
        EXPECT_TRUE(std::isfinite(w.held.gunPointX) && std::isfinite(w.held.gunPointY)) << w.id;
        EXPECT_TRUE(std::isfinite(w.held.sprite.pivotX) && std::isfinite(w.held.sprite.pivotY)) << w.id;
        EXPECT_GE(w.held.sprite.pivotX, 0.0F) << w.id;
        EXPECT_LE(w.held.sprite.pivotX, 1.0F) << w.id;
        // the held sprite is the pickup icon art for every shipped weapon
        EXPECT_EQ(w.held.sprite.sprite, w.icon) << w.id;
    }
}

// ---- Animator clip families --------------------------------------------------------------------

TEST(ActorRigDataTest, RunStateResolvesTheSpeciesOwnFamily) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    EXPECT_EQ(gd.RunState("c01"), "run");
    EXPECT_EQ(gd.RunState("e_slime01"), "run");
    EXPECT_EQ(gd.RunState("e_miner01"), "enemy01_run"); // default state enemy01_ide -> the enemy01_ family
    EXPECT_EQ(gd.RunState("e_knight04"), "enemy02_run");
    EXPECT_EQ(gd.RunState("e_giant"), "giant_run");
    EXPECT_EQ(gd.RunState("e_chest1"), "e_chest_run");
    EXPECT_EQ(gd.RunState("no_such_species"), "");
    // the run frames are real frames, not the idle ones
    EXPECT_NE(gd.SpriteFrames("c01", kResourceRoot, "run"), gd.SpriteFrames("c01", kResourceRoot, "ide"));
}

TEST(ActorRigDataTest, SpeciesWithoutARunClipAreExactlyTheKnownNineteen) {
    // Pinned list (build: tools/validate_data.py --actor-report prints it): these keep the idle clip while they
    // move. Flyers (creeper_fly, mosquito, ufo) and stationary casters (battery, crystal, succulent, tentacle)
    // have a single hover/idle loop in the prefab Animator; bosses 06 / 07 / 10 / 12_1 / 12_2 ship no run state in
    // their controller (P2.16 boss art gaps).
    const std::set<std::string> expected{
        "boss06",       "boss07",    "boss10",          "boss12_1",    "boss12_2", "e_battery01",
        "e_creeper_fly", "e_crystal", "e_mosquito",      "e_succulent", "e_tentacle", "e_tentacle_temp",
        "e_ufo",        "ex_battery01", "ex_creeper_fly", "ex_crystal",  "ex_mosquito", "ex_succulent",
        "ex_ufo"};
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    std::set<std::string> missing;
    std::size_t entities = 0;
    for (const Game::EnemyDef &e : gd.Enemies()) {
        if (gd.FindSprites(e.id) == nullptr) {
            continue; // mineral_*, ex_orc05: no sprite entity at all
        }
        ++entities;
        if (gd.RunState(e.id).empty()) {
            missing.insert(e.id);
        }
    }
    EXPECT_EQ(entities, 143U);
    EXPECT_EQ(missing, expected);
}

TEST(ActorRigDataTest, SpeciesWithoutADeadClipAreExactlyTheKnownFour) {
    // No corpse is drawn for these (SpawnCorpse logs one warning each): boss04 / boss06 art gaps (P2.16) and the
    // tentacle pair, whose controller has no dead state.
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    std::set<std::string> missing;
    for (const Game::EnemyDef &e : gd.Enemies()) {
        if (gd.FindSprites(e.id) != nullptr && gd.DeadState(e.id).empty()) {
            missing.insert(e.id);
        }
    }
    EXPECT_EQ(missing, (std::set<std::string>{"boss04", "boss06", "e_tentacle", "e_tentacle_temp"}));
    // The knight's controller carries all three states of the clip family.
    EXPECT_EQ(gd.DeadState("c01"), "dead");
}

TEST(ActorRigDataTest, NoSpeciesHasAHurtSpriteState) {
    // AC4 "on hit, the hurt state if the Animator has one": none does. The controllers' `hit` trigger drives HitLayer /
    // char_hit (res/player_anim/char_0_skin_0.controller), an EMPTY 0.0667 s clip (no curves) whose animation event
    // calls RGEController.HitBack (a material swap), so the port has no hurt SPRITE to switch to (T-70).
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    std::size_t entities = 0;
    for (const Game::EnemyDef &e : gd.Enemies()) {
        const Game::SpriteSet *s = gd.FindSprites(e.id);
        if (s == nullptr) {
            continue;
        }
        ++entities;
        for (const auto &kv : s->states) {
            EXPECT_EQ(kv.first.find("hit"), std::string::npos) << e.id << " " << kv.first;
            EXPECT_EQ(kv.first.find("hurt"), std::string::npos) << e.id << " " << kv.first;
        }
    }
    for (const Game::CharacterDef &c : gd.Characters()) {
        const Game::SpriteSet *s = gd.FindSprites(c.id);
        ASSERT_NE(s, nullptr) << c.id;
        ++entities;
        for (const auto &kv : s->states) {
            EXPECT_EQ(kv.first.find("hit"), std::string::npos) << c.id << " " << kv.first;
            EXPECT_EQ(kv.first.find("hurt"), std::string::npos) << c.id << " " << kv.first;
        }
    }
    EXPECT_EQ(entities, 156U); // 143 species with art + 13 heroes
}

} // namespace
