#ifndef GAME_GAME_RENDERLAYOUT_HPP
#define GAME_GAME_RENDERLAYOUT_HPP

#include <algorithm>
#include <string>

#include <glm/glm.hpp>

#include "data/GameData.hpp"
#include "game/WorldScale.hpp"

/**
 * @file RenderLayout.hpp
 * @brief P2.17 render slice 1: where a room sprite sits (pivot) and what it draws
 *        over (Unity sorting layer -> the port's single z axis). Pure math, no GL,
 *        so it is unit-tested (test/RenderLayoutTest.cpp).
 *
 * PIVOT. A Unity sprite is placed by its pivot (SpriteMetaData m_Pivot, a fraction of
 * the rect from its bottom-left), not by its centre. Walls, floors, doors and most
 * props are 16x24 / 16x8 sprites with pivot (0.5, 0); the forest/castle bushes, crates
 * and trees (objects2_30/31/50/55/56/57/62/63/68, objects1_5/14/20/26/30/32/34/85,
 * objects4_5/20) are 16x16 rects with pivot (0.5, 0.5) whose art fills only the top
 * rows. Drawn bottom-anchored they float 0.5 unit (16 world px) too high.
 *
 * SORTING. FAITHFUL @ ProjectSettings/TagManager.asset sortingLayers: 0 Default,
 * 1 BackGround, 2 Floor, 3 Shadow, 4 Wall, 5 Character, 6 WallFont, 7 Effect, 8 UI.
 * Prefab SpriteRenderers carry (layer, order): wall/box bases on 4, their w2 overlay
 * (wall top, crate lid, bush face) on 6 -- ABOVE the characters on 5, so a hero
 * standing behind a wall is covered by its top instead of drawn over it.
 * The port draws by one float z, so z = layer + order * kOrderStepZ - y * kYSortZ.
 */
namespace Game::RenderLayout {

/// TagManager sortingLayers (index == SpriteRenderer.m_SortingLayer in the prefabs).
inline constexpr int kLayerFloor = 2;
inline constexpr int kLayerShadow = 3;
inline constexpr int kLayerWall = 4;
inline constexpr int kLayerCharacter = 5;
inline constexpr int kLayerWallFont = 6;
inline constexpr int kLayerEffect = 7;

/// PORT CHOICE (T-59): z gap per SpriteRenderer.m_SortingOrder step. Orders in the room
/// prefabs run 0..3, so a layer occupies [layer, layer + 0.4] before the y-sort term.
inline constexpr float kOrderStepZ = 0.1F;
/// PORT CHOICE (T-59): within one (layer, order) the sprite lower on screen draws on top
/// (the y-sort the old room z already used: a lower wall overdraws the one behind it).
/// 1e-6 per world px keeps +-20000 px (+-0.02) well inside one order step and a 32 px cell
/// step (3.2e-5) many float ulps apart at z ~ 6.
inline constexpr float kYSortZ = 1.0e-6F;
inline constexpr float kYSortClampPx = 20000.0F;

/// z of a sprite on (@p layer, @p order) whose y-sort key is @p worldY (px, y up).
inline float SortZ(int layer, int order, float worldY = 0.0F) {
    const float y = std::clamp(worldY, -kYSortClampPx, kYSortClampPx);
    return static_cast<float>(layer) + static_cast<float>(order) * kOrderStepZ - y * kYSortZ;
}

/// SortZ for a room sprite (wall / box / prop / door / statue). PORT CHOICE (T-59):
///  - a prop's glow child (texiao_01, layer 7) is tinted gold at alpha 0.22 in the original
///    and the port has no per-drawable tint, so it would draw as a white blob over the
///    heroes;
///  - a room sprite on the actors' own layer 5 (mineral ore, statue glow) would tie with
///    the heroes, which have no per-frame y-sort here and would flip over/under with world y.
/// Both are kept at the top of the Wall layer instead (under the actors).
inline float RoomSortZ(int layer, int order, float worldY) {
    if (layer >= kLayerEffect || layer == kLayerCharacter) {
        return SortZ(kLayerWall, 4, worldY);
    }
    return SortZ(layer, order, worldY);
}

// Non-room drawables keep their pre-P2.17 RELATIVE order (floor < shadow < wall base <
// chest/gate < pickups < bullets < actors < boss < effects < HUD); only the wall/box
// w2 overlays move above the actors. PORT CHOICE (T-59): the original puts bullets
// (rgprefab/bullet *.b) and coins (coin_0.img) on layer 6 and ground weapons on layer 5;
// moving them is the bullet/pickup slice, so they stay below the actors here.
inline constexpr float kZGateLight = 4.45F;  ///< transfer_gate `light` is layer 4 order 0 ...
inline constexpr float kZChest = 4.5F;       ///< ... chest00/01 box05/box06 are layer 4 order 0/2 (chest >= light)
inline constexpr float kZGate = 4.6F;        ///< transfer_gate `img` is layer 5 (kept under the hero)
inline constexpr float kZWeaponPickup = 4.7F;
inline constexpr float kZCoin = 4.75F;
inline constexpr float kZBullet = 4.9F;
inline constexpr float kZPlayer = 5.0F;      ///< c01.prefab body: layer 5, order 5
inline constexpr float kZEnemy = 5.0F;       ///< e_slime01 body: layer 5, order 1
/// boss01 body is layer 5 too; kept above the hero (PORT CHOICE T-59) but below layer 6.
inline constexpr float kZBoss = 5.5F;
inline constexpr float kZEffect = 7.0F;
/// Shop machines + their price tags sit in the Wall layer band, above the bases.
inline constexpr float kZMachineBody = 4.3F;
inline constexpr float kZMachineDisplay = 4.32F;
inline constexpr float kZMachineIcon = 4.33F;
inline constexpr float kZMachineText = 4.34F;
/// The statue name label sat above the statue's god sprite (1.34 vs 1.31); the god is now on
/// layer 6, so the label goes above the layer-6 overlays (and below the effects).
inline constexpr float kZStatueTag = 6.5F;

/// Pivot used when the data carries none: the 16x24 bottom-centre wall/floor convention.
inline constexpr glm::vec2 kDefaultPivot{0.5F, 0.0F};

/// Centre of a sprite of source size @p sizePx drawn at @p scale whose PIVOT point is at
/// @p anchor. Unity: sprite centre = pivot point + (0.5 - pivot) * size.
/// For pivot (0.5, 0) this is anchor + (0, size.y*scale/2) -- the old bottom-anchor.
inline glm::vec2 PivotCentre(glm::vec2 anchor, glm::vec2 sizePx, glm::vec2 pivot, float scale) {
    return anchor + glm::vec2((0.5F - pivot.x) * sizePx.x * scale,
                              (0.5F - pivot.y) * sizePx.y * scale);
}

/// World anchor of a map cell's prefab root: the cell's bottom-centre. FAITHFUL @ wall601
/// / box02 prefabs: root at the foot of the cell, collider (0, 0.5) 1x1 above it.
inline glm::vec2 CellAnchor(glm::vec2 cellCentre) {
    return cellCentre - glm::vec2(0.0F, Scale::kCellPx * 0.5F);
}

/// Where one room sprite goes: translation of its quad centre + its z.
struct RoomSpritePlacement {
    glm::vec2 translation{0.0F, 0.0F};
    float z = 0.0F;
};

/// THE placement rule of every room sprite (GameScene's addObjSprite / addBlockSprite use
/// exactly this): the prefab root is the cell's bottom-centre; a child sits @p dyUnits above
/// it; the quad centre follows from the sprite's PIVOT (data/tile_pivots.json); z comes from
/// the prefab's (sorting layer, order), y-sorted by the cell anchor so an object's base and
/// overlay share one key.
inline RoomSpritePlacement PlaceRoomSprite(const GameData &data, const std::string &stem,
                                           glm::vec2 sizePx, glm::vec2 cellCentre, SortKey sort,
                                           float dyUnits = 0.0F) {
    const glm::vec2 cell = CellAnchor(cellCentre);
    const auto pv = data.TilePivot(stem);
    RoomSpritePlacement out;
    out.translation = PivotCentre(cell + glm::vec2(0.0F, dyUnits * Scale::kCellPx), sizePx,
                                  glm::vec2(pv[0], pv[1]), Scale::kSpriteScale);
    out.z = RoomSortZ(sort.layer, sort.order, cell.y);
    return out;
}

/// Camera.backgroundColor: MapManager.camera_bg of the current level (levels.json
/// `camera_bg`); black (the old behaviour) when there is no level table.
inline glm::vec3 ClearColor(const LevelDef *level) {
    if (level == nullptr) {
        return glm::vec3{0.0F};
    }
    return glm::vec3{level->cameraBg[0], level->cameraBg[1], level->cameraBg[2]};
}

} // namespace Game::RenderLayout

#endif /* GAME_GAME_RENDERLAYOUT_HPP */
