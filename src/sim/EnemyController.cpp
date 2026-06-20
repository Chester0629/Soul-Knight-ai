#include "sim/EnemyController.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

#include "sim/EnemyBrainAdapters.hpp"
#include "sim/SimMath.hpp"

namespace Game::Sim {

// Default ctor: the AI01 adapter (back-compat + the determinism tests). Delegates
// to the (d) inject ctor so there is one initialisation path.
EnemyController::EnemyController(const Params &params, glm::vec2 spawn, int seed)
    : EnemyController(std::make_unique<EnemyAI01Adapter>(), params, spawn, seed) {}

EnemyController::EnemyController(std::unique_ptr<IEnemyBrain> brain,
                                const Params &params, glm::vec2 spawn, int seed)
    : m_Params(params), m_Brain(std::move(brain)) {
    m_Brain->SetSeed(seed);
    m_Brain->SetKinematic(params.kinematic);
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
    // A kinematic enemy is a fixed turret (EnemyAI06, the sole `kinematic:1` def): it never
    // translates -- it only re-aims (facing) and fires. The EnemyAI01 brain still picks a
    // wander m_MoveDir on its scout tick, so we must suppress the steering here, not just the
    // knockback term. ResolveHits already skips ApplyForce on kinematic targets, so knockback
    // never accrues either; this makes the "fixed" part faithful to the FixedRotation turret.
    if (m_Params.kinematic) {
        m_State.vel = glm::vec2(0.0F, 0.0F);
        return m_State.vel;
    }
    const float scale = m_Params.speed * (m_Params.speedRate + 1.0F);
    glm::vec2 velocity = m_MoveDir * scale;
    if (m_InertialVel > kKnockbackThreshold) {
        velocity += m_ForceDir * m_InertialVel;
        m_InertialVel *= m_Params.friction;
    }
    m_State.vel = velocity;
    return velocity;
}

void EnemyController::Kill() {
    m_State.dead = true;
    m_Brain->SetDead(true);
    // Stop the repeating scout cadence (mirrors the decomp's CancelInvoke on death)
    // so a dead enemy does not churn a scheduler slot every tick forever. The shoot
    // chain self-terminates via its dead gate, so it needs no explicit cancel.
    if (m_Scheduler != nullptr) {
        m_Scheduler->Cancel(m_ScoutHandle);
    }
}

void EnemyController::OnScoutTick() {
    // Dead/asleep gate: no Scout/RunReflection draw. (Kill() is the single source
    // of truth for the dead latch, so we do not re-write it here.)
    if (m_State.dead || !m_State.awake) {
        return;
    }
    m_Brain->Scout();                     // 1 draw (Range(0,10))
    m_MoveDir = m_Brain->RunReflection(); // 2 draws (Range(-1,1) x2), normalized
}

void EnemyController::OnShootTick() {
    if (m_Scheduler == nullptr || m_FireOut == nullptr || m_State.dead) {
        return; // dead enemies stop firing AND stop rescheduling (chain ends).
    }
    // (No SetDead write here: ShootReflection only READS the dead flag in the
    // decomp, and the gate above already guarantees we are alive.)
    float outCd = m_Params.shootCdSeconds;
    if (m_State.awake) { // an asleep enemy skips the shot but keeps the chain alive.
        const IEnemyBrain::ShootResult shot =
            m_Brain->ShootTick(m_Params.shootCdSeconds);
        outCd = shot.nextCd;
        if (shot.fired) {
            FireIntent intent;
            intent.pattern = FirePattern::Single;
            intent.origin = m_State.pos;
            intent.dir = Normalize(m_Target - m_State.pos);
            intent.speedPxPerSec = m_Params.speed * kSliceBulletSpeedMul;
            intent.lifeMs = kSliceBulletLifeMs;
            intent.damage = 1;
            intent.camp = 1; // enemy bullet
            m_FireOut->push_back(intent);
            m_FiredThisStep = true; // A: latch a shot for the "attack" AnimTrigger.
        }
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
