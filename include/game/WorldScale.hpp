#ifndef GAME_GAME_WORLDSCALE_HPP
#define GAME_GAME_WORLDSCALE_HPP

namespace Game::Scale {

/// World size (screen-world px) of one map cell == one Unity world unit. The
/// room geometry (GameScene), the sim's unit conversions (Sim::kWorldUnitPx) and
/// every collider/offset taken from prefab data (Unity units) share this value.
inline constexpr float kCellPx = 32.0F;

/// Source pixels per Unity unit. FAITHFUL: every character/enemy/boss/bullet/
/// weapon atlas has spritePixelsToUnits 16 (Texture2D/SpriteAtlasTexture-*.png.meta).
inline constexpr float kSourcePxPerUnit = 16.0F;

/// Draw scale of a source sprite, so that 1 unit (16 source px) covers one
/// cell (kCellPx). FAITHFUL: PPU-16 atlases + localScale 1 bodies (c01.prefab,
/// e_slime01.prefab, e_yeti01.prefab, boss01.prefab: the img/body transforms are
/// scale 1; boss01's 1.5 is only on shadow/shadow_lock/point_1). The world tiles
/// already draw at this same scale (kCellPx / 16 == 2.0). Per-species body scale
/// (e_shark 1.5, the ex_* elites 1.5/2.0) is carried by data (`body_scale`) and
/// multiplies this.
inline constexpr float kSpriteScale = kCellPx / kSourcePxPerUnit;

/// Unity units -> screen-world px.
inline constexpr float UnitsToPx(float units) { return units * kCellPx; }

} // namespace Game::Scale

#endif /* GAME_GAME_WORLDSCALE_HPP */
