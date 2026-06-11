#include "entities/Chest.hpp"

#include <memory>

#include "Util/Image.hpp"

namespace Game {

namespace {
constexpr float kChestRadius = 18.0F;
constexpr float kChestZIndex = 2.0F;
} // namespace

Chest::Chest(const std::string &resourceRoot, glm::vec2 pos, int tier)
    : m_Tier(tier) {
    // Placeholder sprite (box02) until a dedicated chest sprite is wired.
    SetDrawable(std::make_shared<Util::Image>(resourceRoot +
                                              "/sprites/box02.png"));
    SetZIndex(kChestZIndex);
    m_Transform.translation = pos;
}

Util::Collider Chest::GetCollider() const {
    return Util::Collider::MakeCircle(m_Transform.translation, kChestRadius);
}

} // namespace Game
