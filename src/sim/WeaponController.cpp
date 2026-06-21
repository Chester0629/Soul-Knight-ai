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

// (d) dispatch ctor: own a per-gun IWeaponBrain. The legacy Gun001/Gun016 members
// are still seeded (inert here) so the object is fully constructed either way.
WeaponController::WeaponController(std::unique_ptr<IWeaponBrain> brain, const Params &params,
                                  int seed)
    : m_Params(params), m_Brain(std::move(brain)) {
    if (m_Brain != nullptr) {
        m_Brain->SetSeed(seed);
    }
    m_Gun001.SetSeed(seed);
    m_Gun016.SetSeed(seed);
}

int WeaponController::Tick(bool firing, glm::vec2 origin, glm::vec2 aimDir,
                           std::vector<FireIntent> &out) {
    // --- (d) brain path: the gun brain owns the pattern + heat/charge/burst state.
    // The fire-rate cooldown gate stays here and is passed as FireContext.canFire
    // (Single/Fan respect it; Burst/Charge are edge-triggered and ignore it).
    if (m_Brain != nullptr) {
        if (m_CooldownTicks > 0) {
            --m_CooldownTicks;
        }
        IWeaponBrain::FireContext ctx;
        ctx.firing = firing;
        ctx.canFire = (m_CooldownTicks == 0);
        ctx.origin = origin;
        ctx.aim = aimDir;
        ctx.fixedStepSeconds = kFixedStepSeconds;
        ctx.baseAngle = m_Params.baseAngle;
        ctx.recoil = m_Params.recoil;
        ctx.bulletSpeedPxPerSec = m_Params.bulletSpeedPxPerSec;
        ctx.lifeMs = m_Params.lifeMs;
        ctx.damage = m_Params.damage;
        ctx.critical = m_Params.critical;
        ctx.repel = m_Params.repel;
        ctx.canThrough = m_Params.canThrough;
        ctx.pierce = m_Params.pierce;
        ctx.camp = 0;
        ctx.count = m_Params.count;
        ctx.stepAngle = m_Params.fanStepDeg;
        const int pulls = m_Brain->Tick(ctx, out);
        if (pulls > 0) {
            m_CooldownTicks = (std::max)(1, Scheduler::SecondsToTicks(m_Params.fireIntervalSeconds));
        }
        return pulls;
    }

    // --- legacy path (Params-driven; WeaponControllerTest regression anchor) ---
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
        return 0;
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
    return 1; // one pull resolved (the data-Fan path is one pull, count pellets).
}

} // namespace Game::Sim
