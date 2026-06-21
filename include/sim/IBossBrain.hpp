#ifndef GAME_SIM_IBOSSBRAIN_HPP
#define GAME_SIM_IBOSSBRAIN_HPP

#include <glm/glm.hpp>

#include "sim/FireIntent.hpp"

namespace Game::Sim {

/**
 * @class IBossBrain
 * @brief Uniform controller-facing boss-brain interface -- the (d) function-
 *        adapter dispatch layer (B1-P2), the boss twin of @ref IEnemyBrain.
 *        BossController drives a boss ONLY through this; a per-brain adapter
 *        translates each concrete @c BossAIxx's heterogeneous signatures
 *        (ChooseAttack int/gated, ShootReflection 7 distinct shapes,
 *        RunReflection/WanderDirection/ChaseMoveDecision/none, OnHurt 6 distinct
 *        argument shapes) into these calls.
 *
 * NB: The brain SOURCE is NEVER modified -- adapters only CALL public brain
 * methods. The BossAI01 adapter forwards verbatim, so the BossAI01 path stays
 * byte-identical (combat/golden hashes unchanged).
 *
 * THE PATTERN SEAM (B1-P2 phase A): @ref AttackResult carries a @ref FirePattern
 * ENUM, NOT a fired-bool. The adapter -- not the controller -- decides the
 * pattern, so when the dedicated FireSystem-patterns stage implements
 * Burst/Charge/Parabola, only the adapters change (return the real enum); the
 * controller<->FireSystem wiring is untouched. TODAY every boss returns Fan: the
 * roll->attack jumptable is owner-truncated for every boss (see each BossAIxx
 * header), so Fan is the faithful approximation and FireSystem renders it. This
 * is the "send the real pattern enum, FireSystem Fan-approximates the rest, fill
 * in the enum impl later without re-wiring" structure.
 */
class IBossBrain {
public:
    /// One shoot-tick outcome (translated from the boss's attack-selection method).
    struct AttackResult {
        bool fired = false;                     ///< did the brain decide to fire this tick?
        FirePattern pattern = FirePattern::Fan; ///< the REAL pattern (Fan-approx today).
        int count = 1;                          ///< number of bullets (Fan).
        float spreadDeg = 0.0F;                 ///< total fan spread (Fan).
        float nextCd = 0.0F;                    ///< cd to reschedule the next shoot-tick on.
    };

    virtual ~IBossBrain() = default;

    // --- State the controller drives -----------------------------------------
    /// Seed the boss's deterministic stream (call once at spawn).
    virtual void SetSeed(int seed) = 0;
    /// Apply post-hit HP; the adapter routes it to the boss's GetHurt/BossAngry
    /// (passing the awake/dead gate inputs each brain expects as true/false).
    virtual void OnHurt(int hpAfter, int maxHp) = 0;
    /// Angry phase latched? (every boss exposes Angry()).
    virtual bool Angry() const = 0;
    /// Current shoot cadence in seconds (angry-aware where the brain models it;
    /// the controller's base cd for brains without a shoot_cd field).
    virtual float ShootCd() const = 0;

    // --- The tick the controller drives --------------------------------------
    /// Move decision: the boss's with-target chase/strafe/retreat (BossAI01) or
    /// its no-target wander (RunReflection/WanderDirection); a stationary boss
    /// (BossAI04/07) returns (0,0). @p chase / @p dist are ignored by wanderers.
    virtual glm::vec2 MoveDecision(glm::vec2 chase, float dist) = 0;
    /// Attack decision for this tick, given the controller's base cd. The adapter
    /// translates the brain's specific attack-selection signature into the uniform
    /// {fired, pattern, count, spreadDeg, nextCd}.
    virtual AttackResult AttackTick(float baseCd) = 0;

    /// Determinism probe (tests): draw once from the brain's own RGRandom stream,
    /// to assert lifecycle calls leave it unadvanced. Every BossAIxx exposes Rng().
    virtual int RngRange(int lo, int hi) = 0;
};

/**
 * @class BossBrainBase
 * @brief Template adapter base: owns a default-constructible concrete brain
 *        @p TBrain and forwards SetSeed / the Rng probe. The heterogeneous tick
 *        methods are left pure -- each per-brain adapter overrides them. The brain
 *        is held by value and is never modified. (BossAI01 has NO default ctor --
 *        it needs baseShootCd -- so its adapter is standalone, not via this base.)
 */
template <class TBrain>
class BossBrainBase : public IBossBrain {
public:
    void SetSeed(int seed) override { m_Brain.SetSeed(seed); }
    int RngRange(int lo, int hi) override { return m_Brain.Rng().Range(lo, hi); }

protected:
    TBrain m_Brain; ///< the wrapped brain (source unchanged).
};

} // namespace Game::Sim

#endif /* GAME_SIM_IBOSSBRAIN_HPP */
