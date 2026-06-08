#ifndef GAME_CHAR_SKILL_C09_HPP
#define GAME_CHAR_SKILL_C09_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class CharSkillC09
 * @brief Faithful, engine-free skill/attack brain for hero C09 (the bow/archer
 *        hero: C09Controller, an RGController player subclass).
 *
 * Per-content port. Models ONLY the pure, unit-testable decisions recovered in
 * the C09Controller bodies; the actual animator/SFX/arrow-spawn/transform wiring
 * is left to the owning entity (referenced in comments only).
 *
 * WHAT THE DECOMP SHOWS (every modeled branch grounded; no reconstruction of
 * unseen ability effects):
 *
 *  - RoleSkill (C09Controller__RoleSkill @ game_full.c:157300) -- a TOGGLE, NOT
 *    the plain C01 enter-only gate:
 *        if (!awake) return;                       // this+0xC == param_1[3]
 *        if (in_skill) { RoleSkillEnd(); return; } // this+0x55 -> vtable slot 0x17c
 *        if (role_attribute.skill_ready == 1) {    // this+0x44 == param_1[0x11]
 *          CreateArrow();                          // owner: animator bools only
 *          get_transform(...);                     // hero-specific spawn (truncated)
 *        }
 *        anim.SetBool("...", 1);                   // owner (animator)
 *      So when the bow is ALREADY drawn (in_skill), the skill button RELEASES the
 *      bow (RoleSkillEnd) -- it does not re-charge. When NOT drawn, it draws iff
 *      skill_ready. NO RGRandom draw. The actual arrow spawn (get_transform tail)
 *      and the in_skill = true latch live past the recoverable head, so we expose
 *      Draw()/Release() and FLAG the in_skill latch + ReSetSkillReload spend as the
 *      established CharSkillC01 cooldown-reuse assumption (see fabrication_flags).
 *
 *  - RoleAtk (C09Controller__RoleAtk @ game_full.c:157566, value = press down):
 *        if (!awake) return;                       // this+0xC
 *        if (in_item && press) {                   // get_in_item == 1 && value == 1
 *          if (!in_skill) { TriggerItem(); return; } // pickup use (only when NOT drawn)
 *          // (in_skill: fall through to the bow-shot block)
 *        } else if (!in_skill) goto normalHand;    // skip bow block unless drawn
 *        // bow-shot block -- reached ONLY while in_skill:
 *        if (role_attribute.skill_ready == 1)      // this+0x44
 *          if (the_bullet != null)                 // this+0xA0 == param_1[0x28]
 *            { ArrowShoot(); return; }             // loose the nocked arrow
 *      normalHand:
 *        if (press && weaponHasFrontGun && !IsMelee)
 *          <aim-nudge get_position>                // truncated tail (owner)
 *        hand.SetAttack(value);                    // this+0x10: primary hand fires/stops
 *      So the bow-shot path is GATED ON in_skill (the drawn state): only while the
 *      bow is drawn does a press loose an arrow (when charged + a bullet is nocked);
 *      otherwise the press toggles the normal weapon. NO RGRandom draw.
 *
 *  - Update (C09Controller__Update @ game_full.c:157193): awake gate ->
 *    AttributeUpdate() (cooldown count-up, the same SkillReload as C01) -> an
 *    aim-angle computation (switch_axis_mode / Vector2.Angle / set_localEulerAngles,
 *    all truncated get_transform tails -- owner) -> SeachUpdate() (owner).
 *    IMPORTANT: UNLIKE C01/C10, C09's Update has NO in_skill_time countdown and NO
 *    auto-end -- param_1[0x25] here is the 'aim' transform, not a timer. The drawn
 *    bow does NOT self-terminate on a timer; it is released by pressing skill again
 *    (RoleSkill toggle) or by RoleSkillEnd. So Tick() advances ONLY the cooldown;
 *    it deliberately does NOT auto-end the skill (that would be fabricated logic).
 *
 *  - GetHurt / KillSomeOne / SetCameraFocus / AimUpDate / CreateArrow /
 *    RoleSkillEnd / ArrowShoot: pure owner side (animator bools, camera child
 *    localPosition, strengthen-gated VFX, base-class chain). No brain logic; one
 *    -line owner comments only. The skill_strengthen reads gate animator state and
 *    a strengthened-bow flag, not a decision this brain owns.
 *
 * Determinism: no recovered C09 body draws from rg_random, so this unit makes NO
 * RNG draws. The RGRandom member is carried for template parity and to keep the
 * deterministic stream untouched; Seeded() lets a caller confirm the seed without
 * ever advancing it.
 *
 * @see recreation Player/RGController.cs (player offsets 0xC awake / 0x44
 *      role_attribute / 0x55 in_skill / vtable 0x17c RoleSkillEnd), CharSkillC01
 *      (cooldown-reuse + fabrication-flag conventions), IL2CPP C09Controller.cs
 *      (field names: aim, aim_p, skill_arrow, the_bullet, switch_axis_mode).
 *
 * fabrication_flags (reused-pattern assumptions, called out exactly as CharSkillC01):
 *   - [CharSkillC09-CD] The skill_cd / this_skill_time cooldown engine is the
 *     established PlayerDash/CharSkillC01 model reused so Draw()/Release() are
 *     unit-testable. The recovered C09 body reads skill_ready but the count-up
 *     itself happens in the base RoleAttributePlayer.SkillReload (AttributeUpdate).
 *   - [CharSkillC09-LATCH] in_skill = true on a successful Draw() is the base
 *     RGController.RoleSkill latch; it is NOT in C09's recovered head (which
 *     truncates at the get_transform arrow spawn). Modeled, flagged.
 *   - [CharSkillC09-SPEND] Release()/RoleSkillEnd restarting the cooldown
 *     (ReSetSkillReload, this_skill_time = 0) is the base RGController chain, not
 *     in C09's RoleSkillEnd body (which only flips an animator bool). Modeled,
 *     flagged. (Mirrors CharSkillC01.EndSkill.)
 */
class CharSkillC09 {
public:
    /**
     * @brief Construct the bow brain from the hero's skill timing stat.
     * @param skillCd cooldown length in seconds (RoleAttributePlayer skill_cd).
     *                Clamped to >= 0. From the CharacterDef stat sheet.
     *
     * Starts READY (this_skill_time == skill_cd) and with the bow NOT drawn
     * (in_skill == false) -- a freshly set-up archer whose first draw is available
     * (matches PlayerDash / CharSkillC01). C09 has NO in_skill_time window: the
     * drawn state persists until released (see class doc), so there is no
     * active-window length to configure.
     */
    explicit CharSkillC09(float skillCd);

    /// Seed the deterministic stream (kept untouched; no draw is ever made).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /// Result of pressing the skill button (RoleSkill toggle), so the owner knows
    /// which side effect to run. Exactly one of the booleans below is true when the
    /// press did something; all false means the press was swallowed (e.g. not
    /// charged) or the hero is not awake (the awake gate is the caller's job).
    struct SkillDecision {
        /// in_skill was set: draw the bow (CreateArrow + arrow spawn; owner).
        bool drewBow = false;
        /// in_skill was cleared via RoleSkillEnd (slot 0x17c): release the bow.
        bool releasedBow = false;
    };

    /**
     * @brief Press the skill button (C09Controller__RoleSkill @ 157300).
     *
     * FAITHFUL toggle: if the bow is already drawn (in_skill) -> RELEASE it
     * (RoleSkillEnd, slot 0x17c); else if skill_ready -> DRAW it. The hero-specific
     * arrow spawn (get_transform tail) is the owner's job. The in_skill latch and
     * the cooldown spend on release are the flagged base-class reuse
     * ([CharSkillC09-LATCH] / [CharSkillC09-SPEND]).
     *
     * @return what happened, for the owner to act on (both-false if neither drawn
     *         nor released this press).
     */
    SkillDecision RoleSkill();

    /**
     * @brief End the drawn-bow state and spend the charge.
     *
     * Models RGController.RoleSkillEnd -> RoleAttributePlayer.ReSetSkillReload (the
     * C09 RoleSkillEnd body itself only flips an animator bool; the spend is the
     * base chain -- [CharSkillC09-SPEND]): clears in_skill and restarts the cooldown
     * (this_skill_time = 0). No-op if the bow is not drawn.
     */
    void Release();

    /// What an attack press/release should do, reported for the owner to execute.
    struct AtkDecision {
        /// in_item && press && !in_skill -> the press triggers the pickup, returns.
        bool triggerItem = false;
        /// While the bow is drawn (in_skill) && skill_ready && a bullet is nocked
        /// (the_bullet != null), the press looses the arrow (ArrowShoot) and returns.
        bool arrowShoot = false;
        /// Should hand.SetAttack(value) be issued this call on the primary hand?
        /// (false on the triggerItem / arrowShoot early-outs; true otherwise.)
        bool setHandAttack = false;
        /// The value passed to hand.SetAttack (== pressDown when setHandAttack).
        bool handAttackValue = false;
    };

    /**
     * @brief Process an attack press/release (C09Controller__RoleAtk @ 157566).
     *
     * FAITHFUL branch order: awake gate (caller's job) ->
     *   (in_item && press) ? (in_skill ? bow-block : TriggerItem+return)
     *                      : (in_skill ? bow-block : normalHand)
     * where the bow-block is: skill_ready && the_bullet != null -> ArrowShoot+return.
     * The truncated press/non-melee aim-nudge (get_position) carries no recoverable
     * pure logic and is not modeled. Then hand.SetAttack(value) on the primary hand.
     * NO RGRandom draw.
     *
     * @param pressDown      true = attack button down (value==1), false = release.
     * @param standingOnItem true if standing on a pickup (RGController.in_item).
     * @param bulletNocked   true if the_bullet (this+0xA0) is non-null -- an arrow
     *                       is nocked and ready to loose. Owner-supplied state.
     * @return the decision the owning entity should act on (all-false if !awake is
     *         the caller's concern; this brain assumes awake).
     */
    AtkDecision RoleAtk(bool pressDown, bool standingOnItem, bool bulletNocked) const;

    /**
     * @brief One awake-frame tick: advance the cooldown ONLY.
     *
     * FAITHFUL: C09Controller__Update @ 157193 -- awake gate -> AttributeUpdate()
     * -> SkillReload(dt): this_skill_time counts UP, clamped to skill_cd, EVERY
     * awake frame (NOT frozen while the bow is drawn -- the in_skill aim branch sits
     * after AttributeUpdate and contains NO timer). UNLIKE C01/C10, C09's Update has
     * NO in_skill_time countdown and NO auto-end, so Tick() deliberately does NOT
     * end the drawn bow on a timer (that would be fabricated). NO RGRandom draw.
     *
     * @param dtMs frame delta in milliseconds (<= 0 is ignored).
     */
    void Tick(float dtMs);

    /// @return true while the bow is drawn (in_skill, this+0x55).
    bool InSkill() const { return m_InSkill; }

    /**
     * @return true when off cooldown and ready to draw.
     * FAITHFUL: RoleAttributePlayer.get_skill_ready -- skill_cd <= this_skill_time.
     */
    bool SkillReady() const { return m_ThisSkillTime >= m_SkillCd; }

    /// @return seconds until the cooldown finishes recharging (0 when ready).
    float CooldownRemaining() const;

    /// Configured cooldown length (skill_cd), seconds.
    float SkillCd() const { return m_SkillCd; }

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};             ///< deterministic stream; never advanced here.
    float m_SkillCd = 0.0F;       ///< skill_cd (RoleAttributePlayer), cooldown length.
    float m_ThisSkillTime = 0.0F; ///< this_skill_time; counts up to skill_cd.
    bool m_InSkill = false;       ///< in_skill (this+0x55); bow-drawn flag.
};

} // namespace Game

#endif /* GAME_CHAR_SKILL_C09_HPP */
