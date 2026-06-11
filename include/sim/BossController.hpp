#ifndef GAME_SIM_BOSSCONTROLLER_HPP
#define GAME_SIM_BOSSCONTROLLER_HPP

#include <vector>

#include <glm/glm.hpp>

#include "combat/BossAI01.hpp"
#include "sim/EntityState.hpp"
#include "sim/FireIntent.hpp"
#include "sim/Scheduler.hpp"

namespace Game::Sim {

/// Drives the BossAI01 boss: chases the player, fires a brain-selected fan on its
/// shoot cadence (halved once on angry), wanders on a wander cadence. BossAI01 is
/// the sole RNG stream. Move/copy deleted (this-capturing scheduler callbacks).
class BossController {
public:
    static constexpr int kFanEven = 3;
    static constexpr int kFanOdd = 5;
    static constexpr float kFanSpreadDeg = 30.0F;
    static constexpr float kSpeed = 45.0F;
    static constexpr float kWanderSeconds = 0.5F;
    static constexpr float kBulletSpeedPxPerSec = 300.0F;
    static constexpr float kBulletLifeMs = 1500.0F;

    BossController(float baseShootCd, glm::vec2 spawn, int maxHp, int seed);

    BossController(BossController &&) = delete;
    BossController &operator=(BossController &&) = delete;

    /// @pre scheduler/fireOut outlive this; fireOut not reallocated while alive.
    void Activate(Scheduler &scheduler, std::vector<FireIntent> &fireOut);

    void SetTarget(glm::vec2 target) { m_Target = target; }

    /// Apply post-hit HP; enters the angry phase once at < 50% (BossAI01.OnHurt).
    void OnHurt(int hpAfter, int maxHp);

    /// Unit chase direction toward the target (zero -> {1,0}).
    glm::vec2 ChaseDir() const;

    void Kill();

    bool Angry() const { return m_Brain.Angry(); }
    float ShootCdSeconds() const { return m_Brain.ShootCd(); }
    const EntityState &State() const { return m_State; }
    EntityState &MutableState() { return m_State; }
    BossAI01 &Brain() { return m_Brain; }
    glm::vec2 WanderDir() const { return m_WanderDir; }
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

    BossAI01 m_Brain;
    EntityState m_State;
    glm::vec2 m_Target{0.0F, 0.0F};
    glm::vec2 m_WanderDir{0.0F, 0.0F};
    bool m_FiredThisStep = false; ///< A: latched in OnShootTick, drained by ConsumeFiredThisStep.

    Scheduler *m_Scheduler = nullptr;
    std::vector<FireIntent> *m_FireOut = nullptr;
    Scheduler::Handle m_WanderHandle = 0;
    Scheduler::Handle m_ShootHandle = 0;
};

} // namespace Game::Sim

#endif /* GAME_SIM_BOSSCONTROLLER_HPP */
