#include <gtest/gtest.h>

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "combat/CombatStats.hpp"
#include "data/GameData.hpp"
#include "game/FloorClear.hpp"
#include "game/RunController.hpp"
#include "game/RunState.hpp"
#include "sim/Simulation.hpp"
#include "world/MapManager.hpp"

using Game::AllHostilesDead;
using Game::CombatStats;
using Game::GameData;
using Game::IsBossFloor;
using Game::MapManager;
using Game::PerFloorSeed;
using Game::PlayerContinuation;
using Game::RunController;
using Game::RunState;
using Game::WeaponDef;
using Game::WeaponEnergyCost;
using EView = Game::Sim::Simulation::EntityView;

namespace {
const char *kResourceRoot = RESOURCE_DIR;
} // namespace

// NOLINTBEGIN(readability-magic-numbers)

// =====================================================================
// Pure logic A1: PerFloorSeed -- the per-floor seed derivation.
//   floor 0 must equal runSeed (floor 0 stays bit-identical to the old
//   hardcoded single floor); consecutive floors must DIFFER (this is the
//   load-bearing premise that makes m_WeaponSwaps safe to reset to 0).
// =====================================================================

TEST(RunLoopPerFloorSeed, FloorZeroEqualsRunSeed) {
    EXPECT_EQ(PerFloorSeed(RunState::kDefaultRunSeed, 0), RunState::kDefaultRunSeed);
    EXPECT_EQ(PerFloorSeed(424242, 0), 424242);
}

// GAP #1 (pinned as a failing-if-broken test, not a comment): every floor's
// base seed is distinct, so the per-floor `seed + 5 + swaps` WeaponController
// seeds never collide across floors even though m_WeaponSwaps resets to 0.
TEST(RunLoopPerFloorSeed, DiffersPerFloor) {
    const int runSeed = RunState::kDefaultRunSeed;
    std::set<int> seen;
    for (int floor = 0; floor < 32; ++floor) {
        const int s = PerFloorSeed(runSeed, floor);
        EXPECT_TRUE(seen.insert(s).second)
            << "floor " << floor << " seed " << s << " collides with an earlier floor";
        if (floor > 0) {
            EXPECT_NE(PerFloorSeed(runSeed, floor), PerFloorSeed(runSeed, floor - 1))
                << "floor " << floor << " and " << (floor - 1) << " share a base seed";
        }
    }
}

// Stronger form of GAP #1: consecutive floors' base seeds are spaced wider than
// the largest per-floor derived offset (boss seed = base + 9000), so the WHOLE
// derived-seed window of floor N is disjoint from floor N+1's -- not just the
// base. This is what guarantees the m_WeaponSwaps reset-to-0 is collision-free.
TEST(RunLoopPerFloorSeed, DerivedSeedWindowsDoNotOverlap) {
    const int runSeed = RunState::kDefaultRunSeed;
    const int kMaxDerivedOffset = 9000; // boss seed = base + 9000 (GameScene.cpp:193,196)
    for (int floor = 0; floor < 32; ++floor) {
        const int lo = PerFloorSeed(runSeed, floor);
        const int hiNext = PerFloorSeed(runSeed, floor + 1);
        EXPECT_GT(hiNext - lo, kMaxDerivedOffset)
            << "floor " << floor << " derived-seed window reaches into floor " << (floor + 1);
    }
}

// =====================================================================
// Pure logic A1b: IsBossFloor -- the chapter boss-floor predicate.
//   A chapter is kChapterFloors (5) floors: index 0..3 are mob floors,
//   index 4 is the boss floor; the cycle repeats (9, 14, ...). This is the
//   single source consumed by GameScene (boss-spawn gate) and RunController
//   (settlement routing); the routing reads it on the PRE-increment index.
// =====================================================================

TEST(RunLoopIsBossFloor, MobFloorsAreNotBossFloors) {
    EXPECT_FALSE(IsBossFloor(0));
    EXPECT_FALSE(IsBossFloor(1));
    EXPECT_FALSE(IsBossFloor(2));
    EXPECT_FALSE(IsBossFloor(3));
}

TEST(RunLoopIsBossFloor, LastFloorOfEachChapterIsBoss) {
    EXPECT_TRUE(IsBossFloor(4));   // chapter 1 boss
    EXPECT_TRUE(IsBossFloor(9));   // chapter 2 boss
    EXPECT_TRUE(IsBossFloor(14));  // chapter 3 boss
}

TEST(RunLoopIsBossFloor, ExactlyOneBossPerChapterBlock) {
    for (int chapter = 0; chapter < 4; ++chapter) {
        int bossCount = 0;
        for (int f = chapter * Game::kChapterFloors;
             f < (chapter + 1) * Game::kChapterFloors; ++f) {
            if (IsBossFloor(f)) {
                ++bossCount;
            }
        }
        EXPECT_EQ(bossCount, 1) << "chapter " << chapter << " must have exactly one boss floor";
    }
}

// =====================================================================
// Pure logic A2: WeaponEnergyCost -- the m_WeaponEnergyCost DERIVE rule.
//   cost = consume, floored at 1 (a consume of 0 or negative still costs 1).
//   This is the single source the firing gate + energy spend read, so a carried
//   weapon whose consume != 1 must re-derive THROUGH this, never default to 1.
// =====================================================================

TEST(RunLoopWeaponEnergyCost, FloorsAtOne) {
    WeaponDef w;
    w.consume = 0;
    EXPECT_EQ(WeaponEnergyCost(w), 1) << "consume 0 must still cost 1";
    w.consume = -3;
    EXPECT_EQ(WeaponEnergyCost(w), 1) << "negative consume must clamp to 1";
}

TEST(RunLoopWeaponEnergyCost, PassesThroughPositiveConsume) {
    WeaponDef w;
    w.consume = 1;
    EXPECT_EQ(WeaponEnergyCost(w), 1);
    w.consume = 3;
    EXPECT_EQ(WeaponEnergyCost(w), 3);
    w.consume = 7;
    EXPECT_EQ(WeaponEnergyCost(w), 7);
}

// =====================================================================
// Pure logic A3: RunController carry/seed orchestration (no engine, no GL).
// =====================================================================

TEST(RunLoopController, FreshRunStartsAtFloorZeroTemplatePlaying) {
    RunController rc;
    EXPECT_EQ(rc.State().floorIndex, 0);
    EXPECT_FALSE(rc.State().carried.has_value()) << "floor 0 uses the template, not a continuation";
    EXPECT_EQ(rc.State().phase, RunState::Phase::Playing);
    EXPECT_EQ(rc.State().runSeed, RunState::kDefaultRunSeed);
    EXPECT_EQ(rc.CurrentFloorSeed(), RunState::kDefaultRunSeed);
}

TEST(RunLoopController, ExplicitRunSeedRoots) {
    RunController rc(987654);
    EXPECT_EQ(rc.State().runSeed, 987654);
    EXPECT_EQ(rc.CurrentFloorSeed(), 987654);
}

TEST(RunLoopController, AdvanceFloorCarriesSnapshotAndAdvances) {
    // AdvanceFloor is the PURE carry that OnFloorCleared wraps (OnFloorCleared also
    // requests a GL-bound Replace, so it is verified in-game, not here).
    RunController rc;

    CombatStats hurt;
    hurt.maxHp = 8;
    hurt.hp = 3; // damaged this floor
    hurt.maxArmor = 4;
    hurt.armor = 1;
    hurt.maxEnergy = 6;
    hurt.energy = 2;
    PlayerContinuation snap{hurt, "Gun099"};

    rc.AdvanceFloor(snap);

    ASSERT_TRUE(rc.State().carried.has_value());
    EXPECT_EQ(rc.State().carried->stats.hp, 3) << "hp must continue, not reset to template";
    EXPECT_EQ(rc.State().carried->stats.armor, 1);
    EXPECT_EQ(rc.State().carried->stats.energy, 2);
    EXPECT_EQ(rc.State().carried->weaponId, "Gun099") << "equipped weapon id must continue";
    EXPECT_EQ(rc.State().floorIndex, 1) << "the floor counter must advance";
    EXPECT_NE(rc.CurrentFloorSeed(), PerFloorSeed(RunState::kDefaultRunSeed, 0))
        << "the next floor must generate from a different seed";
    EXPECT_EQ(rc.CurrentFloorSeed(), PerFloorSeed(RunState::kDefaultRunSeed, 1));
}

// Decision (ii): the continuation carries the FULL CombatStats struct. Proven by
// round-tripping a tampered speed trio + regen accumulator: the carry is faithful
// (the value survives), which is exactly why it is safe -- a future buff would be
// preserved. Movement INERTNESS of that carried speed (player moves at the
// template-derived Player::m_Speed, src/entities/Player.cpp:26,52, never
// Stats().speed) needs a live Player+movement and is verified IN-GAME next step.
TEST(RunLoopController, ContinuationCarriesFullCombatStatsIncludingInertSpeedTrio) {
    RunController rc;
    CombatStats s;
    s.hp = 5;
    s.maxHp = 5;
    s.speed = 999.0F;            // tamper the (currently inert) movement field
    s.speedChangeValue = 42.0F;  // and the buff-revert memo
    s.energyTime = 1.5F;         // mid-charge regen accumulator
    rc.AdvanceFloor(PlayerContinuation{s, "Gun001"});

    ASSERT_TRUE(rc.State().carried.has_value());
    // Full-struct copy: every field round-trips (so a future speed buff is NOT lost).
    EXPECT_FLOAT_EQ(rc.State().carried->stats.speed, 999.0F);
    EXPECT_FLOAT_EQ(rc.State().carried->stats.speedChangeValue, 42.0F);
    EXPECT_FLOAT_EQ(rc.State().carried->stats.energyTime, 1.5F);
}

// =====================================================================
// Ground-truth two-floor scenario (the ultracode-verified core).
//
//   Floor 0 (floorIndex 0): template hp H0, weapon W0 (consume C0).
//   Played: hp -> H1 (< H0), swapped to W1 (consume C1 != C0).
//   Floor-clear snapshot { stats(hp=H1), weaponId=W1 } -> RunState.carried.
//   Floor 1 (floorIndex 1, fresh + continuation):
//     hp == H1 (continued, NOT back to H0),
//     equipped == W1 (NOT back to W0),
//     m_WeaponEnergyCost == WeaponEnergyCost(W1) (re-derived, NOT reset to 1).
// =====================================================================

TEST(RunLoopGroundTruth, SecondFloorContinuesHpWeaponAndDerivedEnergyCost) {
    // --- Synthetic, self-contained ground truth (no JSON dependency) ---
    WeaponDef w0; // initial weapon
    w0.id = "Gun001";
    w0.consume = 1; // C0
    WeaponDef w1; // picked up on floor 0
    w1.id = "Gun099";
    w1.consume = 3; // C1 != C0 -- the case that exposes a silent reset-to-1

    const int H0 = 8;
    const int H1 = 3; // damaged this floor

    // energy cost on floor 0 (initial equip), for contrast.
    const int energyCost0 = WeaponEnergyCost(w0);
    ASSERT_EQ(energyCost0, 1);

    // Floor-clear snapshot the GameScene would produce: final hp + final weapon id.
    CombatStats endOfFloor0;
    endOfFloor0.maxHp = H0;
    endOfFloor0.hp = H1;
    PlayerContinuation snap{endOfFloor0, w1.id};

    RunController rc;
    rc.AdvanceFloor(snap);

    // Ground truth for the SECOND floor (the values a fresh GameScene must apply):
    ASSERT_TRUE(rc.State().carried.has_value());
    EXPECT_EQ(rc.State().carried->stats.hp, H1) << "hp continues (not back to H0)";
    EXPECT_NE(rc.State().carried->stats.hp, H0);
    EXPECT_EQ(rc.State().carried->weaponId, w1.id) << "weapon continues (not back to W0)";

    const int energyCost1 = WeaponEnergyCost(w1); // re-derived from the carried weapon
    EXPECT_EQ(energyCost1, 3) << "re-derived from W1.consume, NOT silently reset to 1";
    EXPECT_NE(energyCost1, energyCost0) << "the whole point: a consume!=1 carried weapon mis-costs if reset";
}

// Real-data half of the DERIVE: the carried weapon id round-trips through
// GameData::FindWeapon back to a real WeaponDef whose energy cost re-derives the
// same way. This exercises the id -> def -> consume path the GameScene OnEnter
// continuation uses (the GL-free half; the actual equip is in-game next step).
TEST(RunLoopGroundTruth, CarriedWeaponIdReDerivesEnergyCostFromRealData) {
    GameData data;
    ASSERT_TRUE(data.LoadAll(kResourceRoot));

    const WeaponDef *gun001 = data.FindWeapon("Gun001");
    ASSERT_NE(gun001, nullptr) << "the default starting weapon must resolve";
    EXPECT_EQ(WeaponEnergyCost(*gun001), gun001->consume > 0 ? gun001->consume : 1);

    // For every loaded weapon, the carried-id round-trip yields the SAME def and
    // the SAME derived cost (no id is lost, no cost silently changes).
    for (const WeaponDef &w : data.Weapons()) {
        const WeaponDef *found = data.FindWeapon(w.id);
        ASSERT_NE(found, nullptr) << "weapon id '" << w.id << "' must round-trip";
        EXPECT_EQ(WeaponEnergyCost(*found), WeaponEnergyCost(w)) << "id " << w.id;
        EXPECT_GE(WeaponEnergyCost(*found), 1) << "energy cost floors at 1 for id " << w.id;
    }
}

// =====================================================================
// Build-touching seam, unit-coverable half (B): PerFloorSeed actually makes
// different floors generate different layouts. The headless proof is at the
// seed->geometry layer (MapManager is engine-free); the visual layout diff is
// in-game. floorIndex 0's layout must stay identical to the legacy single floor.
// =====================================================================

namespace {
std::vector<std::pair<int, int>> RoomPositions(const MapManager &m) {
    std::vector<std::pair<int, int>> out;
    for (const Game::RoomCell &c : m.Rooms()) {
        out.emplace_back(c.gridX, c.gridY);
    }
    return out;
}
} // namespace

TEST(RunLoopFloorGen, DifferentFloorIndexProducesDifferentLayout) {
    MapManager::Options opt;
    opt.mapLong = 7;
    opt.ranRoomProbability = 25;

    const int runSeed = RunState::kDefaultRunSeed;
    const MapManager floor0(PerFloorSeed(runSeed, 0), opt);
    const MapManager floor1(PerFloorSeed(runSeed, 1), opt);
    const MapManager floor2(PerFloorSeed(runSeed, 2), opt);

    // Each floor's seed differs (DiffersPerFloor) -> the deterministic walk lays
    // rooms differently. Assert the placement sequences are not all identical.
    EXPECT_NE(RoomPositions(floor0), RoomPositions(floor1));
    EXPECT_NE(RoomPositions(floor1), RoomPositions(floor2));
    EXPECT_NE(RoomPositions(floor0), RoomPositions(floor2));
}

TEST(RunLoopFloorGen, FloorZeroLayoutIsDeterministicAndSeedStable) {
    MapManager::Options opt;
    opt.mapLong = 7;
    opt.ranRoomProbability = 25;

    // floorIndex 0 -> PerFloorSeed == runSeed, so floor 0 reproduces the exact
    // legacy layout (the hardcoded 20240607 single floor) -- no regression.
    const MapManager a(PerFloorSeed(RunState::kDefaultRunSeed, 0), opt);
    const MapManager b(RunState::kDefaultRunSeed, opt); // legacy direct seed
    EXPECT_EQ(RoomPositions(a), RoomPositions(b));
}

// =====================================================================
// Step 2a: whole-floor clear predicate (AllHostilesDead) over the sim's
// AUTHORITATIVE entity views. Pure -> fabricated view lists, no engine.
// Boss spawns every floor today, so the live-boss cases matter.
// =====================================================================

namespace {
EView View(bool alive) {
    EView v;
    v.alive = alive;
    return v;
}
} // namespace

TEST(RunLoopFloorClear, NoEnemiesNoBossIsClear) {
    EXPECT_TRUE(AllHostilesDead({}, /*hasBoss=*/false, View(false)));
}

TEST(RunLoopFloorClear, AnyAliveEnemyIsNotClear) {
    EXPECT_FALSE(AllHostilesDead({View(false), View(true), View(false)}, false, View(false)));
}

TEST(RunLoopFloorClear, AllEnemiesDeadNoBossIsClear) {
    EXPECT_TRUE(AllHostilesDead({View(false), View(false)}, false, View(false)));
}

TEST(RunLoopFloorClear, LiveBossKeepsFloorUncleared) {
    // Every floor spawns a boss -> the boss must be dead even when all enemies are.
    EXPECT_FALSE(AllHostilesDead({View(false), View(false)}, /*hasBoss=*/true, View(true)));
}

TEST(RunLoopFloorClear, AllEnemiesAndBossDeadIsClear) {
    EXPECT_TRUE(AllHostilesDead({View(false), View(false)}, /*hasBoss=*/true, View(false)));
}

TEST(RunLoopFloorClear, LiveEnemyWithDeadBossIsNotClear) {
    EXPECT_FALSE(AllHostilesDead({View(true)}, /*hasBoss=*/true, View(false)));
}

TEST(RunLoopFloorClear, BossViewIgnoredWhenHasBossFalse) {
    // A live bossView must be ignored when hasBoss is false (no boss on the floor).
    EXPECT_TRUE(AllHostilesDead({View(false)}, /*hasBoss=*/false, View(true)));
}

// =====================================================================
// Step 2b: restart = fresh run (ResetRunState), and death -> Ended phase.
// =====================================================================

// The crux of restart: a brand-new run from the template, NOT a continuation of
// the dead floor. After playing into floor 2 with a carried snapshot, ResetRunState
// (what EndScene's restart drives via StartRun) returns to floor 0 with the
// continuation CLEARED, so the next GameScene builds from the template.
TEST(RunLoopController, ResetRunStateIsAFreshRunFromTemplate) {
    RunController rc;
    CombatStats hurt;
    hurt.hp = 2;
    hurt.maxHp = 6;
    rc.AdvanceFloor(PlayerContinuation{hurt, "Gun099"});
    rc.AdvanceFloor(PlayerContinuation{hurt, "Gun099"});
    ASSERT_EQ(rc.State().floorIndex, 2);
    ASSERT_TRUE(rc.State().carried.has_value());

    rc.ResetRunState();

    EXPECT_EQ(rc.State().floorIndex, 0) << "restart returns to floor 0";
    EXPECT_FALSE(rc.State().carried.has_value())
        << "restart CLEARS the continuation -> next GameScene builds from template, not dead-floor hp";
    EXPECT_EQ(rc.State().phase, RunState::Phase::Playing);
    EXPECT_EQ(rc.CurrentFloorSeed(), PerFloorSeed(RunState::kDefaultRunSeed, 0));
    EXPECT_EQ(rc.State().runSeed, RunState::kDefaultRunSeed) << "runSeed preserved across restart";
}

TEST(RunLoopController, OnPlayerDiedMarksRunEnded) {
    RunController rc;
    EXPECT_EQ(rc.State().phase, RunState::Phase::Playing);
    rc.OnPlayerDied(); // replaces the (empty) stack top with a minimal EndScene.
    EXPECT_EQ(rc.State().phase, RunState::Phase::Ended)
        << "death moves the run to the Ended phase";
}

// NOLINTEND(readability-magic-numbers)
