#ifndef GAME_CHAR_SKILL_C13_HPP
#define GAME_CHAR_SKILL_C13_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class CharSkillC13
 * @brief Faithful, engine-free skill/attack brain for hero C13 (a
 *        transfiguration / armor-regen hero: C13Controller, an RGController
 *        player subclass).
 *
 * Per-content port. Models ONLY the pure, unit-testable decisions in the four
 * pure-logic C13Controller bodies (Update, RoleSkill, RoleAtk, HurtSomeOne);
 * the actual animator/SFX/transform/Instantiate wiring (Transfiguration,
 * AutoLock, SkillAtk) is left to the owning entity (referenced in comments
 * only).
 *
 * FIELD MAP (decoded from the decomp's own self-consistent accesses, cross-
 * checked against the IL2CPP field list in C13Controller.cs and the already-
 * faithful CharSkillC01; PLAYER/RGController offsets, NOT the enemy table):
 *   this+0x0C  awake             (char param_1[3])      -- caller-owned gate
 *   this+0x14  animator          (Transfiguration/owner)
 *   this+0x44  role_attribute    (RoleAttributePlayer*) -- get_skill_ready/RestoreArmor
 *   this+0x55  in_skill          (char)                 -- ultimate-active flag
 *   this+0x26  in_skill_time     (float, param_1[0x26]) -- active-window COUNTDOWN
 *   this+0xA4  changing          (bool "changing")      -- C13 transfiguration lock
 *   this+0xA8  hurt_count        (int)                  -- hits-while-skill counter
 *   this+0xAC  skill_atk         (bool)                 -- per-hit armor-regen flag
 *   this+0x9C  skill_atk_obj     (GameObject, owner Instantiate)
 *
 * NOTE on in_skill_time offset: C01 stores it at param_1[0x25]; C13 stores it at
 * param_1[0x26] because C13 carries three extra fields (changing, hurt_count,
 * skill_atk) that shift the layout. The Update body shape is otherwise identical
 * to C01's (AttributeUpdate -> SeachUpdate -> in_skill gate -> countdown ->
 * IsLocalPlayer/UI -> auto-end via slot 0x17c). IL2CPP confirms C13 declares its
 * own private float in_skill_time.
 *
 * WHAT THE DECOMP SHOWS (every pure body grounded; no reconstruction of unseen
 * logic):
 *
 *  - Update (C13Controller__Update @ game_full.c:158547, fully recovered, pure):
 *      if (awake) {                                  // this+0x0C
 *        AttributeUpdate();                          // -> SkillReload(): cooldown
 *                                                    //    advances EVERY awake frame,
 *                                                    //    UNCONDITIONALLY.
 *        SeachUpdate();                              // aim re-acquire (owner concern)
 *        if (in_skill) {                             // this+0x55
 *          in_skill_time -= deltaTime;               // param_1[0x26] COUNTDOWN
 *          if (local player) <refresh UI>;           // owner concern
 *          if (in_skill_time <= 0) { RoleSkillEnd(); return; } // AUTO-END (slot 0x17c)
 *        }
 *      }
 *      Identical control flow to C01: cooldown count-up is NOT frozen during the
 *      skill, and the active window self-terminates once in_skill_time reaches 0,
 *      routing through the same RoleSkillEnd path EndSkill() models. NO RGRandom draw.
 *
 *  - RoleSkill (C13Controller__RoleSkill @ game_full.c:158606):
 *      The ultimate gate, with C13's EXTRA "changing" lock on top of the base shape:
 *        if (awake)                                 // this+0x0C
 *          if (role_attribute.skill_ready)          // this+0x44 -> get_skill_ready == 1
 *            if (!in_skill && !changing)             // this+0x55 == 0 && this+0xA4 == 0
 *              <enter skill>                         // get_transform tail-call truncated
 *      The decomp computes bVar3 = (in_skill == 0), then ONLY reads changing
 *      (0xA4) when bVar3 is true, and enters only when (bVar3 && changing == 0).
 *      So the C13 gate is awake && skill_ready && !in_skill && !changing -- the
 *      !changing term is the C13-specific addition over CharSkillC01's gate. The
 *      post-gate effect is a get_transform tail-call (hero-specific spawn), so
 *      only the GATE is recoverable. NO RGRandom draw on this path. ReSetSkillReload
 *      (spend the charge) is NOT in this recovered body -- it lives in the base
 *      RGController.RoleSkillEnd chain -- so EndSkill() spends + leaves the state
 *      and that assumption is flagged (see fabrication_flags).
 *
 *  - RoleAtk (C13Controller__RoleAtk @ game_full.c:158758, value = press down):
 *      if (awake) {                                  // this+0x0C
 *        if (in_skill)                               // this+0x55
 *          animator.SetBool("...6685", value);       // owner: paw/transfig anim mirror
 *        if (in_item && value) { TriggerItem(); return; } // use the pickup
 *        if (value && virtual *(0xe4)() == 1)        // press + an aim/ready predicate
 *          if (!hand.IsMelee()) <get_position aim nudge>; // truncated, owner
 *        hand.SetAttack(value);                       // primary hand fires/stops
 *      }
 *      Branch ORDER differs from C01: here the in_skill animator mirror comes
 *      FIRST (before the item check), there is NO 0.1s second-hand Invoke, and the
 *      aim nudge is gated by a virtual predicate + IsMelee (truncated). NO RGRandom
 *      draw. The recoverable brain decisions: awake gate, the in_skill animator
 *      mirror flag, the in_item&&press item-trigger early-out, and the primary
 *      SetAttack. The aim-nudge tail carries no recoverable pure logic / RNG.
 *
 *  - HurtSomeOne (C13Controller__HurtSomeOne @ game_full.c:158810, fully recovered):
 *      bool active = in_skill;                        // this+0x55
 *      if (active && skill_atk) {                     // this+0xAC
 *        skill_atk = false;                           // consume the per-hit flag
 *        if (++hurt_count > 2) {                      // this+0xA8, every 3rd qualifying hit
 *          role_attribute.RestoreArmor(1);            // this+0x44 -> +1 armor
 *          hurt_count = 0;
 *        }
 *      }
 *      base.HurtSomeOne();                            // owner concern
 *      C13's signature passive: while the ultimate is up, every third qualifying
 *      hit (one whose skill_atk flag was set) restores 1 armor and the counter
 *      resets. This is FULLY recoverable pure bookkeeping. NO RGRandom draw. The
 *      skill_atk flag is RAISED elsewhere (owner SkillAtk path / animation event,
 *      not in this body); ArmTickFromHit() lets the owner feed a hit and reports
 *      whether armor was restored. The base.HurtSomeOne() chain is owner.
 *
 * Determinism: NONE of the four recovered bodies draws from rg_random, so this
 * unit makes ZERO RNG draws (the RGRandom member is carried for interface parity
 * with the project template and to keep the deterministic stream untouched;
 * Seeded() lets a caller confirm the seed without ever advancing it).
 *
 * @see recreation Player/RGController.cs (skill timing + offsets), CharSkillC01
 *      (the canonical hero-port pattern), IL2CPP C13Controller.cs (field names).
 */
class CharSkillC13 {
public:
    /// Number of qualifying hits required (while in_skill) to restore 1 armor.
    /// FAITHFUL: C13Controller__HurtSomeOne tests "2 < ++hurt_count", i.e. the
    /// 3rd qualifying hit (hurt_count 1,2,3 -> fires on 3) triggers RestoreArmor.
    static constexpr int kHurtCountToRestoreArmor = 3;

    /// Armor restored on each qualifying 3rd hit (RestoreArmor(1) immediate).
    static constexpr int kArmorRestoredPerCycle = 1;

    /**
     * @brief Construct the skill brain from the hero's skill timing stats.
     * @param skillCd     cooldown length in seconds (RoleAttributePlayer skill_cd).
     *                    Clamped to >= 0. From the CharacterDef stat sheet.
     * @param inSkillTime active-skill window length in seconds (C13.in_skill_time,
     *                    the IL2CPP field). Clamped to >= 0. From the stat sheet.
     *
     * Starts READY (this_skill_time == skill_cd), NOT in skill, NOT changing, with
     * hurt_count 0 and skill_atk cleared -- a freshly set-up hero whose first
     * ultimate is available (matches PlayerDash / CharSkillC01).
     */
    CharSkillC13(float skillCd, float inSkillTime);

    /// Seed the deterministic stream (kept untouched; no draw is ever made).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief Attempt to activate the hero ultimate this frame.
     *
     * FAITHFUL: C13Controller__RoleSkill @ 158606 gate -- requires
     * awake && skill_ready && !in_skill && !changing. On success the hero enters
     * the skill state (in_skill = true). The hero-specific effect (get_transform
     * tail-call) and the actual cooldown spend are owner/EndSkill concerns.
     *
     * @param changing the C13 "changing" transfiguration lock (this+0xA4). While
     *                 true the ultimate cannot start (decomp's second nested gate).
     * @return true if the skill activated this call (state entered), else false.
     */
    bool TryActivateSkill(bool changing = false);

    /**
     * @brief End the active skill window and spend the charge.
     *
     * Models the base RGController.RoleSkillEnd -> RoleAttributePlayer.ReSetSkillReload
     * chain (the C13 body itself does not contain the spend; see fabrication_flags):
     * leaves the skill state (in_skill = false), clears the per-hit passive state
     * (hurt_count = 0, skill_atk = false), and restarts the cooldown
     * (this_skill_time = 0). No-op if not currently in the skill.
     */
    void EndSkill();

    /**
     * @brief Process an attack press/release (C13Controller__RoleAtk @ 158758).
     *
     * Models the pure decisions, returning what the owning entity must do. The
     * actual animator.SetBool / TriggerItem / aim-nudge / hand.SetAttack calls are
     * the owner's job; this reports the in_skill-gated animator mirror and the
     * item-trigger early-out faithfully.
     *
     * @param pressDown true = attack button down (value==1), false = release.
     * @param standingOnItem true if standing on a pickup (RGController.in_item).
     */
    struct AtkDecision {
        /// While in_skill, the paw/transfiguration animator bool mirrors the
        /// press (animator.SetBool(StringLiteral_6685, value)). True only when
        /// in_skill. FAITHFUL: this is the FIRST branch, before the item check.
        bool animatorMirror = false;
        /// The value passed to the animator bool (== pressDown). Valid only when
        /// animatorMirror.
        bool animatorMirrorValue = false;
        /// in_item && pressDown -> the press triggers the pickup (then returns).
        bool triggerItem = false;
        /// Should hand.SetAttack(pressDown) be issued this call? (false on the
        /// item-trigger early-out, true otherwise -- the normal fire/stop path.)
        bool setHandAttack = false;
        /// The value to pass to hand.SetAttack (== pressDown when setHandAttack).
        bool handAttackValue = false;
    };

    /**
     * FAITHFUL: C13Controller__RoleAtk @ 158758. Branch order preserved:
     *   awake gate -> (in_skill -> animator.SetBool mirror) -> (in_item && press ->
     *   TriggerItem + return) -> (press && virtual predicate && !IsMelee -> aim
     *   nudge, owner) -> hand.SetAttack(value).
     * @return the decision the owning entity should act on (all-false if !awake,
     *         which is the caller's gate).
     */
    AtkDecision RoleAtk(bool pressDown, bool standingOnItem) const;

    /**
     * @brief One awake-frame tick: advance the cooldown and the active window.
     *
     * FAITHFUL: C13Controller__Update @ 158547 (fully recovered, pure):
     *   1. AttributeUpdate() -> RoleAttributePlayer.SkillReload(dt): this_skill_time
     *      counts UP, clamped to skill_cd. Called EVERY awake frame, UNCONDITIONALLY
     *      -- it is NOT frozen while in_skill (the in_skill check sits AFTER it).
     *   2. While in_skill, the in_skill_time timer (param_1[0x26]) counts DOWN by
     *      dt; when it reaches <= 0 the skill AUTO-ENDS via the RoleSkillEnd path
     *      (slot 0x17c) -- modeled by calling EndSkill().
     *
     * Order matches the decomp: cooldown advances first, then the in_skill_time
     * countdown / auto-end runs (and the auto-end's reset wins for that frame,
     * exactly as the original returns straight after RoleSkillEnd).
     * @param dtMs frame delta in milliseconds (<= 0 is ignored).
     */
    void Tick(float dtMs);

    /**
     * @brief Feed one incoming hit to the in_skill armor-regen passive.
     *
     * FAITHFUL: C13Controller__HurtSomeOne @ 158810 (the bookkeeping HEAD; the
     * trailing base.HurtSomeOne() is the owner's). Models exactly:
     *   if (in_skill && skill_atk) {
     *     skill_atk = false;                 // consume the per-hit flag
     *     if (++hurt_count > 2) { RestoreArmor(1); hurt_count = 0; }
     *   }
     * The skill_atk flag is RAISED outside this body (owner SkillAtk / anim event),
     * so the caller passes whether it was set for this hit (see SetSkillAtkFlag /
     * the qualifyingHit argument). Returns the armor restored this hit (0 or 1).
     *
     * @return kArmorRestoredPerCycle when this hit completed a 3-hit cycle, else 0.
     */
    int ArmTickFromHit();

    /// Raise the per-hit passive flag (skill_atk, this+0xAC). The decomp raises
    /// this OUTSIDE HurtSomeOne (owner SkillAtk / animation event); exposed so the
    /// passive can be unit-tested. FLAGGED: the raise site is owner-side.
    void SetSkillAtkFlag(bool on) { m_SkillAtk = on; }
    bool SkillAtkFlag() const { return m_SkillAtk; }

    /// @return live hurt_count (this+0xA8); cycles 0..2, resets to 0 after a restore.
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
    /// not in skill. FAITHFUL: C13Controller__Update's in_skill_time decrement.
    float InSkillTimeRemaining() const { return m_InSkill ? m_InSkillTimeLeft : 0.0F; }

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};             ///< deterministic stream; never advanced on this path.
    float m_SkillCd = 0.0F;       ///< skill_cd (RoleAttributePlayer), cooldown length.
    float m_InSkillTime = 0.0F;   ///< in_skill_time (C13 IL2CPP), configured window.
    float m_ThisSkillTime = 0.0F; ///< this_skill_time; counts up to skill_cd.
    float m_InSkillTimeLeft = 0.0F; ///< live in_skill_time countdown (param_1[0x26]).
    int m_HurtCount = 0;          ///< hurt_count (this+0xA8); 0..2 then restore+reset.
    bool m_InSkill = false;       ///< in_skill (this+0x55); ultimate active flag.
    bool m_SkillAtk = false;      ///< skill_atk (this+0xAC); per-hit armor-regen flag.
};

} // namespace Game

#endif /* GAME_CHAR_SKILL_C13_HPP */
