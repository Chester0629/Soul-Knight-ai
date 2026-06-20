#ifndef GAME_SIM_IENEMYBRAIN_HPP
#define GAME_SIM_IENEMYBRAIN_HPP

#include <glm/glm.hpp>

namespace Game::Sim {

/**
 * @class IEnemyBrain
 * @brief Uniform controller-facing enemy-brain interface -- the (d) function-
 *        adapter dispatch layer (B1-P1). EnemyController drives an enemy ONLY
 *        through this; a per-brain @ref EnemyBrainBase adapter translates each
 *        concrete @c EnemyAIxx's heterogeneous signatures (Scout returns
 *        int/bool/void, RunReflection vec2/bool(out)/absent, ShootReflection has
 *        7 distinct shapes) into these calls.
 *
 * NB: The brain SOURCE is NEVER modified -- adapters only CALL public brain methods.
 * The AI01 adapter forwards verbatim, so the EnemyAI01 path stays byte-identical
 * (combat/golden hashes unchanged). This is the template P2/P3/P4 dispatch reuses.
 */
class IEnemyBrain {
public:
    /// One shoot-tick outcome (translated from the brain's ShootReflection).
    struct ShootResult {
        bool fired = false;  ///< did the brain decide to fire this tick?
        float nextCd = 0.0F; ///< the cd to reschedule the next shoot-tick on.
    };

    virtual ~IEnemyBrain() = default;

    // --- State setters every brain exposes -----------------------------------
    virtual void SetSeed(int seed) = 0;
    virtual void SetDead(bool dead) = 0;
    /// Only EnemyAI01 has SetKinematic (the sole kinematic:1 def); default no-op
    /// so non-kinematic brains need no override.
    virtual void SetKinematic(bool /*v*/) {}

    // --- The tick the controller drives --------------------------------------
    /// Scout decision (the brain's Scout(); return value, if any, is discarded).
    virtual void Scout() = 0;
    /// Wander direction (RunReflection); a turret (no RunReflection) returns (0,0).
    virtual glm::vec2 RunReflection() = 0;
    /// Shoot decision for this tick, given the controller's base cd. The adapter
    /// translates the brain's specific ShootReflection signature into the uniform
    /// {fired, nextCd}.
    virtual ShootResult ShootTick(float baseCd) = 0;

    /// Determinism probe (tests): draw once from the brain's own RGRandom stream,
    /// to assert lifecycle calls leave it unadvanced. Every EnemyAIxx exposes Rng().
    virtual int RngRange(int lo, int hi) = 0;
};

/**
 * @class EnemyBrainBase
 * @brief CRTP-free template adapter base: owns a concrete brain @p TBrain and
 *        forwards the methods every brain shares (SetSeed / SetDead / Rng probe).
 *        The heterogeneous tick methods (Scout / RunReflection / ShootTick) are
 *        left pure here -- each per-brain adapter overrides them with that brain's
 *        exact translation. The brain object is held by value (no allocation
 *        beyond this adapter) and is never modified.
 */
template <class TBrain>
class EnemyBrainBase : public IEnemyBrain {
public:
    void SetSeed(int seed) override { m_Brain.SetSeed(seed); }
    void SetDead(bool dead) override { m_Brain.SetDead(dead); }
    int RngRange(int lo, int hi) override { return m_Brain.Rng().Range(lo, hi); }

protected:
    TBrain m_Brain; ///< the wrapped brain (source unchanged).
};

} // namespace Game::Sim

#endif /* GAME_SIM_IENEMYBRAIN_HPP */
