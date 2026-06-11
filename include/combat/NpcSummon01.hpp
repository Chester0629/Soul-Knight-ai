#ifndef GAME_NPC_SUMMON01_HPP
#define GAME_NPC_SUMMON01_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class NpcSummon01
 * @brief Faithful decision/cadence brain for the NpcSummon01 summoned ally (an
 *        RGEController subclass, like WolfController / SnowmanController).
 *
 * NpcSummon01 is a summoned pet/NPC: it scouts toward a master, wanders when
 * idle, and fires on a scheduled cadence gated by an 8/10 chance roll and a
 * can-shoot flag. Its decomp (game_full.c:1671325-1671745) is the cleanest
 * RGEController cadence in this family -- it is structurally the closest sibling
 * to the already-ported Wolf/Snowman brains (same RGEController offset table,
 * same single-int-draw RNG cadence, same owner-delegation split). Modelled here
 * (all pure; no Unity types):
 *
 *   - ShootReflection(): the unconditional rg_random.Range(0, 10) draw (max
 *     EXCLUSIVE), the 8/10 fire gate ANDed with the can-shoot byte (0x71), and
 *     on a pass: clear the can-shoot flag (0x71 = 0) and report the base shoot
 *     cadence (word 0x1b) the owner schedules via Invoke("Shoot", ...). The roll
 *     is drawn BEFORE the gate, so it is taken whether or not the shot fires --
 *     a gated-out shot still advances the stream.
 *   - RunReflection(): the unconditional rg_random.Range(0, 10) wander draw (max
 *     EXCLUSIVE) following the detect virtual call. The detect-result + roll<8
 *     branches only choose which transform the owner reads for facing
 *     (get_position / get_transform), so the only recoverable pure effect is the
 *     single draw plus the move_direction write the tail performs.
 *   - EndCycle(): move_direction (0x50/0x54) = Vector2.zero. No RNG.
 *   - GetHurt(): the dead/destroyed gate (byte 0x0d): a live summon routes the
 *     hit to UICanvas (the floating damage number); a dead one ignores it. No
 *     vitals are touched in THIS override. No RNG.
 *   - OnGameStateChange(): gated on active (byte 0x0c). game_state == 2 (resume):
 *     clear paused (0x44 = 0) and boost the summon's OWN role_attribute.speed_rate
 *     (controller field@0x40 + 0x14) by +0.5. game_state == 1 (pause): set paused
 *     (0x44 = 1). Any other state: no-op. No RNG.
 *
 * Owner concerns (NOT modelled, by design): the Scout body (vtable detect cast,
 * target = master copy at word 7 = word 0x1d, FixedRotation facing) is dominated
 * by Physics2D.CircleCastNonAlloc + Transform reads; PetHand.SetAttackTrigger on
 * the hand (word 0x22), the MonoBehaviour.Invoke("Shoot"/RunReflection) cadence
 * scheduling, the ShootReflection vtable 0x134 jumptable re-dispatch tail
 * ("Could not recover jumptable at 0x0119f308"), FixedRotation's Vector2.Angle
 * facing math, and Dead()'s Animator.SetTrigger / transform spawn. Those are
 * referenced in comments at their decomp sites and marked // TODO[verify].
 *
 * Field offsets are the RGEController layout (decoded against
 * recreation/Enemy/RGEController.cs and cross-checked against this class's own
 * accesses): word index n == byte n*4. Here byte 0x0c is the active gate, 0x0d
 * the dead latch, 0x40 role_attribute ptr (+0x14 speed_rate), 0x44 the paused
 * flag, 0x50/0x54 move_direction (words 0x14/0x15), 0x71 the can-shoot byte,
 * word 0x1b the base shoot cadence, word 0x1a the run-reflection cadence.
 *
 * @see IL2CPP skeleton NpcSummon01 (RGEController subclass);
 *      FAITHFUL: NpcSummon01 @ game_full.c:1671325-1671745.
 */
class NpcSummon01 {
public:
    /**
     * @brief Outcome of OnGameStateChange, mirroring the decomp's branch tree
     *        (NpcSummon01__OnGameStateChange @ game_full.c:1671726).
     *
     *   - Ignored: not active (byte 0x0c == 0) -> whole body skipped (no write).
     *   - Resumed: active && game_state == 2 -> paused(0x44) = 0 and the summon's
     *              own role_attribute.speed_rate (field@0x40 + 0x14) += 0.5.
     *   - Paused:  active && game_state == 1 -> paused(0x44) = 1.
     *   - NoChange: active but game_state is neither 1 nor 2 -> no write.
     */
    enum class StateChange {
        Ignored,  ///< not active (0x0c == 0): no-op.
        Resumed,  ///< game_state == 2: clear paused, boost speed_rate by +0.5.
        Paused,   ///< game_state == 1: set paused.
        NoChange  ///< active but game_state not in {1,2}: no write.
    };

    /// Fire roll ceiling drawn by ShootReflection (Range(0, 10), max EXCLUSIVE).
    static constexpr int kShootRollCeiling = 10;
    /// The roll must be strictly below this for the shot to fire (iVar1 < 8).
    static constexpr int kShootRollThreshold = 8;
    /// Wander roll ceiling drawn by RunReflection (Range(0, 10), max EXCLUSIVE).
    static constexpr int kWanderRollCeiling = 10;
    /// game_state value that resumes the summon (clears paused, boosts speed).
    static constexpr int kStateResume = 2;
    /// game_state value that pauses the summon (sets paused).
    static constexpr int kStatePause = 1;
    /// Additive speed_rate boost applied on resume (line 1671738: += 0.5).
    static constexpr float kResumeSpeedRateBoost = 0.5F;
    /// ctor-initialised value of the can-shoot byte (field 0x71); a freshly
    /// summoned ally is armed (can fire) until ShootReflection clears it.
    static constexpr bool kCanShootInitial = true;

    NpcSummon01() = default;

    /// Seed this summon's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- mutable state (owning entity mirrors these into the real fields) ----

    /// can-shoot byte (field 0x71): gates the fire branch of ShootReflection.
    bool CanShoot() const { return m_CanShoot; }
    void SetCanShoot(bool v) { m_CanShoot = v; }

    /// Dead/destroyed latch (byte field 0x0d): gates GetHurt.
    bool Destroyed() const { return m_Destroyed; }
    void SetDestroyed(bool v) { m_Destroyed = v; }

    /// Active gate (byte field 0x0c): gates OnGameStateChange.
    bool Active() const { return m_Active; }
    void SetActive(bool v) { m_Active = v; }

    /// Paused flag (field 0x44): set by OnGameStateChange(pause).
    bool Paused() const { return m_Paused; }
    void SetPaused(bool v) { m_Paused = v; }

    /// Last move_direction written by EndCycle/RunReflection (0x50/0x54).
    glm::vec2 MoveDirection() const { return m_MoveDirection; }

    /**
     * @brief Outcome of one ShootReflection tick, mirroring the decomp's gate.
     *        (NpcSummon01__ShootReflection @ game_full.c:1671325.)
     *
     *   - Fired:   roll < 8 AND can-shoot byte (0x71) != 0 -> clear 0x71 and the
     *              owner schedules Invoke("Shoot", base cadence @ word 0x1b).
     *   - Held:    the draw was taken but the gate failed (roll >= 8, or can-shoot
     *              was already 0) -> no flag write, no Invoke. The stream still
     *              advanced (the draw is unconditional, before the gate).
     */
    enum class ShootResult {
        Fired, ///< gate passed: 0x71 cleared, owner Invokes the shot.
        Held   ///< draw taken but gate failed: no write.
    };

    /**
     * @brief Roll-and-gate one shot. FAITHFUL: NpcSummon01__ShootReflection @
     *        game_full.c:1671325.
     *
     * Draws rg_random.Range(0, 10) UNCONDITIONALLY (line 1671335, max EXCLUSIVE)
     * BEFORE evaluating the gate, so a gated-out shot still advances the stream.
     * The gate (line 1671337) is `roll < 8 && can_shoot_byte(0x71) != 0`. On a
     * pass it clears the can-shoot byte (0x71 = 0, line 1671338) and the owner
     * schedules PetHand.SetAttackTrigger(hand@word 0x22) + Invoke("Shoot",
     * base_cadence @ word 0x1b) (lines 1671343-1671344). The trailing vtable
     * 0x134 jumptable re-dispatch (line 1671347, "Could not recover jumptable at
     * 0x0119f308") is owner/indirect and NOT modelled.
     * @param outShootCadence filled with the base shoot cadence (word 0x1b) the
     *                        owner passes to Invoke("Shoot", ...); only meaningful
     *                        when the result is Fired.
     * @param baseCadence     this summon's base shoot cadence field (word 0x1b).
     * @return Fired if the gate passed (0x71 cleared, owner fires), else Held.
     */
    ShootResult ShootReflection(float &outShootCadence, float baseCadence);

    /**
     * @brief Re-pick wander state. FAITHFUL: NpcSummon01__RunReflection @
     *        game_full.c:1671485.
     *
     * After a detect virtual call (vtable 0xe4, line 1671504) the body draws
     * rg_random.Range(0, 10) UNCONDITIONALLY (line 1671505, max EXCLUSIVE). The
     * detect-result + roll<8 branches only select which transform the owner reads
     * (get_position on the target at word 7, or get_transform on self) for
     * facing -- those are owner reads and not modelled. The only recoverable pure
     * effect is the single draw plus the move_direction write the tail performs
     * (words 0x14/0x15 = bytes 0x50/0x54, lines 1671520-1671521), here zeroed by
     * the decomp's FUN_00fa16ec(&local,0,0,0) build. Owner: Invoke("RunReflection",
     * run_cadence @ word 0x1a) (line 1671522).
     * @return the rg_random.Range(0, 10) wander roll (the draw that advances the
     *         stream); the gated-out shot path in ShootReflection and this draw
     *         are what keep replay frame-for-frame deterministic.
     */
    int RunReflection();

    /**
     * @brief Stop the AI cycle. FAITHFUL: NpcSummon01__EndCycle @
     *        game_full.c:1671568.
     *
     * The only recoverable state write is move_direction (0x50/0x54) =
     * Vector2.zero (lines 1671583-1671584). Owner: CancelInvoke("RunReflection")
     * (line 1671577) and anim.SetBool("walk", false) (line 1671592). No RNG.
     */
    void EndCycle();

    /**
     * @brief Take a hit. FAITHFUL: NpcSummon01__GetHurt @ game_full.c:1671668.
     *
     * Gate (line 1671672): only a live (byte 0x0d == 0) summon routes the hit to
     * UICanvas.GetInstance() (the floating damage number). A dead one ignores it.
     * This override touches NO vitals (HP is applied by the base damage chain);
     * modelling any HP change here would be fabrication. No RNG.
     * @return true if the hit was routed (summon was alive); false if ignored.
     */
    bool GetHurt();

    /**
     * @brief React to a global game-state change. FAITHFUL:
     *        NpcSummon01__OnGameStateChange @ game_full.c:1671726.
     *
     * Gated on active (byte 0x0c, line 1671730): when not active the whole body
     * is skipped (no write). When active: game_state == 2 (resume) clears paused
     * (0x44 = 0, line 1671733) and adds +0.5 to the summon's OWN
     * role_attribute.speed_rate (controller field@0x40 + 0x14, line 1671738);
     * game_state == 1 (pause) sets paused (0x44 = 1, line 1671743). Any other
     * game_state writes nothing. The decomp reads p+0x40 from the controller
     * itself (param_1) -- the same RoleAttribute pointer -- never a master at 0x74.
     * @param gameState     the broadcast game-state code.
     * @param petSpeedRate  in/out: this summon's own role_attribute.speed_rate;
     *                      bumped by +0.5 only on resume. Untouched otherwise.
     * @return which branch ran (Ignored / Resumed / Paused / NoChange).
     */
    StateChange OnGameStateChange(int gameState, float &petSpeedRate);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};

    bool m_CanShoot = kCanShootInitial; // 0x71 can-shoot byte
    bool m_Destroyed = false;           // 0x0d dead/destroyed latch
    bool m_Active = false;              // 0x0c active gate
    bool m_Paused = false;              // 0x44 paused flag
    glm::vec2 m_MoveDirection{0.0F, 0.0F}; // 0x50/0x54 move_direction
};

} // namespace Game

#endif /* GAME_NPC_SUMMON01_HPP */
