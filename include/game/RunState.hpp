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
    /// Selected character id ("c01".."c13"), re-dispatched to the skill brain on the
    /// next floor (B1-P4a charId seam). Defaults to "c01"; the HeroRoom picker that
    /// sets a non-c01 value is B5 -- until then every run carries the c01 default.
    std::string charId = "c01";
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
    /// Hero picked in HeroSelectScene; drives floor-0 player skill (and sprite, post
    /// re-skin). Default "c01"; survives ResetRunState (a restart keeps the pick).
    std::string selectedCharId = "c01";
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

/// Floors per chapter. A chapter is 4 mob floors (index 0..3) + 1 boss
/// floor (index 4); clearing the boss floor ends the chapter -> settlement.
///
/// PORT-LEVEL DESIGN CHOICE, *not* byte-recovered: the original binary uses one
/// continuous floor counter (RGGameProcess.this_index) 1..16 with a hard 16-floor
/// cap and a 5-floor *cycle* (this_index%5==1, 1-based, is a boss/prep floor),
/// and there is NO inter-chapter settlement screen (the "Statements" results scene
/// only loads at the 16-cap or on death). floor->boss assignment is NOT recoverable
/// (no boss_list; LEVEL_GEN_PLAN.md). We keep the cycle PERIOD (5) but deliberately
/// re-phase the boss to the *last* floor of the block (index%5==4) so a chapter
/// reads as "4 mobs -> boss -> settlement". Single source of the chapter length +
/// boss-floor predicate, consumed by GameScene (boss-spawn gate) and RunController
/// (settlement routing).
constexpr int kChapterFloors = 5;

/// True on the chapter's last floor (boss floor): indices 4, 9, 14, ...
inline bool IsBossFloor(int floorIndex) {
    return floorIndex % kChapterFloors == kChapterFloors - 1;
}

} // namespace Game

#endif /* GAME_RUN_STATE_HPP */
