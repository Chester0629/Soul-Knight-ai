#ifndef GAME_SIM_ENEMYCONTROLLER_HPP
#define GAME_SIM_ENEMYCONTROLLER_HPP

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "sim/EntityState.hpp"
#include "sim/FireIntent.hpp"
#include "sim/IEnemyBrain.hpp"
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
    // PORT CHOICE (T-54, P3.0): the enemy fire path does not read EnemyGunDef.bulletSpeed yet
    // (only 10 of 146 species have an EGun row, and EnemyDef carries no gun link), so every
    // enemy bullet flies at one flat speed. It is given in Unity units/s and goes through the
    // single data-speed factor like every other speed: 10 u/s = 320 px/s, inside the real
    // EGun range (6..24 u/s) and close to the slice's original 300 px/s. Plan 3/4 will source
    // it from EnemyGunDef.bulletSpeed * kDataSpeedToPxPerSec and the bullet's destroy_time.
    static constexpr float kSliceBulletSpeedUnits = 10.0F; ///< bullet speed, Unity units/s.
    static constexpr float kSliceBulletLifeMs = 1500.0F;   ///< bullet lifetime (ms).

    struct Params {
        /// Steering speed in px/s (BrainFactory converts EnemyDef.speed, units/s, with
        /// kDataSpeedToPxPerSec). The 60 default is a PORT CHOICE (T-56) for a def with no species
        /// data (a bare test EnemyDef{}), not a data value.
        float speed = 60.0F;
        float speedRate = 0.0F;
        float friction = 0.9F;     ///< inertial_vel decay per step; must be in (0,1).
        float shootCdSeconds = 1.0F;
        float scoutRateSeconds = 0.5F;
        bool kinematic = false;
    };

    /// Default: drives an EnemyAI01 brain (back-compat + the determinism tests;
    /// byte-identical to the pre-(d) controller).
    EnemyController(const Params &params, glm::vec2 spawn, int seed);
    /// (d) dispatch: drive any injected brain adapter (BrainFactory picks it by
    /// enemy id). @p brain must be non-null.
    EnemyController(std::unique_ptr<IEnemyBrain> brain, const Params &params,
                    glm::vec2 spawn, int seed);

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

    /// P3.0c: the bullet prefab (row, oriented shape, spin) this species fires; stamped onto every
    /// intent. Default (no prefab) keeps the legacy 4 px circle and the generic view.
    void SetBulletLaunch(const BulletLaunch &launch) { m_Launch = launch; }

    /// Set the aim target (the player position), updated each tick by the owner.
    void SetTarget(glm::vec2 target) { m_Target = target; }

    /// Seed knockback (FAITHFUL: RGEController__GetForce @473583). @p power is in Unity
    /// units/s (capped at kForceCap); ComputeVelocity converts it to px/s.
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
    /// A (presentation): true iff this controller emitted a shot since the last call;
    /// reading it clears the latch. The Simulation consumes it once per step to emit an
    /// "attack" AnimTrigger SimEvent for this enemy's view. Does not affect the RNG/sim.
    bool ConsumeFiredThisStep() {
        const bool fired = m_FiredThisStep;
        m_FiredThisStep = false;
        return fired;
    }
    /// Remaining knockback magnitude, Unity units/s (NOT px/s).
    float InertialVel() const { return m_InertialVel; }
    glm::vec2 MoveDir() const { return m_MoveDir; }
    void SetMoveDir(glm::vec2 dir) { m_MoveDir = dir; }
    /// The dispatched brain (via IEnemyBrain). Tests use Brain().RngRange(...) to
    /// probe the brain's RNG; the sim never type-queries the concrete brain.
    IEnemyBrain &Brain() { return *m_Brain; }

private:
    void OnScoutTick();
    void OnShootTick();

    Params m_Params;
    std::unique_ptr<IEnemyBrain> m_Brain; ///< (d) dispatched brain; never null.
    EntityState m_State;
    glm::vec2 m_MoveDir{0.0F, 0.0F};
    glm::vec2 m_ForceDir{0.0F, 0.0F};
    BulletLaunch m_Launch{};      ///< P3.0c: see SetBulletLaunch.
    bool m_FiredThisStep = false; ///< A: latched in OnShootTick, drained by ConsumeFiredThisStep.
    float m_InertialVel = 0.0F;
    glm::vec2 m_Target{0.0F, 0.0F};

    Scheduler *m_Scheduler = nullptr;
    std::vector<FireIntent> *m_FireOut = nullptr;
    Scheduler::Handle m_ScoutHandle = 0; ///< the repeating scout cadence, cancelled on Kill.
};

} // namespace Game::Sim

#endif /* GAME_SIM_ENEMYCONTROLLER_HPP */
