/**
 * @file Bullet.cpp
 * @brief Implementation of the pooled Game::Bullet projectile entity.
 */

#include "entities/Bullet.hpp"

#include <cmath>
#include <memory>
#include <string>

#include <glm/glm.hpp>

#include "Util/Image.hpp"

#include "entities/BulletPose.hpp"
#include "game/WorldScale.hpp"
#include "game/RenderLayout.hpp"

namespace Game {

namespace {
/// Placeholder radius (px) of the entity's own GetCollider(); the sim's hit radius comes
/// from the bullet prefab collider (BulletState::radiusPx).
constexpr float kColliderRadius = 6.0f;
/// Z-index for rendering bullets above the floor and most actors.
constexpr float kBulletZIndex = RenderLayout::kZBullet; // above wall bases, under actors (T-59)
/// Conversion factor from milliseconds to seconds.
constexpr float kMsToSec = 1.0f / 1000.0f;
} // namespace

Bullet::Bullet(const std::string &resourceRoot) {
    SetDrawable(std::make_shared<Util::Image>(
        std::string(resourceRoot) + "/sprites/bullet_0.png"));
    SetZIndex(kBulletZIndex);
    // SCALE (FAITHFUL): PPU-16 bullet atlas. This legacy look (centred, upright, scale 1) is only the
    // fallback for a bullet with no prefab sprite: 42 of the prefabs scale the sprite child and 49
    // offset it, so a bullet that has prefab data is dressed by SetPrefabVisual / SyncPose (P3.0c).
    m_Transform.scale = {Scale::kSpriteScale, Scale::kSpriteScale};
    SetVisible(false);
}

void Bullet::SetSprite(const std::string &path) {
    // Util::Image surfaces are cached by path (AssetStore), so this is cheap.
    SetDrawable(std::make_shared<Util::Image>(path));
}

void Bullet::SetPrefabVisual(const std::string &spritePath, const BulletVisual &visual) {
    auto img = std::make_shared<Util::Image>(spritePath);
    img->SetTint({visual.color[0], visual.color[1], visual.color[2], visual.color[3]});
    const glm::vec2 size = img->GetSize();
    SetDrawable(img);
    // Pivot: the sprite asset's m_Pivot lands on the child's origin (Util::GameObject pivot is the
    // pivot point's offset from the image centre, in source px, y up).
    SetPivot({(visual.pivotX - 0.5F) * size.x, (visual.pivotY - 0.5F) * size.y});
    m_HasVisual = true;
    m_VisScale = {visual.scaleX, visual.scaleY};
    m_VisOffsetPx = {Scale::UnitsToPx(visual.offsetX), Scale::UnitsToPx(visual.offsetY)};
    m_VisRotRad = visual.rotDeg * (3.14159265358979F / 180.0F);
    m_VisFlipX = visual.flipX;
}

void Bullet::ClearPrefabVisual() {
    SetPivot({0.0F, 0.0F});
    m_HasVisual = false;
    m_VisScale = {1.0F, 1.0F};
    m_VisOffsetPx = {0.0F, 0.0F};
    m_VisRotRad = 0.0F;
    m_VisFlipX = false;
    m_Transform.rotation = 0.0F;
    m_Transform.scale = {Scale::kSpriteScale, Scale::kSpriteScale};
}

void Bullet::SyncPose(glm::vec2 pos, float headingRad) {
    if (!m_HasVisual) {
        m_Transform.translation = pos; // legacy: centred, upright
        return;
    }
    const BulletPose p = ComputeBulletPose(pos, headingRad, m_VisOffsetPx, m_VisRotRad,
                                           {Scale::kSpriteScale * m_VisScale.x,
                                            Scale::kSpriteScale * m_VisScale.y},
                                           m_VisFlipX);
    m_Transform.translation = p.translation;
    m_Transform.rotation = p.rotation;
    m_Transform.scale = p.scale;
}

void Bullet::Init(glm::vec2 pos, glm::vec2 velocityPxPerSec, float lifetimeMs,
                  int damage, int camp) {
    Init(pos, velocityPxPerSec, lifetimeMs, damage, camp, 0.0F, 0, false, 0);
}

void Bullet::Init(glm::vec2 pos, glm::vec2 velocityPxPerSec, float lifetimeMs,
                  int damage, int camp, float repel, int critical,
                  bool canThrough, int pierce) {
    m_Transform.translation = pos;
    m_Velocity = velocityPxPerSec;
    m_LifeMs = lifetimeMs;
    m_Damage = damage;
    m_Camp = camp;
    m_Repel = repel;
    m_Critical = critical;
    m_CanThrough = canThrough;
    m_Pierce = pierce;
    m_Active = true;
    SetVisible(true);
}

bool Bullet::ConsumePierce() {
    // Non-piercing bullet despawns on its first hit.
    if (!m_CanThrough) {
        return true;
    }
    // Bool "infinite pierce" path (pierce budget 0): never despawns on hit.
    if (m_Pierce <= 0) {
        return false;
    }
    // Budgeted pierce: spend one pass-through; despawn when exhausted.
    --m_Pierce;
    return m_Pierce <= 0;
}

void Bullet::Update(float dtMs) {
    if (!m_Active) {
        return;
    }

    m_Transform.translation += m_Velocity * (dtMs * kMsToSec);
    m_LifeMs -= dtMs;

    if (m_LifeMs <= 0.0f) {
        Deactivate();
    }
}

bool Bullet::Active() const {
    return m_Active;
}

void Bullet::Deactivate() {
    m_Active = false;
    SetVisible(false);
}

int Bullet::Damage() const {
    return m_Damage;
}

int Bullet::Camp() const {
    return m_Camp;
}

Util::Collider Bullet::GetCollider() const {
    return Util::Collider::MakeCircle(m_Transform.translation, kColliderRadius);
}

} // namespace Game
