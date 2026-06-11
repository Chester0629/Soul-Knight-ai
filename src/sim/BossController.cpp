#include "sim/BossController.hpp"

#include <algorithm>

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

void BossController::OnShootTick() {
    if (m_Scheduler == nullptr || m_FireOut == nullptr || m_State.dead) {
        return;
    }
    if (m_State.awake) {
        const int attack = m_Brain.ChooseAttack(); // 1 draw Range(0,100)
        FireIntent intent;
        intent.pattern = FirePattern::Fan;
        intent.origin = m_State.pos;
        intent.dir = Normalize(m_Target - m_State.pos);
        intent.count = (attack % 2 == 0) ? kFanEven : kFanOdd;
        intent.spreadDeg = kFanSpreadDeg;
        intent.speedPxPerSec = kBulletSpeedPxPerSec;
        intent.lifeMs = kBulletLifeMs;
        intent.damage = 1;
        intent.camp = 1;
        m_FireOut->push_back(intent);
        m_FiredThisStep = true; // A: latch a shot for the boss "attack" AnimTrigger.
    }
    const int next = (std::max)(1, Scheduler::SecondsToTicks(m_Brain.ShootCd()));
    m_ShootHandle = m_Scheduler->Invoke(next, [this] { OnShootTick(); });
}

void BossController::OnWanderTick() {
    if (m_State.dead || !m_State.awake) {
        return;
    }
    m_WanderDir = m_Brain.WanderDirection(); // 2 draws Range(-1,1)
}

void BossController::Activate(Scheduler &scheduler, std::vector<FireIntent> &fireOut) {
    m_Scheduler = &scheduler;
    m_FireOut = &fireOut;
    const int wanderTicks = (std::max)(1, Scheduler::SecondsToTicks(kWanderSeconds));
    const int shootTicks = (std::max)(1, Scheduler::SecondsToTicks(m_Brain.ShootCd()));
    m_WanderHandle = scheduler.InvokeRepeating(wanderTicks, wanderTicks, [this] { OnWanderTick(); });
    m_ShootHandle = scheduler.Invoke(shootTicks, [this] { OnShootTick(); });
}

} // namespace Game::Sim
