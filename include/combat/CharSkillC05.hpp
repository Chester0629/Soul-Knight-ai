#ifndef GAME_CHAR_SKILL_C05_HPP
#define GAME_CHAR_SKILL_C05_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class CharSkillC05
 * @brief Faithful, engine-free skill brain for hero C05 (C05Controller, an
 *        RGController player subclass).
 *
 * Per-content port. Models ONLY the pure, unit-testable decisions in the three
 * recovered C05Controller bodies; the actual animator/SFX/transform/bottle-spawn
 * wiring is left to the owning entity (referenced in comments only).
 *
 * WHAT THE DECOMP SHOWS (all three bodies grounded, no reconstruction of unseen
 * logic). C05 has EXACTLY three recovered bodies -- ctor, Update, RoleSkill --
 * and, unlike C01, NO RoleAtk override and NO recovered RoleSkillEnd body:
 *
 *  - _ctor (C05Controller___ctor @ game_full.c:156531):
 *      A type-init guard followed by RGController___ctor(this, 0). No fields are
 *      set in the recovered body (the IL2CPP skeleton's bottle_objs[] array is an
 *      owner/serialized field, not initialized here). Pure construction; nothing
 *      for the brain to model beyond default state. NO RGRandom draw.
 *
 *  - RoleSkill (C05Controller__RoleSkill @ game_full.c:156562):
 *      The hero ultimate gate, identical in shape to the base RGController one:
 *        if (awake)                                 // this+0xC
 *          if (role_attribute.skill_ready)          // this+0x44 -> get_skill_ready
 *            if (!in_skill)                          // this+0x55
 *              <enter skill>                         // get_transform tail-call truncated
 *      The post-gate effect is a get_transform tail-call (the hero-specific bottle
 *      spawn -- bottle_objs[] in the IL2CPP skeleton), so ONLY the GATE is
 *      recoverable. NO RGRandom draw on this path. The cooldown engine
 *      (this_skill_time count-up + clamp / spend) is the established
 *      PlayerDash/CharSkillC01 pattern; we reuse that timer model here so
 *      TryActivateSkill can be unit-tested end to end. ReSetSkillReload (spend the
 *      charge) is NOT in this recovered body -- it lives in the base
 *      RGController.RoleSkillEnd -> RoleAttributePlayer.ReSetSkillReload chain --
 *      so we expose EndSkill() to spend+leave the state and flag the assumption
 *      (see fabrication_flags).
 *
 *  - Update (C05Controller__Update @ game_full.c:156549, FULLY recovered, pure):
 *      if (awake) {                                  // this+0xC
 *        AttributeUpdate();                          // -> SkillReload(): cooldown
 *                                                    //    advances EVERY awake frame.
 *        SeachUpdate();                              // aim re-acquire (owner concern)
 *      }
 *      CRITICAL DIFFERENCE FROM C01: C05's Update has NO in_skill block. There is
 *      NO param_1[0x25] (in_skill_time) countdown and NO RoleSkillEnd auto-end
 *      (slot 0x17c) in C05's recovered body -- compare C01Controller__Update @
 *      155928, which DOES contain that block. So this brain's Tick advances ONLY
 *      the cooldown; it does NOT decrement an active-window timer and does NOT
 *      auto-end the skill. Inventing such a countdown would be fabrication (see
 *      fabrication_flags). The skill is left active until the owner drives the
 *      base RoleSkillEnd chain (modeled by EndSkill()). NO RGRandom draw.
 *
 * COOLDOWN MODEL (FAITHFUL to RoleAttributePlayer, cross-checked at the decomp):
 *   - SkillReload(dt) @ 432455: this_skill_time (+0x5c) counts UP by dt, clamped
 *     to skill_cd (+0x44). (this_skill_time < skill_cd) guards the advance.
 *   - get_skill_ready @ 432483: ready iff skill_cd <= this_skill_time.
 *   - ReSetSkillReload @ 432381: this_skill_time = 0 (restart the cooldown).
 *   This is the identical engine CharSkillC01 reuses; the field offsets/semantics
 *   are decoded from the decomp itself, not assumed from the enemy table.
 *
 * Determinism: NONE of the three recovered bodies draws from rg_random, so this
 * unit makes ZERO RNG draws (it is a player-driven hero). The RGRandom member is
 * carried for interface parity with the project template and to keep the
 * deterministic stream untouched; Seeded() lets a caller confirm the seed without
 * ever advancing it. (Per the faithfulness rules, no draw is invented to look
 * busy: heroes are player-driven and C05 takes zero draws.)
 *
 * @see recreation Player/RGController.cs (skill timing + offsets), PlayerDash.hpp
 *      and CharSkillC01.hpp (the canonical skill-cooldown pattern),
 *      skeleton C05Controller.cs (bottle_objs[] owner field).
 */
class CharSkillC05 {
public:
    /**
     * @brief Construct the skill brain from the hero's skill timing stats.
     * @param skillCd cooldown length in seconds (RoleAttributePlayer skill_cd,
     *                +0x44). Clamped to >= 0. From the CharacterDef stat sheet.
     *
     * Starts READY (this_skill_time == skill_cd) and NOT in skill -- a freshly
     * SetUpChar hero whose first ultimate is available (matches PlayerDash/C01).
     *
     * NOTE (vs C01): C05 has NO recovered active-window timer (no in_skill_time /
     * param_1[0x25] in its Update), so this constructor intentionally takes ONLY
     * skill_cd. Adding an in_skill_time window here would not be faithful to C05.
     */
    explicit CharSkillC05(float skillCd);

    /// Seed the deterministic stream (kept untouched; no draw is ever made).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief Attempt to activate the hero ultimate this frame.
     *
     * FAITHFUL: C05Controller__RoleSkill @ game_full.c:156562 gate -- requires
     * awake && skill_ready && !in_skill. On success the hero enters the skill
     * state (in_skill = true). The hero-specific effect (get_transform tail-call,
     * the bottle_objs[] spawn) and the actual cooldown spend are owner/EndSkill
     * concerns. NO RGRandom draw.
     *
     * @return true if the skill activated this call (state entered), else false.
     */
    bool TryActivateSkill();

    /**
     * @brief End the active skill window and spend the charge.
     *
     * Models the base RGController.RoleSkillEnd -> RoleAttributePlayer.
     * ReSetSkillReload chain (NOT present in C05's own recovered body -- C05's
     * RoleSkillEnd override is empty in the IL2CPP skeleton and was not
     * decompiled; see fabrication_flags): leaves the skill state
     * (in_skill = false) and restarts the cooldown (this_skill_time = 0).
     * No-op if not currently in the skill.
     */
    void EndSkill();

    /**
     * @brief One awake-frame tick: advance the cooldown ONLY.
     *
     * FAITHFUL: C05Controller__Update @ game_full.c:156549 (fully recovered,
     * pure). The recovered body is:
     *   if (awake) { AttributeUpdate(); SeachUpdate(); }
     * AttributeUpdate -> RoleAttributePlayer.SkillReload(dt) advances
     * this_skill_time UP toward skill_cd (clamped). SeachUpdate() is owner-side
     * aim re-acquire (no brain logic).
     *
     * Unlike C01, C05's Update has NO in_skill block -- there is NO active-window
     * countdown and NO auto-end here. Tick therefore advances the cooldown and
     * nothing else, even while in_skill (the skill stays active until EndSkill).
     * NO RGRandom draw.
     * @param dtMs frame delta in milliseconds (<= 0 is ignored).
     */
    void Tick(float dtMs);

    /// @return true while the ultimate is active (in_skill, this+0x55).
    bool InSkill() const { return m_InSkill; }

    /**
     * @return true when off cooldown and ready to fire.
     * FAITHFUL: RoleAttributePlayer.get_skill_ready @ 432483 --
     * skill_cd <= this_skill_time.
     */
    bool SkillReady() const { return m_ThisSkillTime >= m_SkillCd; }

    /// @return seconds until the cooldown finishes recharging (0 when ready).
    float CooldownRemaining() const;

    /// Configured cooldown length (skill_cd), seconds.
    float SkillCd() const { return m_SkillCd; }

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};             ///< deterministic stream; never advanced on this path.
    float m_SkillCd = 0.0F;       ///< skill_cd (RoleAttributePlayer +0x44), cooldown length.
    float m_ThisSkillTime = 0.0F; ///< this_skill_time (+0x5c); counts up to skill_cd.
    bool m_InSkill = false;       ///< in_skill (this+0x55); ultimate active flag.
};

} // namespace Game

#endif /* GAME_CHAR_SKILL_C05_HPP */
