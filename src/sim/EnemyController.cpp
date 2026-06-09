#include "sim/EnemyController.hpp"

#include <algorithm>
#include <cmath>

namespace Game::Sim {
namespace {

glm::vec2 Normalize(glm::vec2 v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y);
    return len > 0.0F ? v / len : glm::vec2{1.0F, 0.0F};
}

} // namespace

EnemyController::EnemyController(const Params &params, glm::vec2 spawn, int seed)
    : m_Params(params) {
    m_Brain.SetSeed(seed);
    m_Brain.SetKinematic(params.kinematic);
    m_State.pos = spawn;
    m_State.kinematic = params.kinematic;
}

void EnemyController::ApplyForce(glm::vec2 dir, float power) {
    m_ForceDir = dir;
    // Clamp to [0, cap]: negative power is a caller error (it would leave inertia
    // below the knockback threshold and silently disable knockback).
    m_InertialVel = std::clamp(power, 0.0F, kForceCap);
}

glm::vec2 EnemyController::ComputeVelocity() {
    const float scale = m_Params.speed * (m_Params.speedRate + 1.0F);
    glm::vec2 velocity = m_MoveDir * scale;
    if (!m_Params.kinematic && m_InertialVel > kKnockbackThreshold) {
        velocity += m_ForceDir * m_InertialVel;
        m_InertialVel *= m_Params.friction;
    }
    m_State.vel = velocity;
    return velocity;
}

void EnemyController::Kill() {
    m_State.dead = true;
    m_Brain.SetDead(true);
    // Stop the repeating scout cadence (mirrors the decomp's CancelInvoke on death)
    // so a dead enemy does not churn a scheduler slot every tick forever. The shoot
    // chain self-terminates via its dead gate, so it needs no explicit cancel.
    if (m_Scheduler != nullptr) {
        m_Scheduler->Cancel(m_ScoutHandle);
    }
}

void EnemyController::OnScoutTick() {
    if (m_State.dead) {
        m_Brain.SetDead(true);
        return; // dead gate: no Scout/RunReflection draw.
    }
    m_Brain.Scout();                     // 1 draw (Range(0,10))
    m_MoveDir = m_Brain.RunReflection(); // 2 draws (Range(-1,1) x2), normalized
}

void EnemyController::OnShootTick() {
    if (m_Scheduler == nullptr || m_FireOut == nullptr || m_State.dead) {
        return; // dead enemies stop firing and stop rescheduling.
    }
    // (No SetDead write here: ShootReflection only READS the dead flag in the
    // decomp, and the gate above already guarantees we are alive.)
    float outCd = m_Params.shootCdSeconds;
    const bool fired = m_Brain.ShootReflection(outCd, m_Params.shootCdSeconds);
    if (fired) {
        FireIntent intent;
        intent.pattern = FirePattern::Single;
        intent.origin = m_State.pos;
        intent.dir = Normalize(m_Target - m_State.pos);
        intent.speedPxPerSec = m_Params.speed * 5.0F; // enemy bullet speed (slice constant)
        intent.lifeMs = 1500.0F;
        intent.damage = 1;
        intent.camp = 1; // enemy bullet
        m_FireOut->push_back(intent);
    }
    const int next = (std::max)(1, Scheduler::SecondsToTicks(outCd));
    m_Scheduler->Invoke(next, [this] { OnShootTick(); });
}

void EnemyController::Activate(Scheduler &scheduler,
                               std::vector<FireIntent> &fireOut) {
    m_Scheduler = &scheduler;
    m_FireOut = &fireOut;
    const int scoutTicks = (std::max)(1, Scheduler::SecondsToTicks(m_Params.scoutRateSeconds));
    const int shootTicks = (std::max)(1, Scheduler::SecondsToTicks(m_Params.shootCdSeconds));
    m_ScoutHandle = scheduler.InvokeRepeating(scoutTicks, scoutTicks, [this] { OnScoutTick(); });
    scheduler.Invoke(shootTicks, [this] { OnShootTick(); });
}

} // namespace Game::Sim
