#include "entities/Chest.hpp"

#include <memory>
#include <utility>

namespace Game {

namespace {
constexpr float kChestRadius = 18.0F;
constexpr float kChestZIndex = 2.0F;
constexpr float kScale = 2.0F; // 16 px per unit -> 32 px cell

// FAITHFUL @ res/item/chest*.prefab SpriteRenderers (objects_common atlas).
const char *ClosedSprite(int tier, Chest::Kind kind) {
    if (kind == Chest::Kind::RoomReward) {
        return "objects_common_9";
    }
    if (kind == Chest::Kind::Boss || tier >= 2) {
        return "objects_common_13";
    }
    return tier == 1 ? "objects_common_3" : "objects_common_6";
}
const char *OpenSprite(int tier, Chest::Kind kind) {
    if (kind == Chest::Kind::RoomReward) {
        return "objects_common_8";
    }
    if (kind == Chest::Kind::Boss || tier >= 2) {
        return "objects_common_10";
    }
    return "objects_common_0";
}
} // namespace

Chest::Chest(const std::string &resourceRoot, glm::vec2 pos, int tier, Kind kind)
    : m_Tier(tier), m_Kind(kind) {
    SetDrawable(std::make_shared<Util::Image>(resourceRoot + "/sprites/" +
                                              ClosedSprite(tier, kind) + ".png"));
    m_OpenImage = std::make_shared<Util::Image>(resourceRoot + "/sprites/" +
                                                OpenSprite(tier, kind) + ".png");
    SetZIndex(kChestZIndex);
    m_Transform.translation = pos;
    m_Transform.scale = {kScale, kScale};
}

bool Chest::Open() {
    if (m_Opened) {
        return false;
    }
    m_Opened = true;
    SetDrawable(m_OpenImage);
    return true;
}

Util::Collider Chest::GetCollider() const {
    return Util::Collider::MakeCircle(m_Transform.translation, kChestRadius);
}

} // namespace Game
