#include "sim/BossController.hpp"

#include "sim/SimMath.hpp"

namespace Game::Sim {

BossController::BossController(float baseShootCd, glm::vec2 spawn, int maxHp, int seed)
    : m_Brain(baseShootCd) {
    m_Brain.SetSeed(seed);
    m_State.pos = spawn;
    m_State.stats.maxHp = maxHp;
    m_State.stats.hp = maxHp;
}

void BossController::OnHurt(int hpAfter, int maxHp) {
    m_Brain.OnHurt(hpAfter, maxHp);
}

glm::vec2 BossController::ChaseDir() const {
    return Normalize(m_Target - m_State.pos);
}

void BossController::Kill() {
    m_State.dead = true;
    if (m_Scheduler != nullptr) {
        m_Scheduler->Cancel(m_WanderHandle);
        m_Scheduler->Cancel(m_ShootHandle);
    }
}

void BossController::OnShootTick() {}  // Task 3
void BossController::OnWanderTick() {} // Task 3

void BossController::Activate(Scheduler &scheduler, std::vector<FireIntent> &fireOut) {
    m_Scheduler = &scheduler;
    m_FireOut = &fireOut;
    // cadence scheduling in Task 3.
}

} // namespace Game::Sim
