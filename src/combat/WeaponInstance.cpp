#include "combat/WeaponInstance.hpp"

#include <algorithm>
#include <cmath>

namespace Game {
namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kDeg2Rad = kPi / 180.0F;

/// Rotate @p v counter-clockwise by @p degrees (+y up), matching the original's
/// Quaternion.Euler(0,0,z) applied to the muzzle forward axis.
glm::vec2 RotateDeg(glm::vec2 v, float degrees) {
    const float rad = degrees * kDeg2Rad;
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    return glm::vec2(v.x * c - v.y * s, v.x * s + v.y * c);
}

} // namespace

WeaponInstance::WeaponInstance(const WeaponDef &def)
    : m_Def(&def),
      m_FireIntervalMs(kBaseIntervalMs / std::max(def.weaponSpeed, 0.01F)) {}

void WeaponInstance::Update(float dtMs) {
    if (dtMs > 0.0F && m_CooldownMs > 0.0F) {
        m_CooldownMs -= dtMs;
        if (m_CooldownMs < 0.0F) {
            m_CooldownMs = 0.0F;
        }
    }
}

bool WeaponInstance::Ready() const { return m_CooldownMs <= 0.0F; }

bool WeaponInstance::TryFire() {
    if (!Ready()) {
        return false;
    }
    m_CooldownMs = m_FireIntervalMs;
    return true;
}

int WeaponInstance::EnergyCost() const { return m_Def->consume; }

int WeaponInstance::ShotCount() const {
    // Gun002 count loop: count<=0 collapses to a single straight shot.
    return m_Def->count > 0 ? m_Def->count : 1;
}

// FAITHFUL: Gun001__Attack @ game_full.c:315755 (spread = dev + dev*energyFactor).
float WeaponInstance::SpreadDegrees() const {
    const float dev = static_cast<float>(m_Def->deviation);
    return dev + dev * m_RecoilFactor;
}

void WeaponInstance::SetRecoilFactor(float factor) { m_RecoilFactor = factor; }

// FAITHFUL: RGWeapon::FireOnce (Gun001__Attack @ 315755 + Gun002 count loop),
//           RGBullet::SetBulletVelocity @ 0x5A8F98,
//           RGBullet::UpdateAttribute @ 0x5A8638 / 0x5A88DC.
FirePlan WeaponInstance::BuildFirePlan(glm::vec2 aimDir, RGRandom &rng) const {
    FirePlan plan;
    plan.energyCost = m_Def->consume; // MakeConsume spends once per pull.

    // Normalize the aim; a degenerate (zero) aim falls back to +x so the plan is
    // still well-formed (the original always has a muzzle forward axis).
    const float len = std::sqrt(aimDir.x * aimDir.x + aimDir.y * aimDir.y);
    const glm::vec2 baseDir =
        (len > 1e-6F) ? glm::vec2(aimDir.x / len, aimDir.y / len)
                      : glm::vec2(1.0F, 0.0F);

    const int count = ShotCount();
    const float spread = SpreadDegrees();
    const float fanStep = static_cast<float>(m_Def->angle);
    // Gun002 symmetric fan: centered on the aim. odd count => a center shot.
    const float fanStart = -(static_cast<float>(count) - 1.0F) * 0.5F * fanStep;

    plan.bullets.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        // Per-shot random deviation in [-spread, +spread] (RGRandom float Range is
        // max-INCLUSIVE). Draw order is shot order => deterministic for a seed.
        const float devAngle = (spread > 0.0F) ? rng.Range(-spread, spread) : 0.0F;
        const float fanOffset = fanStart + static_cast<float>(i) * fanStep;
        const glm::vec2 dir = RotateDeg(baseDir, fanOffset + devAngle);

        BulletSpawn b;
        b.direction = dir;
        b.velocity = dir * m_Def->bulletSpeed;
        b.damage = m_Def->atk;
        b.repel = m_Def->repel;
        b.critical = m_Def->critical;
        // Pierce: int through_count overload when a budget exists, else the bool
        // can_through overload (pierce stays 0 = "infinite" for the consumer).
        if (m_Def->throughCount > 0) {
            b.canThrough = true;
            b.pierce = m_Def->throughCount;
        } else {
            b.canThrough = (m_Def->canThrough != 0);
            b.pierce = 0;
        }
        plan.bullets.push_back(b);
    }

    return plan;
}

} // namespace Game
