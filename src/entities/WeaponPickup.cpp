#include "entities/WeaponPickup.hpp"

#include <memory>

#include "Util/Image.hpp"

namespace Game {

namespace {
constexpr float kPickupRadius = 16.0F;
constexpr float kPickupZIndex = 3.0F;
} // namespace

WeaponPickup::WeaponPickup(const std::string &resourceRoot, glm::vec2 pos,
                           const WeaponDef *def)
    : m_Def(def) {
    // Placeholder weapon-icon sprite until per-weapon sprites are mapped (#3).
    SetDrawable(std::make_shared<Util::Image>(resourceRoot +
                                              "/sprites/weapons_9.png"));
    SetZIndex(kPickupZIndex);
    m_Transform.translation = pos;
}

Util::Collider WeaponPickup::GetCollider() const {
    return Util::Collider::MakeCircle(m_Transform.translation, kPickupRadius);
}

} // namespace Game
