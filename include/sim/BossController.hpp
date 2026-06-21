#ifndef GAME_SIM_BOSSCONTROLLER_HPP
#define GAME_SIM_BOSSCONTROLLER_HPP

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "sim/EntityState.hpp"
#include "sim/FireIntent.hpp"
#include "sim/IBossBrain.hpp"
#include "sim/Scheduler.hpp"

namespace Game::Sim {

/// Drives a boss through the (d) @ref IBossBrain dispatch layer (B1-P2): moves
/// per the brain's move decision (chase / strafe / retreat for BossAI01, wander
/// for the rest) on a think cadence, and fires a brain-selected pattern on its
/// shoot cadence. The brain is the sole RNG stream. Move/copy deleted
/// (this-capturing scheduler callbacks). The default ctor builds a BossAI01
/// adapter so the legacy path stays byte-identical; the injecting ctor takes any
/// dispatched brain (B1-P2 roster).
class BossController {
public:
    static constexpr int kFanEven = 3;
    static constexpr int kFanOdd = 5;
    static constexpr float kFanSpreadDeg = 30.0F;
    static constexpr float kSpeed = 45.0F;
    static constexpr float kWanderSeconds = 0.5F;
    static constexpr float kBulletSpeedPxPerSec = 300.0F;
    static constexpr float kBulletLifeMs = 1500.0F;

    /// Legacy/default: builds the BossAI01 adapter internally (byte-identical to
    /// the pre-(d) controller -- the determinism gate).
    BossController(float baseShootCd, glm::vec2 spawn, int maxHp, int seed);

    /// (d) dispatch: drive an injected brain (the B1-P2 boss roster). @p brain is
    /// seeded here with @p seed; @p baseShootCd is the controller's base cadence.
    BossController(std::unique_ptr<IBossBrain> brain, float baseShootCd,
                   glm::vec2 spawn, int maxHp, int seed);

    BossController(BossController &&) = delete;
    BossController &operator=(BossController &&) = delete;

    /// @pre scheduler/fireOut outlive this; fireOut not reallocated while alive.
    void Activate(Scheduler &scheduler, std::vector<FireIntent> &fireOut);

    void SetTarget(glm::vec2 target) { m_Target = target; }

    /// Apply post-hit HP; enters the angry phase once at < 50% (brain OnHurt).
    void OnHurt(int hpAfter, int maxHp);

    /// Unit chase direction toward the target (zero -> {1,0}).
    glm::vec2 ChaseDir() const;

    void Kill();

    bool Angry() const { return m_Brain->Angry(); }
    float ShootCdSeconds() const { return m_Brain->ShootCd(); }
    const EntityState &State() const { return m_State; }
    EntityState &MutableState() { return m_State; }
    /// The (d) interface the controller drives (exposes the RngRange probe for tests).
    IBossBrain &Brain() { return *m_Brain; }
    /// F2: the per-cycle move decision (chase / strafe / retreat / wander) the boss moves
    /// along; {0,0} before the first think tick. Consumed by Simulation::MoveControllers.
    glm::vec2 MoveDir() const { return m_MoveDir; }
    /// A (presentation): true iff the boss fired since the last call; reading clears the
    /// latch. Simulation drains it per step to emit a boss "attack" AnimTrigger SimEvent.
    bool ConsumeFiredThisStep() {
        const bool fired = m_FiredThisStep;
        m_FiredThisStep = false;
        return fired;
    }

private:
    void OnShootTick();
    void OnWanderTick();

    std::unique_ptr<IBossBrain> m_Brain;
    float m_BaseShootCd; ///< controller base cadence, fed to the brain's AttackTick.
    EntityState m_State;
    glm::vec2 m_Target{0.0F, 0.0F};
    glm::vec2 m_MoveDir{0.0F, 0.0F}; ///< F2: move decision; consumed by MoveControllers.
    bool m_FiredThisStep = false; ///< A: latched in OnShootTick, drained by ConsumeFiredThisStep.

    Scheduler *m_Scheduler = nullptr;
    std::vector<FireIntent> *m_FireOut = nullptr;
    Scheduler::Handle m_WanderHandle = 0;
    Scheduler::Handle m_ShootHandle = 0;
};

} // namespace Game::Sim

#endif /* GAME_SIM_BOSSCONTROLLER_HPP */
