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
    FireIntent intent;
    intent.origin = origin;
    intent.speedPxPerSec = m_Params.bulletSpeedPxPerSec;
    intent.lifeMs = m_Params.lifeMs;
    intent.damage = m_Params.damage;
    intent.camp = 0;
    intent.critical = m_Params.critical;
    intent.repel = m_Params.repel;
    intent.canThrough = m_Params.canThrough;
    intent.pierce = m_Params.pierce;
    if (m_Params.kind == Kind::HeatMinigun) {
        // Heat-ramped single stream (Gun016): always one scattered shot, ignores count.
        const float ratio = Gun016::HeatRatio(m_HeatTime, m_Params.heatMaxTime);
        const float spread = Gun016::Spread(m_Params.heatBaseAngle, m_Params.heatRecoil, ratio);
        intent.pattern = FirePattern::Single;
        intent.dir = RotateDeg(Normalize(aimDir), m_Gun016.ScatterAngle(spread));
    } else if (m_Params.count > 1) {
        // Multi-shot weapon (WeaponDef.count): one deterministic Fan over the data fan-step.
        // No RNG scatter draw -- the spread IS the shape (FireSystem expands count bullets).
        intent.pattern = FirePattern::Fan;
        intent.dir = Normalize(aimDir);
        intent.count = m_Params.count;
        intent.spreadDeg = m_Params.fanSpreadDeg;
    } else {
        // Single shot (Gun001) with the brain's scatter cone.
        const float scatter = m_Gun001.ScatterAngle(m_Params.baseAngle, m_Params.recoil);
        intent.pattern = FirePattern::Single;
        intent.dir = RotateDeg(Normalize(aimDir), scatter);
    }
    out.push_back(intent);
    m_CooldownTicks = (std::max)(1, Scheduler::SecondsToTicks(m_Params.fireIntervalSeconds));
}

} // namespace Game::Sim
