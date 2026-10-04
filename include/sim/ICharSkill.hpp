#ifndef GAME_SIM_ICHARSKILL_HPP
#define GAME_SIM_ICHARSKILL_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

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

    /// P3.3.b: the result of @ref OnPlayerHurt -- what the caller should do with an
    /// incoming hit BEFORE the normal armor/hp pipeline runs. Default (no override)
    /// is an exact pass-through (absorbed=false, damageToApply==the raw damage), so
    /// every hero without a hurt hook stays byte-identical to today.
    struct HurtOutcome {
        bool absorbed = false;  ///< true -> the skill fully handled the hit itself
                                 ///< (e.g. C08's shield); skip the normal HP/armor
                                 ///< pipeline entirely (no damage reaches the player).
        int damageToApply = 0;  ///< the damage the normal pipeline should apply when
                                 ///< !absorbed. Defaults to the incoming damage.
    };

    /// P3.3.b: C04's melee sword-cut decision. RoleAtk (above) carries no atk /
    /// skill_strengthen, which the cut damage formula needs, so this parallel hook
    /// exists for the one hero that needs them; every other hero's default leaves
    /// suppressWeaponFire=false / doCut=false (the normal weapon-fire path runs
    /// completely unaffected).
    struct MeleeAtkEffect {
        bool doCut = false;            ///< true -> the owner must deal cutDamage as a
                                        ///< melee hit this step (C04's AtkCut).
        int cutDamage = 0;             ///< valid only when doCut.
        bool suppressWeaponFire = false; ///< true -> the normal ranged weapon must NOT
                                          ///< fire this step (the cut REPLACES the shot,
                                          ///< faithful to C04Controller__RoleAtk not
                                          ///< reaching hand.SetAttack on that branch).
    };

    /// P3.3.c: one live, awake hostile the sim exposes to a skill's world effect
    /// (enemy view id == index into the sim's enemy list; the boss uses
    /// Simulation::kBossViewId). Positions are sim pixels.
    struct HostileView {
        std::uint32_t id = 0;
        glm::vec2 pos{0.0F, 0.0F};
    };

    /// P3.3.c: the read-only world snapshot handed to @ref TickEffects each fixed
    /// step (only built when @ref NeedsWorld is true, so every other hero is
    /// untouched). All positions are sim pixels; @c kWorldUnitPx converts to the
    /// Unity world units the decompiled radii/speeds are expressed in.
    struct SkillWorldView {
        float dtSec = 0.0F;               ///< the fixed step, seconds.
        glm::vec2 playerPos{0.0F, 0.0F};
        glm::vec2 aimDir{1.0F, 0.0F};
        int playerHp = 0;
        int playerMaxHp = 0;
        std::vector<HostileView> hostiles; ///< live + awake enemies, then the boss.
        /// Wall line-of-sight query (Physics2D.Linecast on layer "Wall", approximated
        /// by sampling WorldCollision::Blocks -- see Simulation::SkillLineBlocked).
        /// Null -> nothing blocks.
        std::function<bool(glm::vec2, glm::vec2)> lineBlocked;
    };

    /// P3.3.c: what a skill's world effect asks the sim to do this step.
    struct SkillEffectOut {
        struct Hit {
            std::uint32_t targetId = 0; ///< HostileView::id.
            int damage = 0;             ///< straight damage (RGEController.GetHurt).
        };
        std::vector<Hit> hits;           ///< skill damage on hostiles (DeathSource::PlayerSkill).
        int healHp = 0;                  ///< player RoleAttribute.RestoreHealth.
        int restoreEnergy = 0;           ///< player RoleAttributePlayer.RestoreEnergy.
        int meleeCutDamage = 0;          ///< >0 -> RGHand.AtkCut at the player (T-36 reach).
        std::vector<std::string> cues;   ///< "skill_effect:<cue>" sim events (log / NOT PORTED).
    };

    virtual ~ICharSkill() = default;

    /// P3.3.c hook: true when this skill needs the per-step world snapshot. Default
    /// false -> the Simulation never builds a view for the other heroes.
    virtual bool NeedsWorld() const { return false; }

    /// P3.3.c hook: called once per fixed step AFTER the press edge + Tick, with the
    /// world snapshot, when NeedsWorld(). Skills with a spawned effect (thunder chain,
    /// bat swarm, holy light, melee cut) advance it here and return what the sim must
    /// apply. Default no-op.
    virtual void TickEffects(const SkillWorldView &world, SkillEffectOut &out) {
        (void)world;
        (void)out;
    }

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

    /// P3.3.b hook: called once per player-bullet fire step in place of / alongside
    /// RoleAtk for heroes whose ultimate replaces the normal shot with a melee cut
    /// (C04). Default no-op (all-false) -- every other hero's normal weapon fire is
    /// completely unaffected.
    virtual MeleeAtkEffect RoleAtkMelee(bool pressDown, bool standingOnItem, int atk,
                                        bool skillStrengthen) {
        (void)pressDown;
        (void)standingOnItem;
        (void)atk;
        (void)skillStrengthen;
        return {};
    }

    /// P3.3.b hook: called BEFORE the normal player damage pipeline for every
    /// incoming hit (bullet or DoT). Default is an exact pass-through (C08 is the
    /// only hero that intercepts here, absorbing the hit into its shield).
    virtual HurtOutcome OnPlayerHurt(int damage, bool awake) {
        (void)awake;
        return HurtOutcome{false, damage};
    }

    /// P3.3.b hook: called once when an enemy/boss the PLAYER killed dies (Cull
    /// attributes the kill via EntityState::deathSource). Default no-op; C04
    /// uses this to drive its in-window kill combo.
    virtual void OnEnemyKilled() {}

    /// P3.3.b hook: called once per player-bullet-lands-on-a-target hit (enemy or
    /// boss). @return armor to restore this hit (0 = none). Default no-op; C10/C13
    /// use this for their every-3rd-qualifying-hit armor-regen passive.
    virtual int OnPlayerBulletHit() { return 0; }

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

    /// Test/debug introspection ONLY: direct read access to the wrapped hero brain,
    /// bypassing the uniform ICharSkill abstraction. Some per-hero state (C04's
    /// Combo()/InSkillEffect(), for instance) has no ICharSkill-level equivalent --
    /// by design, so the uniform interface stays uniform -- but sim-level tests
    /// still need to observe it (e.g. proving Simulation::Cull's kill-attribution
    /// gate did or did not call OnEnemyKilled). Production code (Simulation) only
    /// ever holds an ICharSkill&/ICharSkill*, never this. Callers reach it via
    /// dynamic_cast<const ConcreteAdapter*>(sim.PlayerSkill())->Brain().
    const TBrain &Brain() const { return m_Brain; }

protected:
    TBrain m_Brain; ///< the wrapped hero skill brain (source unchanged).
};

} // namespace Game::Sim

#endif /* GAME_SIM_ICHARSKILL_HPP */
