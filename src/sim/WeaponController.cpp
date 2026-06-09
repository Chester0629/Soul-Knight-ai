#include "sim/WeaponController.hpp"

#include <algorithm>

#include "sim/Scheduler.hpp"
#include "sim/SimMath.hpp"

namespace Game::Sim {

WeaponController::WeaponController(const Params &params, int seed)
    : m_Params(params) {
    m_Gun001.SetSeed(seed);
    m_Gun016.SetSeed(seed);
}

void WeaponController::Tick(bool firing, glm::vec2 origin, glm::vec2 aimDir,
                           std::vector<FireIntent> &out) {
    if (m_CooldownTicks > 0) {
        --m_CooldownTicks;
    }
    if (!firing || m_CooldownTicks > 0) {
        return;
    }
    // emit one shot.
    float scatter = 0.0F;
    if (m_Params.kind == Kind::Single) {
        scatter = m_Gun001.ScatterAngle(m_Params.baseAngle, m_Params.recoil);
    }
    // (HeatMinigun branch added in Task 5.)
    FireIntent intent;
    intent.pattern = FirePattern::Single;
    intent.origin = origin;
    intent.dir = RotateDeg(Normalize(aimDir), scatter);
    intent.speedPxPerSec = m_Params.bulletSpeedPxPerSec;
    intent.lifeMs = m_Params.lifeMs;
    intent.damage = m_Params.damage;
    intent.camp = 0; // player bullet
    out.push_back(intent);
    m_CooldownTicks = (std::max)(1, Scheduler::SecondsToTicks(m_Params.fireIntervalSeconds));
}

} // namespace Game::Sim
