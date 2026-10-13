#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

#include <glm/glm.hpp>

#include "data/GameData.hpp"
#include "game/RenderLayout.hpp"
#include "game/WorldScale.hpp"

// P2.17 render slice 1. Pure math + the real data tables (no GL): where a room sprite sits
// (pivot), what it draws over (sorting layer -> z), the camera background. FAITHFUL sources:
// rgtexture/**/<stem>.asset m_Pivot, rgprefab/room/wall/wall601.prefab + box02.prefab
// (base layer 4, w2 overlay layer 6), TagManager sortingLayers, MapManager.camera_bg.
namespace {

namespace RL = Game::RenderLayout;
using Game::Scale::kCellPx;
using Game::Scale::kSpriteScale;

const char *kResourceRoot = RESOURCE_DIR;

// The pre-P2.17 rule: every room sprite bottom-anchored on the cell (pivot (0.5, 0)).
glm::vec2 OldBottomAnchoredCentre(glm::vec2 cellCentre, glm::vec2 sizePx) {
    return cellCentre + glm::vec2(0.0F, sizePx.y * kSpriteScale * 0.5F - kCellPx * 0.5F);
}

TEST(RenderLayoutTest, PivotCentreMatchesUnitySpritePlacement) {
    const glm::vec2 anchor{100.0F, 50.0F};
    // 16x24 pivot (0.5, 0): sprite stands on the anchor, centre half its height up (old rule).
    const glm::vec2 wall = RL::PivotCentre(anchor, {16.0F, 24.0F}, {0.5F, 0.0F}, kSpriteScale);
    EXPECT_FLOAT_EQ(wall.x, 100.0F);
    EXPECT_FLOAT_EQ(wall.y, 50.0F + 24.0F * kSpriteScale * 0.5F);
    // 16x16 pivot (0.5, 0.5): centred on the anchor -> 16 world px LOWER than bottom-anchored.
    const glm::vec2 bush = RL::PivotCentre(anchor, {16.0F, 16.0F}, {0.5F, 0.5F}, kSpriteScale);
    EXPECT_FLOAT_EQ(bush.y, 50.0F);
    EXPECT_FLOAT_EQ(bush.y, RL::PivotCentre(anchor, {16.0F, 16.0F}, {0.5F, 0.0F}, kSpriteScale).y - 16.0F);
    // A pivot with x != 0.5 (statue god (0.6, 0)) shifts the quad horizontally the other way.
    const glm::vec2 god = RL::PivotCentre(anchor, {20.0F, 30.0F}, {0.6F, 0.0F}, kSpriteScale);
    EXPECT_FLOAT_EQ(god.x, 100.0F - 0.1F * 20.0F * kSpriteScale);
}

TEST(RenderLayoutTest, CellAnchorIsTheCellBottomCentre) {
    const glm::vec2 a = RL::CellAnchor({320.0F, 480.0F});
    EXPECT_FLOAT_EQ(a.x, 320.0F);
    EXPECT_FLOAT_EQ(a.y, 480.0F - kCellPx * 0.5F);
}

TEST(RenderLayoutTest, TilePivotTableCarriesTheCenterPivotSprites) {
    Game::GameData gd;
    gd.LoadAll(kResourceRoot);
    ASSERT_GT(gd.TilePivotCount(), 100u);
    // Forest bush wall + crates + tree: 16x16, pivot (0.5, 0.5).
    for (const char *s : {"objects2_50", "objects2_55", "objects2_30", "objects2_31", "objects2_68"}) {
        const auto pv = gd.TilePivot(s);
        EXPECT_FLOAT_EQ(pv[0], 0.5F) << s;
        EXPECT_FLOAT_EQ(pv[1], 0.5F) << s;
    }
    // Walls / floors / doors / shadow2: bottom-centre. Unknown stems default to it as well.
    for (const char *s : {"w601", "f501", "door02", "shadow2", "no_such_sprite"}) {
        const auto pv = gd.TilePivot(s);
        EXPECT_FLOAT_EQ(pv[0], 0.5F) << s;
        EXPECT_FLOAT_EQ(pv[1], 0.0F) << s;
    }
    // Statue pedestal is NOT a plain bottom pivot (statue_01: objects3_6 pivot (0.5, 0.1)).
    EXPECT_FLOAT_EQ(gd.TilePivot("objects3_6")[1], 0.1F);
}

TEST(RenderLayoutTest, ForestBushWallSitsFlushNotHalfAUnitHigh) {
    Game::GameData gd;
    gd.LoadAll(kResourceRoot);
    const Game::LevelDef *a1 = gd.FindLevel("A1");
    ASSERT_NE(a1, nullptr);
    const Game::LevelWallDef &bush = a1->walls[0];
    ASSERT_EQ(bush.prefab, "wall_bush1");
    const glm::vec2 cell{0.0F, 0.0F};
    const glm::vec2 size{16.0F, 16.0F}; // objects2_50 / objects2_55 rects
    // base: centre on the anchor (cell bottom), i.e. 16 px below the old bottom-anchored centre.
    const RL::RoomSpritePlacement base = RL::PlaceRoomSprite(gd, bush.top, size, cell, bush.topSort);
    EXPECT_FLOAT_EQ(base.translation.y, -kCellPx * 0.5F);
    EXPECT_FLOAT_EQ(OldBottomAnchoredCentre(cell, size).y - base.translation.y, 16.0F);
    // w2 face one unit up, also centre pivot: occupies [0.5, 1.5] units above the root.
    const RL::RoomSpritePlacement face =
        RL::PlaceRoomSprite(gd, bush.face, size, cell, bush.faceSort, bush.faceDy);
    EXPECT_FLOAT_EQ(face.translation.y, -kCellPx * 0.5F + kCellPx);
    // wall601 (ice) is a 16x24 bottom-pivot sprite: unchanged by the pivot rule.
    const Game::LevelDef *b1 = gd.FindLevel("B1");
    ASSERT_NE(b1, nullptr);
    const Game::LevelWallDef &w601 = b1->walls[1];
    ASSERT_EQ(w601.top, "w601");
    const RL::RoomSpritePlacement ice = RL::PlaceRoomSprite(gd, w601.top, {16.0F, 24.0F}, cell, w601.topSort);
    EXPECT_FLOAT_EQ(ice.translation.y, OldBottomAnchoredCentre(cell, {16.0F, 24.0F}).y);
}

TEST(RenderLayoutTest, EveryNonDefaultPivotInTheTableMovesTheSpriteByExactlyThatPivot) {
    Game::GameData gd;
    gd.LoadAll(kResourceRoot);
    const auto &table = gd.TilePivots();
    ASSERT_EQ(table.size(), 162u);
    const glm::vec2 cell{0.0F, 0.0F};
    const glm::vec2 size{20.0F, 30.0F};
    std::set<std::string> moved;
    for (const auto &kv : table) {
        const glm::vec2 pv{kv.second[0], kv.second[1]};
        const RL::RoomSpritePlacement pl =
            RL::PlaceRoomSprite(gd, kv.first, size, cell, Game::SortKey{4, 0});
        const glm::vec2 old = OldBottomAnchoredCentre(cell, size);
        // Unity: centre = pivot point + (0.5 - pivot) * size; the old rule was pivot (0.5, 0).
        EXPECT_NEAR(pl.translation.x - old.x, (0.5F - pv.x) * size.x * kSpriteScale, 1e-3F) << kv.first;
        EXPECT_NEAR(pl.translation.y - old.y, -pv.y * size.y * kSpriteScale, 1e-3F) << kv.first;
        const bool isDefault = std::fabs(pv.y) < 1e-4F && std::fabs(pv.x - 0.5F) < 1e-4F;
        if (!isDefault) {
            moved.insert(kv.first);
            EXPECT_GT(std::fabs(pl.translation.x - old.x) + std::fabs(pl.translation.y - old.y), 1e-3F)
                << kv.first;
        }
    }
    // All 36 non-default stems (26 centre-pivot props + 7 statue sprites + shadow0/1 + texiao_01).
    EXPECT_EQ(moved.size(), 36u);
    for (const char *s : {"objects2_30", "objects2_31", "objects2_50", "objects2_55", "objects2_56",
                          "objects2_57", "objects2_62", "objects2_63", "objects2_68", "objects1_5",
                          "objects1_14", "objects1_20", "objects1_26", "objects1_30", "objects1_32",
                          "objects1_34", "objects1_85", "objects1_2", "objects1_22", "objects4_5",
                          "objects4_20", "objects3_0", "objects3_6", "shadow0", "shadow1", "texiao_01"}) {
        EXPECT_EQ(moved.count(s), 1u) << s;
    }
    // Walls, floors, doors, shadow2/3 never move.
    for (const char *s : {"w601", "w505", "f501", "f601", "objects1_0", "door01", "door02", "shadow2"}) {
        EXPECT_EQ(moved.count(s), 0u) << s;
        EXPECT_EQ(table.count(s), 1u) << s;
    }
}

TEST(RenderLayoutTest, WallAndBoxOverlaysDrawOverCharactersBasesUnder) {
    Game::GameData gd;
    gd.LoadAll(kResourceRoot);
    ASSERT_FALSE(gd.Levels().empty());
    // Whatever the world y of the actor or the wall, the layer-6 overlay beats every actor and
    // the layer-4 base loses to every actor.
    for (float y : {-20000.0F, -3000.0F, 0.0F, 1300.0F, 20000.0F}) {
        for (const Game::LevelDef &lv : gd.Levels()) {
            for (const Game::LevelWallDef &w : lv.walls) {
                if (w.face.empty()) {
                    continue;
                }
                const float top = RL::RoomSortZ(w.topSort.layer, w.topSort.order, y);
                const float face = RL::RoomSortZ(w.faceSort.layer, w.faceSort.order, y);
                EXPECT_LT(top, RL::kZPlayer) << lv.id << " " << w.prefab;
                EXPECT_LT(top, RL::kZEnemy) << lv.id << " " << w.prefab;
                EXPECT_GT(face, RL::kZPlayer) << lv.id << " " << w.prefab;
                EXPECT_GT(face, RL::kZEnemy) << lv.id << " " << w.prefab;
                EXPECT_GT(face, RL::kZBoss) << lv.id << " " << w.prefab;
            }
            for (std::size_t i = 0; i < lv.obstacles.size(); ++i) {
                const Game::LevelObstacleDef &o = lv.obstacles[i];
                EXPECT_LT(RL::RoomSortZ(o.sort.layer, o.sort.order, y), RL::kZPlayer)
                    << lv.id << " obstacle " << i;
                if (!o.alt.empty() && o.altSort.layer == 6) {
                    EXPECT_GT(RL::RoomSortZ(o.altSort.layer, o.altSort.order, y), RL::kZBoss)
                        << lv.id << " obstacle " << i;
                }
                // Every room sprite stays under the effects (7) and the HUD.
                if (!o.alt.empty()) {
                    EXPECT_LT(RL::RoomSortZ(o.altSort.layer, o.altSort.order, y), RL::kZEffect)
                        << lv.id << " obstacle " << i;
                }
            }
        }
    }
}

TEST(RenderLayoutTest, CrateLidIsOverTheHeroBehindItAndUnderTheHeroInFront) {
    Game::GameData gd;
    gd.LoadAll(kResourceRoot);
    const Game::LevelDef *a1 = gd.FindLevel("A1");
    ASSERT_NE(a1, nullptr);
    const Game::LevelObstacleDef &crate = a1->obstacles[1]; // box02: base objects2_31, lid objects2_30
    ASSERT_EQ(crate.prefab, "box02");
    EXPECT_EQ(crate.sort.layer, 4);
    EXPECT_EQ(crate.altSort.layer, 6);
    const glm::vec2 cell{64.0F, 64.0F};
    const RL::RoomSpritePlacement base = RL::PlaceRoomSprite(gd, crate.sprite, {16.0F, 16.0F}, cell, crate.sort);
    const RL::RoomSpritePlacement lid =
        RL::PlaceRoomSprite(gd, crate.alt, {16.0F, 16.0F}, cell, crate.altSort, crate.altDy);
    // Hero standing in the cell above (behind) the crate and in the cell below (in front).
    EXPECT_GT(lid.z, RL::kZPlayer);
    EXPECT_LT(base.z, RL::kZPlayer);
    // The lid is drawn above the base too (it is the same object), whatever the y key.
    EXPECT_GT(lid.z, base.z);
}

TEST(RenderLayoutTest, WithinOneLayerLowerOnScreenDrawsOnTopAndOrderBeatsY) {
    // y-sort inside a (layer, order): the wall one row further south overdraws the one behind it.
    EXPECT_GT(RL::SortZ(4, 0, 0.0F), RL::SortZ(4, 0, kCellPx));
    // ... resolvable at one-cell resolution at the far end of the float range.
    EXPECT_GT(RL::SortZ(6, 2, 10000.0F), RL::SortZ(6, 2, 10000.0F + kCellPx));
    // m_SortingOrder dominates y: order 3 at the very top of the map still beats order 2 at the bottom.
    EXPECT_GT(RL::SortZ(6, 3, 20000.0F), RL::SortZ(6, 2, -20000.0F));
    // layers dominate everything.
    EXPECT_GT(RL::SortZ(5, 0, -20000.0F), RL::SortZ(4, 3, 20000.0F));
    EXPECT_GT(RL::SortZ(6, 0, -20000.0F), RL::SortZ(5, 0, 20000.0F));
    EXPECT_GT(RL::SortZ(3, 0, -20000.0F), RL::SortZ(2, 3, 20000.0F));
}

TEST(RenderLayoutTest, NonRoomDrawablesKeepTheirRelativeOrder) {
    // floor < shadow < wall base < chest < gate < weapon < coin < bullet < actors < boss < overlays < effects
    const float floorZ = RL::SortZ(RL::kLayerFloor, 1, 20000.0F);
    const float shadowZ = RL::SortZ(RL::kLayerShadow, 1, 20000.0F);
    const float wallBaseMin = RL::SortZ(RL::kLayerWall, 0, 20000.0F);
    const float wallBaseMax = RL::SortZ(RL::kLayerWall, 4, -20000.0F);
    EXPECT_LT(floorZ, RL::SortZ(RL::kLayerShadow, 0, -20000.0F));
    EXPECT_LT(shadowZ, wallBaseMin);
    // transfer_gate `light` (layer 4 order 0) is under the chest (box05 4/0, box06 4/2): the
    // pre-P2.17 order (light 1.5 < chest 2.0) is kept; the gate img (layer 5) is above both.
    EXPECT_LT(wallBaseMax, RL::kZGateLight);
    EXPECT_LT(RL::kZGateLight, RL::kZChest);
    EXPECT_LT(RL::kZChest, RL::kZGate);
    EXPECT_LT(RL::kZGate, RL::kZWeaponPickup);
    EXPECT_LT(RL::kZWeaponPickup, RL::kZCoin);
    EXPECT_LT(RL::kZCoin, RL::kZBullet);
    EXPECT_LT(RL::kZBullet, RL::kZPlayer);
    EXPECT_FLOAT_EQ(RL::kZPlayer, RL::kZEnemy);
    EXPECT_LT(RL::kZPlayer, RL::kZBoss);
    EXPECT_LT(RL::kZBoss, RL::SortZ(RL::kLayerWallFont, 0, 20000.0F));
    EXPECT_LT(RL::SortZ(RL::kLayerWallFont, 4, -20000.0F), RL::kZEffect);
    // Shop machine + price tag live in the Wall band (above bases, under actors).
    EXPECT_GT(RL::kZMachineBody, wallBaseMin);
    EXPECT_LT(RL::kZMachineText, RL::kZChest);
    // The statue label stays above the statue god (layer 6, up to 6.4) and under the effects.
    EXPECT_GT(RL::kZStatueTag, RL::SortZ(RL::kLayerWallFont, 4, -20000.0F));
    EXPECT_LT(RL::kZStatueTag, RL::kZEffect);
}

TEST(RenderLayoutTest, GlowAndActorLayerRoomSpritesStayUnderTheActors) {
    // PORT CHOICE T-59: layer-7 glows (texiao_01) and layer-5 ore/statue light would otherwise
    // draw over / tie with the heroes.
    for (float y : {-5000.0F, 0.0F, 5000.0F}) {
        EXPECT_LT(RL::RoomSortZ(7, 0, y), RL::kZPlayer);
        EXPECT_LT(RL::RoomSortZ(5, 1, y), RL::kZPlayer);
        EXPECT_GT(RL::RoomSortZ(5, 1, y), RL::SortZ(RL::kLayerWall, 0, 20000.0F) - 1e-3F);
    }
    // The statue god (layer 6, over the heroes) and its pedestal (layer 4, under them).
    Game::GameData gd;
    gd.LoadAll(kResourceRoot);
    const Game::StatueDef *st = gd.FindStatue(1);
    ASSERT_NE(st, nullptr);
    EXPECT_EQ(st->baseSort.layer, 4);
    EXPECT_EQ(st->bodySort.layer, 6);
    EXPECT_EQ(st->lightSort.layer, 5);
}

TEST(RenderLayoutTest, CameraBackgroundIsTheLevelsMapManagerColour) {
    Game::GameData gd;
    gd.LoadAll(kResourceRoot);
    // No level -> the old black.
    EXPECT_EQ(RL::ClearColor(nullptr), glm::vec3(0.0F));
    // A1 forest / B1 ice: dark teal / dark navy (levels.json camera_bg = map_A1/map_B1 MapManager).
    const Game::LevelDef *a1 = gd.FindLevel("A1");
    const Game::LevelDef *b1 = gd.FindLevel("B1");
    ASSERT_NE(a1, nullptr);
    ASSERT_NE(b1, nullptr);
    const glm::vec3 ca = RL::ClearColor(a1);
    const glm::vec3 cb = RL::ClearColor(b1);
    EXPECT_NEAR(ca.r, 0.0196F, 1e-3F);
    EXPECT_NEAR(ca.g, 0.1216F, 1e-3F);
    EXPECT_NEAR(ca.b, 0.1569F, 1e-3F);
    EXPECT_NEAR(cb.r, 0.0267F, 1e-3F);
    EXPECT_NEAR(cb.g, 0.1027F, 1e-3F);
    EXPECT_NEAR(cb.b, 0.1912F, 1e-3F);
    // Every level has a non-black background (the void is never pure black in the original).
    for (const Game::LevelDef &lv : gd.Levels()) {
        const glm::vec3 c = RL::ClearColor(&lv);
        EXPECT_GT(c.r + c.g + c.b, 0.05F) << lv.id;
    }
}

} // namespace
