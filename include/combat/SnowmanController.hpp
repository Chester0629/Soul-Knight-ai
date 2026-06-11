#ifndef GAME_SNOWMAN_CONTROLLER_HPP
#define GAME_SNOWMAN_CONTROLLER_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class SnowmanController
 * @brief Faithful decision/state brain for the Snowman summon (an RGPetController
 *        subclass, NOT a plain RGEController enemy).
 *
 * SnowmanController is a deployable pet/turret: it stands, tracks its master, and
 * fires a reflection bullet on a scheduled cadence. Its decomp (game_full.c:
 * 1652651-1652903) is overwhelmingly Unity-side: Instantiate<RGWeapon>, Animator
 * SetTrigger/SetBool, MonoBehaviour.Invoke, Transform reads, and Vector2 angle
 * math. There are ZERO rg_random draws anywhere in the class -- a Snowman is fully
 * deterministic without consuming the stream. What remains pure and unit-testable
 * is a handful of GATES and small scalar/flag writes:
 *
 *   - ShootReflection(): the dead/destroyed gate (field 0x0d) early-return, and
 *     the single recoverable state write -- byte field 0x71 = 0 (a re-arm/latch
 *     flag the ctor initialises to 1). Owner: Invoke("OnAtk", hand@0x6c),
 *     anim.SetTrigger("tap_atk") and SetTrigger(...) (lines 1652680-1652690).
 *   - Scout(): the paused gate (field 0x44 == 1 -> early-return), and the target
 *     refresh target(0x1c) = master(0x74). Owner: the two virtual dispatches
 *     (FixedRotation / ShootReflection cadence) and the transform read.
 *   - GetHurt(): the dead/destroyed gate (field 0x0d): a live snowman routes the
 *     hit to UICanvas (floating damage number); a dead one ignores it. No vitals
 *     are touched in THIS override (Pet HP lives in role_attribute, applied by the
 *     base RGPetController chain).
 *   - OnGameStateChange(): the only fully-pure body. Gated on active (field 0x0c).
 *     game_state == 2 (resume): clear paused (0x44 = 0) and boost the pet's OWN
 *     role_attribute.speed_rate (controller field@0x40 + 0x14) by +0.5. The decomp
 *     reads p+0x40 from the controller itself (param_1), the same RoleAttribute
 *     RGPetController__get_attribute@427175 returns and FixedUpdate@427251 reads as
 *     the pet's speed multiplier -- NOT the master pointer at 0x74. game_state == 1
 *     (pause): set paused (0x44 = 1). Any other state: no-op.
 *
 * Owner concerns (NOT modelled, by design): every Animator/Transform/Instantiate/
 * Invoke/Vector2 call, the FixedRotation angle computation (pure-Unity Vector2
 * .Angle against the master's transform), the FixedUpdate velocity composition
 * (that lives in RGPetController, not this subclass), and OnAtk's RGWeapon spawn.
 * Those are referenced in comments at their decomp sites.
 *
 * Field offsets are the RGPetController layout (decoded from RGPetController__ctor
 * /__FixedUpdate/__Scout @ game_full.c:427144-427384), which differs from the
 * RGEController base map: here 0x0c is the active/awake gate, 0x0d the dead latch,
 * 0x40 role_attribute, 0x44 the paused flag, 0x74 the master, 0x1c the target,
 * 0x71 the ctor-initialised re-arm flag.
 *
 * @see IL2CPP skeleton SnowmanController.cs (RGPetController subclass);
 *      FAITHFUL: SnowmanController @ game_full.c:1652651-1652903.
 */
class SnowmanController {
public:
    /**
     * @brief Outcome of OnGameStateChange, mirroring the decomp's branch tree
     *        (SnowmanController__OnGameStateChange @ game_full.c:1652881).
     *
     *   - Ignored: not active (field 0x0c == 0) -> whole body skipped (no write).
     *   - Resumed: active && game_state == 2 -> paused(0x44) = 0 and
     *              pet's own role_attribute.speed_rate += 0.5 (line 1652894;
     *              p+0x40 is the controller's own RoleAttribute, not the master).
     *   - Paused:  active && game_state == 1 -> paused(0x44) = 1.
     *   - NoChange: active but game_state is neither 1 nor 2 -> no write.
     */
    enum class StateChange {
        Ignored,  ///< not active (0x0c == 0): no-op.
        Resumed,  ///< game_state == 2: clear paused, boost speed_rate by +0.5.
        Paused,   ///< game_state == 1: set paused.
        NoChange  ///< active but game_state not in {1,2}: no write.
    };

    /// game_state value that resumes the pet (clears paused, boosts speed_rate).
    static constexpr int kStateResume = 2;
    /// game_state value that pauses the pet (sets paused).
    static constexpr int kStatePause = 1;
    /// Additive speed_rate boost applied on resume (line 1652894: += 0.5).
    static constexpr float kResumeSpeedRateBoost = 0.5F;
    /// ctor-initialised value of the re-arm flag (field 0x71); ShootReflection
    /// clears it to 0 (RGPetController__ctor sets it to 1).
    static constexpr bool kReArmFlagInitial = true;

    SnowmanController() = default;

    /// Seed this pet's deterministic stream. The Snowman takes NO draws of its
    /// own; the stream is exposed only for parity with sibling controllers.
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- mutable state (owning entity mirrors these into the real fields) ----

    /// Dead/destroyed latch (field 0x0d): gates ShootReflection and GetHurt.
    bool Destroyed() const { return m_Destroyed; }
    void SetDestroyed(bool v) { m_Destroyed = v; }

    /// Active/awake gate (field 0x0c): gates OnGameStateChange (and FixedUpdate,
    /// which is owned by the RGPetController base, not this subclass).
    bool Active() const { return m_Active; }
    void SetActive(bool v) { m_Active = v; }

    /// Paused flag (field 0x44): set by OnGameStateChange(pause), read by Scout.
    bool Paused() const { return m_Paused; }
    void SetPaused(bool v) { m_Paused = v; }

    /// Re-arm/latch flag (byte field 0x71): ctor sets it true; ShootReflection
    /// clears it to false.
    bool ReArmFlag() const { return m_ReArmFlag; }

    /// Whether Scout has linked a target (target(0x1c) = master(0x74)).
    bool HasTarget() const { return m_HasTarget; }
    /// Whether a non-null master was supplied to the last Scout call.
    bool HasMaster() const { return m_HasMaster; }

    /**
     * @brief Begin a shot. FAITHFUL: SnowmanController__ShootReflection @
     *        game_full.c:1652669.
     *
     * Gate (line 1652676): if dead/destroyed (field 0x0d) do nothing and return
     * false. When active, clears the re-arm flag (byte 0x71 = 0, line 1652679).
     * Owner: Invoke("OnAtk", hand@0x6c) (line 1652680), anim.SetTrigger("tap_atk")
     * and SetTrigger(...) (lines 1652685-1652690). No RNG draws.
     * @return true if the shot proceeded (was not gated by the dead latch).
     */
    bool ShootReflection();

    /**
     * @brief Target-refresh tick. FAITHFUL: SnowmanController__Scout @
     *        game_full.c:1652696.
     *
     * The body always copies target(0x1c) = master(0x74) (line 1652704), then
     * gates on paused (field 0x44 == 1, line 1652705): when paused it bails to a
     * transform read and the downstream dispatches do NOT run. When not paused it
     * dispatches FixedRotation then the shoot cadence (virtual calls, line
     * 1652709). Pure part: link the target from the master and report whether the
     * cadence dispatch would run.
     * @param hasMaster whether master(0x74) is non-null.
     * @return true if not paused (the cadence dispatch would run); false if paused.
     */
    bool Scout(bool hasMaster);

    /**
     * @brief Take a hit. FAITHFUL: SnowmanController__GetHurt @ game_full.c:1652826
     *        (override of RGPetController__GetHurt).
     *
     * Gate (line 1652833): only a live (field 0x0d == 0) snowman routes the hit to
     * UICanvas (the floating damage number). A dead one ignores it. This override
     * touches NO vitals (Pet HP is applied by the base chain); modelling more would
     * be fabrication.
     * @return true if the hit was routed (snowman was alive); false if ignored.
     */
    bool GetHurt();

    /**
     * @brief React to a global game-state change. FAITHFUL:
     *        SnowmanController__OnGameStateChange @ game_full.c:1652881.
     *
     * Gated on active (field 0x0c, line 1652886): when not active the whole body
     * is skipped (no write). When active: game_state == 2 (resume) clears paused
     * (0x44 = 0, line 1652888) and adds +0.5 to the pet's OWN role_attribute
     * .speed_rate (controller field@0x40 + 0x14, line 1652894); game_state == 1
     * (pause) sets paused (0x44 = 1, line 1652897). Any other game_state writes
     * nothing. The decomp reads p+0x40 from the controller itself (param_1) -- the
     * same RoleAttribute get_attribute@427175 returns -- never the master at 0x74.
     * @param gameState     the broadcast game-state code.
     * @param petSpeedRate  in/out: this pet's own role_attribute.speed_rate; bumped
     *                      by +0.5 only on resume. Pass the live value; read it back
     *                      after the call. Untouched on every other path.
     * @return which branch ran (Ignored / Resumed / Paused / NoChange).
     */
    StateChange OnGameStateChange(int gameState, float &petSpeedRate);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};

    bool m_Destroyed = false;             // 0x0d dead/destroyed latch
    bool m_Active = false;                // 0x0c active/awake gate
    bool m_Paused = false;                // 0x44 paused flag
    bool m_ReArmFlag = kReArmFlagInitial; // 0x71 ctor-initialised re-arm flag
    bool m_HasTarget = false;             // target(0x1c) linked from master
    bool m_HasMaster = false;             // master(0x74) was non-null last Scout
};

} // namespace Game

#endif /* GAME_SNOWMAN_CONTROLLER_HPP */
