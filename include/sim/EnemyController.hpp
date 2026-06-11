#ifndef GAME_SIM_ENEMYCONTROLLER_HPP
#define GAME_SIM_ENEMYCONTROLLER_HPP

#include <vector>

#include <glm/glm.hpp>

#include "combat/EnemyAI01.hpp"
#include "sim/EntityState.hpp"
#include "sim/FireIntent.hpp"
#include "sim/Scheduler.hpp"

namespace Game::Sim {

/// Drives one EnemyAI01 enemy: the brain (sole RNG stream) decides scout/wander/
/// shoot on the Scheduler cadence; the controller computes the faithful velocity
/// and emits FireIntents. Engine-free and deterministic.
///
/// Inertia ownership: this controller owns the knockback state (m_InertialVel /
/// m_ForceDir) and reproduces EnemyAI01__FixedUpdate's velocity composition in
/// ComputeVelocity. The brain's own inertial_vel (0x44) / FixedUpdateStep /
/// SetInertialVel are therefore NOT used here -- driving them too would split the
/// state into two diverging copies. Use the brain only for the RNG decisions
/// (Scout / RunReflection / ShootReflection).
class EnemyController {
public:
    /// The RGEController GetForce knockback cap (FAITHFUL @ game_full.c:473583).
    static constexpr float kForceCap = 28.0F;
    /// inertialVel must exceed this for the knockback term (FAITHFUL @ 675976).
    static constexpr float kKnockbackThreshold = 1.0F;

    // Slice placeholders for the enemy bullet (greppable + obviously temporary).
    // Plan 3/4 will source these from EnemyGunDef.bulletSpeed * kDataSpeedToPxPerSec
    // and the bullet's destroy_time instead of these flat values.
    static constexpr float kSliceBulletSpeedMul = 5.0F;  ///< bullet speed = speed * this.
    static constexpr float kSliceBulletLifeMs = 1500.0F; ///< bullet lifetime (ms).

    struct Params {
        float speed = 60.0F;
        float speedRate = 0.0F;
        float friction = 0.9F;     ///< inertial_vel decay per step; must be in (0,1).
        float shootCdSeconds = 1.0F;
        float scoutRateSeconds = 0.5F;
        bool kinematic = false;
    };

    EnemyController(const Params &params, glm::vec2 spawn, int seed);

    // Non-movable / non-copyable: Activate registers scheduler callbacks that
    // capture `this`, so the controller must keep a stable address for its whole
    // lifetime (the owner stores it by stable pointer, e.g. unique_ptr, never in a
    // reallocating vector-of-values). Moving it would dangle those callbacks.
    EnemyController(EnemyController &&) = delete;
    EnemyController &operator=(EnemyController &&) = delete;

    /// Schedule the scout + shoot cadence. @p fireOut receives emitted intents.
    /// @pre @p scheduler and @p fireOut must outlive this controller, and @p fireOut
    ///      must not be reallocated while the controller is alive (the controller
    ///      keeps raw pointers to both).
    void Activate(Scheduler &scheduler, std::vector<FireIntent> &fireOut);

    /// Set the aim target (the player position), updated each tick by the owner.
    void SetTarget(glm::vec2 target) { m_Target = target; }

    /// Seed knockback (FAITHFUL: RGEController__GetForce @473583).
    void ApplyForce(glm::vec2 dir, float power);

    /// Compose this fixed step's velocity and decay knockback.
    /// MUST be called exactly once per fixed step: it mutates m_InertialVel
    /// (friction decay) and writes m_State.vel, so a second call in the same tick
    /// decays twice and returns a stale velocity.
    glm::vec2 ComputeVelocity();

    /// Latch death: brain + state stop acting; scheduled callbacks no-op.
    void Kill();

    const EntityState &State() const { return m_State; }
    EntityState &MutableState() { return m_State; }
    float InertialVel() const { return m_InertialVel; }
    glm::vec2 MoveDir() const { return m_MoveDir; }
    void SetMoveDir(glm::vec2 dir) { m_MoveDir = dir; }
    EnemyAI01 &Brain() { return m_Brain; }

private:
    void OnScoutTick();
    void OnShootTick();

    Params m_Params;
    EnemyAI01 m_Brain;
    EntityState m_State;
    glm::vec2 m_MoveDir{0.0F, 0.0F};
    glm::vec2 m_ForceDir{0.0F, 0.0F};
    float m_InertialVel = 0.0F;
    glm::vec2 m_Target{0.0F, 0.0F};

    Scheduler *m_Scheduler = nullptr;
    std::vector<FireIntent> *m_FireOut = nullptr;
    Scheduler::Handle m_ScoutHandle = 0; ///< the repeating scout cadence, cancelled on Kill.
};

} // namespace Game::Sim

#endif /* GAME_SIM_ENEMYCONTROLLER_HPP */
