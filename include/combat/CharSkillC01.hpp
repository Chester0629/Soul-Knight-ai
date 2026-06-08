#ifndef GAME_CHAR_SKILL_C01_HPP
#define GAME_CHAR_SKILL_C01_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class CharSkillC01
 * @brief Faithful, engine-free skill/attack brain for hero C01 (a dual-wield
 *        hero: C01Controller, an RGController player subclass).
 *
 * Per-content port. Models ONLY the pure, unit-testable decisions in the two
 * recovered C01Controller bodies; the actual animator/SFX/transform/hand wiring
 * is left to the owning entity (referenced in comments only).
 *
 * WHAT THE DECOMP SHOWS (both bodies grounded, no reconstruction of unseen logic):
 *
 *  - RoleSkill (C01Controller__RoleSkill @ game_full.c:155986):
 *      The hero ultimate gate, identical in shape to the base RGController one:
 *        if (awake)                                 // this+0xC
 *          if (role_attribute.skill_ready)          // this+0x44 -> get_skill_ready
 *            if (!in_skill)                          // this+0x55
 *              <enter skill>                         // get_transform tail-call truncated
 *      The post-gate effect is a get_transform tail-call (the hero-specific
 *      ability spawn), so only the GATE is recoverable. NO RGRandom draw on this
 *      path. The cooldown engine (skill_cd / this_skill_time count-up + spend)
 *      is the established PlayerDash pattern; we reuse that timer model here so
 *      TryActivateSkill can be unit-tested end to end. ReSetSkillReload (spend
 *      the charge) is NOT in this recovered body -- it lives in the base
 *      RGController.RoleSkill / RoleSkillEnd chain -- so we expose EndSkill() to
 *      spend+leave the state and flag the assumption (see fabrication_flags).
 *
 *  - RoleAtk (C01Controller__RoleAtk @ game_full.c:156016, value = press down):
 *      if (awake) {                                  // this+0xC
 *        if (in_item && value) {                     // standing on a pickup + press
 *          if (in_skill) <stop second hand>;         // virtual *(this+0x17c) (Hand2AtkStop)
 *          TriggerItem(); return;                    // use the pickup
 *        }
 *        // (a press-only, non-melee aim nudge via get_position is truncated here)
 *        hand.SetAttack(value);                      // primary hand fires/stops
 *        if (in_skill) {                             // C01's gimmick: while the skill is
 *          Invoke(value ? "Hand2Atk" : "Hand2AtkStop", 0.1f); // up, the 2nd hand mirrors
 *        }                                           // the attack 0.1s later
 *      }
 *      NO RGRandom draw on this path. The dual-hand mirror (the 0.1s Invoke) only
 *      happens WHILE in_skill -- that gating is the modeled brain. The literal
 *      0.1f is the immediate operand 0x3dcccccd in the binary.
 *
 *  - Update (C01Controller__Update @ game_full.c:155928, fully recovered, pure):
 *      if (awake) {                                  // this+0xC
 *        AttributeUpdate();                          // -> SkillReload(): cooldown
 *                                                    //    advances EVERY awake frame,
 *                                                    //    UNCONDITIONALLY (before the
 *                                                    //    in_skill check below).
 *        SeachUpdate();                              // aim re-acquire (owner concern)
 *        if (in_skill) {                             // this+0x55
 *          in_skill_time -= deltaTime;               // param_1[0x25] COUNTDOWN
 *          if (local player) <refresh UI>;           // owner concern
 *          if (in_skill_time <= 0) { RoleSkillEnd(); return; } // AUTO-END (slot 0x17c)
 *        }
 *      }
 *      So the cooldown count-up does NOT freeze during the skill, and the active
 *      window self-terminates once its in_skill_time timer reaches 0, routing
 *      through the same RoleSkillEnd path EndSkill() models. NO RGRandom draw.
 *
 * Determinism: neither recovered body draws from rg_random, so this unit makes
 * NO RNG draws (the RGRandom member is carried for interface parity with the
 * project template and to keep the deterministic stream untouched; Seeded() lets
 * a caller confirm the seed without ever advancing it).
 *
 * @see recreation Player/RGController.cs (skill timing + offsets), PlayerDash.hpp
 *      (the canonical skill-cooldown pattern), skeleton C01Controller.cs.
 */
class CharSkillC01 {
public:
    /// Delay (seconds) of the second-hand mirror Invoke while the skill is up.
    /// FAITHFUL: immediate operand 0x3dcccccd == 0.1f in RoleAtk's Invoke call.
    static constexpr float kHand2AttackDelay = 0.1F;

    /**
     * @brief Construct the skill brain from the hero's skill timing stats.
     * @param skillCd     cooldown length in seconds (RoleAttributePlayer skill_cd).
     *                    Clamped to >= 0. From the CharacterDef stat sheet.
     * @param inSkillTime active-skill window length in seconds (C01.in_skill_time,
     *                    the skeleton field). Clamped to >= 0. From the stat sheet.
     *
     * Starts READY (this_skill_time == skill_cd) and NOT in skill -- a freshly
     * set-up hero whose first ultimate is available (matches PlayerDash).
     */
    CharSkillC01(float skillCd, float inSkillTime);

    /// Seed the deterministic stream (kept untouched; no draw is ever made).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief Attempt to activate the hero ultimate this frame.
     *
     * FAITHFUL: C01Controller__RoleSkill @ 155986 gate -- requires
     * awake && skill_ready && !in_skill. On success the hero enters the skill
     * state (in_skill = true). The hero-specific effect (get_transform tail-call)
     * and the actual cooldown spend are owner/EndSkill concerns.
     *
     * @return true if the skill activated this call (state entered), else false.
     */
    bool TryActivateSkill();

    /**
     * @brief End the active skill window and spend the charge.
     *
     * Models the base RGController.RoleSkillEnd -> ReSetSkillReload chain (the C01
     * body itself does not contain the spend; see fabrication_flags): leaves the
     * skill state (in_skill = false) and restarts the cooldown (this_skill_time = 0).
     * No-op if not currently in the skill.
     */
    void EndSkill();

    /**
     * @brief Process an attack press/release (C01Controller__RoleAtk @ 156016).
     *
     * Models the pure decisions, returning what the owning entity must do. The
     * actual TriggerItem / hand.SetAttack / Invoke calls are the owner's job; this
     * reports the in_skill-gated dual-hand decision faithfully.
     *
     * @param pressDown true = attack button down (value==1), false = release.
     * @param standingOnItem true if standing on a pickup (RGController.in_item).
     */
    struct AtkDecision {
        /// in_item && pressDown -> the press triggers the pickup (then returns).
        bool triggerItem = false;
        /// Should hand.SetAttack(pressDown) be issued this call? (false on the
        /// item-trigger early-out, true otherwise -- the normal fire/stop path.)
        bool setHandAttack = false;
        /// The value to pass to hand.SetAttack (== pressDown when setHandAttack).
        bool handAttackValue = false;
        /// While in_skill, the second hand mirrors the attack 0.1s later
        /// (Invoke "Hand2Atk"/"Hand2AtkStop"). True only when in_skill.
        bool mirrorSecondHand = false;
        /// The string the mirror Invoke uses: true -> "Hand2Atk" (press),
        /// false -> "Hand2AtkStop" (release). Valid only when mirrorSecondHand.
        bool secondHandAttackValue = false;
    };

    /**
     * FAITHFUL: C01Controller__RoleAtk @ 156016. Branch order preserved:
     *   awake gate -> (in_item && press -> stop-2nd-hand-if-in_skill + TriggerItem)
     *   -> hand.SetAttack(value) -> (in_skill -> Invoke Hand2Atk/Stop @ 0.1s).
     * @return the decision the owning entity should act on (all-false if !awake).
     */
    AtkDecision RoleAtk(bool pressDown, bool standingOnItem) const;

    /**
     * @brief One awake-frame tick: advance the cooldown and the active window.
     *
     * FAITHFUL: C01Controller__Update @ 155928 (fully recovered, pure):
     *   1. AttributeUpdate() -> RoleAttributePlayer.SkillReload(dt): this_skill_time
     *      counts UP, clamped to skill_cd. Called EVERY awake frame, UNCONDITIONALLY
     *      -- it is NOT frozen while in_skill (the in_skill check sits AFTER it).
     *   2. While in_skill, the in_skill_time timer (param_1[0x25]) counts DOWN by
     *      dt; when it reaches <= 0 the skill AUTO-ENDS via the RoleSkillEnd path
     *      (slot 0x17c) -- modeled by calling EndSkill() (in_skill = false,
     *      this_skill_time reset to 0 per RoleSkillEnd -> ReSetSkillReload).
     *
     * Order matters and matches the decomp: the cooldown advances first, then the
     * in_skill_time countdown / auto-end runs (and the auto-end's reset wins for
     * that frame, exactly as the original returns straight after RoleSkillEnd).
     * @param dtMs frame delta in milliseconds (<= 0 is ignored).
     */
    void Tick(float dtMs);

    /// @return true while the ultimate is active (in_skill, this+0x55).
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
    /// Configured active-window length (in_skill_time), seconds.
    float InSkillTime() const { return m_InSkillTime; }

    /// @return seconds left in the active window (param_1[0x25] countdown), 0 when
    /// not in skill. FAITHFUL: C01Controller__Update's in_skill_time decrement.
    float InSkillTimeRemaining() const { return m_InSkill ? m_InSkillTimeLeft : 0.0F; }

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};         ///< deterministic stream; never advanced on this path.
    float m_SkillCd = 0.0F;       ///< skill_cd (RoleAttributePlayer), cooldown length.
    float m_InSkillTime = 0.0F;   ///< in_skill_time (C01 skeleton), configured window.
    float m_ThisSkillTime = 0.0F; ///< this_skill_time; counts up to skill_cd.
    float m_InSkillTimeLeft = 0.0F; ///< live in_skill_time countdown (param_1[0x25]).
    bool m_InSkill = false;       ///< in_skill (this+0x55); ultimate active flag.
};

} // namespace Game

#endif /* GAME_CHAR_SKILL_C01_HPP */
