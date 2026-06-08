#ifndef GAME_RG_BATTERY_CONTROLLER_HPP
#define GAME_RG_BATTERY_CONTROLLER_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class RGBatteryController
 * @brief Faithful fire-cadence/charge brain for the Battery turret summon (an
 *        RGEController-derived deployable, like the Wolf/Snowman pets).
 *
 * RGBatteryController is a stationary "battery" turret the player deploys: it
 * sits, and on a scheduled ShootReflection cadence it latches into a charging
 * state, freezes its rigidbody, drives its PetHand, schedules a StopShooting,
 * and (in rocket mode) spawns a rocket. StopShooting clears the charge latch and
 * re-Invokes the next ShootReflection on the cadence field.
 *
 * THERE ARE ZERO rg_random DRAWS ANYWHERE IN THIS CLASS. Every recoverable body
 * (ShootReflection, StopShooting, CreateRocket, EndCycle, GetHurt, Dead) is
 * deterministic without consuming the stream -- so the faithful model takes NO
 * draws (rule: "if a body takes no draw, model zero"). The stream is exposed
 * only for parity with sibling controllers; a parallel same-seeded RGRandom
 * stays in lockstep across any number of these calls.
 *
 * OFFSET MAP (RGEController-derived; decoded from this class's own accesses,
 * cross-checked against the RGEController table). `param_1` is `int*` in the
 * ShootReflection/EndCycle/CreateRocket bodies (word indexing) and `int` in the
 * StopShooting/GetHurt/Dead bodies (byte indexing) -- both resolve to the same
 * byte offsets:
 *   0x0d dead latch (byte; GetHurt gate),
 *   0x14 anim ptr (Dead's SetTrigger target) / 0x50 move_direction (EndCycle,
 *        word index param_1[0x14] == byte 0x50),
 *   0x18 rigibody ptr (ShootReflection word index param_1[6]),
 *   0x6c shoot-cadence period (StopShooting's re-Invoke delay),
 *   0x71 can_shoot gate flag (byte),
 *   0x80 rocket_mode flag (byte; ShootReflection word index param_1[0x20]),
 *   0x8c hand (PetHand) ptr (word index param_1[0x23]),
 *   0x90 shoot_duration (StopShooting delay; word index param_1[0x24]),
 *   0x98 is_charging latch (byte; ShootReflection word index param_1[0x26],
 *        StopShooting byte index param_1 + 0x98 -- same field).
 *
 * Modelled here (all pure; no Unity types):
 *   - ShootReflection(): the detect-gate (can_shoot 0x71 && !is_charging 0x98) +
 *     the charge state machine (clear can_shoot, set is_charging) + the cadence
 *     formula passed to Invoke("StopShooting", shoot_duration 0x90) + the rocket
 *     branch (rocket_mode 0x80 -> CreateRocket). Owner: Vector2.zero velocity,
 *     PetHand.SetAttack(1), the actual Invoke, and the vtable-0x134 re-dispatch.
 *   - StopShooting(): clear is_charging (0x98 = 0) + the re-Invoke cadence delay
 *     (shoot-cadence field 0x6c). Owner: CancelInvoke, PetHand.SetAttack(0), the
 *     actual Invoke.
 *   - CreateRocket(): the re-Invoke HEAD gated on is_charging (0x98 != 0) with a
 *     fixed 1.0s delay. Owner: the detect virtual (vtable 0xe4) and the
 *     PrefabPool.get_Inst rocket spawn (tail-call-truncated).
 *   - EndCycle(): move_direction = Vector2.zero (0x50). Owner: vtable-0x124
 *     re-dispatch.
 *   - GetHurt(): the dead-latch gate (0x0d): a live battery routes the hit to
 *     UICanvas (floating damage number); a dead one ignores it. Touches NO
 *     vitals (fabrication to model any HP change here).
 *
 * Owner-only bodies (NOT modelled, one-line note at the decomp site):
 *   - Scout(): vtable-0xec detect dispatch + get_transform tail; no flag write,
 *     no RNG.
 *   - FixedRotation(): vtable-0xe4 detect -> get_position(target 0x1c); indirect/
 *     owner, no RNG.
 *   - Dead(): anim.SetTrigger("dead") (0x14) + get_transform; owner, no RNG.
 *
 * @see IL2CPP skeleton RGBatteryController.cs (RGEController-derived turret);
 *      FAITHFUL: RGBatteryController @ game_full.c:467933-468140.
 */
class RGBatteryController {
public:
    /**
     * @brief Outcome of one ShootReflection tick, mirroring the decomp's branch
     *        tree (RGBatteryController__ShootReflection @ game_full.c:467947).
     *
     *   - Gated:   can_shoot (0x71) == 0 OR is_charging (0x98) != 0 -> the whole
     *              charge block is skipped (no flag write, no scheduled stop). The
     *              vtable-0x134 tail still re-dispatches (owner).
     *   - Charged: can_shoot && !is_charging -> clear can_shoot, set is_charging,
     *              (owner zeroes velocity + SetAttack(1)), schedule StopShooting
     *              at shoot_duration. rocket_mode was false -> no rocket.
     *   - Rocket:  same as Charged, and rocket_mode (0x80) was set -> CreateRocket
     *              also runs (its re-Invoke head + owner spawn).
     */
    enum class FireResult {
        Gated,   ///< !can_shoot || is_charging: charge block skipped.
        Charged, ///< latched charging; StopShooting scheduled; no rocket.
        Rocket   ///< latched charging AND rocket_mode -> CreateRocket too.
    };

    /// Fixed re-Invoke delay (seconds) CreateRocket schedules when still charging
    /// (line 467980 site: Invoke(..., 0x3f800000 == 1.0f)).
    static constexpr float kRocketReInvokeDelay = 1.0F;

    RGBatteryController() = default;

    /// Seed this turret's deterministic stream. The battery takes NO draws of its
    /// own; the stream is exposed only for parity with sibling controllers.
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- mutable state (owning entity mirrors these into the real fields) ----

    /// can_shoot gate flag (byte 0x71): ShootReflection requires it set, then
    /// clears it; the owner's TurnCanShoot cadence re-arms it.
    bool CanShoot() const { return m_CanShoot; }
    void SetCanShoot(bool v) { m_CanShoot = v; }

    /// is_charging latch (byte 0x98): set by ShootReflection, cleared by
    /// StopShooting. Gates a second ShootReflection and CreateRocket's re-Invoke.
    bool IsCharging() const { return m_IsCharging; }
    void SetIsCharging(bool v) { m_IsCharging = v; }

    /// rocket_mode flag (byte 0x80): when set, a Charged shot also runs CreateRocket.
    bool RocketMode() const { return m_RocketMode; }
    void SetRocketMode(bool v) { m_RocketMode = v; }

    /// dead latch (byte 0x0d): gates GetHurt.
    bool Dead() const { return m_Dead; }
    void SetDead(bool v) { m_Dead = v; }

    /// move_direction (0x50): zeroed by EndCycle.
    glm::vec2 MoveDirection() const { return m_MoveDirection; }
    void SetMoveDirection(const glm::vec2 &v) { m_MoveDirection = v; }

    /**
     * @brief Begin a charged shot. FAITHFUL: RGBatteryController__ShootReflection
     *        @ game_full.c:467947.
     *
     * Detect-gate (line 467967): proceed only when can_shoot (byte 0x71) != 0 AND
     * is_charging (byte 0x98) == 0. When it proceeds: clear can_shoot (0x71 = 0,
     * line 467968), set is_charging (0x98 = 1, line 467969). Owner then reads
     * rigibody (0x18, line 467970), sets velocity to Vector2.zero (lines
     * 467971-467978) and PetHand.SetAttack(hand 0x8c, 1) (line 467979 site). The
     * cadence: Invoke("StopShooting", shoot_duration 0x90) -- returned via
     * out-param for the owner to schedule. If rocket_mode (byte 0x80) is set
     * (line 467980), CreateRocket also runs. The vtable-0x134 re-dispatch
     * (line 467985) at the tail is owner and is NOT modelled.
     *
     * @param outStopDelay filled with shoot_duration (the StopShooting delay)
     *                     when the shot proceeds; left untouched when gated.
     * @param shootDuration this turret's shoot_duration field (0x90) value.
     * @return Gated / Charged / Rocket per the branch tree. No RNG draws.
     */
    FireResult ShootReflection(float &outStopDelay, float shootDuration);

    /**
     * @brief End the charged shot. FAITHFUL: RGBatteryController__StopShooting @
     *        game_full.c:468033.
     *
     * Owner first CancelInvoke("StopShooting") (line 468040). The only state write
     * is is_charging (byte 0x98 = 0, line 468041) -- the inverse of
     * ShootReflection's latch. Owner: PetHand.SetAttack(hand 0x8c, 0) (line 468046
     * site). The cadence: Invoke("ShootReflection", shoot-cadence 0x6c) re-arms the
     * next fire -- returned via out-param. No gate, no RNG draws.
     *
     * @param outReInvokeDelay filled with the shoot-cadence field (0x6c): the
     *                         delay before the next ShootReflection.
     * @param shootCadence     this turret's shoot-cadence field (0x6c) value.
     */
    void StopShooting(float &outReInvokeDelay, float shootCadence);

    /**
     * @brief Rocket-mode spawn dispatch HEAD. FAITHFUL:
     *        RGBatteryController__CreateRocket @ game_full.c:467989.
     *
     * Owner: the detect virtual (vtable 0xe4, line 467997) -> on a hit, the
     * PrefabPool.get_Inst rocket spawn (lines 468000-468008) -- tail-call-
     * truncated, NOT modelled. The recoverable HEAD: when is_charging (byte 0x98)
     * is still set (line 468010), re-Invoke("...", 1.0f) (line 468011) to keep the
     * rocket cadence going. We report whether that re-Invoke would fire and at what
     * fixed delay (kRocketReInvokeDelay == 1.0f). No RNG draws.
     *
     * @param outReInvokeDelay set to kRocketReInvokeDelay when still charging.
     * @return true if the re-Invoke would fire (is_charging set); false otherwise.
     */
    bool CreateRocket(float &outReInvokeDelay) const;

    /**
     * @brief Stop the AI cycle. FAITHFUL: RGBatteryController__EndCycle @
     *        game_full.c:468053.
     *
     * The only state write is move_direction = Vector2.zero (0x50, lines
     * 468070-468071; word index param_1[0x14]). Owner: the vtable-0x124
     * re-dispatch (line 468073). No dead-gate, no RNG.
     */
    void EndCycle();

    /**
     * @brief Take a hit. FAITHFUL: RGBatteryController__GetHurt @ game_full.c:468077.
     *
     * Gate (line 468083): only a live (byte 0x0d == 0) battery routes the hit to
     * UICanvas (the floating damage number). A dead one ignores it. This override
     * touches NO vitals -- modelling any HP change here would be fabrication.
     * @return true if the hit was routed (battery alive); false if ignored.
     */
    bool GetHurt() const;

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};

    bool m_CanShoot = false;   // 0x71 can_shoot gate flag
    bool m_IsCharging = false; // 0x98 is_charging latch
    bool m_RocketMode = false; // 0x80 rocket_mode flag
    bool m_Dead = false;       // 0x0d dead latch (GetHurt gate)

    glm::vec2 m_MoveDirection{0.0F, 0.0F}; // 0x50 move_direction
};

} // namespace Game

#endif /* GAME_RG_BATTERY_CONTROLLER_HPP */
