#include "entities/CoinPickup.hpp"

#include <memory>
#include <vector>

#include "Util/Animation.hpp"

namespace Game {
namespace {
constexpr float kZIndex = 3.5F; // above floor tiles / weapon pickups, below actors.
constexpr float kScale = 1.5F;
/// FAITHFUL: RGCoin.speed 20 / RGEnergy.speed 14, range 6 (res/register prefabs).
constexpr float kCoinSpeedUnits = 20.0F;
constexpr float kEnergySpeedUnits = 14.0F;
constexpr float kRangeUnits = 6.0F;
/// Spawn scatter impulse (units/s) -- RGEController hands GetForce a small random
/// direction on drop; magnitude is a port choice tuned to the 0.9 friction.
constexpr float kScatterImpulseUnits = 6.0F;
/// FAITHFUL @ coin_0.controller clips: 4-frame spin, sampled 8 fps in the clip.
constexpr int kFrameMs = 125;

// FAITHFUL @ AnimationClip coin_gold_0 / coin_silver_0 / coin_copper_0 (sprite
// curves of the coin_0.controller states) and energy.prefab's bullet_16 orb.
std::vector<std::string> Frames(const std::string &root, RewardKind k) {
    const char *base = nullptr;
    int first = 0;
    switch (k) {
    case RewardKind::Gold: base = "objects_common_"; first = 31; break;
    case RewardKind::Silver: base = "objects_common_"; first = 27; break;
    case RewardKind::Copper: base = "objects_common_"; first = 23; break;
    case RewardKind::Energy: return {root + "/sprites/bullet_16.png"};
    }
    std::vector<std::string> out;
    for (int i = 0; i < 4; ++i) {
        out.push_back(root + "/sprites/" + base + std::to_string(first + i) + ".png");
    }
    return out;
}
} // namespace

CoinPickup::CoinPickup(const std::string &resourceRoot, glm::vec2 pos, RewardKind kind,
                       glm::vec2 scatterDir)
    : m_Kind(kind), m_Value(RewardValue(kind)),
      m_Magnet(CoinMagnet::Make(pos, kind == RewardKind::Energy ? kEnergySpeedUnits
                                                                : kCoinSpeedUnits,
                                kRangeUnits)) {
    SetDrawable(std::make_shared<Util::Animation>(Frames(resourceRoot, kind), true,
                                                  kFrameMs, true));
    SetZIndex(kZIndex);
    m_Transform.translation = pos;
    m_Transform.scale = {kScale, kScale};
    m_Magnet.GetForce(scatterDir, kScatterImpulseUnits);
}

bool CoinPickup::Update(float dtMs, glm::vec2 playerPos) {
    const bool collected = m_Magnet.Step(dtMs, playerPos);
    m_Transform.translation = m_Magnet.pos;
    return collected;
}

} // namespace Game
