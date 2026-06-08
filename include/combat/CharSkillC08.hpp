#ifndef GAME_CHAR_SKILL_C08_HPP
#define GAME_CHAR_SKILL_C08_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class CharSkillC08
 * @brief Faithful, engine-free skill/damage brain for hero C08 (a shield hero:
 *        C08Controller, an RGController player subclass).
 *
 * Per-content port. Models ONLY the pure, unit-testable decisions in the five
 * recovered C08Controller bodies; the actual animator/SFX/transform/singleton
 * (RGGameProcess) wiring is left to the owning entity (referenced in comments
 * only).
 *
 * WHAT THE DECOMP SHOWS (all five bodies grounded; no reconstruction of unseen
 * ability effect):
 *
 *  - _ctor (C08Controller___ctor @ game_full.c:156911, pure HEAD recoverable):
 *      *(this+0x90) = 100;                            // shield_value = 100
 *      RGController___ctor(this, 0);                  // base ctor (owner)
 *      The ONE pure-logic line is the shield_value initializer (offset 0x90 is
 *      the FIRST C08-specific field, == C08Controller.shield_value in the IL2CPP
 *      dump). We model that as the brain's starting shield. The base-ctor call is
 *      owner boilerplate. NO RGRandom draw.
 *
 *  - Awake (C08Controller__Awake @ game_full.c:156930):
 *      RGController__AwakeController(this, 0); get_transform(this) tail-call.
 *      Pure owner wiring (component resolve + transform). OWNER concern, no brain
 *      logic.
 *
 *  - Update (C08Controller__Update @ game_full.c:156955, fully recovered, pure):
 *      if (awake) {                                   // this+0xc
 *        AttributeUpdate();                           // -> SkillReload(): cooldown
 *                                                     //    count-up, run EVERY awake
 *                                                     //    frame, UNCONDITIONALLY.
 *        SeachUpdate();                               // aim re-acquire (owner concern)
 *      }
 *      LIKE C02 (and UNLIKE C01): C08's Update has NO in_skill_time countdown and
 *      NO auto-end branch -- it is JUST the awake-gated AttributeUpdate+SeachUpdate.
 *      The C08 skill ends only through the explicit RoleSkillEnd path (base slot
 *      0x17c), never on a self-expiring timer. Modeling an auto-end here would be
 *      fabrication, so Tick() advances ONLY the cooldown and never auto-ends.
 *      NO RGRandom draw.
 *
 *  - GetHurt (C08Controller__GetHurt @ game_full.c:156968, fully recovered, pure):
 *      if (in_skill == 0) {                           // this+0x55 == 0
 *        RGController__GetHurt(damage, source);       // NORMAL damage pipeline (base)
 *        return;
 *      }
 *      if (awake == 0) return;                        // this+0xc (skill-active guard)
 *      *(this+0x90) -= damage;                        // shield_value ABSORBS the hit
 *      <RGGameProcess singleton effect>               // owner/process tail-call
 *      This is C08's gimmick: WHILE the ultimate (shield) is up, GetHurt bypasses
 *      the normal HP/armor damage pipeline and subtracts the incoming damage
 *      straight from shield_value (the raised shield eats the hit). When NOT in
 *      skill, it defers to the base RGController.GetHurt (owner). The recoverable
 *      brain logic is the in_skill branch + the shield_value subtraction; the base
 *      GetHurt body and the RGGameProcess singleton tail-call are owner concerns.
 *      NO RGRandom draw.
 *
 *  - RoleSkill (C08Controller__RoleSkill @ game_full.c:157131):
 *      The hero ultimate gate, identical in shape to the base RGController one:
 *        if (awake)                                   // this+0xc
 *          if (role_attribute.skill_ready)            // this+0x44 -> get_skill_ready==1
 *            if (!in_skill)                            // this+0x55 == 0
 *              <enter skill>                           // get_transform tail-call truncated
 *      The post-gate effect is a get_transform tail-call (the hero-specific shield
 *      raise / spawn), so only the GATE is recoverable. NO RGRandom draw. The
 *      cooldown engine (skill_cd / this_skill_time count-up + spend) is the
 *      established PlayerDash / CharSkillC01 pattern; we REUSE that timer model so
 *      TryActivateSkill can be unit-tested end to end (see fabrication_flags).
 *
 * RoleSkillEnd: C08Controller overrides RoleSkillEnd (per the IL2CPP dump), but
 * its recovered body is an inlined / vtable tail (FUN_002675d4 / FUN_00267614 /
 * FUN_00267644 around game_full.c:156994-157130) accessing role_attribute
 * (this+0x44), skill_strengthen, and the base slot 0x17c -- it carries no
 * recoverable pure scalar besides leaving the skill state. We model EndSkill() as
 * the established RGController.RoleSkillEnd -> ReSetSkillReload chain (leave
 * in_skill, restart the cooldown), flagged as the reused pattern below. The
 * skill_strengthen-gated effects there are owner concerns.
 *
 * Determinism: none of the five recovered bodies draws from rg_random, so this
 * unit makes NO RNG draws (the RGRandom member is carried for interface parity
 * with the project template and to keep the deterministic stream untouched;
 * Seeded() lets a caller confirm the seed without ever advancing it).
 *
 * FABRICATION FLAGS (every reused-pattern / inherited-effect assumption):
 *   - CharSkillC08-CD01: the skill_cd / this_skill_time count-up cooldown engine is
 *     NOT in any recovered C08 body (RoleSkill only reads role_attribute.skill_ready
 *     via get_skill_ready; Update only calls AttributeUpdate). It is the established
 *     PlayerDash / CharSkillC01 timer model, reused so TryActivateSkill/SkillReady
 *     are unit-testable. Same allowed reconstruction as CharSkillC01/C02.
 *   - CharSkillC08-CD02: EndSkill() models the base RGController.RoleSkillEnd ->
 *     RoleAttributePlayer.ReSetSkillReload chain (clear in_skill + restart cooldown).
 *     The recovered C08 RoleSkillEnd tail is inlined/truncated and does not contain
 *     the spend in a recoverable scalar form; this is an inherited base-class effect
 *     exposed as a flagged assumption, never silently inlined.
 *   - CharSkillC08-CD03: starts READY (this_skill_time == skill_cd), matching a
 *     freshly SetUpChar hero (PlayerDash / C01 / C02). Not separately observed in a
 *     C08 body; carried over from the sibling convention.
 *   - CharSkillC08-CD04: kInitialShieldValue (100) is the _ctor's *(this+0x90)=100;
 *     it is faithfully the immediate operand, but the shield-regen / max-shield
 *     bookkeeping (if any) lives in C08Shield / RoleAttribute and is NOT recovered
 *     here -- shield_value is only ever SET to 100 (ctor) and DECREMENTED by GetHurt
 *     in the modeled bodies; no clamp-to-zero or regen is invented.
 *
 * @see recreation Player/RGController.cs (skill timing + offsets: 0xc awake, 0x44
 *      role_attribute, 0x55 in_skill), PlayerDash.hpp (canonical skill-cooldown
 *      pattern), CharSkillC01/CharSkillC02 (sibling hero brains), IL2CPP
 *      C08Controller.cs (shield_value field name).
 */
class CharSkillC08 {
public:
    /// Initial shield_value (this+0x90) set by the ctor.
    /// FAITHFUL: immediate operand 100 in C08Controller___ctor @ 156911
    /// (*(this+0x90) = 100). shield_value is the first C08-specific field.
    static constexpr int kInitialShieldValue = 100;

    /**
     * @brief Construct the skill brain from the hero's skill timing stats.
     * @param skillCd cooldown length in seconds (RoleAttributePlayer skill_cd).
     *                Clamped to >= 0. From the CharacterDef stat sheet.
     *
     * Starts READY (this_skill_time == skill_cd), NOT in skill, and with
     * shield_value == kInitialShieldValue (100) -- a freshly set-up C08 hero whose
     * first ultimate is available (see fabrication_flags CharSkillC08-CD03/CD04).
     */
    explicit CharSkillC08(float skillCd);

    /// Seed the deterministic stream (kept untouched; no draw is ever made).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief Attempt to activate the hero ultimate (raise the shield) this frame.
     *
     * FAITHFUL: C08Controller__RoleSkill @ 157131 gate -- requires
     * awake && skill_ready && !in_skill. On success the hero enters the skill state
     * (in_skill = true). The awake check (this+0xc) is the caller's responsibility;
     * the gate this unit owns is skill_ready && !in_skill (the recovered nested-if
     * at 157146/157151/157153). The hero-specific effect (get_transform tail-call at
     * 157156) and the actual cooldown spend are owner/EndSkill concerns.
     *
     * @return true if the skill activated this call (state entered), else false.
     */
    bool TryActivateSkill();

    /**
     * @brief End the active skill window (lower the shield) and spend the charge.
     *
     * Models the base RGController.RoleSkillEnd -> ReSetSkillReload chain (the C08
     * RoleSkillEnd body is an inlined/truncated tail; see fabrication_flags
     * CharSkillC08-CD02): leaves the skill state (in_skill = false) and restarts the
     * cooldown (this_skill_time = 0). No-op if not currently in the skill.
     */
    void EndSkill();

    /**
     * @brief Apply incoming damage (C08Controller__GetHurt @ game_full.c:156968).
     *
     * Models the pure in_skill branch + shield_value subtraction, returning what the
     * owning entity must do.
     *
     * Branch order preserved from the decomp:
     *   - if (!in_skill): defer to the base RGController.GetHurt pipeline (owner).
     *     The brain reports deferToBase = true and does NOT touch shield_value.
     *   - else (in_skill): the awake guard applies (this+0xc); when awake the shield
     *     ABSORBS the hit (shield_value -= damage) and an RGGameProcess singleton
     *     effect fires (owner). Reports absorbedByShield = true.
     *
     * @param damage incoming damage amount (param_2 in the decomp).
     * @param awake  the hero's awake flag (this+0xc); when in_skill && !awake the
     *               decomp early-returns with no effect (matches the decomp guard).
     */
    struct HurtDecision {
        /// !in_skill -> defer to base RGController.GetHurt (normal HP/armor pipeline).
        bool deferToBase = false;
        /// in_skill && awake -> the shield absorbed the hit (shield_value -= damage).
        bool absorbedByShield = false;
        /// in_skill && awake -> the owner must fire the RGGameProcess singleton effect.
        bool triggerProcessEffect = false;
    };

    /**
     * FAITHFUL: C08Controller__GetHurt @ 156968. Subtracts from shield_value (this
     * brain's mirror of *(this+0x90)) ONLY on the in_skill && awake path, exactly
     * as the decomp does (156984: *(this+0x90) -= param_2). NO RGRandom draw.
     * @return the decision the owning entity should act on.
     */
    HurtDecision GetHurt(int damage, bool awake);

    /**
     * @brief One awake-frame tick: advance the cooldown only.
     *
     * FAITHFUL: C08Controller__Update @ 156955 (fully recovered, pure):
     *   if (awake) { AttributeUpdate() -> SkillReload(dt): this_skill_time counts UP,
     *   clamped to skill_cd, run EVERY awake frame UNCONDITIONALLY; SeachUpdate()
     *   (owner aim re-acquire). }
     * There is NO in_skill_time countdown and NO auto-end in C08's Update (unlike
     * C01) -- so Tick advances the cooldown and NEVER ends the skill. NO RGRandom draw.
     * @param dtMs frame delta in milliseconds (<= 0 is ignored).
     */
    void Tick(float dtMs);

    /// @return true while the ultimate (shield) is active (in_skill, this+0x55).
    bool InSkill() const { return m_InSkill; }

    /**
     * @return true when off cooldown and ready to fire.
     * FAITHFUL: RoleAttributePlayer.get_skill_ready -- skill_cd <= this_skill_time.
     */
    bool SkillReady() const { return m_ThisSkillTime >= m_SkillCd; }

    /// @return seconds until the cooldown finishes recharging (0 when ready).
    float CooldownRemaining() const;

    /// Configured cooldown length (skill_cd), seconds.
    float SkillCd() const { return m_SkillCd; }

    /// @return current shield_value (this+0x90); decremented by GetHurt while in_skill.
    /// FABRICATED-PATTERN NOTE: no regen/clamp is modeled (see CharSkillC08-CD04).
    int ShieldValue() const { return m_ShieldValue; }

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};             ///< deterministic stream; never advanced on this path.
    float m_SkillCd = 0.0F;       ///< skill_cd (RoleAttributePlayer), cooldown length.
    float m_ThisSkillTime = 0.0F; ///< this_skill_time; counts up to skill_cd.
    int m_ShieldValue = kInitialShieldValue; ///< shield_value (this+0x90).
    bool m_InSkill = false;       ///< in_skill (this+0x55); ultimate active flag.
};

} // namespace Game

#endif /* GAME_CHAR_SKILL_C08_HPP */
