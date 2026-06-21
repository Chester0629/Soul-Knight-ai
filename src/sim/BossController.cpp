#include "sim/BossController.hpp"

#include <algorithm>
#include <utility>

#include "sim/BossBrainAdapters.hpp"
#include "sim/SimMath.hpp"

namespace Game::Sim {

BossController::BossController(float baseShootCd, glm::vec2 spawn, int maxHp, int seed)
    : BossController(MakeBossBrain("BossAI01", baseShootCd), baseShootCd, spawn, maxHp,
                     seed) {}

BossController::BossController(std::unique_ptr<IBossBrain> brain, float baseShootCd,
                              glm::vec2 spawn, int maxHp, int seed)
    : m_Brain(std::move(brain)), m_BaseShootCd(baseShootCd) {
    m_Brain->SetSeed(seed);
    m_State.pos = spawn;
    m_State.stats.maxHp = maxHp;
    m_State.stats.hp = maxHp;
}

void BossController::OnHurt(int hpAfter, int maxHp) {
    m_Brain->OnHurt(hpAfter, maxHp);
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
    // FAITHFUL/determinism: the attack roll is taken ONLY when awake (the old
    // ChooseAttack() draw lived inside `if (awake)`); an asleep boss takes NO draw
    // and just reschedules on its cadence, keeping the stream in lockstep.
    float nextCd = 0.0F;
    if (m_State.awake) {
        const IBossBrain::AttackResult atk = m_Brain->AttackTick(m_BaseShootCd);
        if (atk.fired) {
            FireIntent intent;
            // The pattern ORIGINATES at the adapter (the (d) seam). Fan today;
            // FireSystem renders it. When the patterns stage lands, adapters return
            // Burst/Charge/Parabola and FireSystem implements them -- no re-wire here.
            intent.pattern = atk.pattern;
            intent.origin = m_State.pos;
            intent.dir = Normalize(m_Target - m_State.pos);
            intent.count = atk.count;
            intent.spreadDeg = atk.spreadDeg;
            intent.speedPxPerSec = kBulletSpeedPxPerSec;
            intent.lifeMs = kBulletLifeMs;
            intent.damage = 1;
            intent.camp = 1;
            m_FireOut->push_back(intent);
            m_FiredThisStep = true; // A: latch a shot for the boss "attack" AnimTrigger.
        }
        nextCd = atk.nextCd;
    } else {
        nextCd = m_Brain->ShootCd(); // no draw (const cadence read).
    }
    const int next = (std::max)(1, Scheduler::SecondsToTicks(nextCd));
    m_ShootHandle = m_Scheduler->Invoke(next, [this] { OnShootTick(); });
}

void BossController::OnWanderTick() {
    if (m_State.dead || !m_State.awake) {
        return;
    }
    // FAITHFUL: BossAI01's WITH-TARGET move decision (chase/strafe/retreat); the
    // wanderer bosses ignore chase/dist and reroll a random heading. The sim always
    // supplies the player as the target. Two int/float draws either way -- the same
    // stream length, so the shoot-tick attack roll is unchanged.
    const glm::vec2 chase = ChaseDir();
    const float dist = glm::distance(m_Target, m_State.pos);
    m_MoveDir = m_Brain->MoveDecision(chase, dist);
}

void BossController::Activate(Scheduler &scheduler, std::vector<FireIntent> &fireOut) {
    m_Scheduler = &scheduler;
    m_FireOut = &fireOut;
    const int wanderTicks = (std::max)(1, Scheduler::SecondsToTicks(kWanderSeconds));
    const int shootTicks = (std::max)(1, Scheduler::SecondsToTicks(m_Brain->ShootCd()));
    m_WanderHandle = scheduler.InvokeRepeating(wanderTicks, wanderTicks, [this] { OnWanderTick(); });
    m_ShootHandle = scheduler.Invoke(shootTicks, [this] { OnShootTick(); });
}

} // namespace Game::Sim
