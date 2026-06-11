#ifndef GAME_CHAR_SKILL_C03_HPP
#define GAME_CHAR_SKILL_C03_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class CharSkillC03
 * @brief Faithful, engine-free skill brain for hero C03 (a thunder-summoning
 *        hero: C03Controller, an RGController player subclass).
 *
 * Per-content port. Models ONLY the pure, unit-testable decisions in the five
 * recovered C03Controller bodies; the actual animator/SFX/transform/thunder-prefab
 * (CreateTunder spawn) wiring is left to the owning entity (referenced in comments
 * only).
 *
 * WHAT THE DECOMP SHOWS (all five bodies grounded; no reconstruction of the
 * unseen thunder ability effect):
 *
 *  - _ctor (C03Controller___ctor @ game_full.c:156183):
 *      Pure il2cpp boilerplate -> RGController___ctor(this, 0). OWNER concern,
 *      no brain logic.
 *
 *  - Update (C03Controller__Update @ game_full.c:156201, fully recovered, pure):
 *      if (awake) {                                  // this+0xc
 *        AttributeUpdate();                          // -> SkillReload(): cooldown
 *                                                    //    count-up, run EVERY awake
 *                                                    //    frame, UNCONDITIONALLY.
 *        SeachUpdate();                              // aim re-acquire (owner concern)
 *      }
 *      CRUCIAL CONTRAST WITH C01 (and matching C02): C03's Update has NO
 *      in_skill_time countdown and NO auto-end branch -- it is JUST the
 *      awake-gated AttributeUpdate+SeachUpdate. There is no param_1[0x25]
 *      decrement and no slot-0x17c (RoleSkillEnd) auto-call here. The C03 skill
 *      ends only through the explicit RoleSkillEnd path, never on a self-expiring
 *      timer. Modeling an auto-end here would be fabrication, so Tick() advances
 *      ONLY the cooldown and never auto-ends. NO RGRandom draw.
 *
 *  - RoleSkill (C03Controller__RoleSkill @ game_full.c:156214):
 *      The hero ultimate gate, identical in shape to the base RGController one and
 *      to C01/C02:
 *        if (awake)                                  // this+0xc
 *          if (role_attribute.skill_ready)           // this+0x44 -> get_skill_ready==1
 *            if (!in_skill)                           // this+0x55 == 0
 *              <enter skill>                          // get_transform tail-call truncated
 *      The post-gate effect is a get_transform tail-call (the hero-specific thunder
 *      spawn -- CreateTunder), so only the GATE is recoverable. NO RGRandom draw.
 *      The cooldown engine (skill_cd / this_skill_time count-up + spend) is the
 *      established PlayerDash / CharSkillC01 pattern; we REUSE that timer model so
 *      TryActivateSkill can be unit-tested end to end (see fabrication_flags).
 *
 *  - CreateTunder (C03Controller__CreateTunder @ game_full.c:156245):
 *      Pure il2cpp boilerplate guard -> get_transform tail-call only. This is the
 *      thunder-prefab spawn/positioning (the C03Controller.thunder GameObject in
 *      the IL2CPP dump). PURELY OWNER -- the spawn/Instantiate/animator is the
 *      owner's job; no recoverable brain logic. NO RGRandom draw is visible in the
 *      recovered head (the prefab spawn tail is truncated; see TODO note).
 *
 *  - RoleSkillEnd (C03Controller__RoleSkillEnd @ game_full.c:156258):
 *      if (this+0x40 == 0) <null-guard fault>;       // a Component reference (owner)
 *      get_transform(this+0x40);                      // tail-call truncated
 *      CRUCIAL CONTRAST WITH C02's RoleSkillEnd: C02's body DIRECTLY contains the
 *      in_skill clear (this+0x55 = 0) and the cooldown spend
 *      (RoleAttributePlayer.ReSetSkillReload). C03's RECOVERED body does NOT: the
 *      only recoverable instructions are the null-guard and the get_transform
 *      tail-call on this+0x40 (an owner-side Component, distinct from the
 *      role_attribute at 0x44). The in_skill-clear + ReSetSkillReload spend that a
 *      faithful RoleSkillEnd must perform live in the truncated tail / base
 *      RGController.RoleSkillEnd chain and are NOT visible in THIS body. We model
 *      that spend in EndSkill() (in_skill = false; this_skill_time = 0) and FLAG it
 *      as a clearly-marked assumption (see fabrication_flags), exactly as C01/C02
 *      discipline requires -- never silently inlined.
 *
 * Determinism: NONE of the five recovered bodies draws from rg_random, so this
 * unit makes ZERO RNG draws (heroes are player-driven). The RGRandom member is
 * carried for interface parity with the project template and to keep the
 * deterministic stream untouched; Seeded() lets a caller confirm the seed without
 * ever advancing it.
 *
 * fabrication_flags (every reused-pattern / inherited-effect assumption):
 *   - CharSkillC03::CooldownModel -- the skill_cd / this_skill_time count-up +
 *     spend timer is the established PlayerDash / CharSkillC01 reconstruction
 *     reused to make TryActivateSkill / EndSkill unit-testable. C03's RoleSkill
 *     body only shows the get_skill_ready GATE, never the timer itself.
 *   - CharSkillC03::EndSkillSpend -- the in_skill-clear (this+0x55 = 0) and the
 *     ReSetSkillReload cooldown spend (this_skill_time = 0) modeled by EndSkill()
 *     are NOT in C03's recovered RoleSkillEnd body (truncated tail-call on 0x40);
 *     they are the base RGController.RoleSkillEnd chain (made explicit in C02's
 *     fully-recovered RoleSkillEnd) reused here as a flagged assumption.
 *   - CharSkillC03::StartsReady -- starting this_skill_time == skill_cd (first
 *     ultimate available on a freshly SetUpChar hero) is the PlayerDash/C01
 *     convention, not a value read from C03's bodies.
 *
 * @see recreation Player/RGController.cs (offsets: 0xc awake, 0x44 role_attribute,
 *      0x55 in_skill; AttributeUpdate -> ArmorReload+SkillReload on 0x44),
 *      RoleAttributePlayer.cs (skill_cd / skill_ready / ReSetSkillReload),
 *      PlayerDash.hpp + CharSkillC01 + CharSkillC02 (the canonical skill-cooldown
 *      pattern + the no-auto-end Update shape), IL2CPP C03Controller.cs
 *      (thunder GameObject; RoleSkill/CreateTunder/RoleSkillEnd overrides).
 */
class CharSkillC03 {
public:
    /**
     * @brief Construct the skill brain from the hero's skill timing stat.
     * @param skillCd cooldown length in seconds (RoleAttributePlayer.skill_cd).
     *               Clamped to >= 0. From the CharacterDef stat sheet.
     *
     * Starts READY (this_skill_time == skill_cd) and NOT in skill -- a freshly
     * set-up hero whose first ultimate is available (matches PlayerDash / C01 /
     * C02; flagged as StartsReady).
     *
     * NOTE: like CharSkillC02 (and unlike CharSkillC01), C03 takes NO in_skill_time
     * window length: C03's Update has no in_skill_time countdown / auto-end (see
     * header doc), so the active window is closed only by the explicit EndSkill()
     * (RoleSkillEnd) path.
     */
    explicit CharSkillC03(float skillCd);

    /// Seed the deterministic stream (kept untouched; no draw is ever made).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief Attempt to activate the hero ultimate (summon thunder) this frame.
     *
     * FAITHFUL: C03Controller__RoleSkill @ 156214 gate -- requires
     * awake && skill_ready && !in_skill (the awake check is the caller's
     * responsibility; the gate this unit owns is skill_ready && !in_skill, exactly
     * the recovered nested-if at 156229/156235/156239). On success the hero enters
     * the skill state (in_skill = true). The hero-specific thunder spawn
     * (CreateTunder / get_transform tail-call) and the actual cooldown spend are
     * owner/EndSkill concerns.
     *
     * @return true if the skill activated this call (state entered), else false.
     */
    bool TryActivateSkill();

    /**
     * @brief End the active skill window: leave the skill state, spend the charge.
     *
     * Models the base RGController.RoleSkillEnd chain -- the in_skill clear
     * (this+0x55 = 0) and the cooldown spend (RoleAttributePlayer.ReSetSkillReload:
     * this_skill_time = 0). FLAGGED ASSUMPTION (EndSkillSpend): C03's recovered
     * RoleSkillEnd body (@ 156258) does NOT contain these instructions -- it shows
     * only a null-guard + get_transform tail-call on this+0x40 (an owner-side
     * Component). The spend is reconstructed from the base chain (explicit in C02's
     * fully-recovered RoleSkillEnd), never read from C03's body. The get_transform
     * effect on 0x40 is an owner concern.
     * No-op if not currently in the skill.
     */
    void EndSkill();

    /**
     * @brief One awake-frame tick: advance ONLY the cooldown (no auto-end).
     *
     * FAITHFUL: C03Controller__Update @ 156201 (fully recovered, pure):
     *   if (awake) { AttributeUpdate() -> SkillReload(dt): this_skill_time counts
     *   UP, clamped to skill_cd; SeachUpdate() (owner aim re-acquire). }
     * There is NO in_skill_time countdown and NO auto-end in C03's Update (matching
     * C02, unlike C01) -- so Tick advances the cooldown and NEVER ends the skill.
     * The active window is closed only by the explicit EndSkill() (RoleSkillEnd)
     * path. NO RGRandom draw.
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

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};             ///< deterministic stream; never advanced on this path.
    float m_SkillCd = 0.0F;       ///< skill_cd (RoleAttributePlayer), cooldown length.
    float m_ThisSkillTime = 0.0F; ///< this_skill_time; counts up to skill_cd.
    bool m_InSkill = false;       ///< in_skill (this+0x55); ultimate active flag.
};

} // namespace Game

#endif /* GAME_CHAR_SKILL_C03_HPP */
