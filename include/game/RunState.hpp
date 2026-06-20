#ifndef GAME_RUN_STATE_HPP
#define GAME_RUN_STATE_HPP

#include <optional>
#include <string>

#include "combat/CombatStats.hpp"

namespace Game {

/**
 * @struct PlayerContinuation
 * @brief What survives a floor transition under Route (a) (fresh-instance
 *        `Replace`): the player's essential state, lifted off the dying floor's
 *        `GameScene` and re-applied to the next floor's freshly-built `Player`.
 *
 * The port analog of Soul Knight's `PlayerSaveData`. Two fields only (RUN_LOOP_PLAN
 * D3): the player's combat vitals and the equipped weapon id.
 *
 * @par speed trio / regen accumulators (the documented dormant seam, decision (ii))
 * @ref stats is a **full `CombatStats` struct copy** (not a hand-picked subset).
 * This deliberately carries, alongside hp/armor/energy + their maxes:
 *   - the regen accumulators (`armorTime`/`energyTime`) -- carrying a mid-charge
 *     value forward is *more* faithful, not a bug: the original `RoleAttributePlayer`
 *     object persists across floors rather than being reconstructed, so its reload
 *     timers keep ticking. The effect is bounded (<= one point, <= 2.0s) and
 *     self-correcting.
 *   - the speed trio (`speed`/`speedRate`/`speedChangeValue`) -- currently **inert**:
 *     player movement reads the separate, template-derived `Player::m_Speed`
 *     (`src/entities/Player.cpp:26,52`), never `CombatStats.speed`, and nothing
 *     wires `ChangeSpeed`/`SpeedBack` outside tests. The value is carried (so a
 *     future buff would round-trip correctly) but does not affect movement on
 *     either floor.
 * Chosen over copying only {hp,armor,energy} because (a) it matches the ratified
 * D3 ("full CombatStats"), (b) it future-proofs `maxHp`/`maxArmor` for hp-up
 * pickups (RUN_LOOP_PLAN section future), and (c) the dragged sub-state is provably
 * inert/bounded today. **If `Stats().speed` ever drives movement, revisit whether a
 * mid-debuff snapshot should carry the half-applied delta (likely reset the trio).**
 */
struct PlayerContinuation {
    CombatStats stats;    ///< full combat-vitals copy (hp/armor/energy + max + accumulators).
    std::string weaponId; ///< equipped WeaponDef id, re-equipped on the next floor.
};

/**
 * @struct RunState
 * @brief Pure run-level state that outlives any single `GameScene` (the
 *        `PlayerSaveData` + `RGGameProcess.this_index` analog). No engine deps,
 *        fully unit-testable.
 */
struct RunState {
    /// Base seed for the whole run; per-floor seeds derive from it (@ref PerFloorSeed).
    static constexpr int kDefaultRunSeed = 20240607;

    int runSeed = kDefaultRunSeed; ///< whole-run base seed.
    int floorIndex = 0;            ///< 0-based; the port analog of RGGameProcess this_index (+0x14).
    enum class Phase { Playing, Ended } phase = Phase::Playing;
    std::optional<PlayerContinuation> carried; ///< empty on floor 0 (template); set after floor 0.
};

/// Stride between consecutive floors' base seeds. A large prime so each floor's
/// derived stream (`seed + 5`, `seed + 1000 + roomIndex`, `seed + 9000`, ...) sits
/// in a non-overlapping window vs its neighbours -- which is exactly what makes
/// `GameScene::m_WeaponSwaps` safe to reset to 0 per floor (RUN_LOOP_PLAN section gap-1).
/// Chosen so `floorIndex * stride` cannot overflow `int` for any realistic floor
/// count (a run is at most a few dozen floors).
constexpr int kFloorSeedStride = 1000003;

/**
 * @brief Per-floor base seed = f(runSeed, floorIndex), so each floor generates
 *        a *different* layout/rooms/enemies (the only in-game-observable
 *        `floorIndex` effect in Phase 1 -- RUN_LOOP_PLAN section 4a).
 *
 * `floorIndex == 0` yields exactly `runSeed` (floor 0 is bit-identical to the old
 * hardcoded single floor). Consecutive floors differ by @ref kFloorSeedStride,
 * guaranteeing distinct seeds (and non-overlapping derived-seed windows).
 */
inline int PerFloorSeed(int runSeed, int floorIndex) {
    return runSeed + floorIndex * kFloorSeedStride;
}

} // namespace Game

#endif /* GAME_RUN_STATE_HPP */
