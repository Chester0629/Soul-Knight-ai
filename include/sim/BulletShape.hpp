#ifndef GAME_SIM_BULLETSHAPE_HPP
#define GAME_SIM_BULLETSHAPE_HPP

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

#include "data/GameData.hpp"
#include "game/WorldScale.hpp"

namespace Game::Sim {

/// P3.0c: a bullet's collision shape, in the bullet's LOCAL frame (px; local +x = the travel
/// direction, the way the original rotates the bullet root: RGBullet.SetBulletVelocity does
/// `transform.right = dir`, DISASM 0x5b8f98). FAITHFUL @ the prefab trigger collider
/// (BoxCollider2D/CircleCollider2D size/radius x lossy scale, m_Offset composed through the
/// child GameObject chain, rotated child -> boxRotRad): see tools/bullet_geometry.py.
/// `Kind::None` = no prefab shape (fall back to the legacy circle of BulletState::radiusPx).
struct BulletShape {
    enum class Kind { None, Circle, Box };
    Kind kind = Kind::None;
    float radiusPx = 0.0F;         ///< Circle radius.
    glm::vec2 halfPx{0.0F, 0.0F};  ///< Box half extents along the BOX's own axes.
    glm::vec2 offsetPx{0.0F, 0.0F}; ///< shape centre in the bullet frame.
    float boxRotRad = 0.0F;        ///< Box axes rotation relative to the bullet frame (CCW).

    bool Present() const { return kind != Kind::None; }

    /// Radius of the smallest origin-centred circle containing the shape (broad phase).
    float BoundRadius() const {
        const float off = std::sqrt(offsetPx.x * offsetPx.x + offsetPx.y * offsetPx.y);
        if (kind == Kind::Circle) {
            return off + radiusPx;
        }
        if (kind == Kind::Box) {
            return off + std::sqrt(halfPx.x * halfPx.x + halfPx.y * halfPx.y);
        }
        return 0.0F;
    }

    /// Smaller half extent (the legacy "inscribed" radius: EquivalentRadiusPx without offset).
    float InscribedRadius() const {
        if (kind == Kind::Circle) {
            return radiusPx;
        }
        if (kind == Kind::Box) {
            return std::min(halfPx.x, halfPx.y);
        }
        return 0.0F;
    }
};

/// Everything the sim needs to launch one bullet type: which bullets.json row (the view's
/// sprite/hit-effect id), its collision shape and its spin. Resolved ONCE where an intent's
/// owner (weapon / enemy / boss) is built, so the per-step code never touches GameData.
struct BulletLaunch {
    int bulletIdx = -1;           ///< GameData::Bullets() index, -1 = no prefab row.
    BulletShape shape;            ///< prefab trigger collider (Kind::None = none).
    /// Degrees added to the bullet's heading EVERY fixed step (Bullet01 / RGSBullet01 FixedUpdate
    /// `eulerAngles.z += rotate_angle`, DISASM 0xaeec1c / 0x517254); 0 = no spin.
    float spinDegPerStep = 0.0F;
};

/// Unit-converted shape of a prefab collider (data units -> px; offset and rotation kept).
inline BulletShape MakeBulletShape(const ColliderDef &c) {
    BulletShape s;
    if (c.shape == ColliderDef::Shape::Circle) {
        s.kind = BulletShape::Kind::Circle;
        s.radiusPx = Scale::UnitsToPx(c.radius);
    } else if (c.shape == ColliderDef::Shape::Box) {
        s.kind = BulletShape::Kind::Box;
        s.halfPx = {Scale::UnitsToPx(c.sizeX) * 0.5F, Scale::UnitsToPx(c.sizeY) * 0.5F};
        s.boxRotRad = c.rotDeg * (3.14159265358979F / 180.0F);
    } else {
        return s;
    }
    s.offsetPx = {Scale::UnitsToPx(c.offsetX), Scale::UnitsToPx(c.offsetY)};
    return s;
}

/// The launch record of bullets.json row @p def at index @p idx (pass GameData::BulletIndex).
inline BulletLaunch MakeBulletLaunch(const BulletDef &def, int idx) {
    BulletLaunch l;
    l.bulletIdx = idx;
    l.shape = MakeBulletShape(def.collider);
    if ((def.bulletClass == "Bullet01" || def.bulletClass == "RGSBullet01") && def.rotateAngle != 0) {
        l.spinDegPerStep = static_cast<float>(def.rotateAngle);
    }
    return l;
}

/// Launch record for the bullets.json row @p id of @p data; a default (no prefab) record when
/// the id is empty or unknown.
inline BulletLaunch MakeBulletLaunch(const GameData &data, const std::string &id) {
    const int idx = id.empty() ? -1 : data.BulletIndex(id);
    if (idx < 0) {
        return BulletLaunch{};
    }
    return MakeBulletLaunch(data.Bullets()[static_cast<std::size_t>(idx)], idx);
}

/// The world-space placement of a bullet shape for one step: centre offset (rotated by the
/// bullet heading) and the box axis angle. Pure math, shared by the sim and the tests.
struct BulletShapeWorld {
    BulletShape::Kind kind = BulletShape::Kind::Circle;
    float radius = 0.0F;
    glm::vec2 half{0.0F, 0.0F};
    glm::vec2 centreOffset{0.0F, 0.0F}; ///< shape centre - bullet position, world axes.
    float boxAngle = 0.0F;              ///< world angle of the box's x axis.
};

inline BulletShapeWorld PlaceShape(const BulletShape &s, float headingRad) {
    BulletShapeWorld w;
    w.kind = s.kind;
    w.radius = s.radiusPx;
    w.half = s.halfPx;
    const float c = std::cos(headingRad);
    const float sn = std::sin(headingRad);
    w.centreOffset = {s.offsetPx.x * c - s.offsetPx.y * sn, s.offsetPx.x * sn + s.offsetPx.y * c};
    w.boxAngle = headingRad + s.boxRotRad;
    return w;
}

} // namespace Game::Sim

#endif /* GAME_SIM_BULLETSHAPE_HPP */
