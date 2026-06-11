#ifndef GAME_PLAYER_DASH_HPP
#define GAME_PLAYER_DASH_HPP

#include <glm/glm.hpp>

namespace Game {

/**
 * @class PlayerDash
 * @brief Faithful, engine-free cooldown + active-state machine for the hero
 *        dash/skill (the RGController.RoleSkill -> RoleSkillEnd loop over the
 *        RoleAttributePlayer skill-reload timer + the RGBaseController.GetForce
 *        impulse).
 *
 * WHAT THE ORIGINAL DOES (all grounded in the decompiled bodies):
 *
 *  - COOLDOWN ENGINE (RoleAttributePlayer.SkillReload(dt) @ game_full.c:432444,
 *    get_skill_ready @ 432483, ReSetSkillReload @ 432381):
 *      this_skill_time (role+0x5C) counts UP each frame:
 *        if (this_skill_time < skill_cd) this_skill_time = min(this_skill_time+dt, skill_cd);
 *      the skill is READY when  skill_cd <= this_skill_time  (get_skill_ready).
 *      Spending the charge is ReSetSkillReload(): this_skill_time = 0.
 *      (ReflashSkillCd sets this_skill_time = skill_cd for an instant recharge.)
 *
 *  - ACTIVATE (every C0xController.RoleSkill @ 155986.. shares one gate):
 *      requires awake (this+0xC) && skill_ready && !in_skill (this+0x55).
 *      On success the hero enters the skill (in_skill = true) and runs the
 *      hero-specific effect; the dash characters apply their impulse on END.
 *
 *  - DASH IMPULSE + RE-ARM (C02Controller.RoleSkillEnd @ 156134, the canonical
 *    dash, fully recovered):
 *      GetForce(move_dir, power);   // launch along the current move direction
 *      friction -= 0.15f;           // ease the inertia decay so the dash carries
 *      in_skill = false;            // leave the skill state
 *      ReSetSkillReload();          // restart the cooldown (this_skill_time = 0)
 *    GetForce (RGBaseController.GetForce @ 467560) stores force_direction = dir
 *    and inertial_vel = min(power, 30f) -- the hard force cap is 30. SetVelocity
 *    then plays the impulse: while inertial_vel > 1 the body moves at
 *    force_direction * inertial_vel and inertial_vel *= min(1, friction) each
 *    FixedUpdate (see RGController.SetVelocity @ 471833).
 *
 * MODEL HERE: TryDash(dir) succeeds only when off cooldown (skill_ready) and not
 * already dashing; it latches the dash impulse (capped at kForceCap), enters the
 * active window for @p inSkillTime, and on Tick() reaching the end of that window
 * it re-arms the cooldown (this_skill_time = 0). Tick(dtMs) advances both the
 * active window and the cooldown. CurrentDashVelocity() exposes the per-step
 * impulse contribution (force_direction * inertial_vel) for the owning entity to
 * feed into its Rigidbody, matching SetVelocity's knockback branch.
 *
 * Determinism: the dash is fully deterministic (no RGRandom draw on this path in
 * any recovered body), so this unit carries no RNG. Replays line up because the
 * cooldown and active window are purely time-based.
 *
 * The engine/animation/transform wiring (the actual Rigidbody2D.velocity write,
 * the skill animation/SFX, reading the live move_dir) is left to the owning
 * entity; this is the cooldown/state brain only.
 *
 * @see recreation RGController.cs / RGBaseController.cs; FAITHFUL notes per
 *      method below. skill_cd / in_skill_time come from the CharacterDef stat
 *      sheet (Resources/data/characters.json: skill_cd, in_skill_time) and are
 *      passed in -- never hardcoded.
 */
class PlayerDash {
public:
    /// Hard impulse cap from RGBaseController.GetForce @ 467560:
    /// `if (30.0 < power) power = 30.0;`. Immediate-operand 30.0f in the binary.
    static constexpr float kForceCap = 30.0F;

    /// Friction reduction applied on the dash (C02.RoleSkillEnd: friction -= 0.15f).
    /// Exposed for the owning entity that owns the friction field; this unit does
    /// not run the SetVelocity decay itself.
    static constexpr float kDashFrictionDelta = -0.15F;

    /// inertial_vel must exceed this for SetVelocity to play the impulse (the
    /// `inertial_vel > 1f` knockback branch in RGController.SetVelocity @ 471833).
    static constexpr float kInertiaActiveThreshold = 1.0F;

    /**
     * @brief Construct the dash brain from the hero's CharacterDef stats.
     * @param skillCd      seconds the cooldown takes to recharge (RoleAttributePlayer
     *                     skill_cd, role+0x44). >= 0; characters.json skill_cd.
     * @param inSkillTime  seconds the active dash window lasts (RoleAttributePlayer
     *                     in_skill_time, role+0x48). >= 0; characters.json in_skill_time.
     *
     * Starts READY (this_skill_time = skill_cd), matching a freshly set-up hero
     * whose first skill is available.
     */
    PlayerDash(float skillCd, float inSkillTime);

    /**
     * @brief Attempt to start a dash this frame.
     *
     * FAITHFUL: the C0xController.RoleSkill gate (skill_ready && !in_skill) +
     * RGBaseController.GetForce (force_direction = dir, inertial_vel = min(power, 30)).
     *
     * Succeeds only when SkillReady() and not already dashing. On success: latches
     * force_direction = normalized @p dir, inertial_vel = min(@p force, kForceCap),
     * enters the active window (m_InSkill = true) for in_skill_time. The cooldown
     * is restarted at the END of the window (ReSetSkillReload in RoleSkillEnd), so
     * the active duration is "free" -- exactly the original's End-driven re-arm.
     *
     * @param dir    the dash direction (the hero's current move_dir). Normalized
     *               internally; a (near-)zero direction yields a zero impulse but
     *               the dash still consumes the cooldown, matching GetForce of a
     *               zero move_dir.
     * @param force  the dash impulse magnitude (the `power` passed to GetForce).
     *               Exposed as a parameter because the concrete magnitude is not
     *               in the recovered RoleSkillEnd body (see manual flags); capped
     *               at kForceCap. Pass the game's value.
     * @return true if the dash started this call.
     */
    bool TryDash(glm::vec2 dir, float force);

    /**
     * @brief Advance the active window and the cooldown by @p dtMs milliseconds.
     *
     * FAITHFUL: RoleAttributePlayer.SkillReload(dt) for the cooldown
     * (this_skill_time += dt, clamped to skill_cd) and the End-of-skill re-arm
     * (RoleSkillEnd: ReSetSkillReload, this_skill_time = 0).
     *
     * While dashing, the active timer accumulates; when it reaches in_skill_time
     * the dash ends (m_InSkill = false) and the cooldown is RESET to 0 (it then
     * recharges from 0 on subsequent ticks). While not dashing, the cooldown
     * recharges: this_skill_time = min(this_skill_time + dt, skill_cd).
     *
     * @param dtMs frame delta in milliseconds (>= 0).
     */
    void Tick(float dtMs);

    /// @return true while the dash active window is running (in_skill, this+0x55).
    bool IsDashing() const { return m_InSkill; }

    /**
     * @return true when the skill is off cooldown and ready to fire.
     * FAITHFUL: get_skill_ready @ 432483 -- skill_cd <= this_skill_time.
     */
    bool SkillReady() const { return m_ThisSkillTime >= m_SkillCd; }

    /**
     * @return seconds until the cooldown finishes recharging (0 when ready).
     * Derived from this_skill_time vs skill_cd. NOTE: the cooldown is only
     * restarted (this_skill_time = 0) at RoleSkillEnd, so during the active dash
     * window this still reports 0 -- the in_skill gate (not the cooldown) is what
     * blocks a re-fire mid-dash. The full skill_cd appears once the window ends.
     */
    float CooldownRemaining() const;

    /// Seconds remaining in the active dash window (0 when not dashing).
    float DashTimeRemaining() const { return m_InSkill ? (m_InSkillTime - m_ActiveElapsed) : 0.0F; }

    /**
     * @brief Per-step dash velocity contribution: force_direction * inertial_vel.
     *
     * FAITHFUL: RGController.SetVelocity knockback branch (velocity =
     * force_direction * inertial_vel while inertial_vel > 1). Returns the zero
     * vector once the impulse has decayed to/below kInertiaActiveThreshold or the
     * dash is not active. The owning entity is responsible for writing this into
     * its Rigidbody and for the per-FixedUpdate inertial_vel *= min(1, friction)
     * decay (that decay lives in SetVelocity, not on this logic-only unit).
     */
    glm::vec2 CurrentDashVelocity() const;

    /// Current latched impulse magnitude (inertial_vel, role-side this+0x30).
    float InertialVel() const { return m_InertialVel; }
    /// Current latched impulse direction (force_direction, this+0x34), unit or zero.
    glm::vec2 ForceDirection() const { return m_ForceDirection; }

    /**
     * @brief Decay the active impulse by one SetVelocity step (inertial_vel *= f).
     *
     * Optional helper mirroring SetVelocity's `inertial_vel *= min(1, friction)`.
     * Engine wiring usually owns this; provided so callers/tests can drive the
     * exact decay. Clamps the factor to <= 1 as the original does.
     */
    void DecayImpulse(float frictionFactor);

private:
    float m_SkillCd;        ///< skill_cd (role+0x44), seconds. Cooldown length.
    float m_InSkillTime;    ///< in_skill_time (role+0x48), seconds. Active window.
    float m_ThisSkillTime;  ///< this_skill_time (role+0x5C). Counts up to skill_cd.
    bool m_InSkill = false; ///< in_skill (this+0x55). Dash active window flag.
    float m_ActiveElapsed = 0.0F;   ///< elapsed seconds inside the active window.
    float m_InertialVel = 0.0F;     ///< inertial_vel (this+0x30). Impulse magnitude.
    glm::vec2 m_ForceDirection{0.0F, 0.0F}; ///< force_direction (this+0x34).
};

} // namespace Game

#endif /* GAME_PLAYER_DASH_HPP */
