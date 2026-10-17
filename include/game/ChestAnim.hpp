#ifndef GAME_GAME_CHESTANIM_HPP
#define GAME_GAME_CHESTANIM_HPP

#include <algorithm>
#include <vector>

#include <glm/glm.hpp>

#include "data/GameData.hpp"
#include "game/WorldScale.hpp"

/**
 * @file ChestAnim.hpp
 * @brief P2.20: where each layer of a chest sits, closed and while opening -- pure maths, no GL,
 *        unit-tested (test/ChestAnimTest.cpp).
 *
 * FAITHFUL @ res/item/chest00|01|02|chest_room_reward|chest_boss*.prefab: root = the chest's feet;
 * children box05 (interior, pivot (0.5,0), local (0,-0.45), layer 4 order 0), box06 (front panel,
 * pivot (0.5,0), (0,0), layer 4 order 2), box03 / box04 (the two lid halves, pivots (1,0) / (0,0),
 * (0,0), layer 6 order 1) and `shadow` (shadow1 at alpha 0.392, scale (1.2,1), layer 3 order 1); the
 * rganim/chest_open.anim clip (1 s, Animator bool OPEN) then moves box03 to (-0.5, 0.1), box04 to
 * (0.5, 0.1) (x over 0.233 s, y over 0.5 s) and lifts box05 from y -0.5 to 0 over 0.5 s.
 * The clip's position keys are linearly interpolated -- PORT CHOICE (T-75, same as the doors).
 */
namespace Game::ChestAnim {

/// Linear sample of position keys at @p t (clamped to the first / last key); @p rest when empty.
inline glm::vec2 SamplePos(const std::vector<ClipKey2> &keys, float t, glm::vec2 rest) {
    if (keys.empty()) {
        return rest;
    }
    if (t <= keys.front().t) {
        return {keys.front().x, keys.front().y};
    }
    for (std::size_t i = 1; i < keys.size(); ++i) {
        if (t <= keys[i].t) {
            const ClipKey2 &a = keys[i - 1];
            const ClipKey2 &b = keys[i];
            const float span = b.t - a.t;
            const float u = span <= 0.0F ? 1.0F : (t - a.t) / span;
            return {a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u};
        }
    }
    return {keys.back().x, keys.back().y};
}

/// Local positions (units, root space) of the three animated layers.
struct Pose {
    glm::vec2 box03{0.0F, 0.0F};
    glm::vec2 box04{0.0F, 0.0F};
    glm::vec2 box05{0.0F, 0.0F};
};

/// The closed pose: every layer at its prefab position (the Animator's default state has no motion).
inline Pose ClosedPose(const ChestPropsDef &c) {
    return Pose{{c.box03.offsetX, c.box03.offsetY}, {c.box04.offsetX, c.box04.offsetY},
                {c.box05.offsetX, c.box05.offsetY}};
}

/// The pose @p t seconds after Open() (t clamped to [0, clip.stop]).
inline Pose OpenPose(const ChestPropsDef &c, const ChestOpenClipDef &clip, float t) {
    const Pose closed = ClosedPose(c);
    const float tt = std::clamp(t, 0.0F, clip.stop);
    return Pose{SamplePos(clip.box03, tt, closed.box03), SamplePos(clip.box04, tt, closed.box04),
                SamplePos(clip.box05, tt, closed.box05)};
}

/// How one layer is drawn: the quad's pivot point lands on @c translation (px), scaled by @c scale,
/// pivoted at @c pivotPx (source px from the rect centre, Util::GameObject::SetPivot's convention).
struct NodePlacement {
    glm::vec2 translation{0.0F, 0.0F};
    glm::vec2 scale{1.0F, 1.0F};
    glm::vec2 pivotPx{0.0F, 0.0F};
};

/// Placement of @p node at root-space @p localUnits under a chest whose feet are at @p rootPx.
inline NodePlacement Place(const RigNodeDef &node, glm::vec2 rootPx, glm::vec2 localUnits,
                           glm::vec2 imageSizePx) {
    NodePlacement p;
    p.translation = rootPx + glm::vec2(Scale::UnitsToPx(localUnits.x), Scale::UnitsToPx(localUnits.y));
    p.scale = {Scale::kSpriteScale * node.scaleX, Scale::kSpriteScale * node.scaleY};
    p.pivotPx = {(node.pivotX - 0.5F) * imageSizePx.x, (node.pivotY - 0.5F) * imageSizePx.y};
    return p;
}

} // namespace Game::ChestAnim

#endif /* GAME_GAME_CHESTANIM_HPP */
