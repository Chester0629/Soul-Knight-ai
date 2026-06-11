#ifndef GAME_CHAR_SKILL_C10_HPP
#define GAME_CHAR_SKILL_C10_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class CharSkillC10
 * @brief Faithful, engine-free skill/attack brain for hero C10 (C10Controller,
 *        an RGController player subclass -- a transforming "beast/paw" hero).
 *
 * Per-content port. Models ONLY the pure, unit-testable decisions in the
 * recovered C10Controller bodies; the actual animator/SFX/transform/hand/weapon
 * wiring is left to the owning entity (referenced in comments only).
 *
 * WHAT THE DECOMP SHOWS (all grounded; no reconstruction of unseen effects):
 *
 *  - Update (C10Controller__Update @ game_full.c:157842, fully recovered, pure):
 *      if (awake) {                                  // (char)param_1[3], this+0xC
 *        AttributeUpdate();                          // cooldown count-up (SkillReload),
 *                                                    //   run EVERY awake frame, BEFORE the
 *                                                    //   in_skill check below.
 *        SeachUpdate();                              // aim re-acquire (owner concern)
 *        if (in_skill) {                             // this+0x55
 *          in_skill_time -= deltaTime;               // param_1[0x26] COUNTDOWN
 *          if (IsLocalPlayer) <refresh UI>;          // owner concern (UICanvas)
 *          if (in_skill_time <= 0) { (*slot 0x17c)(); return; } // AUTO-END (RoleSkillEnd)
 *        }
 *      }
 *      NOTE: C10 stores its in_skill_time at param_1[0x26] (one int later than
 *      C01's param_1[0x25]) -- C10 has an extra `changing` bool before it, so the
 *      field layout shifts. Decoded from THIS body's own self-consistent
 *      read-modify-write of param_1[0x26]. NO RGRandom draw.
 *
 *  - RoleSkill (C10Controller__RoleSkill @ game_full.c:157874, gate recovered):
 *      if (awake)                                    // this+0xC
 *        if (role_attribute.skill_ready)             // this+0x44 -> get_skill_ready
 *          if (!in_skill && !changing)               // this+0x55 == 0  &&  this+0xA4 == 0
 *            <enter skill>                            // get_transform tail-call truncated
 *      C10 adds a SECOND gate the base/C01 do NOT have: `!changing` (this+0xA4,
 *      the Transfiguration animation flag) -- you cannot re-cast mid-transform.
 *      The post-gate effect (the transform spawn) is a get_transform tail-call,
 *      so only the GATE is recoverable. NO RGRandom draw. Cooldown engine
 *      (this_skill_time count-up + spend) is the established PlayerDash/C01
 *      pattern, reused so TryActivateSkill is unit-testable; ReSetSkillReload
 *      (spend the charge) lives in base RGController.RoleSkillEnd, NOT in this
 *      body -- exposed via EndSkill() and flagged (see fabrication_flags).
 *
 *  - RoleAtk (C10Controller__RoleAtk @ game_full.c:158107, value = press down):
 *      if (awake) {                                  // (char)param_1[3]
 *        if (in_skill)                               // this+0x55  -- C10-SPECIFIC, FIRST
 *          anim.SetBool("...6685", value);           // param_1[5] animator; the paw/skill
 *                                                    //   attack bool mirrors the press.
 *        if (in_item && value) { TriggerItem(); return; } // pickup use
 *        if (value && (*slot 0xe4)() == 1)           // press && has_target/NeedLock head
 *          if (!hand.IsMelee()) <aim nudge>;         // get_position tail-call truncated
 *        hand.SetAttack(value);                      // primary hand fires/stops
 *      }
 *      C10's gimmick differs from C01: the in_skill animator-bool is the FIRST
 *      thing checked (BEFORE the item/aim branches), and unlike C01 it does NOT
 *      early-return -- the press still flows to TriggerItem / hand.SetAttack.
 *      NO RGRandom draw. The actual SetBool/get_position/SetAttack are owner work;
 *      the brain reports the in_skill-gated animator decision + the trigger/hand
 *      routing faithfully.
 *
 *  - HurtSomeOne (C10Controller__HurtSomeOne @ game_full.c:158177, fully
 *    recovered, pure -- a real countable brain):
 *      if (in_skill && skill_atk) {                  // this+0x55  &&  this+0xAC
 *        skill_atk = false;                          // consume the flag
 *        if (++hurt_count > 2) {                     // this+0xA8: every 3rd qualifying hit
 *          role_attribute.RestoreArmor(1);           // this+0x44 -> +1 armor
 *          hurt_count = 0;                           // reset the counter
 *        }
 *      }
 *      RGController.HurtSomeOne();                    // then the base reaction
 *      So while the skill is up AND a skill_atk hit landed, every THIRD such hit
 *      restores 1 armor. `skill_atk` (this+0xAC) is armed by the owner-side
 *      sword/weapon-fire fragment FUN_0026a58c (NOT by SkillAtk @ 158017, which
 *      only adds 0.12 to +0x98 and spawns RGWeapon) and consumed here. NO RGRandom
 *      draw.
 *
 *  - SkillAtk (C10Controller__SkillAtk @ 158017): param_1[0x98] += 0.12 then
 *    Instantiate<RGWeapon> + a get_transform tail -- ALL owner-side (the spawn and
 *    its parameter). 158017 itself does NOT touch this+0xAC. (param_1[0x98] starts
 *    5.0f from the ctor and is a spawn parameter, not modeled.)
 *
 *  - skill_atk arming (this+0xAC = 1): set by the orphaned sword/weapon-fire
 *    fragment FUN_0026a58c @ game_full.c:158063 (a decompiler-split tail that also
 *    does RGSword__UpdateAttribute and RGMusicManager__PlayEffect at +0xa0). That
 *    write is the ONE pure side effect feeding this brain -- it arms skill_atk for
 *    the next HurtSomeOne, surfaced via ArmSkillAtk(). TODO[verify which named
 *    method owns this fragment]; the recovered code unambiguously writes 0xAC = 1.
 *
 *  - Awake / Transfiguration / AutoLock / ctor: animator SetBool / get_transform /
 *    Instantiate tail-calls -- pure owner concerns, no brain logic.
 *
 * Determinism: NO recovered body draws from rg_random, so this unit makes ZERO
 * RNG draws (C10 is fully player-driven). The RGRandom member is carried for
 * interface parity with the project template and to keep the deterministic stream
 * untouched; Seeded() lets a caller confirm the seed without ever advancing it.
 *
 * FABRICATION FLAGS (reused-pattern assumptions, called out exactly like C01):
 *  - [CharSkillC10-CD] The skill-cooldown timer model (this_skill_time count-up to
 *    skill_cd + spend) is the PlayerDash/CharSkillC01 reconstruction, reused so
 *    TryActivateSkill/EndSkill are testable. The C10 RoleSkill body shows only the
 *    skill_ready GATE, not the timer arithmetic.
 *  - [CharSkillC10-End] EndSkill() models base RGController.RoleSkillEnd ->
 *    RoleAttributePlayer.ReSetSkillReload (leave in_skill + restart cooldown). That
 *    spend is NOT in C10's own recovered body; it lives in the inherited base.
 *  - [CharSkillC10-Awake] The outer awake gate (this+0xC / param_1[3]) is the
 *    caller's responsibility; this brain has no awake field (an all-quiet decision
 *    represents a non-awake hero), matching CharSkillC01.
 *  - [CharSkillC10-Changing] `changing` (this+0xA4) is set/cleared by the
 *    Transfiguration animator path (owner). This brain carries it as caller-driven
 *    state (SetChanging) so the !changing RoleSkill gate is testable; the transition
 *    timing itself is owner-driven and not modeled.
 *
 * @see recreation Player/RGController.cs (skill timing + offsets), PlayerDash.hpp
 *      (canonical skill-cooldown pattern), skeleton C10Controller.cs (field names:
 *      in_skill_time, changing, hurt_count, skill_atk), CharSkillC01 (shape).
 */
class CharSkillC10 {
public:
    /// Hits required (inclusive) before an armor restore. FAITHFUL: the `2 <
    /// hurt_count` test in HurtSomeOne means the 3rd qualifying hit restores armor.
    static constexpr int kArmorRestoreEveryNthHit = 3;
    /// Armor restored per trigger. FAITHFUL: RestoreArmor(1, ...) immediate operand.
    static constexpr int kArmorRestoreAmount = 1;

    /**
     * @brief Construct the skill brain from the hero's skill timing stats.
     * @param skillCd     cooldown length in seconds (RoleAttributePlayer skill_cd).
     *                    Clamped to >= 0. From the CharacterDef stat sheet.
     * @param inSkillTime active-skill window length in seconds (C10.in_skill_time,
     *                    the skeleton field at param_1[0x26]). Clamped to >= 0.
     *
     * Starts READY (this_skill_time == skill_cd), NOT in skill, NOT changing,
     * skill_atk unarmed, hurt_count 0 -- a freshly set-up hero whose first ultimate
     * is available (matches PlayerDash / CharSkillC01).
     */
    CharSkillC10(float skillCd, float inSkillTime);

    /// Seed the deterministic stream (kept untouched; no draw is ever made).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief Attempt to activate the hero ultimate this frame.
     *
     * FAITHFUL: C10Controller__RoleSkill @ 157874 gate -- requires
     * awake && skill_ready && !in_skill && !changing. On success the hero enters
     * the skill state (in_skill = true) and arms the in_skill_time countdown. The
     * hero-specific transform effect (get_transform tail-call) and the cooldown
     * spend are owner/EndSkill concerns. [CharSkillC10-CD][CharSkillC10-Changing]
     *
     * @return true if the skill activated this call (state entered), else false.
     */
    bool TryActivateSkill();

    /**
     * @brief End the active skill window and spend the charge.
     *
     * Models the base RGController.RoleSkillEnd -> ReSetSkillReload chain (the C10
     * body itself does not contain the spend -- [CharSkillC10-End]): leaves the
     * skill state (in_skill = false) and restarts the cooldown (this_skill_time = 0).
     * No-op if not currently in the skill.
     */
    void EndSkill();

    /**
     * @brief Set/clear the `changing` flag (Transfiguration in progress).
     *
     * FAITHFUL: this+0xA4 is the C10-only RoleSkill gate. The Transfiguration
     * animator path that toggles it is owner-driven; this setter lets a caller
     * model the !changing gate. [CharSkillC10-Changing]
     */
    void SetChanging(bool changing) { m_Changing = changing; }
    /// @return true while a transform is in progress (this+0xA4 blocks RoleSkill).
    bool Changing() const { return m_Changing; }

    /**
     * @brief Decision for a skill/attack press (C10Controller__RoleAtk @ 158107).
     *
     * The actual anim.SetBool / TriggerItem / hand.SetAttack calls are owner work;
     * this reports the pure routing decisions faithfully.
     */
    struct AtkDecision {
        /// in_skill -> anim.SetBool(StringLiteral_6685, value) FIRST. True only
        /// when in_skill (the bool value mirrors the press in setSkillAnimValue).
        bool setSkillAnim = false;
        /// The value to pass to anim.SetBool (== pressDown when setSkillAnim).
        bool skillAnimValue = false;
        /// in_item && pressDown -> TriggerItem(); return (skips the hand path).
        bool triggerItem = false;
        /// Should hand.SetAttack(pressDown) be issued? (false on the item-trigger
        /// early-out, true otherwise -- the normal fire/stop path.)
        bool setHandAttack = false;
        /// The value to pass to hand.SetAttack (== pressDown when setHandAttack).
        bool handAttackValue = false;
    };

    /**
     * FAITHFUL: C10Controller__RoleAtk @ 158107. Branch order preserved:
     *   awake gate -> (in_skill -> anim.SetBool FIRST, NO early-return) ->
     *   (in_item && press -> TriggerItem; return) ->
     *   (press && has_target head -> non-melee aim nudge, truncated -- not modeled)
     *   -> hand.SetAttack(value).
     * Note C10 differs from C01: the in_skill animator bool fires BEFORE the item
     * branch and does NOT short-circuit the rest.
     * @return the decision the owning entity should act on (all-quiet if !awake).
     */
    AtkDecision RoleAtk(bool pressDown, bool standingOnItem) const;

    /**
     * @brief Arm the skill_atk flag (this+0xAC) for the next qualifying hit.
     *
     * FAITHFUL: the orphaned sword/weapon-fire fragment FUN_0026a58c @ 158063 sets
     * this+0xAC = 1 (alongside RGSword__UpdateAttribute and the weapon SFX, which
     * are the owner's job). NOTE: C10Controller__SkillAtk @ 158017 does NOT do this
     * write. TODO[verify which named method owns the 158063 fragment]. HurtSomeOne
     * consumes the flag; modeled as a pure setter so the armor-restore counter is
     * testable end to end.
     */
    void ArmSkillAtk() { m_SkillAtk = true; }
    /// @return true while a skill_atk hit is pending consumption (this+0xAC).
    bool SkillAtkArmed() const { return m_SkillAtk; }

    /**
     * @brief Register that the hero took a hit (C10Controller__HurtSomeOne @ 158177).
     *
     * FAITHFUL: while in_skill && skill_atk, consume skill_atk, increment hurt_count
     * (this+0xA8), and every 3rd such hit (2 < hurt_count) restore 1 armor and reset
     * the counter. The non-qualifying path (not in_skill, or skill_atk not armed)
     * leaves the counter alone. The trailing base RGController.HurtSomeOne reaction
     * is the owner's concern.
     *
     * @return true if this hit triggered the armor restore (caller does
     *         role_attribute.RestoreArmor(1) -- the owner side).
     */
    bool HurtSomeOne();

    /// @return the live hurt_count (this+0xA8), 0..kArmorRestoreEveryNthHit-1.
    int HurtCount() const { return m_HurtCount; }

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

    /// @return seconds left in the active window (param_1[0x26] countdown), 0 when
    /// not in skill. FAITHFUL: C10Controller__Update's in_skill_time decrement.
    float InSkillTimeRemaining() const { return m_InSkill ? m_InSkillTimeLeft : 0.0F; }

    /**
     * @brief One awake-frame tick: advance the cooldown and the active window.
     *
     * FAITHFUL: C10Controller__Update @ 157842 (fully recovered, pure):
     *   1. AttributeUpdate() -> SkillReload(dt): this_skill_time counts UP, clamped
     *      to skill_cd. EVERY awake frame, UNCONDITIONALLY (the in_skill check sits
     *      AFTER it). [CharSkillC10-CD]
     *   2. While in_skill, in_skill_time (param_1[0x26]) counts DOWN by dt; when it
     *      reaches <= 0 the skill AUTO-ENDS via RoleSkillEnd (slot 0x17c) and the
     *      body returns immediately -- modeled by EndSkill() (in_skill = false,
     *      this_skill_time reset to 0 per ReSetSkillReload).
     *
     * Order matches the decomp: cooldown advances first, then the in_skill_time
     * countdown / auto-end (whose reset wins for that frame, exactly as the original
     * returns straight after RoleSkillEnd). NO RGRandom draw.
     * @param dtMs frame delta in milliseconds (<= 0 is ignored).
     */
    void Tick(float dtMs);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};             ///< deterministic stream; never advanced on this path.
    float m_SkillCd = 0.0F;       ///< skill_cd (RoleAttributePlayer), cooldown length.
    float m_InSkillTime = 0.0F;   ///< in_skill_time (C10 skeleton), configured window.
    float m_ThisSkillTime = 0.0F; ///< this_skill_time; counts up to skill_cd.
    float m_InSkillTimeLeft = 0.0F; ///< live in_skill_time countdown (param_1[0x26]).
    bool m_InSkill = false;       ///< in_skill (this+0x55); ultimate active flag.
    bool m_Changing = false;      ///< changing (this+0xA4); Transfiguration in progress.
    bool m_SkillAtk = false;      ///< skill_atk (this+0xAC); pending skill-hit flag.
    int m_HurtCount = 0;          ///< hurt_count (this+0xA8); qualifying-hit counter.
};

} // namespace Game

#endif /* GAME_CHAR_SKILL_C10_HPP */
