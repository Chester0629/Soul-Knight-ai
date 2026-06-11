#ifndef GAME_CHAR_SKILL_C12_HPP
#define GAME_CHAR_SKILL_C12_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class CharSkillC12
 * @brief Faithful, engine-free skill brain for hero C12 (C12Controller, an
 *        RGController player subclass).
 *
 * Per-content port. Models ONLY the pure, unit-testable decisions in the four
 * recovered C12Controller bodies; the actual animator/SFX/transform/hand/spawn
 * wiring is left to the owning entity (referenced in comments only).
 *
 * WHAT THE DECOMP SHOWS (every body grounded, no reconstruction of unseen logic):
 *
 *  - Update (C12Controller__Update @ game_full.c:158438, fully recovered, pure):
 *      if (awake) {                                  // this+0xC
 *        AttributeUpdate();                          // -> SkillReload(): cooldown
 *                                                    //    advances EVERY awake frame,
 *                                                    //    UNCONDITIONALLY.
 *        SeachUpdate();                              // aim re-acquire (owner concern)
 *      }
 *      NOTE (FAITHFUL, the key difference from C01/C13): C12's Update has NO
 *      in_skill branch, NO in_skill_time countdown, and NO auto-end. It is the
 *      bare base shape: just the unconditional cooldown count-up. (C13Controller,
 *      the very next class in the decomp, DOES have the 0x26 in_skill_time
 *      countdown + slot-0x17c auto-end -- C12 deliberately does NOT, so this port
 *      must NOT borrow C13's/C01's window timer. See fabrication_flags.)
 *
 *  - RoleSkill (C12Controller__RoleSkill @ game_full.c:158451):
 *      if (awake) {                                  // this+0xC
 *        if (role_attribute == 0) <crash>;           // this+0x44 must be non-null
 *        if (role_attribute.skill_ready)             // get_skill_ready: 0x44 <= 0x5c
 *          <enter skill effect>;                     // get_transform tail-call
 *      }
 *      The gate is awake && skill_ready -- note there is NO "!in_skill" guard here
 *      (unlike C01Controller__RoleSkill, which adds the 0x55 check). The post-gate
 *      effect is the get_transform tail-call -> the spawn/hand path recovered as
 *      FUN_0026b1dc (skill_obj spawn, RGHand.AtkCut, slot 0x17c) which sets
 *      in_skill = 1 (this+0x55) and is otherwise OWNER-side. So the only
 *      brain-recoverable facts are the gate (awake && skill_ready) and that
 *      activation sets in_skill = true. NO RGRandom draw on this path.
 *
 *  - RoleSkillEnd (C12Controller__RoleSkillEnd @ game_full.c:158514, FAITHFUL,
 *    fully in C12's OWN body -- unlike C01, where the spend was inherited):
 *      if (role_attribute == 0) <crash>;             // this+0x44
 *      role_attribute.ReSetSkillReload();            // this_skill_time (0x5c) = 0
 *      UpdateShadowLock();                           // owner concern (shadow vfx)
 *      So ending the skill restarts the cooldown (this_skill_time = 0). The body
 *      does NOT itself clear in_skill (0x55); that is cleared by the slot-0x17c
 *      virtual the owner routes through when the ability finishes -- so EndSkill()
 *      leaves the skill state AND runs the recovered ReSetSkillReload spend, with
 *      the in_skill clear flagged as the owner-path assumption (see
 *      fabrication_flags). NO RGRandom draw on this path.
 *
 *  - _ctor (C12Controller___ctor @ game_full.c:158420): chains RGController__ctor;
 *      no skill state -> OWNER/engine concern, no brain logic.
 *
 * Determinism: none of the recovered bodies draw from rg_random, so this unit
 * makes NO RNG draws (the RGRandom member is carried for interface parity with
 * the project template and to keep the deterministic stream untouched; Seeded()
 * lets a caller confirm the seed without ever advancing it).
 *
 * @see recreation Player/RGController.cs (skill timing + offsets), PlayerDash.hpp
 *      (the canonical skill-cooldown pattern), CharSkillC01 (sibling hero brain),
 *      skeleton C12Controller.cs (RoleSkill / RoleSkillEnd / GetHeroType only).
 */
class CharSkillC12 {
public:
    /**
     * @brief Construct the skill brain from the hero's skill cooldown stat.
     * @param skillCd cooldown length in seconds (RoleAttributePlayer skill_cd,
     *                field 0x44). Clamped to >= 0. From the CharacterDef stat sheet.
     *
     * Starts READY (this_skill_time == skill_cd) and NOT in skill -- a freshly
     * set-up hero whose first ultimate is available (matches PlayerDash / C01).
     *
     * NOTE: C12 has NO active-window length stat. Unlike C01/C13, C12Controller's
     * Update never decrements an in_skill_time timer, so there is nothing to time;
     * the skill simply stays active until the owner routes RoleSkillEnd. (See
     * fabrication_flags re: the missing window timer.)
     */
    explicit CharSkillC12(float skillCd);

    /// Seed the deterministic stream (kept untouched; no draw is ever made).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief Attempt to activate the hero ultimate this frame.
     *
     * FAITHFUL: C12Controller__RoleSkill @ 158451 gate -- requires
     * awake && role_attribute.skill_ready. (The awake check is the caller's
     * responsibility; the gate this brain owns is skill_ready -- and, unlike C01,
     * there is deliberately NO !in_skill guard in C12's RoleSkill body.) On success
     * the hero enters the skill state (in_skill = true), matching the spawn-tail's
     * "this+0x55 = 1". The hero-specific effect (get_transform tail-call / spawn /
     * RGHand.AtkCut in FUN_0026b1dc) and the cooldown spend (EndSkill) are
     * owner/EndSkill concerns.
     *
     * @return true if the skill activated this call (skill_ready was satisfied),
     *         else false. Re-activation while already in_skill is NOT blocked by
     *         the C12 body, but is blocked here while still on cooldown because
     *         activation does not itself spend the charge (the spend is RoleSkillEnd
     *         per the recovered body), so skill_ready stays true until EndSkill.
     */
    bool TryActivateSkill();

    /**
     * @brief End the active skill and spend the charge.
     *
     * FAITHFUL: C12Controller__RoleSkillEnd @ 158514 -- runs
     * RoleAttributePlayer.ReSetSkillReload (this_skill_time (0x5c) = 0, restart the
     * cooldown) then UpdateShadowLock (owner vfx). Leaves the skill state
     * (in_skill = false). The in_skill clear is the owner slot-0x17c path, not in
     * this body itself (flagged). No-op if not currently in the skill.
     */
    void EndSkill();

    /**
     * @brief One awake-frame tick: advance the cooldown.
     *
     * FAITHFUL: C12Controller__Update @ 158438 (fully recovered, pure):
     *   AttributeUpdate() -> RoleAttributePlayer.SkillReload(dt): this_skill_time
     *   (0x5c) counts UP, clamped to skill_cd (0x44). Called EVERY awake frame,
     *   UNCONDITIONALLY -- C12's Update has NO in_skill branch, so the cooldown is
     *   never frozen and there is NO in_skill_time countdown / auto-end (that is a
     *   C13/C01 feature C12 does not have). SeachUpdate() is the aim re-acquire
     *   (owner concern), modeled as a no-op here. NO RGRandom draw.
     *
     * @param dtMs frame delta in milliseconds (<= 0 is ignored).
     */
    void Tick(float dtMs);

    /// @return true while the ultimate is active (in_skill, this+0x55).
    bool InSkill() const { return m_InSkill; }

    /**
     * @return true when off cooldown and ready to fire.
     * FAITHFUL: RoleAttributePlayer.get_skill_ready -- skill_cd <= this_skill_time
     * (0x44 <= 0x5c).
     */
    bool SkillReady() const { return m_ThisSkillTime >= m_SkillCd; }

    /// @return seconds until the cooldown finishes recharging (0 when ready).
    float CooldownRemaining() const;

    /// Configured cooldown length (skill_cd, 0x44), seconds.
    float SkillCd() const { return m_SkillCd; }

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};             ///< deterministic stream; never advanced on this path.
    float m_SkillCd = 0.0F;       ///< skill_cd (RoleAttributePlayer 0x44), cooldown length.
    float m_ThisSkillTime = 0.0F; ///< this_skill_time (0x5c); counts up to skill_cd.
    bool m_InSkill = false;       ///< in_skill (this+0x55); ultimate active flag.
};

} // namespace Game

#endif /* GAME_CHAR_SKILL_C12_HPP */
