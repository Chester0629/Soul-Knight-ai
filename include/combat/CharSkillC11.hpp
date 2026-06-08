#ifndef GAME_CHAR_SKILL_C11_HPP
#define GAME_CHAR_SKILL_C11_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class CharSkillC11
 * @brief Faithful, engine-free skill brain for hero C11 (C11Controller, an
 *        RGController player subclass).
 *
 * Per-content port. Models ONLY the pure, unit-testable decisions in the FOUR
 * recovered C11Controller bodies; the actual animator/SFX/transform/spawn wiring
 * is left to the owning entity (referenced in comments only).
 *
 * WHAT THE DECOMP SHOWS (every body grounded; no reconstruction of unseen logic):
 *
 *  - _ctor (C11Controller___ctor @ game_full.c:158246):
 *      Pure base delegation: RGController___ctor(this, 0). No brain state set
 *      here -- OWNER concern, not modeled.
 *
 *  - Update (C11Controller__Update @ game_full.c:158264, FULLY recovered, pure):
 *      if (awake) {                                  // this+0xC
 *        AttributeUpdate();                          // -> SkillReload(): cooldown
 *                                                    //    count-up, run EVERY awake
 *                                                    //    frame, UNCONDITIONALLY.
 *        SeachUpdate();                              // aim re-acquire (owner concern)
 *      }
 *      CRITICAL DIFFERENCE vs C01: C11's Update has NO in_skill_time countdown and
 *      NO auto-end (no RoleSkillEnd call, no param_1[0x25] decrement). The whole
 *      body is awake-gated AttributeUpdate + SeachUpdate. So this brain models the
 *      cooldown count-up ONLY; the skill does NOT self-terminate -- it is ended
 *      exclusively by the explicit RoleSkillEnd call (owner/caller-triggered).
 *      Modeling an in_skill_time auto-end here would be fabrication, so we do NOT.
 *
 *  - RoleSkill (C11Controller__RoleSkill @ game_full.c:158277):
 *      The hero ultimate gate, identical in shape to the base RGController one:
 *        if (awake)                                  // this+0xC
 *          if (role_attribute.skill_ready)           // this+0x44 -> get_skill_ready
 *            if (!in_skill)                          // this+0x55
 *              <enter skill>                         // get_transform tail-call (the
 *                                                    //   skill_obj spawn) truncated
 *      The post-gate effect is a get_transform tail-call (the C11.skill_obj spawn),
 *      so only the GATE is recoverable. NO RGRandom draw on this path. The cooldown
 *      engine (this_skill_time count-up + spend) is the established PlayerDash /
 *      CharSkillC01 pattern; we reuse that timer model so TryActivateSkill can be
 *      unit-tested end to end (see fabrication_flags).
 *
 *  - RoleSkillEnd (C11Controller__RoleSkillEnd @ game_full.c:158395, FULLY
 *      recovered, pure):
 *        in_skill = 0;                               // this+0x55
 *        role_attribute.ReSetSkillReload();          // this+0x44; this_skill_time = 0
 *        UpdateShadowLock();                          // owner concern (shadow sprite)
 *      Unlike C01 (whose RoleSkillEnd lived in the base class and had to be assumed),
 *      C11's RoleSkillEnd IS recovered here, so EndSkill() is FAITHFUL to this body:
 *      it leaves the skill state and restarts the cooldown. UpdateShadowLock is the
 *      only owner-side tail and is not modeled.
 *
 * Determinism: NONE of the recovered bodies draws from rg_random, so this unit
 * makes ZERO RNG draws (heroes are player-driven). The RGRandom member is carried
 * only for interface parity with the project template and to keep the deterministic
 * stream untouched; Seeded() lets a caller confirm the seed without advancing it.
 *
 * @see recreation Player/RGController.cs (skill timing + offsets), PlayerDash.hpp
 *      (the canonical skill-cooldown pattern), CharSkillC01 (sibling hero brain),
 *      IL2CPP C11Controller.cs (skill_obj; RoleSkill/RoleSkillEnd overrides).
 */
class CharSkillC11 {
public:
    /**
     * @brief Construct the skill brain from the hero's skill timing stat.
     * @param skillCd cooldown length in seconds (RoleAttributePlayer skill_cd,
     *                attribute offset +0x44). Clamped to >= 0. From the
     *                CharacterDef stat sheet.
     *
     * Starts READY (this_skill_time == skill_cd) and NOT in skill -- a freshly
     * set-up hero whose first ultimate is available (matches PlayerDash / C01).
     */
    explicit CharSkillC11(float skillCd);

    /// Seed the deterministic stream (kept untouched; no draw is ever made).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief Attempt to activate the hero ultimate this frame.
     *
     * FAITHFUL: C11Controller__RoleSkill @ 158277 gate -- requires
     * awake && skill_ready && !in_skill. On success the hero enters the skill
     * state (in_skill = true). The hero-specific effect (the skill_obj get_transform
     * tail-call at 158298) is an owner concern; the cooldown spend is deferred to
     * EndSkill (RoleSkillEnd -> ReSetSkillReload), exactly as the decomp does --
     * RoleSkill itself does NOT spend the charge.
     *
     * The decomp's outer awake gate (this+0xC) is the caller's responsibility (this
     * brain has no awake field); the gate this unit owns is skill_ready && !in_skill,
     * the recovered nested-if at 158291/158294/158296.
     *
     * @return true if the skill activated this call (state entered), else false.
     */
    bool TryActivateSkill();

    /**
     * @brief End the active skill window and spend the charge.
     *
     * FAITHFUL: C11Controller__RoleSkillEnd @ 158395 (recovered, pure):
     *   in_skill = 0 (this+0x55); role_attribute.ReSetSkillReload() (this_skill_time
     *   = 0, restart cooldown); UpdateShadowLock() (owner concern, not modeled).
     * Unlike C01, the spend body is recovered here, so this is faithful (not an
     * assumed base-class effect). No-op if not currently in the skill.
     */
    void EndSkill();

    /**
     * @brief One awake-frame tick: advance the cooldown.
     *
     * FAITHFUL: C11Controller__Update @ 158264 (fully recovered, pure):
     *   1. AttributeUpdate() -> RoleAttributePlayer.SkillReload(dt): this_skill_time
     *      (attribute +0x5c) counts UP, clamped to skill_cd (+0x44). Called EVERY
     *      awake frame, UNCONDITIONALLY -- NOT frozen while in_skill.
     *   2. SeachUpdate() -- aim re-acquire (owner concern, not modeled).
     * There is NO in_skill_time countdown and NO auto-end in C11's Update (unlike
     * C01), so Tick advances the cooldown ONLY and never ends the skill. The skill
     * ends solely via the explicit EndSkill()/RoleSkillEnd path.
     * @param dtMs frame delta in milliseconds (<= 0 is ignored).
     */
    void Tick(float dtMs);

    /// @return true while the ultimate is active (in_skill, this+0x55).
    bool InSkill() const { return m_InSkill; }

    /**
     * @return true when off cooldown and ready to fire.
     * FAITHFUL: RoleAttributePlayer.get_skill_ready @ 432483 --
     * skill_cd (+0x44) <= this_skill_time (+0x5c).
     */
    bool SkillReady() const { return m_ThisSkillTime >= m_SkillCd; }

    /// @return seconds until the cooldown finishes recharging (0 when ready).
    float CooldownRemaining() const;

    /// Configured cooldown length (skill_cd), seconds.
    float SkillCd() const { return m_SkillCd; }

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};         ///< deterministic stream; never advanced on this path.
    float m_SkillCd = 0.0F;       ///< skill_cd (RoleAttributePlayer +0x44), cooldown.
    float m_ThisSkillTime = 0.0F; ///< this_skill_time (+0x5c); counts up to skill_cd.
    bool m_InSkill = false;       ///< in_skill (this+0x55); ultimate active flag.
};

} // namespace Game

#endif /* GAME_CHAR_SKILL_C11_HPP */
