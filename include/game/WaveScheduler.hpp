#ifndef GAME_WAVE_SCHEDULER_HPP
#define GAME_WAVE_SCHEDULER_HPP

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace Game {

/**
 * @brief Engine-free port of the original EnemyMaker's wave timing -- map work
 *        M1 / plan item P2.3.
 *
 * FAITHFUL: EnemyMaker plans a room's picks (Game::PlanRoomSpawns) once, then
 * spends them in parts of `this_count` enemies each (StartEnemyMaker ->
 * this_count = level * 3 + this_count, prefab this_count is 0 on the r1_*
 * rooms, so this_count == level*3 in practice -- the size of each part, not
 * a count of parts). The first part spawns immediately (on StartRoom); every
 * later part spawns 0.8 s after the room has no live hostile
 * (EnemyMaker.EnemyDead -> Invoke("CreatePart", 0.8f)). A room is clear only
 * once nothing is pending AND no part timer is running.
 *
 * This struct owns only that bookkeeping (no engine/scene types) so it can be
 * unit-tested directly; GameScene drives it and still owns spawn points,
 * point-spread indexing and the actual enemy instantiation.
 */
struct WaveScheduler {
    static constexpr float kPartDelayMs = 800.0F; // FAITHFUL: EnemyMaker.EnemyDead -> Invoke("CreatePart", 0.8f)

    std::vector<std::string> pending;
    int waveSize = 1;
    float timerMs = -1.0F; ///< -1 = idle; counts down the kPartDelayMs delay

    /// Plan a fresh room: @p picks is the full spend (Game::PlanRoomSpawns
    /// output) and @p parts is the size of each wave (level*3 in the
    /// original, at least 1 -- callers pass 0 for "no spawns" prefabs and
    /// still get a valid (if inert, empty-picks) scheduler).
    void Plan(std::vector<std::string> picks, int parts) {
        pending = std::move(picks);
        waveSize = std::max(1, parts);
        timerMs = -1.0F;
    }

    /// Pop up to @c waveSize ids off the front of @c pending (the first wave,
    /// or a wave released by Tick/NotifyRoomEmpty). Empty if nothing pending.
    std::vector<std::string> TakeWave() {
        std::vector<std::string> out;
        if (pending.empty()) {
            return out;
        }
        const auto n = std::min(static_cast<std::size_t>(waveSize), pending.size());
        out.assign(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(n));
        pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(n));
        return out;
    }

    /// The room has just gone hostile-free with parts still queued: arm the
    /// kPartDelayMs timer if it isn't already running. No-op once idle
    /// (pending empty) or already counting down.
    void NotifyRoomEmpty() {
        if (!pending.empty() && timerMs < 0.0F) {
            timerMs = kPartDelayMs;
        }
    }

    /// Advance the part timer by @p dtMs. While @p roomEmpty is false the
    /// timer never starts or advances (a live hostile keeps the room busy).
    /// Returns the next wave's ids once the timer expires, otherwise empty.
    std::vector<std::string> Tick(float dtMs, bool roomEmpty) {
        if (pending.empty()) {
            timerMs = -1.0F;
            return {};
        }
        if (!roomEmpty) {
            return {};
        }
        if (timerMs < 0.0F) {
            timerMs = kPartDelayMs;
            return {};
        }
        timerMs -= dtMs;
        if (timerMs > 0.0F) {
            return {};
        }
        timerMs = -1.0F;
        return TakeWave();
    }

    /// The room's wave schedule is fully spent: nothing pending and no part
    /// timer running.
    bool Done() const { return pending.empty() && timerMs < 0.0F; }
};

} // namespace Game

#endif /* GAME_WAVE_SCHEDULER_HPP */
