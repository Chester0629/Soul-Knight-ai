#ifndef GAME_CHAR_SKILL_C07_HPP
#define GAME_CHAR_SKILL_C07_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class CharSkillC07
 * @brief Faithful, engine-free skill brain for hero C07 (C07Controller, an
 *        RGController player subclass).
 *
 * Per-content port. Models ONLY the pure, unit-testable decisions in the five
 * recovered C07Controller bodies; the actual animator/particle/weapon-spawn/
 * transform wiring is left to the owning entity (referenced in comments only).
 *
 * WHAT THE DECOMP SHOWS (every body grounded; no reconstruction of unseen logic):
 *
 *  - _ctor (C07Controller___ctor @ game_full.c:156714):
 *      Pure base-ctor delegation (RGController___ctor). OWNER -- no brain logic.
 *
 *  - Awake (C07Controller__Awake @ game_full.c:156732):
 *      RGController.AwakeController then a get_transform tail-call. OWNER -- no
 *      brain logic, no RNG.
 *
 *  - Update (C07Controller__Update @ game_full.c:156746, fully recovered, pure):
 *      if (awake) {                                  // this+0xC
 *        AttributeUpdate();                          // -> SkillReload(dt): cooldown
 *                                                    //    counts UP every awake frame
 *        SeachUpdate();                              // aim re-acquire (owner concern)
 *      }
 *      NOTE: unlike C01, C07's Update has NO in_skill_time countdown and NO
 *      auto-end -- the active window is ended explicitly by RoleSkillEnd (input/
 *      base-toggle driven), not by a timer in Update. NO RGRandom draw.
 *
 *  - RoleSkill (C07Controller__RoleSkill @ game_full.c:156759):
 *      The hero ultimate gate, byte-identical in shape to C01's:
 *        if (awake)                                   // this+0xC
 *          if (role_attribute.skill_ready)            // this+0x44 -> get_skill_ready
 *            if (!in_skill)                            // this+0x55
 *              <enter skill>                           // get_transform tail truncated
 *      The post-gate enter effect tail-calls get_transform (truncated); the real
 *      enter body is the orphaned spawn tail (FUN_00266a0c) which sets in_skill=1
 *      (this+0x55), zeroes this+0x98/0x9c, Plays a ParticleSystem (this+0x94),
 *      reads skill_strengthen, and Instantiates an RGWeapon (this+0x90) -> a
 *      BulletBat. So only the GATE plus the in_skill=1 state-entry is recoverable
 *      pure logic; the particle/weapon spawn is the OWNER's job. NO RGRandom draw.
 *
 *  - RoleSkillEnd (C07Controller__RoleSkillEnd @ game_full.c:156834, recovered):
 *      in_skill = 0;                                  // this+0x55
 *      role_attribute.ReSetSkillReload();             // this_skill_time(0x5c) = 0
 *      role_attribute.SkillReload(this->skillEndCredit); // bank this+0xa0 toward cd
 *      this->skillEndCredit(0xa0) = 0;                // consume the banked credit
 *      UpdateShadowLock();                            // owner concern
 *      So C07's end is richer than C01's: after restarting the cooldown at 0 it
 *      immediately feeds back a banked credit (this+0xa0) via SkillReload, then
 *      clears the credit. this+0xa0 is NEVER written by any recovered C07 body
 *      (the spawn tail zeroes 0x98/0x9c, not 0xa0) -- it is an inherited
 *      accumulator, so we expose it as a settable, clearly-flagged credit that
 *      defaults to 0 (in which case the end behaves exactly like C01's
 *      ReSetSkillReload). See fabrication_flags.
 *
 * Determinism: NONE of the five recovered bodies draws from rg_random, so this
 * unit makes ZERO RNG draws. The RGRandom member is carried only for interface
 * parity with the project template and to keep the deterministic stream
 * untouched; Seeded() lets a caller confirm the seed without ever advancing it.
 *
 * RECONSTRUCTION (flagged, mirrors CharSkillC01): the cooldown engine
 * (this_skill_time count-up to skill_cd + spend) is the established PlayerDash /
 * CharSkillC01 pattern, reused so TryActivateSkill / EndSkill / Tick are
 * unit-testable end to end. RoleAttributePlayer.SkillReload / get_skill_ready /
 * ReSetSkillReload are base-class methods called from C07's body; their internal
 * field math (skill_cd @ +0x44, this_skill_time @ +0x5c) is decoded from the
 * recovered RoleAttributePlayer bodies, not invented.
 *
 * @see recreation Player/RGController.cs (skill timing + offsets), PlayerDash.hpp
 *      (canonical cooldown pattern), CharSkillC01 (canonical hero brain).
 */
class CharSkillC07 {
public:
    /**
     * @brief Construct the skill brain from the hero's skill timing stats.
     * @param skillCd cooldown length in seconds (RoleAttributePlayer skill_cd,
     *                offset +0x44). Clamped to >= 0. From the CharacterDef sheet.
     *
     * Starts READY (this_skill_time == skill_cd) and NOT in skill -- a freshly
     * set-up hero whose first ultimate is available (matches PlayerDash / C01).
     * Unlike C01, C07 has no in_skill_time window stat (its Update has no
     * countdown), so there is no inSkillTime ctor argument.
     */
    explicit CharSkillC07(float skillCd);

    /// Seed the deterministic stream (kept untouched; no draw is ever made).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief Attempt to activate the hero ultimate this frame.
     *
     * FAITHFUL: C07Controller__RoleSkill @ 156759 gate -- requires
     * awake && skill_ready && !in_skill (the awake check is the caller's
     * responsibility; the gate this brain owns is skill_ready && !in_skill,
     * exactly the recovered nested-if at 156774/156780/156784). On success the
     * hero enters the skill state (in_skill = true, matching in_skill=1 set in the
     * spawn tail FUN_00266a0c). The particle Play / RGWeapon (BulletBat)
     * Instantiate are OWNER concerns. NO RGRandom draw.
     *
     * @return true if the skill activated this call (state entered), else false.
     */
    bool TryActivateSkill();

    /**
     * @brief End the active skill window (C07Controller__RoleSkillEnd @ 156834).
     *
     * Faithfully models the recovered body: leave the skill state
     * (in_skill = false), ReSetSkillReload (this_skill_time = 0, restart cd), then
     * SkillReload(skillEndCredit) -- bank the credit at this+0xa0 toward the
     * cooldown -- then clear the credit. UpdateShadowLock is an OWNER concern.
     * No-op if not currently in the skill.
     *
     * Unlike C01, C07's Update does NOT auto-end the skill (no in_skill_time
     * countdown), so this is the explicit, input/base-toggle-driven end path.
     */
    void EndSkill();

    /**
     * @brief One awake-frame tick: advance the cooldown.
     *
     * FAITHFUL: C07Controller__Update @ 156746 (fully recovered, pure):
     *   if (awake) { AttributeUpdate() -> SkillReload(dt); SeachUpdate(); }
     * this_skill_time counts UP by dt, clamped to skill_cd, every awake frame.
     * SeachUpdate (aim) is an OWNER concern. There is NO in_skill_time countdown
     * and NO auto-end here (that is the C01 Update shape, NOT C07's). The cooldown
     * advance is unconditional w.r.t. in_skill -- in_skill is not even read by
     * C07's Update. NO RGRandom draw.
     * @param dtMs frame delta in milliseconds (<= 0 is ignored).
     */
    void Tick(float dtMs);

    /// @return true while the ultimate is active (in_skill, this+0x55).
    bool InSkill() const { return m_InSkill; }

    /**
     * @return true when off cooldown and ready to fire.
     * FAITHFUL: RoleAttributePlayer.get_skill_ready @ 432483 --
     * skill_cd(+0x44) <= this_skill_time(+0x5c).
     */
    bool SkillReady() const { return m_ThisSkillTime >= m_SkillCd; }

    /// @return seconds until the cooldown finishes recharging (0 when ready).
    float CooldownRemaining() const;

    /// Configured cooldown length (skill_cd, +0x44), seconds.
    float SkillCd() const { return m_SkillCd; }

    /**
     * @brief The banked skill-reload credit (this+0xa0) RoleSkillEnd feeds back
     *        into SkillReload after the ReSetSkillReload(0).
     *
     * FLAGGED ASSUMPTION: this+0xa0 is read+zeroed by C07Controller__RoleSkillEnd
     * but is NEVER written by any recovered C07 body -- it is an inherited
     * accumulator. We expose it as a settable credit (default 0 -> end behaves
     * exactly like C01's ReSetSkillReload). Clamped to >= 0. See fabrication_flags.
     */
    void SetSkillEndCredit(float seconds) {
        m_SkillEndCredit = seconds < 0.0F ? 0.0F : seconds;
    }
    float SkillEndCredit() const { return m_SkillEndCredit; }

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};          ///< deterministic stream; never advanced on this path.
    float m_SkillCd = 0.0F;        ///< skill_cd (RoleAttributePlayer +0x44), cd length.
    float m_ThisSkillTime = 0.0F;  ///< this_skill_time (+0x5c); counts up to skill_cd.
    float m_SkillEndCredit = 0.0F; ///< banked end credit (this+0xa0); flagged inherited.
    bool m_InSkill = false;        ///< in_skill (this+0x55); ultimate active flag.
};

} // namespace Game

#endif /* GAME_CHAR_SKILL_C07_HPP */
