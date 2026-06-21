#ifndef GAME_SIM_IWEAPONBRAIN_HPP
#define GAME_SIM_IWEAPONBRAIN_HPP

#include <vector>

#include <glm/glm.hpp>

#include "sim/FireIntent.hpp"

namespace Game::Sim {

/**
 * @class IWeaponBrain
 * @brief Uniform controller-facing weapon-brain interface -- the (d) function-
 *        adapter dispatch layer for player guns (B1-P3), the weapon twin of
 *        @ref IEnemyBrain (P1) / @ref IBossBrain (P2). The driver (WeaponController)
 *        pumps a gun ONLY through this; a per-gun adapter translates each concrete
 *        @c GunNNN's heterogeneous public signatures (ScatterAngle(base,recoil) /
 *        ScatterAngle(spread) / Fan helpers / Burst pump / charge-ratio scalars)
 *        into Tick().
 *
 * NB: The gun-brain SOURCE is NEVER modified -- adapters only CALL public brain
 * methods (the P1/P2 invariant). Each single-class gun encodes its OWN FirePattern
 * in its own body, so unlike the boss layer (owner-truncated jumptable -> Fan-approx)
 * a weapon adapter returns the REAL pattern.
 *
 * DRIVER MODEL (B): @ref Tick is called ONCE PER FIXED STEP by the driver (the
 * Simulation already polls WeaponController::Tick every Step). The adapter therefore
 * owns its own multi-tick state -- heat/charge accrual (from @c firing + @c
 * fixedStepSeconds) and burst progress (the wrapped brain's counter) -- and on each
 * tick pushes 0+ SINGLE-TICK FireIntents into @c out. This keeps FireSystem stateless
 * and RNG-free: a Fan emits N pre-rotated Single intents; a Burst emits one Single
 * sub-shot per tick across ticks; only Charge tags its intent FirePattern::Charge so
 * FireSystem scales its speed by the accrued ratio (stateless geometry).
 *
 * DETERMINISM: all scatter RNG is drawn HERE (brain-side) and baked into
 * FireIntent.dir; FireSystem takes ZERO draws. The per-fixed-step cadence is the
 * same clock the Scheduler ticks on, so the polled driver is replay-identical to a
 * Scheduler-driven one.
 */
class IWeaponBrain {
public:
    /// Per-tick inputs the driver supplies: the live trigger/aim plus the
    /// WeaponDef-derived bullet attributes the adapter bakes into each FireIntent.
    struct FireContext {
        bool firing = false;             ///< trigger held this tick.
        bool canFire = true;             ///< fire-rate cooldown elapsed this tick. Single/Fan
                                         ///< guns gate emission on this; Burst/Charge guns are
                                         ///< edge-triggered and ignore it (they pump/accrue every
                                         ///< tick once started). Default true (direct-drive tests).
        glm::vec2 origin{0.0F, 0.0F};    ///< muzzle origin.
        glm::vec2 aim{1.0F, 0.0F};       ///< aim direction (need not be unit; adapters Normalize).
        float fixedStepSeconds = 0.0F;   ///< the sim fixed step (heat/charge accrual unit).
        // --- bullet attributes (WeaponDef-derived) baked into every FireIntent ---
        float baseAngle = 0.0F;          ///< spread base (WeaponDef.deviation).
        float recoil = 0.0F;             ///< owner recoil multiplier (default 0 -> cone == deviation).
        float bulletSpeedPxPerSec = 0.0F;
        float lifeMs = 0.0F;
        int damage = 0;
        int critical = 0;
        float repel = 0.0F;
        bool canThrough = false;
        int pierce = 0;
        int camp = 0;                    ///< 0 = player.
        // --- fan geometry (multi-pellet guns) ---
        int count = 1;                   ///< pellets per pull (WeaponDef.count).
        float stepAngle = 0.0F;          ///< per-pellet fan step in degrees (WeaponDef.angle).
    };

    virtual ~IWeaponBrain() = default;

    /// Seed the gun's deterministic scatter stream (call once at equip).
    virtual void SetSeed(int seed) = 0;

    /// Advance one fixed step. Push 0+ FireIntents into @p out. @return the number
    /// of trigger PULLS resolved this tick (1 when a pull fired, regardless of how
    /// many pellets/intents it produced; 0 when idle/charging) -- the unit the
    /// energy-spend accounting should use, NOT the intent count.
    virtual int Tick(const FireContext &ctx, std::vector<FireIntent> &out) = 0;

    /// Determinism probe (tests): draw once from the gun's own RGRandom stream, to
    /// assert lifecycle calls leave it unadvanced. Every GunNNN exposes Rng().
    virtual int RngRange(int lo, int hi) = 0;

    /// Heat ("spin-up") accumulator for inspection (Gun016); default 0 for guns
    /// with no heat. Mirrors the legacy WeaponController::HeatTime() getter.
    virtual float HeatTime() const { return 0.0F; }
};

/**
 * @class WeaponBrainBase
 * @brief Template adapter base: owns a default-constructible concrete gun @p TBrain
 *        by value and forwards SetSeed / the Rng probe. Tick() is left pure -- each
 *        per-gun adapter overrides it. The brain is never modified. Guns whose ctor
 *        takes args (Gun004 burstCount, Gun005 maxCharge) use a STANDALONE adapter
 *        instead (the BossAI01Adapter precedent), not this base.
 */
template <class TBrain>
class WeaponBrainBase : public IWeaponBrain {
public:
    void SetSeed(int seed) override { m_Brain.SetSeed(seed); }
    int RngRange(int lo, int hi) override { return m_Brain.Rng().Range(lo, hi); }

protected:
    TBrain m_Brain; ///< the wrapped gun brain (source unchanged).
};

} // namespace Game::Sim

#endif /* GAME_SIM_IWEAPONBRAIN_HPP */
