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
 * FAITHFUL (game_typed.c):
 *   - EnemyMaker__EnemyDead @ 73138-73185: `this_count--`; if `this_count > 0`
 *     return (still hostiles alive in this part); else if `enemy_point > 0`
 *     call CreatePart() IMMEDIATELY (no delay); else the room is cleared
 *     (RGGameSceneManager.OnGameStateChange(2)). So a new part is released the
 *     instant the current part's last enemy dies -- not after any timer.
 *   - EnemyMaker__CreatePart @ 78030-78084: spends the room's budget
 *     (`enemy_point`) into `this_count` via a do/while that creates enemies
 *     until the budget or room_size is exhausted; only AFTER that does it
 *     `if (part != 0) Invoke(<literal>, 0.8f)` and increment `part`. That
 *     Invoke does not gate spawning -- CreatePart has already spawned the part
 *     synchronously by the time it fires.
 *   - EnemyMaker__StartEnemyMaker @ 77871-77995: `max_room_size = level*3 +
 *     max_room_size` (the serialized field is 0 on r1_* rooms), so each part
 *     holds level*3 enemies -- the size of a part, not a count of parts.
 *
 * So the scheduler here releases the next wave on the first Tick where the
 * room has no live hostile (`roomEmpty`) and picks are still pending -- no
 * 800 ms wait. A room is clear once nothing is pending (`Done()`).
 *
 * INFERRED (T-04): CreateEnemy's body is truncated in the decompile right
 * after `rg_random.Range(0, 200)`, so exactly how it consumes room_size /
 * enemy_point per enemy is not recovered; PlanRoomSpawns' consume-charging
 * loop (see EnemyMaker.hpp) is this port's stand-in.
 *
 * NOT PORTED (T-29): kShowUpEffectDelayMs documents CreatePart's
 * `Invoke("ShowUpEffect", 0.8f)` call for parts >= 1 (`part != 0`). Ghidra
 * does not resolve the literal string, but `"ShowUpEffect"` is in the string
 * table (`"CreatePart"` is not) and EnemyMaker__ShowUpEffect @
 * game_full.c:682319 just plays sound effect 7 -- this is a show-up SOUND
 * cue, not a spawn delay, so it is not implemented here.
 *
 * This struct owns only that bookkeeping (no engine/scene types) so it can be
 * unit-tested directly; GameScene drives it and still owns spawn points,
 * point-spread indexing and the actual enemy instantiation.
 */
struct WaveScheduler {
    // NOT PORTED (T-29): Invoke("ShowUpEffect", 0.8f) on parts >= 1 -- sound only.
    static constexpr float kShowUpEffectDelayMs = 800.0F;

    std::vector<std::string> pending;
    int waveSize = 1;

    /// Plan a fresh room: @p picks is the full spend (Game::PlanRoomSpawns
    /// output) and @p parts is the size of each wave (level*3 in the
    /// original, at least 1 -- callers pass 0 for "no spawns" prefabs and
    /// still get a valid (if inert, empty-picks) scheduler).
    void Plan(std::vector<std::string> picks, int parts) {
        pending = std::move(picks);
        waveSize = std::max(1, parts);
    }

    /// Pop up to @c waveSize ids off the front of @c pending (the first wave,
    /// or a wave released by Tick). Empty if nothing pending.
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

    /// Advance one tick: FAITHFUL to EnemyMaker.EnemyDead -> CreatePart(),
    /// the next part is released on the first tick where @p roomEmpty is
    /// true and picks are still pending -- immediately, no delay. @p dtMs is
    /// unused (kept so callers don't need to special-case the call site).
    std::vector<std::string> Tick(float /*dtMs*/, bool roomEmpty) {
        if (pending.empty() || !roomEmpty) {
            return {};
        }
        return TakeWave();
    }

    /// The room's wave schedule is fully spent: nothing pending.
    bool Done() const { return pending.empty(); }
};

} // namespace Game

#endif /* GAME_WAVE_SCHEDULER_HPP */
