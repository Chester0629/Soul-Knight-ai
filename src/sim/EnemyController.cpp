#include "sim/EnemyController.hpp"

#include <algorithm>
#include <cmath>

namespace Game::Sim {
namespace {

[[maybe_unused]] glm::vec2 Normalize(glm::vec2 v) {
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
}

void EnemyController::OnScoutTick() {}
void EnemyController::OnShootTick() {}

void EnemyController::Activate(Scheduler &scheduler,
                               std::vector<FireIntent> &fireOut) {
    m_Scheduler = &scheduler;
    m_FireOut = &fireOut;
}

} // namespace Game::Sim
