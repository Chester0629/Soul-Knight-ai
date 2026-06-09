#include "sim/WeaponController.hpp"

#include <algorithm>

#include "sim/Scheduler.hpp"
#include "sim/SimConfig.hpp"
#include "sim/SimMath.hpp"

namespace Game::Sim {

WeaponController::WeaponController(const Params &params, int seed)
    : m_Params(params) {
    // Seed both brains; `kind` is fixed at construction so only the active brain's
    // stream is ever drawn -- the identical seed on the unused brain is inert.
    m_Gun001.SetSeed(seed);
    m_Gun016.SetSeed(seed);
}

void WeaponController::Tick(bool firing, glm::vec2 origin, glm::vec2 aimDir,
                           std::vector<FireIntent> &out) {
    // Heat model (Gun016): ramp while firing toward heatMaxTime, cool toward 0 when
    // released. The ramp/cool step is the fixed timestep (FAITHFUL: Gun016 heat tick).
    if (m_Params.kind == Kind::HeatMinigun) {
        if (firing) {
            if (Gun016::ShouldTickHeat(true, m_HeatTime, m_Params.heatMaxTime)) {
                m_HeatTime += kFixedStepSeconds;
            }
        } else {
            m_HeatTime = (std::max)(0.0F, m_HeatTime - kFixedStepSeconds);
        }
    }
    if (m_CooldownTicks > 0) {
        --m_CooldownTicks;
    }
    if (!firing || m_CooldownTicks > 0) {
        return;
    }
    float scatter = 0.0F;
    if (m_Params.kind == Kind::Single) {
        scatter = m_Gun001.ScatterAngle(m_Params.baseAngle, m_Params.recoil);
    } else { // HeatMinigun
        const float ratio = Gun016::HeatRatio(m_HeatTime, m_Params.heatMaxTime);
        const float spread = Gun016::Spread(m_Params.heatBaseAngle, m_Params.heatRecoil, ratio);
        scatter = m_Gun016.ScatterAngle(spread);
    }
    FireIntent intent;
    intent.pattern = FirePattern::Single;
    intent.origin = origin;
    intent.dir = RotateDeg(Normalize(aimDir), scatter);
    intent.speedPxPerSec = m_Params.bulletSpeedPxPerSec;
    intent.lifeMs = m_Params.lifeMs;
    intent.damage = m_Params.damage;
    intent.camp = 0;
    out.push_back(intent);
    m_CooldownTicks = (std::max)(1, Scheduler::SecondsToTicks(m_Params.fireIntervalSeconds));
}

} // namespace Game::Sim
