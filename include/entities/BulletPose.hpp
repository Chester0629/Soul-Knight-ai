#ifndef GAME_ENTITIES_BULLETPOSE_HPP
#define GAME_ENTITIES_BULLETPOSE_HPP

#include <cmath>

#include <glm/glm.hpp>

namespace Game {

/// P3.0c: where a bullet view's sprite is drawn, from the bullet root's pose and the prefab's
/// sprite child (see Bullet::SyncPose). Pure math (no GL) so it is unit-tested.
struct BulletPose {
    glm::vec2 translation{0.0F, 0.0F}; ///< world px of the sprite's pivot point.
    float rotation = 0.0F;             ///< radians, counter-clockwise (heading + child rotation).
    glm::vec2 scale{1.0F, 1.0F};       ///< x scale is negative for a flipX child.
};

/// @p pos the bullet root, @p headingRad its travel direction (the root is rotated to it:
/// RGBullet.SetBulletVelocity does transform.right = dir), @p childOffsetPx the sprite child's
/// position in the bullet's LOCAL frame (px), @p childRotRad the child's own rotation,
/// @p childScale its lossy scale times the sprite scale, @p flipX its SpriteRenderer flip.
inline BulletPose ComputeBulletPose(glm::vec2 pos, float headingRad, glm::vec2 childOffsetPx,
                                    float childRotRad, glm::vec2 childScale, bool flipX) {
    const float c = std::cos(headingRad);
    const float s = std::sin(headingRad);
    BulletPose p;
    p.translation = pos + glm::vec2{childOffsetPx.x * c - childOffsetPx.y * s,
                                    childOffsetPx.x * s + childOffsetPx.y * c};
    p.rotation = headingRad + childRotRad;
    p.scale = {childScale.x * (flipX ? -1.0F : 1.0F), childScale.y};
    return p;
}

} // namespace Game

#endif /* GAME_ENTITIES_BULLETPOSE_HPP */
