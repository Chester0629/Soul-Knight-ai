#ifndef GAME_ENTITIES_SPRITEANCHOR_HPP
#define GAME_ENTITIES_SPRITEANCHOR_HPP

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Util/Animation.hpp"
#include "Util/GameObject.hpp"

#include "data/GameData.hpp"
#include "game/WorldScale.hpp"

namespace Game {

/**
 * @struct SpriteAnchor
 * @brief How a character/enemy/boss sprite sits on its entity position.
 *
 * SCALE (FAITHFUL): sprites are drawn at Scale::kSpriteScale (kCellPx / 16: PPU-16
 * atlases + localScale-1 bodies) times the species' own body scale from data
 * (`body_scale`: e_shark 1.5, the ex_* elites 1.5/2.0; 1 for every hero/normal
 * enemy/boss).
 *
 * PIVOT (FAITHFUL): a Unity sprite is positioned by its pivot, not its centre. The
 * character/enemy/boss frames carry a bottom-centre pivot (c01_4: 0.486, -0.002), and
 * the prefab root origin the pivot lands on is the FEET. The port's entity position
 * is the centre of the physical body circle (Sim::BodyGeom), which the prefab places
 * `feetOffsetPx` above the feet (c01: collider.offset.y 0.5 unit = 16 px). The
 * collider offsets are ROOT-SPACE (the whole child Transform chain composed by
 * tools/build_gamedata.py), so feet = Position() - feetOffsetPx is the true root origin.
 * The sprite-bearing GameObject (root/img/body) may itself sit off the root origin
 * (data `sprite_offset`, root-space units: boss11 (-0.074, -1.1), boss05 (0.2, 0.3), the
 * flyers ...); the pivot lands there. So each frame is drawn with its pivot point at
 *   Position() - feetOffsetPx + spriteOffsetPx ;
 * the per-frame pivot fractions come from sprites.json (`pivots`, from the sprite
 * assets). Frames without data are anchored bottom-centre {0.5, 0}.
 */
struct SpriteAnchor {
    /// Pivot fractions (x, y from the frame rect's bottom-left) per animation frame.
    std::vector<std::array<float, 2>> framePivots;
    glm::vec2 feetOffsetPx{0.0F, 0.0F}; ///< Position() - feet (root origin), px.
    glm::vec2 spriteOffsetPx{0.0F, 0.0F}; ///< sprite GameObject origin - feet, px (y up).
    float bodyScale = 1.0F;             ///< data `body_scale` (multiplies kSpriteScale).
    float bodyRadiusPx = 16.0F;         ///< physical body circle (entity GetCollider()).

    float DrawScale() const { return Scale::kSpriteScale * bodyScale; }

    /// GameObject pivot (source px, as Util::GameObject::SetPivot wants it) for frame
    /// @p frameIndex of source size @p frameSize. Draw() puts the quad centre at
    /// translation - pivot * scale, so the sprite's pivot point lands on
    /// translation - feetOffset + spriteOffset when
    ///   pivot = (feetOffset - spriteOffset) / scale + ((fx - 0.5) * w, (fy - 0.5) * h).
    /// Pure math (no GL), so it is unit-tested.
    ///
    /// SLICE 4 FACING (RGController$$set_facing, game_typed.c:50534, turns the whole `img` subtree between
    /// RotRight / RotLeft; the captured left-facing frames are the x MIRROR of the right-facing pose --
    /// see game/ActorPose.hpp, T-69, for what is and is not verified): with @p facing -1 the sprite
    /// is drawn mirrored (scale.x < 0 mirrors about the pivot point, which Util::GameObject keeps on
    /// the translation) and the sprite GameObject's own x offset mirrors with it, so the pivot lands
    /// on feet + (facing * spriteOffset.x, spriteOffset.y). Solving T + facing*s*(v - P) = target for
    /// the pivot parameter P gives  P.x = (facing * feet.x - sprite.x) / s + (fx - 0.5) * w  (the
    /// facing = +1 formula above unchanged).
    glm::vec2 PivotPx(glm::vec2 frameSize, std::size_t frameIndex, int facing = 1) const {
        std::array<float, 2> pv{0.5F, 0.0F}; // no data: bottom-centre
        if (frameIndex < framePivots.size()) {
            pv = framePivots[frameIndex];
        }
        const float f = facing < 0 ? -1.0F : 1.0F;
        const float s = DrawScale();
        return glm::vec2{(f * feetOffsetPx.x - spriteOffsetPx.x) / s + (pv[0] - 0.5F) * frameSize.x,
                         (feetOffsetPx.y - spriteOffsetPx.y) / s + (pv[1] - 0.5F) * frameSize.y};
    }

    /// Set @p obj's scale and the pivot of the frame @p anim is about to draw. Call
    /// once per frame before the renderer draws (Animation advances only inside Draw,
    /// so the current frame is stable until then). @p facing -1 mirrors the sprite in x.
    void Apply(Util::GameObject &obj, const Util::Animation &anim, int facing = 1) const {
        const float s = DrawScale();
        obj.m_Transform.scale = {facing < 0 ? -s : s, s};
        obj.SetPivot(PivotPx(anim.GetSize(), anim.GetCurrentFrameIndex(), facing));
    }
};

/// Build an entity's anchor from data: the species' sprite pivots (index-aligned with
/// GameData::SpriteFrames(spriteId)), the feet offset (physical body collider offset,
/// else the hurtbox's), the sprite GameObject's root-space offset @p spriteOffsetUnits
/// (data `sprite_offset`, units) and the body scale. @p fallbackRadiusPx is the
/// placeholder body radius used when neither collider is in data.
inline SpriteAnchor MakeSpriteAnchor(const GameData &data, const std::string &spriteId,
                                     const ColliderDef &body, const ColliderDef &hurtbox,
                                     float bodyScale, glm::vec2 spriteOffsetUnits,
                                     float fallbackRadiusPx) {
    SpriteAnchor a;
    a.spriteOffsetPx = {Scale::UnitsToPx(spriteOffsetUnits.x),
                        Scale::UnitsToPx(spriteOffsetUnits.y)};
    a.framePivots = data.SpriteFramePivots(spriteId);
    a.bodyScale = bodyScale > 0.0F ? bodyScale : 1.0F;
    a.bodyRadiusPx = fallbackRadiusPx;
    const ColliderDef &c = body.Present() ? body : hurtbox;
    if (c.Present()) {
        a.feetOffsetPx = {Scale::UnitsToPx(c.offsetX), Scale::UnitsToPx(c.offsetY)};
    }
    if (body.shape == ColliderDef::Shape::Circle) {
        a.bodyRadiusPx = Scale::UnitsToPx(body.radius);
    }
    return a;
}

} // namespace Game

#endif /* GAME_ENTITIES_SPRITEANCHOR_HPP */
