#include "entities/CoinPickup.hpp"
#include "game/WorldScale.hpp"

#include <memory>
#include <vector>

#include "Util/Animation.hpp"
#include "Util/Image.hpp"

namespace Game {
namespace {
constexpr float kZIndex = 3.5F; // above floor tiles / weapon pickups, below actors.
constexpr float kScale = Scale::kSpriteScale; // 16 px per unit -> 32 px cell (shared rule)
} // namespace

CoinPickup::CoinPickup(const std::string &resourceRoot, glm::vec2 pos, RewardKind kind,
                       glm::vec2 scatterDir, const PickupDef &def, float friction,
                       float scatterImpulse)
    : m_Kind(kind), m_Value(def.value), m_Magnet(CoinMagnet::Make(pos, def.speed, def.range)) {
    // P1.12: frames / speed / range / value all come from pickups.json
    // (res/register coin_N + energy prefabs, coin_0.controller clips).
    std::vector<std::string> frames;
    for (const std::string &f : def.frames) {
        frames.push_back(resourceRoot + "/sprites/" + f + ".png");
    }
    if (frames.size() > 1) {
        SetDrawable(std::make_shared<Util::Animation>(frames, true, def.frameMs, true));
    } else if (!frames.empty()) {
        SetDrawable(std::make_shared<Util::Image>(frames.front()));
    }
    SetZIndex(kZIndex);
    m_Transform.translation = pos;
    m_Transform.scale = {kScale, kScale};
    m_Magnet.friction = friction;
    m_Magnet.GetForce(scatterDir, scatterImpulse);
}

bool CoinPickup::Update(float dtMs, glm::vec2 playerPos) {
    const bool collected = m_Magnet.Step(dtMs, playerPos);
    m_Transform.translation = m_Magnet.pos;
    return collected;
}

} // namespace Game
