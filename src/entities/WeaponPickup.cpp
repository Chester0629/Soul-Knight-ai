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
    // WEAPON RE-KEY: the weapon's real icon (weapons.json icon, resolved from its
    // prefab's SpriteRenderer); the weapons_9 placeholder only when unresolved.
    const std::string icon =
        (def != nullptr && !def->icon.empty()) ? def->icon : std::string("weapons_9.png");
    SetDrawable(std::make_shared<Util::Image>(resourceRoot + "/sprites/" + icon));
    SetZIndex(kPickupZIndex);
    m_Transform.translation = pos;
}

Util::Collider WeaponPickup::GetCollider() const {
    return Util::Collider::MakeCircle(m_Transform.translation, kPickupRadius);
}

} // namespace Game
