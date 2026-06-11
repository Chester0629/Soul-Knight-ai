#ifndef GAME_CHAR_SKILL_C06_HPP
#define GAME_CHAR_SKILL_C06_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class CharSkillC06
 * @brief Faithful, engine-free skill/attack brain for hero C06 (C06Controller,
 *        an RGController player subclass -- the "deployable battery" hero).
 *
 * Per-content port. Models ONLY the pure, unit-testable decisions in the three
 * recovered C06Controller bodies (ctor / Update / RoleSkill); the actual
 * animator / SFX / transform / Instantiate wiring is left to the owning entity
 * (referenced in comments only).
 *
 * IL2CPP cross-check (asset soul 2/.../C06Controller.cs, field names only):
 *   public  GameObject           battery;        -> this+0x90 (spawn template)
 *   private RGBatteryController   the_battery;    -> this+0x94 (deployed instance)
 *   private bool                  canCancelSkill; -> this+0x98 (recall gate)
 *   overrides: RoleSkill, RoleSkillEnd, TurnSkillCancelable, Update.
 * The decomp's RoleSkill body accesses exactly 0x90/0x94/0x98 (op_Inequality on
 * the_battery, then the canCancelSkill byte, then RGBatteryController.Dead), and
 * the deploy helper (FUN_00266544) Instantiates the battery from 0x90 into 0x94,
 * sets in_skill (0x55) and canCancelSkill (0x98) -- so the offset->field mapping
 * is decoded from the decomp's own self-consistent accesses and confirmed by the
 * metadata names, NOT assumed from the enemy RGEController table.
 *
 * WHAT THE DECOMP SHOWS (every body grounded, no reconstruction of unseen logic):
 *
 *  - Update (C06Controller__Update @ game_full.c:156619, fully recovered, pure):
 *      if (awake) {                                 // this+0xC
 *        AttributeUpdate();                          // -> SkillReload(): cooldown
 *                                                    //    count-up, every awake frame
 *        SeachUpdate();                              // aim re-acquire (owner concern)
 *      }
 *      NOTE: unlike C01, C06's Update has NO in_skill_time countdown and NO
 *      auto-end -- the deployed-battery skill does NOT self-terminate on a timer.
 *      It is ended only by the recall (RGBatteryController.Dead) cancel path or by
 *      the base RoleSkillEnd. We therefore model NO active-window timer here. NO
 *      RGRandom draw.
 *
 *  - RoleSkill (C06Controller__RoleSkill @ game_full.c:156632): ONE override that
 *    carries BOTH the activation gate AND the early-recall (cancel) gate:
 *      // (A) ACTIVATE -- identical shape to the base RGController gate:
 *      if (awake)                                    // this+0xC
 *        if (role_attribute.skill_ready)             // this+0x44 -> get_skill_ready
 *          if (!in_skill)                             // this+0x55
 *            <deploy>                                 // get_transform tail (owner);
 *                                                     // the real deploy helper sets
 *                                                     // in_skill=1, canCancelSkill,
 *                                                     // Instantiates battery -> 0x94
 *      // (B) EARLY RECALL -- fired by pressing skill again while deployed:
 *      if (in_skill)                                  // this+0x55
 *        if (the_battery != null                      // 0x94 op_Inequality
 *            && canCancelSkill)                        // this+0x98
 *          the_battery.Dead();                         // RGBatteryController.Dead(0x94)
 *      Both halves run in branch order. The post-activate effect is a get_transform
 *      tail-call (owner spawn), so only the GATE is recoverable; Dead() itself is
 *      an animator-trigger tail (owner). NO RGRandom draw on either path.
 *
 *  - _ctor (C06Controller___ctor @ game_full.c:156592): static-init guards then
 *    RGController.__ctor -- pure base construction, no brain logic.
 *
 * Reused PlayerDash/CharSkillC01 cooldown model (FLAGGED, see fabrication note):
 *   The skill_cd / this_skill_time count-up + spend timer is the established
 *   PlayerDash pattern reused so TryActivateSkill / EndSkill are unit-testable.
 *   ReSetSkillReload (spend the charge) lives in the base RGController.RoleSkillEnd
 *   chain, NOT in this recovered body, so EndSkill() exposes it as a clearly
 *   flagged assumption. canCancelSkill starting false on deploy and being flipped
 *   true by TurnSkillCancelable is the metadata-confirmed delayed-Invoke pattern
 *   (the literal Invoke delay is not in the recovered head -> not modeled).
 *
 * Determinism: NO recovered C06 body draws from rg_random, so this unit makes
 * ZERO RNG draws. The RGRandom member is carried for interface parity and to keep
 * the deterministic stream untouched; Seeded() lets a caller confirm the seed
 * without ever advancing it.
 *
 * @see CharSkillC01.hpp (canonical hero-port shape), PlayerDash.hpp (cooldown
 *      pattern), recreation Player/RGController.cs (offsets), C06Controller.cs.
 */
class CharSkillC06 {
public:
    /**
     * @brief Construct the skill brain from the hero's skill timing stats.
     * @param skillCd cooldown length in seconds (RoleAttributePlayer skill_cd,
     *                this+0x44 of the attribute). Clamped to >= 0.
     *
     * Starts READY (this_skill_time == skill_cd) and NOT in skill -- a freshly
     * set-up hero whose first ultimate is available (matches PlayerDash / C01).
     * canCancelSkill starts false (no battery deployed yet).
     */
    explicit CharSkillC06(float skillCd);

    /// Seed the deterministic stream (kept untouched; no draw is ever made).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief Attempt to deploy the battery (RoleSkill ACTIVATE half).
     *
     * FAITHFUL: C06Controller__RoleSkill @ 156632 gate (A) -- requires
     * skill_ready (0x44) && !in_skill (0x55). On success the hero enters the skill
     * state (in_skill = true), marks a battery as deployed, and resets
     * canCancelSkill to false (the recall is locked until TurnSkillCancelable
     * fires). The actual Instantiate of the battery into 0x94 + the get_transform
     * tail are owner concerns. The awake (0xC) gate is the caller's responsibility.
     *
     * @return true if the skill deployed this call (state entered), else false.
     */
    bool TryActivateSkill();

    /**
     * @brief Attempt the early recall (RoleSkill CANCEL half) -- press skill again
     *        while a battery is deployed to kill it early.
     *
     * FAITHFUL: C06Controller__RoleSkill @ 156662 gate (B) -- requires in_skill
     * (0x55) && the_battery != null (0x94 op_Inequality) && canCancelSkill (0x98).
     * On success the deployed battery is recalled. The actual
     * RGBatteryController.Dead(0x94) call (an animator-trigger tail) is the owner's
     * job; this reports the recall decision and leaves the active state via the
     * same spend path as EndSkill (the battery's death ends the skill window).
     *
     * @return true if the battery was recalled this call, else false (gate failed).
     */
    bool TryCancelSkill();

    /**
     * @brief Unlock the early recall (TurnSkillCancelable override).
     *
     * FAITHFUL (owner-driven): C06Controller.TurnSkillCancelable -- the target of
     * the delayed Invoke armed on deploy. Sets canCancelSkill = true so the next
     * TryCancelSkill can recall the battery. No-op when not in skill. The literal
     * Invoke delay is not in the recovered head, so the owner fires this when the
     * timer elapses (see fabrication note).
     */
    void TurnSkillCancelable();

    /**
     * @brief End the active skill window and spend the charge (RoleSkillEnd).
     *
     * Models the base RGController.RoleSkillEnd -> RoleAttributePlayer
     * .ReSetSkillReload chain (the C06 body itself does not contain the spend; see
     * fabrication note): leaves the skill state (in_skill = false), clears the
     * deployed battery + canCancelSkill, and restarts the cooldown
     * (this_skill_time = 0). No-op if not currently in the skill.
     */
    void EndSkill();

    /**
     * @brief One awake-frame tick: advance the cooldown (Update).
     *
     * FAITHFUL: C06Controller__Update @ 156619 (fully recovered, pure):
     *   if (awake) AttributeUpdate() -> SkillReload(dt): this_skill_time counts UP,
     *   clamped to skill_cd, EVERY awake frame. SeachUpdate() is an owner concern.
     * Unlike C01 there is NO in_skill_time countdown and NO auto-end here -- the
     * deployed-battery skill does not expire on a timer. NO RGRandom draw.
     * @param dtMs frame delta in milliseconds (<= 0 is ignored).
     */
    void Tick(float dtMs);

    /// @return true while the skill is active / battery deployed (in_skill, 0x55).
    bool InSkill() const { return m_InSkill; }

    /// @return true while a battery is deployed (the_battery != null, 0x94).
    bool BatteryDeployed() const { return m_BatteryDeployed; }

    /// @return true once the early recall is unlocked (canCancelSkill, 0x98).
    bool CanCancelSkill() const { return m_CanCancelSkill; }

    /**
     * @return true when off cooldown and ready to fire.
     * FAITHFUL: RoleAttributePlayer.get_skill_ready @ 432483 --
     * skill_cd (0x44) <= this_skill_time (0x5c).
     */
    bool SkillReady() const { return m_ThisSkillTime >= m_SkillCd; }

    /// @return seconds until the cooldown finishes recharging (0 when ready).
    float CooldownRemaining() const;

    /// Configured cooldown length (skill_cd), seconds.
    float SkillCd() const { return m_SkillCd; }

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};             ///< deterministic stream; never advanced on this path.
    float m_SkillCd = 0.0F;       ///< skill_cd (RoleAttributePlayer 0x44), cooldown length.
    float m_ThisSkillTime = 0.0F; ///< this_skill_time (0x5c); counts up to skill_cd.
    bool m_InSkill = false;        ///< in_skill (this+0x55); skill-active flag.
    bool m_BatteryDeployed = false;///< the_battery != null (this+0x94); a battery is out.
    bool m_CanCancelSkill = false; ///< canCancelSkill (this+0x98); early-recall unlocked.
};

} // namespace Game

#endif /* GAME_CHAR_SKILL_C06_HPP */
