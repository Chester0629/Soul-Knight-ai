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
 * @struct RunStats
 * @brief B4 results data -- the port analog of the RGGameProcess counters the
 *        original's Statements scene (UIStatement) displays: `kill_count`,
 *        `coin_value`, elapsed play time (`Time.time - start_time`) and the gems
 *        earned this run (UIStatement.gem_this_value). PURE, accumulated across
 *        floors by RunController (RecordKill / RecordTime), cleared per run.
 *
 * coins / gemsThisRun stay 0 until the economy (B3) and gem drops are ported --
 * the screen shows the REAL counter, never a fake number.
 */
struct RunStats {
    int kills = 0;        ///< RGGameProcess.kill_count analog: hostiles killed this run.
    int coins = 0;        ///< RGGameProcess.coin_value analog (B3 economy: not yet earned).
    int gemsThisRun = 0;  ///< UIStatement.gem_this_value analog (gem drops not yet ported).
    float timeMs = 0.0F;  ///< elapsed play time across all floors (Time.time - start_time).
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
    /// The run's chosen hero (B5 picker; RunController::SetHero). Floor 0 reads it
    /// (no continuation yet); later floors carry it in PlayerContinuation::charId.
    /// KEPT across ResetRunState so an EndScene restart replays the same hero.
    std::string charId = "c01";
    /// B4: run-long counters shown by the results screen. Reset per run.
    RunStats stats;
    /// M3e: the worshipped statue (RGGameProcess.statue_index, 1..7; 0 = none).
    int statueIndex = 0;
    /// B4: how the run ended (title of the results screen: I_gameover / I_victory).
    /// None while Playing.
    enum class Outcome { None, Dead, Victory } outcome = Outcome::None;
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

/// 1-based chapter of a floor: 0..4 -> 1, 5..9 -> 2, 10..14 -> 3, then it wraps
/// (the original caps at 16 floors; the port keeps running). Consumed by
/// GameScene to pick the chapter's biome mob pool + boss roster (GameData).
inline int ChapterOf(int floorIndex) {
    return (floorIndex / kChapterFloors) % 3 + 1;
}

/// MAP M1: the biome VARIANT a run plays for a chapter. The original ships two
/// MapManager sets per chapter (A forest / B ice, C castle / D graveyard, E alien
/// / F volcano) and picks one per run; the selection rule did not survive the
/// dump, so the port derives it from the run seed (PORT CHOICE): deterministic,
/// replayable, both variants reachable.
inline char BiomeVariantFor(int runSeed, int chapter) {
    const unsigned mix = static_cast<unsigned>(runSeed) * 2654435761u + static_cast<unsigned>(chapter) * 40503u;
    const int bit = static_cast<int>((mix >> 13) & 1u);
    return static_cast<char>('A' + 2 * (chapter - 1) + bit);
}

/// MAP M1: the level-table id for a 0-based floor: variant letter + 1-based stage
/// within the chapter ("B3"). Chapters wrap after 3 (ChapterOf).
inline std::string LevelIdFor(int runSeed, int floorIndex) {
    const int chapter = ChapterOf(floorIndex);
    const int stage = floorIndex % kChapterFloors + 1;
    return std::string(1, BiomeVariantFor(runSeed, chapter)) + std::to_string(stage);
}

/// Number of floors the original run spans (RGGameProcess.this_index 1..16 cap);
/// also the length of the Statements scene's level_list.
constexpr int kOriginalFloorCount = 16;

/// FAITHFUL @ Statements.unity UIStatement.level_list: the label the results
/// screen shows for a 0-based floor index ("1-1" .. "3-6"). The original indexes
/// the list with this_index; past the 16-floor cap (the port keeps running) the
/// last label is repeated rather than inventing one.
inline const char *LevelLabel(int floorIndex) {
    static const char *const kLevelList[kOriginalFloorCount] = {
        "1-1", "1-2", "1-3", "1-4", "1-5", "2-1", "2-2", "2-3",
        "2-4", "2-5", "3-1", "3-2", "3-3", "3-4", "3-5", "3-6"};
    if (floorIndex < 0) {
        floorIndex = 0;
    }
    if (floorIndex >= kOriginalFloorCount) {
        floorIndex = kOriginalFloorCount - 1;
    }
    return kLevelList[floorIndex];
}

/// The results screen's clock text, "h:mm:ss" (Statements.unity time text
/// placeholder "0:12:29"). Whole seconds, truncated.
inline std::string FormatRunClock(float timeMs) {
    long total = static_cast<long>(timeMs / 1000.0F);
    if (total < 0) {
        total = 0;
    }
    const long h = total / 3600;
    const long m = (total / 60) % 60;
    const long sec = total % 60;
    std::string out = std::to_string(h) + ":";
    out += (m < 10 ? "0" : "") + std::to_string(m) + ":";
    out += (sec < 10 ? "0" : "") + std::to_string(sec);
    return out;
}

} // namespace Game

#endif /* GAME_RUN_STATE_HPP */
