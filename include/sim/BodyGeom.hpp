#ifndef GAME_SIM_BODYGEOM_HPP
#define GAME_SIM_BODYGEOM_HPP

#include <algorithm>

#include <glm/glm.hpp>

#include "data/GameData.hpp"
#include "game/WorldScale.hpp"
#include "sim/SimConfig.hpp"
#include "sim/SimMath.hpp"

namespace Game::Sim {

/// Collision geometry of one body (hero / enemy / boss), in screen-world px, derived
/// from the prefab colliders in data (GameData ColliderDef, Unity units * kCellPx). Every
/// collider offset is ROOT-SPACE (tools/build_gamedata.py composes the child GameObjects'
/// localPosition/localScale chain), so `feetOffsetPx` is the true body-centre-above-root
/// distance even when the collider sits on a displaced child (mineral_gold: 1.3 units).
///
/// POSITION CONVENTION: an entity's Position() is the centre of its PHYSICAL body
/// circle (the prefab's "collider" child). The Unity root origin (the sprite pivot,
/// i.e. the FEET) is `feetOffsetPx` below it; the HURTBOX centre is `hurtOffsetPx`
/// from Position(). Keeping Position() = body centre leaves every spawn point, wall
/// test and aim origin of the port unchanged; only the sprite anchor and the hit test
/// carry the original offsets.
///
/// Two colliders, as in the original (Physics2DSettings layer matrix): the BODY circle
/// (layer Body) blocks against walls/doors/obstacles and other bodies but NOT bullets;
/// the HURTBOX (root, IsTrigger, layer Body_P/Body_E) is what bullets hit.
struct BodyGeom {
    float bodyRadiusPx = kEnemyBodyRadius;  ///< physical circle: wall blocking.
    bool hurtIsBox = false;                 ///< hurtbox shape (else a circle).
    float hurtRadiusPx = kEnemyBodyRadius;  ///< circle hurtbox radius.
    glm::vec2 hurtHalfPx{0.0F, 0.0F};       ///< box hurtbox half extents.
    glm::vec2 hurtOffsetPx{0.0F, 0.0F};     ///< hurtbox centre - Position().
    glm::vec2 feetOffsetPx{0.0F, 0.0F};     ///< Position() - feet (sprite pivot).
    bool fromData = false;                  ///< false => the placeholder fallback radius.

    /// Placeholder geometry (no collider data): one circle of @p radiusPx for both the
    /// body and the hurtbox, centred on Position(), feet at Position().
    static BodyGeom Fallback(float radiusPx) {
        BodyGeom g;
        g.bodyRadiusPx = radiusPx;
        g.hurtRadiusPx = radiusPx;
        return g;
    }

    /// True if the circle (@p c, @p r) touches or overlaps this body's hurtbox when the
    /// body stands at @p pos (inclusive boundary, like CirclesOverlap).
    bool HurtOverlaps(glm::vec2 pos, glm::vec2 c, float r) const {
        const glm::vec2 centre = pos + hurtOffsetPx;
        if (!hurtIsBox) {
            return CirclesOverlap(c, r, centre, hurtRadiusPx);
        }
        const glm::vec2 d = c - centre;
        const glm::vec2 nearest{std::clamp(d.x, -hurtHalfPx.x, hurtHalfPx.x),
                                std::clamp(d.y, -hurtHalfPx.y, hurtHalfPx.y)};
        const glm::vec2 gap = d - nearest;
        return (gap.x * gap.x + gap.y * gap.y) <= r * r;
    }
};

/// Equivalent circle radius (px) of a collider: a circle's radius, a box's inscribed
/// circle (its smaller half extent); 0 when the collider is absent.
///
/// PORT CHOICE (T-52): a BOX bullet collider is flattened to its inscribed circle and the
/// collider's OFFSET IS IGNORED (the bullet's hit circle stays centred on the bullet
/// position). The original tests the real oriented BoxCollider2D at its offset against
/// the target; the sim keeps one circle per bullet (cheap, deterministic, rotation-free).
/// Consequence: an elongated box bullet hits slightly SHORT along its long axis. Circle
/// bullets (the large majority) are exact apart from the ignored offset.
inline float EquivalentRadiusPx(const ColliderDef &c) {
    switch (c.shape) {
    case ColliderDef::Shape::Circle:
        return Scale::UnitsToPx(c.radius);
    case ColliderDef::Shape::Box:
        return Scale::UnitsToPx(std::min(c.sizeX, c.sizeY) * 0.5F);
    case ColliderDef::Shape::None:
        break;
    }
    return 0.0F;
}

/// Build a body's geometry from its prefab colliders (data units -> px).
///
/// PORT CHOICE (T-52), the missing-body-collider rule: a prefab with NO physical
/// "collider" child (data: e_snowman_temp only; tools/validate_data.py NO_BODY_COLLIDER)
/// uses its HURTBOX for the body too -- feet offset = the hurtbox's root-space offset and
/// body radius = EquivalentRadiusPx(hurtbox), i.e. the hurtbox's INSCRIBED circle (its
/// smaller half extent for a box), NOT the fixed kEnemyBodyRadius 16 px. Only with no
/// collider data at all (a default def) does it fall back to @p fallbackRadiusPx
/// (BodyGeom::Fallback; fromData == false). The original has no such prefab case to
/// copy (it would simply have no wall collider), so this keeps the enemy blockable.
inline BodyGeom MakeBodyGeom(const ColliderDef &body, const ColliderDef &hurt,
                             float fallbackRadiusPx) {
    if (!body.Present() && !hurt.Present()) {
        return BodyGeom::Fallback(fallbackRadiusPx);
    }
    BodyGeom g;
    g.fromData = true;
    const ColliderDef &anchor = body.Present() ? body : hurt;
    g.feetOffsetPx = {Scale::UnitsToPx(anchor.offsetX), Scale::UnitsToPx(anchor.offsetY)};
    g.bodyRadiusPx = EquivalentRadiusPx(anchor);
    if (!hurt.Present()) {
        g.hurtRadiusPx = g.bodyRadiusPx; // no hurtbox: bullets hit the body circle.
        return g;
    }
    g.hurtOffsetPx = {Scale::UnitsToPx(hurt.offsetX) - g.feetOffsetPx.x,
                      Scale::UnitsToPx(hurt.offsetY) - g.feetOffsetPx.y};
    if (hurt.shape == ColliderDef::Shape::Box) {
        g.hurtIsBox = true;
        g.hurtHalfPx = {Scale::UnitsToPx(hurt.sizeX) * 0.5F, Scale::UnitsToPx(hurt.sizeY) * 0.5F};
    } else {
        g.hurtRadiusPx = Scale::UnitsToPx(hurt.radius);
    }
    return g;
}

} // namespace Game::Sim

#endif /* GAME_SIM_BODYGEOM_HPP */
