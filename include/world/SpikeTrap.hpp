#ifndef GAME_WORLD_SPIKETRAP_HPP
#define GAME_WORLD_SPIKETRAP_HPP

#include <vector>

#include "data/GameData.hpp"

namespace Game {

/**
 * @class SpikeTrap
 * @brief Pure, engine-free model of one sting01 trap -- the original's `ObjectSting` MonoBehaviour
 *        (P2.20). Time is simulated in milliseconds; no RNG, no engine objects.
 *
 * FAITHFUL (DISASM, tools/arm_disasm.py 0x487aac 0x488520; ObjectSting fields @ struct_field_map:
 * anim +0xC, damage +0x10, offet_time +0x14, show_time +0x18, hide_time +0x1C, hit_player +0x20,
 * hit_enemy +0x21, show_sting +0x22, can_hit +0x23):
 *  - OnGameStateChange(value) @ 0x488414: `value == 1` (RGRoomX$$StartRoom writes game_state = 1 and
 *    invokes the delegate) and the trap's own room `process == 1` (parent.parent RGRoomX +0x10) ->
 *    `Invoke("ShowSting", offet_time)`. A room with no hostiles never reaches that delegate
 *    (StartRoom returns early with process = 2), so its spikes never start.
 *  - ShowSting @ 0x488208: show_sting = true; TurnCanHit(); anim.SetBool("show", true) (the `ding`
 *    spike appears); `Invoke("HideSting", show_time)`.
 *  - HideSting @ 0x4882b4: show_sting = false; CancelInvoke() (kills the pending TurnCanHit);
 *    anim.SetBool("show", false) (the spike disappears); `Invoke("ShowSting", hide_time)`.
 *  - TurnCanHit @ 0x488190: can_hit = true; `Invoke("TurnCanHit", 0.3f)` (movw/movt 0x3e99999a) -- a
 *    0.3 s damage tick that repeats while the trap is up.
 *  - Update @ 0x487be4: `if (show_sting && can_hit) { can_hit = false; <hit test> }`.
 * Nothing stops the cycle afterwards: OnGameStateChange ignores every value but 1 and ClearRoom
 * (process = 2) never reaches the trap, so a started trap keeps cycling after the room is cleared.
 * With the prefab numbers (offet_time 0, show_time 1, hide_time 2): up for 1 s, down for 2 s, damage
 * ticks at +0 / +0.3 / +0.6 / +0.9 s of every up phase (4 per cycle), none while down.
 *
 * PORT CHOICE (T-74): Unity's Invoke list is replaced by an explicit timer list advanced in whole
 * frames: a timer fires on the first Advance() whose time reaches its due time (due = the frame time
 * at which it was scheduled + delay, exactly as Unity schedules from Time.time), timers scheduled
 * while firing wait for the NEXT Advance(), and Update's hit test runs after the due timers of the
 * same Advance(). The relative order of Unity's invoke pass and Update within one frame, and a
 * zero-delay Invoke running on the next frame, are INFERRED from Unity's documented script lifecycle,
 * not observed in the binary.
 */
class SpikeTrap {
public:
    /// FAITHFUL: TurnCanHit's Invoke delay, 0x3e99999a = 0.3f.
    static constexpr double kTickMs = 300.0;

    explicit SpikeTrap(const StingDef &def);

    /// OnGameStateChange(1) while the room's process == 1: schedule ShowSting after offet_time.
    /// Calling it again re-schedules (Unity runs one more Invoke); the shell calls it once.
    void OnRoomStarted();

    /// Advance the trap's clock by @p dtMs and run its Update. Returns true when Update's hit test
    /// fires on this advance (show_sting && can_hit): the shell then applies the damage box.
    bool Advance(double dtMs);

    /// ShowSting .. HideSting: the spike (`ding`) is up and the trap can damage.
    bool Up() const { return m_ShowSting; }
    /// OnRoomStarted() has been called.
    bool Started() const { return m_Started; }
    /// Current trap clock in ms (sum of Advance() calls).
    double NowMs() const { return m_NowMs; }
    /// Pending Invoke timers (a started trap always has exactly one: ShowSting, HideSting or the
    /// next TurnCanHit in addition to HideSting while up).
    std::size_t PendingTimers() const { return m_Timers.size(); }

    int Damage() const { return m_Def.damage; }
    bool HitPlayer() const { return m_Def.hitPlayer; }
    bool HitEnemy() const { return m_Def.hitEnemy; }
    /// Damage dealt to an enemy: `(damage + (damage >> 31)) >> 1` at 0x487e64 = damage / 2 (toward 0).
    int EnemyDamage() const { return m_Def.damage / 2; }

private:
    enum class Kind { ShowSting, HideSting, TurnCanHit };
    struct Timer {
        Kind kind;
        double dueMs;
        unsigned seq;
    };
    void Invoke(Kind kind, double delayMs);
    void Fire(Kind kind);

    StingDef m_Def;
    double m_NowMs = 0.0;
    bool m_Started = false;
    bool m_ShowSting = false; ///< +0x22
    bool m_CanHit = false;    ///< +0x23
    unsigned m_Seq = 0;
    std::vector<Timer> m_Timers;
};

} // namespace Game

#endif /* GAME_WORLD_SPIKETRAP_HPP */
