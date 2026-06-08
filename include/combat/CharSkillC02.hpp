#ifndef GAME_CHAR_SKILL_C02_HPP
#define GAME_CHAR_SKILL_C02_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class CharSkillC02
 * @brief Faithful, engine-free skill/attack brain for hero C02 (a dash/charge
 *        hero: C02Controller, an RGController player subclass).
 *
 * Per-content port. Models ONLY the pure, unit-testable decisions in the five
 * recovered C02Controller bodies; the actual animator/SFX/transform/physics
 * (GetForce impulse) wiring is left to the owning entity (referenced in comments
 * only).
 *
 * WHAT THE DECOMP SHOWS (all five bodies grounded; no reconstruction of unseen
 * ability effect):
 *
 *  - _ctor (C02Controller___ctor @ game_full.c:156073):
 *      Pure il2cpp boilerplate -> RGController___ctor(this, 0). OWNER concern,
 *      no brain logic.
 *
 *  - Update (C02Controller__Update @ game_full.c:156091, fully recovered, pure):
 *      if (awake) {                                  // this+0xc
 *        AttributeUpdate();                          // -> SkillReload(): cooldown
 *                                                    //    count-up, run EVERY awake
 *                                                    //    frame, UNCONDITIONALLY.
 *        SeachUpdate();                              // aim re-acquire (owner concern)
 *      }
 *      CRUCIAL CONTRAST WITH C01: C02's Update has NO in_skill_time countdown and
 *      NO auto-end branch -- it is JUST the awake-gated AttributeUpdate+SeachUpdate.
 *      The C02 skill ends only through the explicit RoleSkillEnd path (slot 0x17c),
 *      never on a self-expiring timer. Modeling an auto-end here would be
 *      fabrication, so Tick() advances ONLY the cooldown and never auto-ends.
 *      NO RGRandom draw.
 *
 *  - RoleSkill (C02Controller__RoleSkill @ game_full.c:156104):
 *      The hero ultimate gate, identical in shape to the base RGController one:
 *        if (awake)                                  // this+0xc
 *          if (role_attribute.skill_ready)           // this+0x44 -> get_skill_ready==1
 *            if (!in_skill)                           // this+0x55 == 0
 *              <enter skill>                          // get_transform tail-call truncated
 *      The post-gate effect is a get_transform tail-call (the hero-specific dash
 *      spawn / charge windup), so only the GATE is recoverable. NO RGRandom draw.
 *      The cooldown engine (skill_cd / this_skill_time count-up + spend) is the
 *      established PlayerDash / CharSkillC01 pattern; we REUSE that timer model so
 *      TryActivateSkill can be unit-tested end to end (see fabrication_flags).
 *
 *  - RoleSkillEnd (C02Controller__RoleSkillEnd @ game_full.c:156134, pure):
 *      get_move_dir(&dir);                           // current move direction
 *      GetForce(dir, mag);                           // forward DASH impulse (owner physics)
 *      friction(this+0x24) -= 0.15f;                 // lower inertia decay so the dash carries
 *      in_skill(this+0x55) = 0;                      // leave the skill state
 *      ReSetSkillReload();                           // SPEND the charge (restart cooldown)
 *      UpdateShadowLock();                           // refresh targeting marker tint (owner)
 *      So C02's ultimate is a friction-reduced forward dash: the brain logic here
 *      is the friction decrement, the in_skill clear, and the cooldown spend. The
 *      GetForce impulse and UpdateShadowLock tint are owner concerns. NO RGRandom
 *      draw. (friction this+0x24 = RGBaseController.friction; -0.15 == immediate
 *      operand 0xbe19999a == -0.15f in the binary.)
 *
 *  - EndSkillShoot (C02Controller__EndSkillShoot @ game_full.c:156155, pure):
 *      if (!skill_shoot) return;                     // this+0x90 (C02-specific bool)
 *      if (role_attribute.skill_strengthen)          // get_skill_strengthen == 1
 *        role_attribute[+0x24] -= 50;                // 0x32; strengthen-only resource cost
 *      skill_shoot(this+0x90) = 0;                   // consume the shoot flag
 *      A one-shot, skill_shoot-gated end-of-shoot hook: when the hero's skill is
 *      STRENGTHENED it pays a fixed 50-unit resource cost out of a RoleAttribute
 *      int field, then clears the shoot flag. The gate, the strengthen-conditional
 *      50-cost, and the flag-clear are the brain logic. NO RGRandom draw.
 *
 * Determinism: NONE of the five recovered bodies draws from rg_random, so this
 * unit makes ZERO RNG draws (heroes are player-driven). The RGRandom member is
 * carried for interface parity with the project template and to keep the
 * deterministic stream untouched; Seeded() lets a caller confirm the seed without
 * ever advancing it.
 *
 * @see recreation Player/RGController.cs + RGBaseController.cs (offsets: 0xc awake,
 *      0x24 friction, 0x44 role_attribute, 0x55 in_skill), RoleAttributePlayer.cs
 *      (skill_cd / in_skill_time / skill_ready / ReSetSkillReload / skill_strengthen),
 *      PlayerDash.hpp + CharSkillC01 (the canonical skill-cooldown pattern).
 */
class CharSkillC02 {
public:
    /// Friction (this+0x24) reduction applied on RoleSkillEnd so the dash carries.
    /// FAITHFUL: immediate operand 0xbe19999a == -0.15f in C02Controller__RoleSkillEnd.
    static constexpr float kSkillEndFrictionDrop = 0.15F;

    /// Strengthen-only resource cost paid in EndSkillShoot (role_attribute[+0x24] -= 50).
    /// FAITHFUL: immediate operand 0x32 == 50 in C02Controller__EndSkillShoot.
    static constexpr int kStrengthenedShootCost = 50;

    /**
     * @brief Construct the skill brain from the hero's skill timing stats.
     * @param skillCd cooldown length in seconds (RoleAttributePlayer.skill_cd).
     *               Clamped to >= 0. From the CharacterDef stat sheet.
     *
     * Starts READY (this_skill_time == skill_cd) and NOT in skill -- a freshly
     * set-up hero whose first ultimate is available (matches PlayerDash / C01).
     *
     * NOTE: unlike CharSkillC01, C02 takes NO in_skill_time window length: C02's
     * Update has no in_skill_time countdown / auto-end (see header doc), so the
     * active window is closed only by the explicit RoleSkillEnd path.
     */
    explicit CharSkillC02(float skillCd);

    /// Seed the deterministic stream (kept untouched; no draw is ever made).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief Attempt to activate the hero ultimate this frame.
     *
     * FAITHFUL: C02Controller__RoleSkill @ 156104 gate -- requires
     * awake && skill_ready && !in_skill. On success the hero enters the skill
     * state (in_skill = true). The hero-specific dash spawn (get_transform
     * tail-call) and the actual cooldown spend are owner/RoleSkillEnd concerns.
     *
     * @return true if the skill activated this call (state entered), else false.
     */
    bool TryActivateSkill();

    /**
     * @brief End the active skill window: dash, drop friction, spend the charge.
     *
     * FAITHFUL: C02Controller__RoleSkillEnd @ 156134. Models the pure portion:
     * the friction reduction (this+0x24 -= 0.15), the in_skill clear (this+0x55 = 0)
     * and the cooldown spend (RoleAttributePlayer.ReSetSkillReload(): this_skill_time
     * = 0). The forward GetForce dash impulse and the UpdateShadowLock tint are
     * owner concerns (see RoleSkillEndDecision for what the owner must do).
     * No-op if not currently in the skill.
     *
     * @return the side effects the owning entity must apply (all-false when no-op).
     */
    struct RoleSkillEndDecision {
        /// The skill was active and is now ending (owner should run the dash now).
        bool ended = false;
        /// Owner must apply the forward GetForce impulse along move_dir (dash).
        bool applyDashImpulse = false;
        /// Owner must call UpdateShadowLock() to refresh the marker tint.
        bool updateShadowLock = false;
    };
    RoleSkillEndDecision EndSkill();

    /**
     * @brief End-of-shoot hook (C02Controller__EndSkillShoot @ 156155).
     *
     * FAITHFUL: gated by skill_shoot (this+0x90). When the hero's skill is
     * STRENGTHENED (role_attribute.skill_strengthen), pays a fixed 50-unit cost out
     * of the RoleAttribute resource field (this+0x24), then clears skill_shoot.
     * No-op (returns false) when skill_shoot is not set.
     *
     * @param skillStrengthened RoleAttributePlayer.get_skill_strengthen() == 1.
     * @return the resource delta the owner must apply this call.
     */
    struct EndShootDecision {
        /// skill_shoot was set: the hook fired (and skill_shoot is now cleared).
        bool fired = false;
        /// Resource units to subtract from the RoleAttribute field (0 or 50).
        /// FAITHFUL: 50 only when strengthened, else 0.
        int resourceCost = 0;
    };
    EndShootDecision EndSkillShoot(bool skillStrengthened);

    /// Arm the one-shot skill_shoot flag (this+0x90). Owner sets this when the
    /// skill fires its shot; EndSkillShoot consumes it. Exposed so the gated
    /// EndSkillShoot logic is unit-testable.
    void SetSkillShoot(bool on) { m_SkillShoot = on; }
    /// @return the live skill_shoot flag (this+0x90).
    bool SkillShoot() const { return m_SkillShoot; }

    /**
     * @brief One awake-frame tick: advance ONLY the cooldown (no auto-end).
     *
     * FAITHFUL: C02Controller__Update @ 156091 (fully recovered, pure):
     *   if (awake) { AttributeUpdate() -> SkillReload(dt): this_skill_time counts
     *   UP, clamped to skill_cd; SeachUpdate() (owner aim re-acquire). }
     * There is NO in_skill_time countdown and NO auto-end in C02's Update (unlike
     * C01) -- so Tick advances the cooldown and NEVER ends the skill. The active
     * window is closed only by the explicit EndSkill() (RoleSkillEnd) path.
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
    bool m_SkillShoot = false;    ///< skill_shoot (this+0x90); one-shot end-of-shoot gate.
};

} // namespace Game

#endif /* GAME_CHAR_SKILL_C02_HPP */
