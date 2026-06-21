#ifndef GAME_SIM_ICHARSKILL_HPP
#define GAME_SIM_ICHARSKILL_HPP

#include <utility>

namespace Game::Sim {

/**
 * @class ICharSkill
 * @brief Uniform controller-facing character-skill interface -- the (d) function-
 *        adapter dispatch layer for hero ultimates (B1-P4a), the skill twin of
 *        @ref IEnemyBrain (P1) / @ref IBossBrain (P2) / @ref IWeaponBrain (P3).
 *        Simulation drives the player's skill ONLY through this; a per-hero adapter
 *        translates each concrete @c CharSkillCNN's heterogeneous public signatures
 *        (ctor (skillCd) vs (skillCd,inSkillTime); TryActivateSkill() vs RoleSkill()
 *        toggle vs TryActivateSkill(changing); void/decision EndSkill; RoleAtk
 *        presence/arity) into these calls.
 *
 * NB: the skill-brain SOURCE is NEVER modified -- adapters only CALL public brain
 * methods (the P1/P2/P3 invariant). Each single-class skill encodes its own
 * gate/timing/decision in its own body (SELECTION is recoverable); the actual
 * ultimate EFFECT spawn is owner-truncated for 12/13 heroes, so P4a wires the
 * gate/cooldown faithfully and the EFFECT is faithful only for C01 (the dual-hand
 * mirror, already sim-approximated) and C02 (the forward dash impulse). The other
 * 11 heroes are GATE+COOLDOWN-ONLY stubs: trigger/cooldown/window run, the combat
 * effect is a deliberate no-op until each effect subsystem stage lands (recorded
 * debt -- summons/buff/shield/melee/homing are S1-S5).
 *
 * TRIGGER vs WEAPON FIRE: unlike IWeaponBrain (polled every fixed step, continuous
 * fire), a skill is EDGE-triggered (a button press) and opens an active window the
 * brain itself closes (auto-end timer C01/C10/C13, or explicit/toggle for the rest).
 * So this interface splits @ref TryTrigger (the edge entry, returns a TYPED result
 * absorbing toggle/two-half/dash outcomes) from @ref Tick (per-step cooldown count-up
 * + self-close) and @ref RoleAtk (the in_skill attack-modifier hook, the C01 mirror).
 *
 * DETERMINISM: all 13 skill brains take ZERO RNG draws; effects are additive + gated
 * (default-off when no skill is set), so the existing SimulationTest replay traces
 * stay byte-identical.
 */
class ICharSkill {
public:
    /// The outcome of an edge trigger (absorbs the heterogeneous trigger shapes:
    /// a plain activate, C09's draw/release toggle, C06's deploy/cancel two-half).
    enum class TriggerOutcome { None, Activated, Released, Canceled };

    /// Typed trigger result: the outcome + any instant effect the owner must apply.
    /// For P4a the only instant effect surfaced is C02's forward dash impulse; other
    /// per-hero instant effects are stubbed (their subsystem stage fills them in).
    struct TriggerResult {
        TriggerOutcome outcome = TriggerOutcome::None;
        bool dashImpulse = false; ///< C02: apply a forward dash to the player (owner-side).
    };

    /// The in_skill attack-modifier a skill applies to the normal attack (the C01
    /// dual-hand mirror precedent). Stubs return an all-false (no-op) effect.
    struct AtkEffect {
        bool mirrorSecondHand = false; ///< C01: the second hand mirrors this attack.
        bool secondHandValue = false;  ///< the mirrored press/release value.
        bool triggerItem = false;      ///< standing-on-item press triggers the pickup.
    };

    virtual ~ICharSkill() = default;

    /// Seed the hero's deterministic stream (never advanced; parity only).
    virtual void SetSeed(int seed) = 0;
    /// One fixed step: advance the cooldown count-up + (for windowed heroes) the
    /// active-window auto-end. NEVER auto-ends the explicit/toggle heroes.
    virtual void Tick(float dtMs) = 0;
    /// Edge entry (called on the skill-button press). @return the typed outcome +
    /// any instant effect. Idempotent: a held press cannot re-cast (the brain gates).
    virtual TriggerResult TryTrigger() = 0;
    /// The in_skill attack-modifier hook (C01 mirror). Returns all-false when the
    /// hero has no RoleAtk or the skill is inactive.
    virtual AtkEffect RoleAtk(bool pressDown, bool standingOnItem) = 0;

    virtual bool InSkill() const = 0;
    virtual bool SkillReady() const = 0;
    /// Cooldown charge as 0..1 for the HUD (1.0 = ready, 0.0 = just cast).
    virtual float CooldownProgress() const = 0;

    /// Determinism probe (tests): draw once from the hero's own RGRandom stream.
    virtual int RngRange(int lo, int hi) = 0;
};

/**
 * @class CharSkillBase
 * @brief Template adapter base: owns a concrete hero skill @p TBrain BY VALUE and
 *        forwards the methods every skill shares (SetSeed/Tick/InSkill/SkillReady/
 *        CooldownProgress/Rng probe). TryTrigger + RoleAtk are left pure -- each
 *        per-hero adapter overrides them. No skill is default-constructible (all need
 *        skillCd; C01/C10/C13 also inSkillTime), so the variadic ctor forwards the
 *        timing args to the brain. The brain SOURCE is never modified.
 */
template <class TBrain>
class CharSkillBase : public ICharSkill {
public:
    template <class... Args>
    explicit CharSkillBase(Args &&...args) : m_Brain(std::forward<Args>(args)...) {}

    void SetSeed(int seed) override { m_Brain.SetSeed(seed); }
    void Tick(float dtMs) override { m_Brain.Tick(dtMs); }
    bool InSkill() const override { return m_Brain.InSkill(); }
    bool SkillReady() const override { return m_Brain.SkillReady(); }
    float CooldownProgress() const override {
        const float cd = m_Brain.SkillCd();
        if (cd <= 0.0F) {
            return 1.0F; // no cooldown configured -> always "ready"
        }
        const float p = 1.0F - m_Brain.CooldownRemaining() / cd;
        return p < 0.0F ? 0.0F : (p > 1.0F ? 1.0F : p);
    }
    int RngRange(int lo, int hi) override { return m_Brain.Rng().Range(lo, hi); }

protected:
    TBrain m_Brain; ///< the wrapped hero skill brain (source unchanged).
};

} // namespace Game::Sim

#endif /* GAME_SIM_ICHARSKILL_HPP */
