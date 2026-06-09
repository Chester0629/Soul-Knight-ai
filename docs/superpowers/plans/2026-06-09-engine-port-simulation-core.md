# Engine Port — Simulation Core (Plan 4a of 4) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Author `Game::Sim::Simulation` — the headless, deterministic aggregator that owns
`FixedClock` + `Scheduler` + `FireSystem` + the Enemy/Boss/Weapon controllers + bullets +
in-sim hit resolution — wiring the already-built sim leaves into one fixed-timestep tick,
fully unit-tested with `NullWorldCollision`. **Zero GameScene changes** (that is Plan 4b).

**Architecture:** `Simulation(runSeed, WorldCollision*)` holds the clock/scheduler/firesystem,
controllers via `std::unique_ptr` (stable pointers — they capture `this` in scheduler
callbacks; `BossController` is move-deleted), a single `std::vector<FireIntent>` the
controllers push into, the `std::vector<BulletState>` it owns, and a dedicated
`RGRandom m_HitRng` for crit rolls. `Advance(dtMs, WorldInputs)` runs N fixed `Step()`s; each
`Step()` follows the spec §5 order: set targets → `Scheduler.Tick()` → weapon tick → move +
wall-slide → drain `FireIntent`s through `FireSystem` → integrate bullets → resolve hits
(ascending bullet id) → cull. The class is move/copy-deleted (controllers hold raw pointers to
its members). The player is an **input** (`WorldInputs`), not a sim-integrated entity.

**Tech Stack:** C++17, MSVC `/W4`, GoogleTest, CMake (`SoulKnightTests`).
**Conventions:** ASCII-only; guard `GAME_SIM_<NAME>_HPP`; `namespace Game::Sim`; init every
member; `kFoo` constants; Doxygen-light. Build/test:
`cmake --build build --config Debug --target SoulKnightTests` then
`ctest --test-dir build -C Debug -R <Suite>`.

---

## Design (read first)

**Locked design decisions (grounded in the spec + the existing code; see the Plan-4 understanding sweep):**

1. **Aggregator class** `Game::Sim::Simulation` in `include/sim/Simulation.hpp` +
   `src/sim/Simulation.cpp`. Headless, `NullWorldCollision`-testable. **Move/copy-deleted**
   (controllers and the scheduler hold raw pointers to its members, so the object must never
   relocate after `Activate`).
2. **Slice content:** `EnemyAI01` enemies + one `BossAI01` boss + `Gun001`/`Gun016` player
   weapon. **`EnemyAI06` is deferred** (Plan 3 review: it is a child-enemy brain, not a
   kinematic turret) — out of scope here.
3. **Controllers held by `std::unique_ptr`** uniformly: `std::vector<std::unique_ptr<EnemyController>>`
   and `std::unique_ptr<BossController>`. `WeaponController` is movable; held in
   `std::optional<WeaponController>`. **Construction subtlety (move-deleted types):** both
   `EnemyController` and `BossController` are move/copy-deleted, so a factory *prvalue* cannot
   be moved onto the heap — `make_unique` must construct **in place from ctor args**.
   `BossController` takes scalars, so `std::make_unique<BossController>(baseShootCd, spawn,
   maxHp, seed)` works directly. `EnemyController` needs the `EnemyDef`->`Params` mapping, so
   Task 5 adds `BrainFactory::MakeEnemyPtr` (`std::make_unique<EnemyController>(EnemyParams(def),
   spawn, seed)` — in place, no move). The movable `WeaponController` uses
   `m_Weapon.emplace(BrainFactory::MakeWeapon(...))`.
4. **Player is an input.** `WorldInputs{playerPos, playerAlive, firing, aimDir, playerRoomId}`
   is fed each `Advance`; held constant across that frame's N fixed steps. Player movement is
   the shell's job (Plan 4b). The sim owns the player's `CombatStats` only so enemy bullets can
   damage it (`SetPlayerStats` seeds it; `PlayerStats()` reads it back).
5. **Hit resolution lives in the sim.** A dedicated `RGRandom m_HitRng` (seeded `runSeed ^
   kHitRngSalt`) draws the crit roll; hits are resolved in **ascending `BulletState.id` order**
   (deterministic) via `Combat::ResolveHit` + `Combat::ApplyToEnemy`/`ApplyToPlayer`. Enemy
   knockback routes `hr.repelMagnitude` into `EnemyController::ApplyForce`; boss takes
   `MutableState().stats` damage then `OnHurt(hpAfter, maxHp)`.
6. **Sim-local collision:** a pure `CirclesOverlap` helper in `SimMath.hpp` + radius constants
   in `SimConfig.hpp` (no `Util::Collider` dependency → stays headless).
7. **Enemy HP:** `EnemyController`'s ctor leaves `stats.hp == 0` (a fresh enemy reads dead!),
   and `EnemyDef` has no hp field, so `AddEnemy` sets `stats.hp = stats.maxHp = kSliceEnemyHp`
   (3, matching the old `Enemy` entity's `CombatStats{3,3,...}`). `BossController` already sets
   its hp in-ctor, so the boss needs none.
8. **Boss movement = chase-dominant** (`vel = ChaseDir() * BossController::kSpeed`) for the
   slice. `WanderDir()` stays RNG-locked (its cadence draws regardless) but is slice-unused —
   a movement-fidelity follow-on.
9. **Awake gating:** `Advance` wakes every controller whose `State().roomId == in.playerRoomId`
   (and sleeps the rest), mirroring the original room activation. A controller spawns asleep.
10. **Energy stays scene-side** (Plan 4b): the sim exposes `PlayerShotsLastAdvance()` (count of
    player shots emitted in the last `Advance`) so the shell can meter energy. **SimEvent
    emission is deferred** (the `DrainEvents()` queue exists but stays empty this cycle).

**The `Simulation::Step()` order (one 0.02s tick), implemented across Tasks 3–7:**
```
1. for each controller: SetTarget(playerPos)                 // up-to-date aim this tick
2. m_Scheduler.Tick()                                        // enemy/boss scout/wander/shoot callbacks push FireIntents
3. if weapon: m_Weapon->Tick(firing, playerPos, aimDir, m_FireIntents)  // +count player shots
4. movement: enemies ComputeVelocity()+integrate+wall-slide; boss ChaseDir()*kSpeed+integrate+wall-slide
5. drain: for each FireIntent -> m_FireSystem.Expand(fi, m_NextBulletId, m_Bullets); m_FireIntents.clear()
6. bullets: pos += vel*kFixedStepSeconds; lifeMs -= kFixedStepMs; wall-cull (unless canThrough)
7. hits: ascending bullet id; camp 0 -> enemies/boss, camp 1 -> player; ResolveHit + Apply + knockback; pierce
8. cull: Kill() controllers whose stats.IsDead(); deactivate bullets (!active || lifeMs<=0)
```
Dead enemies are **kept in `m_Enemies`** (Kill() cancels their callbacks) so their index stays
a stable view id; only inactive bullets are erased.

---

## File structure

| File | Responsibility |
|---|---|
| `include/sim/SimMath.hpp` (extend) | add inline `CirclesOverlap(a, ar, b, br)`. |
| `include/sim/SimConfig.hpp` (extend) | add slice radius + hit constants. |
| `include/sim/WorldInputs.hpp` (new) | `WorldInputs` POD (per-frame shell input). |
| `include/sim/BrainFactory.hpp` + `src/sim/BrainFactory.cpp` (extend) | add `EnemyParams` + `MakeEnemyPtr` (in-place `unique_ptr` ctor for the move-deleted `EnemyController`). |
| `include/sim/Simulation.hpp` + `src/sim/Simulation.cpp` (new) | the aggregator. |
| `test/SimMathTest.cpp` (extend), `test/SimulationTest.cpp` (new) | tests. |
| `files.cmake` (extend) | register WorldInputs.hpp, Simulation.cpp/.hpp, SimulationTest.cpp. |

---

## Task 1: SimMath `CirclesOverlap` + SimConfig slice constants

**Files:** Modify `include/sim/SimMath.hpp`, `include/sim/SimConfig.hpp`,
`test/SimMathTest.cpp`.

- [ ] **Step 1: Append the test to `test/SimMathTest.cpp`** (immediately before the closing
  `// NOLINTEND(readability-magic-numbers)` line):
```cpp
TEST(SimMathTest, CirclesOverlapByCenterDistance) {
    using Game::Sim::CirclesOverlap;
    // centers 5 apart, radii 3+3=6 -> overlap; radii 2+2=4 -> no overlap.
    EXPECT_TRUE(CirclesOverlap(glm::vec2{0.0F, 0.0F}, 3.0F, glm::vec2{5.0F, 0.0F}, 3.0F));
    EXPECT_FALSE(CirclesOverlap(glm::vec2{0.0F, 0.0F}, 2.0F, glm::vec2{5.0F, 0.0F}, 2.0F));
    // exact touch (distance == sum) counts as overlap (inclusive).
    EXPECT_TRUE(CirclesOverlap(glm::vec2{0.0F, 0.0F}, 2.0F, glm::vec2{4.0F, 0.0F}, 2.0F));
}
```
Add `using Game::Sim::CirclesOverlap;` is done inline above; no new include needed (SimMath
already included).

- [ ] **Step 2: Add `CirclesOverlap` to `include/sim/SimMath.hpp`** (inside `namespace
  Game::Sim`, after `Normalize`):
```cpp
/// True if two circles touch or overlap (distance(a,b) <= ar+br). Squared-distance
/// compare; no sqrt. Used by the sim's bullet<->entity collision (engine-free).
inline bool CirclesOverlap(glm::vec2 a, float ar, glm::vec2 b, float br) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float sum = ar + br;
    return (dx * dx + dy * dy) <= (sum * sum);
}
```

- [ ] **Step 3: Add slice constants to `include/sim/SimConfig.hpp`** (inside `namespace
  Game::Sim`, before the closing brace):
```cpp
/// Slice collision radii (px). The sim is geometry-light: bodies/bullets are circles.
/// These mirror GameScene's kPlayerRadius(16)/kBossBodyRadius(24) placeholders.
inline constexpr float kBulletRadius = 4.0F;
inline constexpr float kPlayerBodyRadius = 16.0F;
inline constexpr float kEnemyBodyRadius = 16.0F;
inline constexpr float kBossBodyRadius = 24.0F;

/// Slice enemy HP. EnemyDef carries no hp field and EnemyController leaves stats.hp=0,
/// so the Simulation seeds it; mirrors the old Enemy entity's CombatStats{3,3,...}.
inline constexpr int kSliceEnemyHp = 3;

/// Repel input scale fed to Combat::AttackerInput (mirrors GameScene's kRepelScale=30).
inline constexpr float kRepelScale = 30.0F;

/// Salt mixed into runSeed for the hit-resolution RNG stream, so the crit-roll stream
/// is distinct from every brain stream (which are seeded from runSeed + offsets).
inline constexpr int kHitRngSalt = 0x5170;
```

- [ ] **Step 4: Build + test.** No `files.cmake` change (both headers already registered).
```bash
cmake --build build --config Debug --target SoulKnightTests 2>&1 | tail -8
ctest --test-dir build -C Debug -R SimMathTest --output-on-failure
```
Expected: `/W4`-clean, `SimMathTest` 3/3 (the 2 existing + `CirclesOverlapByCenterDistance`).

- [ ] **Step 5: Commit**
```bash
git add include/sim/SimMath.hpp include/sim/SimConfig.hpp test/SimMathTest.cpp
git commit -m "feat(sim): SimMath CirclesOverlap + SimConfig slice collision/hit constants"
```

---

## Task 2: WorldInputs + Simulation skeleton (construction, empty Advance, views)

**Files:** Create `include/sim/WorldInputs.hpp`, `include/sim/Simulation.hpp`,
`src/sim/Simulation.cpp`, `test/SimulationTest.cpp`; Modify `files.cmake`.

- [ ] **Step 1: Write the failing test `test/SimulationTest.cpp`**
```cpp
#include <gtest/gtest.h>

#include <vector>

#include "sim/Simulation.hpp"
#include "sim/WorldCollision.hpp"
#include "sim/WorldInputs.hpp"

using Game::Sim::Simulation;
using Game::Sim::WorldInputs;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
Game::Sim::NullWorldCollision g_NullWorld;
WorldInputs Idle() {
    WorldInputs in;
    in.playerPos = glm::vec2{0.0F, 0.0F};
    in.aimDir = glm::vec2{1.0F, 0.0F};
    in.playerRoomId = 0;
    return in;
}
} // namespace

TEST(SimulationTest, AdvanceRunsFixedStepsAndStartsEmpty) {
    Simulation sim(/*runSeed=*/123, &g_NullWorld);
    EXPECT_TRUE(sim.Bullets().empty());
    EXPECT_FALSE(sim.HasBoss());
    EXPECT_TRUE(sim.EnemyViews().empty());

    const int steps = sim.Advance(/*dtMs=*/100.0F, Idle()); // 100/20 = 5 steps
    EXPECT_EQ(steps, 5);
    EXPECT_EQ(sim.PlayerShotsLastAdvance(), 0);
    EXPECT_TRUE(sim.Bullets().empty());
    EXPECT_TRUE(sim.DrainEvents().empty());
}

TEST(SimulationTest, EmptyAdvanceIsReplayDeterministic) {
    auto run = [](int seed) {
        Simulation sim(seed, &g_NullWorld);
        for (int i = 0; i < 10; ++i) {
            sim.Advance(20.0F, Idle());
        }
        return sim.Bullets().size();
    };
    EXPECT_EQ(run(7), run(7));
}

// NOLINTEND(readability-magic-numbers)
```

- [ ] **Step 2: Create `include/sim/WorldInputs.hpp`**
```cpp
#ifndef GAME_SIM_WORLDINPUTS_HPP
#define GAME_SIM_WORLDINPUTS_HPP

#include <glm/glm.hpp>

namespace Game::Sim {

/// Per-frame input the shell feeds the Simulation. Held constant across the N fixed
/// steps of one Advance(). The player is an INPUT, not a sim-integrated entity:
/// movement + energy stay shell-side (Plan 4b).
struct WorldInputs {
    glm::vec2 playerPos{0.0F, 0.0F};
    glm::vec2 aimDir{1.0F, 0.0F}; ///< unit aim (shell normalizes).
    bool playerAlive = true;
    bool firing = false;
    int playerRoomId = -1; ///< wakes controllers whose roomId matches.
};

} // namespace Game::Sim

#endif /* GAME_SIM_WORLDINPUTS_HPP */
```

- [ ] **Step 3: Create `include/sim/Simulation.hpp`** (full public API declared up front;
  later tasks fill the .cpp — keeps the header type-stable):
```cpp
#ifndef GAME_SIM_SIMULATION_HPP
#define GAME_SIM_SIMULATION_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "combat/CombatStats.hpp"
#include "data/GameData.hpp"
#include "data/RGRandom.hpp"
#include "sim/BossController.hpp"
#include "sim/BulletState.hpp"
#include "sim/EnemyController.hpp"
#include "sim/FireIntent.hpp"
#include "sim/FireSystem.hpp"
#include "sim/FixedClock.hpp"
#include "sim/Scheduler.hpp"
#include "sim/SimEvent.hpp"
#include "sim/WeaponController.hpp"
#include "sim/WorldCollision.hpp"
#include "sim/WorldInputs.hpp"

namespace Game::Sim {

/// The deterministic, headless sim root. Owns the clock/scheduler/firesystem, the
/// Enemy/Boss/Weapon controllers, the bullet list, and a dedicated hit-resolution RNG.
/// Move/copy DELETED: controllers + scheduler hold raw pointers to this object's
/// members, so it must never relocate after Activate.
class Simulation {
public:
    /// A render-facing snapshot of one controller's logical state.
    struct EntityView {
        std::uint32_t id = 0; ///< stable within a run (enemy index / boss sentinel).
        glm::vec2 pos{0.0F, 0.0F};
        glm::vec2 facing{1.0F, 0.0F};
        int hp = 0;
        int maxHp = 0;
        bool alive = false;
        int roomId = -1;
    };

    static constexpr std::uint32_t kBossViewId = 0xB055; ///< sentinel id for the boss view.

    Simulation(int runSeed, WorldCollision *world);

    Simulation(const Simulation &) = delete;
    Simulation &operator=(const Simulation &) = delete;
    Simulation(Simulation &&) = delete;
    Simulation &operator=(Simulation &&) = delete;

    // --- setup (call before driving) ---------------------------------------
    /// Spawn an EnemyAI01 enemy (slice hp) in @p roomId; Activated immediately (asleep).
    void AddEnemy(const Game::EnemyDef &def, glm::vec2 spawn, int roomId, int seed);
    /// Spawn the single boss in @p roomId; Activated immediately (asleep).
    void SetBoss(float baseShootCd, glm::vec2 spawn, int maxHp, int roomId, int seed);
    /// Build (or rebuild, cold) the player weapon from a def. "Gun016" -> HeatMinigun.
    void EquipWeapon(const Game::WeaponDef &def, const std::string &weaponId, int seed);
    /// Seed the player's combat vitals (enemy bullets damage these; read back via PlayerStats).
    void SetPlayerStats(const Game::CombatStats &stats);

    // --- drive -------------------------------------------------------------
    /// Run FixedClock.Advance(dtMs) fixed Step()s with @p in held constant. Returns step count.
    int Advance(float dtMs, const WorldInputs &in);

    // --- views (poll after Advance) ----------------------------------------
    const std::vector<BulletState> &Bullets() const { return m_Bullets; }
    std::vector<EntityView> EnemyViews() const;
    bool HasBoss() const { return m_Boss != nullptr; }
    EntityView BossView() const;
    const Game::CombatStats &PlayerStats() const { return m_PlayerStats; }
    int PlayerShotsLastAdvance() const { return m_PlayerShotsLastAdvance; }
    /// Move out the queued anim/sfx events (empty this cycle; emission is deferred).
    std::vector<SimEvent> DrainEvents();

private:
    void Step();                              // one fixed tick (built across Tasks 3-7).
    void WakeByRoom();                         // awake = (roomId == m_Input.playerRoomId).
    void TickWeapon();                         // Task 3.
    void MoveControllers();                    // Task 5/6.
    void DrainFireIntents();                   // Task 3.
    void IntegrateBullets();                   // Task 4.
    void ResolveHits();                        // Task 7.
    void Cull();                               // Task 5/7.
    static EntityView ViewOf(const EntityState &s, std::uint32_t id);

    int m_RunSeed;
    NullWorldCollision m_NullWorld;          ///< fallback; declared BEFORE m_World so the
                                             ///< ctor can point m_World at it safely.
    WorldCollision *m_World;                 ///< non-owning; never null (ctor uses m_NullWorld if null).

    FixedClock m_Clock;
    Scheduler m_Scheduler;
    FireSystem m_FireSystem;
    std::vector<FireIntent> m_FireIntents; ///< controllers push here; cleared each step.
    std::vector<BulletState> m_Bullets;
    std::uint32_t m_NextBulletId = 1;
    Game::RGRandom m_HitRng;

    std::vector<std::unique_ptr<EnemyController>> m_Enemies;
    std::unique_ptr<BossController> m_Boss;
    std::optional<WeaponController> m_Weapon;

    Game::CombatStats m_PlayerStats{};
    WorldInputs m_Input{};
    int m_PlayerShotsLastAdvance = 0;
    std::vector<SimEvent> m_Events;
};

} // namespace Game::Sim

#endif /* GAME_SIM_SIMULATION_HPP */
```

- [ ] **Step 4: Create `src/sim/Simulation.cpp`** (ctor + skeleton; the per-step helpers are
  no-ops until Tasks 3–7 fill them):
```cpp
#include "sim/Simulation.hpp"

#include <algorithm>

#include "sim/SimConfig.hpp"
#include "sim/SimMath.hpp"

namespace Game::Sim {

Simulation::Simulation(int runSeed, WorldCollision *world)
    : m_RunSeed(runSeed), m_World(world != nullptr ? world : &m_NullWorld) {
    m_HitRng.SetRandomSeed(runSeed ^ kHitRngSalt);
    // The controllers capture &m_FireIntents; reserve so the buffer is stable churn-free.
    m_FireIntents.reserve(64);
}

void Simulation::SetPlayerStats(const Game::CombatStats &stats) { m_PlayerStats = stats; }

std::vector<SimEvent> Simulation::DrainEvents() {
    std::vector<SimEvent> out;
    out.swap(m_Events);
    return out;
}

Simulation::EntityView Simulation::ViewOf(const EntityState &s, std::uint32_t id) {
    EntityView v;
    v.id = id;
    v.pos = s.pos;
    v.facing = s.facing;
    v.hp = s.stats.hp;
    v.maxHp = s.stats.maxHp;
    v.alive = !s.dead;
    v.roomId = s.roomId;
    return v;
}

std::vector<Simulation::EntityView> Simulation::EnemyViews() const {
    std::vector<EntityView> views;
    views.reserve(m_Enemies.size());
    std::uint32_t id = 0;
    for (const auto &e : m_Enemies) {
        views.push_back(ViewOf(e->State(), id));
        ++id;
    }
    return views;
}

Simulation::EntityView Simulation::BossView() const {
    return m_Boss != nullptr ? ViewOf(m_Boss->State(), kBossViewId) : EntityView{};
}

void Simulation::WakeByRoom() {
    for (auto &e : m_Enemies) {
        e->MutableState().awake = (e->State().roomId == m_Input.playerRoomId);
    }
    if (m_Boss != nullptr) {
        m_Boss->MutableState().awake = (m_Boss->State().roomId == m_Input.playerRoomId);
    }
}

// Per-step helpers filled across Tasks 3-7.
void Simulation::TickWeapon() {}
void Simulation::MoveControllers() {}
void Simulation::DrainFireIntents() {}
void Simulation::IntegrateBullets() {}
void Simulation::ResolveHits() {}
void Simulation::Cull() {}

void Simulation::Step() {
    const glm::vec2 target = m_Input.playerPos;
    for (auto &e : m_Enemies) {
        e->SetTarget(target);
    }
    if (m_Boss != nullptr) {
        m_Boss->SetTarget(target);
    }
    m_Scheduler.Tick();
    TickWeapon();
    MoveControllers();
    DrainFireIntents();
    IntegrateBullets();
    ResolveHits();
    Cull();
}

int Simulation::Advance(float dtMs, const WorldInputs &in) {
    m_Input = in;
    m_PlayerShotsLastAdvance = 0;
    WakeByRoom();
    const int steps = m_Clock.Advance(dtMs);
    for (int i = 0; i < steps; ++i) {
        Step();
    }
    return steps;
}

// AddEnemy / SetBoss / EquipWeapon implemented in Tasks 3, 5, 6.
void Simulation::AddEnemy(const Game::EnemyDef &, glm::vec2, int, int) {}
void Simulation::SetBoss(float, glm::vec2, int, int, int) {}
void Simulation::EquipWeapon(const Game::WeaponDef &, const std::string &, int) {}

} // namespace Game::Sim
```

- [ ] **Step 5: Register + build + test.** `files.cmake`: SRC `sim/Simulation.cpp`; INCLUDE
  `sim/WorldInputs.hpp` and `sim/Simulation.hpp` (after `sim/BrainFactory.hpp`/`sim/BossController.hpp`
  in the "Engine port -- sim infrastructure" block); TEST `SimulationTest.cpp` (after
  `BossControllerTest.cpp`).
```bash
cmake --build build --config Debug --target SoulKnightTests 2>&1 | tail -12
ctest --test-dir build -C Debug -R SimulationTest --output-on-failure
```
Expected: `/W4`-clean, `SimulationTest` 2/2.

- [ ] **Step 6: Commit**
```bash
git add include/sim/WorldInputs.hpp include/sim/Simulation.hpp src/sim/Simulation.cpp test/SimulationTest.cpp files.cmake
git commit -m "feat(sim): Simulation skeleton -- WorldInputs, fixed-step Advance, views"
```

---

## Task 3: Player weapon path (EquipWeapon + per-step Tick -> FireSystem bullets)

**Files:** Modify `src/sim/Simulation.cpp`, `test/SimulationTest.cpp`.

- [ ] **Step 1: Append the test** (before `// NOLINTEND`):
```cpp
TEST(SimulationTest, EquippedWeaponFiresPlayerBulletsOnCadence) {
    Simulation sim(55, &g_NullWorld);
    Game::WeaponDef def{};
    def.weaponSpeed = 1.0F;
    def.bulletSpeed = 10.0F; // *15 = 150 px/s
    def.atk = 4;
    def.deviation = 0;       // clean +x direction
    sim.EquipWeapon(def, "Gun001", 808);

    WorldInputs in = Idle();
    in.firing = true;
    // fireInterval = 0.15s/weaponSpeed -> SecondsToTicks(0.15)=8 ticks; first shot on tick 1.
    sim.Advance(20.0F, in); // 1 step -> first shot fires
    ASSERT_EQ(sim.Bullets().size(), 1U);
    EXPECT_EQ(sim.Bullets()[0].camp, 0);     // player bullet
    EXPECT_EQ(sim.Bullets()[0].damage, 4);   // def.atk
    EXPECT_EQ(sim.PlayerShotsLastAdvance(), 1);

    in.firing = false;
    sim.Advance(20.0F, in);
    EXPECT_EQ(sim.PlayerShotsLastAdvance(), 0); // not firing -> no new shot
    EXPECT_EQ(sim.Bullets().size(), 1U);
}

TEST(SimulationTest, NotFiringProducesNoBullets) {
    Simulation sim(1, &g_NullWorld);
    Game::WeaponDef def{};
    sim.EquipWeapon(def, "Gun001", 1);
    for (int i = 0; i < 10; ++i) {
        sim.Advance(20.0F, Idle()); // firing defaults false
    }
    EXPECT_TRUE(sim.Bullets().empty());
}
```

- [ ] **Step 2: Implement `EquipWeapon`, `TickWeapon`, `DrainFireIntents` in
  `src/sim/Simulation.cpp`.** Add `#include "sim/BrainFactory.hpp"` to the .cpp. Replace the
  stub bodies:
```cpp
void Simulation::EquipWeapon(const Game::WeaponDef &def, const std::string &weaponId,
                             int seed) {
    m_Weapon.emplace(BrainFactory::MakeWeapon(def, weaponId, seed)); // cold rebuild
}

void Simulation::TickWeapon() {
    if (!m_Weapon.has_value()) {
        return;
    }
    const std::size_t before = m_FireIntents.size();
    m_Weapon->Tick(m_Input.firing, m_Input.playerPos, m_Input.aimDir, m_FireIntents);
    m_PlayerShotsLastAdvance += static_cast<int>(m_FireIntents.size() - before);
}

void Simulation::DrainFireIntents() {
    for (const FireIntent &fi : m_FireIntents) {
        m_FireSystem.Expand(fi, m_NextBulletId, m_Bullets);
    }
    m_FireIntents.clear();
}
```
(`Game::WeaponDef def{}` default has `weaponSpeed = 1.0F`, so `MakeWeapon`'s
`fireIntervalSeconds /= max(0.01, weaponSpeed)` is safe; `damage = def.atk`,
`bulletSpeedPxPerSec = def.bulletSpeed * kDataSpeedToPxPerSec`.)

- [ ] **Step 3: Build + test** `ctest -R SimulationTest` -> 4/4. `/W4`-clean.
- [ ] **Step 4: Commit** `git commit -am "feat(sim): Simulation player weapon path -- EquipWeapon + FireSystem drain"`

---

## Task 4: Bullet motion + lifetime + wall culling

**Files:** Modify `src/sim/Simulation.cpp`, `test/SimulationTest.cpp`.

- [ ] **Step 1: Append the test** (before `// NOLINTEND`). Includes a blocking world stub:
```cpp
namespace {
/// Blocks any point with x >= kWallX (a vertical wall) -- for bullet-cull tests.
class RightWall : public Game::Sim::WorldCollision {
public:
    static constexpr float kWallX = 100.0F;
    bool Blocks(glm::vec2 pos, float /*radius*/) const override { return pos.x >= kWallX; }
};
} // namespace

TEST(SimulationTest, BulletAdvancesAndExpires) {
    Simulation sim(1, &g_NullWorld);
    Game::WeaponDef def{};
    def.bulletSpeed = 10.0F; // 150 px/s -> 3 px/step
    sim.EquipWeapon(def, "Gun001", 1);
    WorldInputs in = Idle();
    in.firing = true;
    sim.Advance(20.0F, in);           // fire one bullet (lifeMs default 1500)
    ASSERT_EQ(sim.Bullets().size(), 1U);
    const float x0 = sim.Bullets()[0].pos.x;
    in.firing = false;
    sim.Advance(20.0F, in);           // one more step: pos advances ~3px, life -20ms
    ASSERT_EQ(sim.Bullets().size(), 1U);
    EXPECT_GT(sim.Bullets()[0].pos.x, x0);
    EXPECT_LT(sim.Bullets()[0].lifeMs, 1500.0F);
    // Drive long enough to expire (1500ms / 20ms = 75 steps) -> culled.
    for (int i = 0; i < 80; ++i) {
        sim.Advance(20.0F, in);
    }
    EXPECT_TRUE(sim.Bullets().empty());
}

TEST(SimulationTest, BulletCulledByWallUnlessCanThrough) {
    RightWall wall;
    Simulation sim(1, &wall);
    Game::WeaponDef def{};
    def.bulletSpeed = 40.0F; // 600 px/s -> 12 px/step, crosses x=100 quickly
    sim.EquipWeapon(def, "Gun001", 1);
    WorldInputs in = Idle();
    in.firing = true;
    sim.Advance(20.0F, in); // fire at origin aiming +x
    in.firing = false;
    for (int i = 0; i < 12; ++i) {
        sim.Advance(20.0F, in); // bullet marches toward the wall at x>=100
    }
    EXPECT_TRUE(sim.Bullets().empty()); // hit the wall, culled
}
```

- [ ] **Step 2: Implement `IntegrateBullets` and the bullet-cull half of `Cull` in
  `src/sim/Simulation.cpp`.** Replace the `IntegrateBullets` stub and the `Cull` stub:
```cpp
void Simulation::IntegrateBullets() {
    for (BulletState &b : m_Bullets) {
        if (!b.active) {
            continue;
        }
        b.pos += b.vel * kFixedStepSeconds;
        b.lifeMs -= kFixedStepMs;
        if (!b.canThrough && m_World->Blocks(b.pos, kBulletRadius)) {
            b.active = false;
        }
    }
}

void Simulation::Cull() {
    // Enemy/boss death culling is added in Tasks 5/7; bullet culling here.
    m_Bullets.erase(std::remove_if(m_Bullets.begin(), m_Bullets.end(),
                                   [](const BulletState &b) {
                                       return !b.active || b.lifeMs <= 0.0F;
                                   }),
                    m_Bullets.end());
}
```
(`kFixedStepSeconds`/`kFixedStepMs`/`kBulletRadius` come from SimConfig, already included.)

- [ ] **Step 3: Build + test** `ctest -R SimulationTest` -> 6/6. `/W4`-clean.
- [ ] **Step 4: Commit** `git commit -am "feat(sim): Simulation bullet motion + lifetime + wall cull"`

---

## Task 5: Enemy spawn + drive (MakeEnemyPtr, awake-by-room, move, fire)

**Files:** Modify `include/sim/BrainFactory.hpp`, `src/sim/BrainFactory.cpp`,
`src/sim/Simulation.cpp`, `test/SimulationTest.cpp`.

- [ ] **Step 1: Extend `BrainFactory` with `EnemyParams` + `MakeEnemyPtr`.** `EnemyController`
  is move-deleted, so the by-value `MakeEnemy` prvalue cannot feed a `unique_ptr`; add an
  in-place pointer factory. In `include/sim/BrainFactory.hpp` add `#include <memory>` and these
  declarations inside the class, after `MakeEnemy`:
```cpp
    /// The EnemyDef -> Params mapping (shared by MakeEnemy + MakeEnemyPtr).
    static EnemyController::Params EnemyParams(const Game::EnemyDef &def);

    /// Heap-construct an EnemyController in place. EnemyController is move-deleted, so a
    /// by-value factory prvalue cannot be moved into a unique_ptr; make_unique forwards the
    /// ctor args instead (no move).
    static std::unique_ptr<EnemyController> MakeEnemyPtr(const Game::EnemyDef &def,
                                                         glm::vec2 spawn, int seed);
```
In `src/sim/BrainFactory.cpp` add `#include <memory>`, extract the mapping into `EnemyParams`,
refactor `MakeEnemy` to reuse it, and add `MakeEnemyPtr`:
```cpp
EnemyController::Params BrainFactory::EnemyParams(const Game::EnemyDef &def) {
    EnemyController::Params p;
    p.speed = 60.0F;     // slice default (EnemyDef has no per-enemy speed)
    p.speedRate = 0.0F;  // slice default (EnemyDef has no speed_rate field)
    p.friction = def.friction;
    p.scoutRateSeconds = def.scoutRate;
    p.shootCdSeconds = def.shootCd;
    p.kinematic = def.kinematic != 0;
    return p;
}

EnemyController BrainFactory::MakeEnemy(const Game::EnemyDef &def, glm::vec2 spawn, int seed) {
    return EnemyController(EnemyParams(def), spawn, seed); // prvalue -> C++17 guaranteed elision
}

std::unique_ptr<EnemyController> BrainFactory::MakeEnemyPtr(const Game::EnemyDef &def,
                                                            glm::vec2 spawn, int seed) {
    // make_unique forwards (Params, spawn, seed) to the ctor -> in-place, no move.
    return std::make_unique<EnemyController>(EnemyParams(def), spawn, seed);
}
```
(`MakeEnemy`'s observable behaviour is unchanged, so `BrainFactoryTest` still passes 6/6.)

- [ ] **Step 2: Append the SimulationTest enemy tests** (before `// NOLINTEND`):
```cpp
TEST(SimulationTest, EnemyAsleepUntilPlayerEntersRoom) {
    Simulation sim(20240607, &g_NullWorld);
    Game::EnemyDef def{};
    def.shootCd = 0.5F;
    def.scoutRate = 0.5F;
    def.friction = 0.9F;
    sim.AddEnemy(def, glm::vec2{50.0F, 0.0F}, /*roomId=*/2, /*seed=*/1234);

    WorldInputs in = Idle();
    in.playerRoomId = 1; // different room -> enemy asleep
    for (int i = 0; i < 60; ++i) {
        sim.Advance(20.0F, in);
    }
    ASSERT_EQ(sim.EnemyViews().size(), 1U);
    EXPECT_TRUE(sim.Bullets().empty());                 // asleep: no fire
    EXPECT_FLOAT_EQ(sim.EnemyViews()[0].pos.x, 50.0F);  // asleep: no move
    EXPECT_TRUE(sim.EnemyViews()[0].alive);
    EXPECT_EQ(sim.EnemyViews()[0].hp, Game::Sim::kSliceEnemyHp);
}

TEST(SimulationTest, AwakeEnemyMovesTowardPlayerAndFires) {
    Simulation sim(20240607, &g_NullWorld);
    Game::EnemyDef def{};
    def.shootCd = 0.2F;
    def.scoutRate = 0.1F;
    def.friction = 0.9F;
    sim.AddEnemy(def, glm::vec2{100.0F, 0.0F}, /*roomId=*/2, /*seed=*/4242);

    WorldInputs in = Idle();
    in.playerPos = glm::vec2{0.0F, 0.0F};
    in.playerRoomId = 2; // same room -> awake
    for (int i = 0; i < 120; ++i) {
        sim.Advance(20.0F, in);
    }
    const auto ev = sim.EnemyViews()[0];
    // EnemyAI01 scouts/wanders, so don't assume a direction -- just that it moved.
    EXPECT_GT(glm::length(ev.pos - glm::vec2{100.0F, 0.0F}), 0.5F);
    EXPECT_FALSE(sim.Bullets().empty()); // fired enemy bullets (camp 1)
    EXPECT_EQ(sim.Bullets().front().camp, 1);
}

TEST(SimulationTest, EnemyReplayIsByteIdentical) {
    auto run = [](int seed) {
        Simulation sim(seed, &g_NullWorld);
        Game::EnemyDef def{};
        def.shootCd = 0.2F;
        def.scoutRate = 0.1F;
        def.friction = 0.9F;
        sim.AddEnemy(def, glm::vec2{80.0F, 20.0F}, 2, seed + 1000);
        WorldInputs in = Idle();
        in.playerRoomId = 2;
        std::vector<float> trace;
        for (int i = 0; i < 80; ++i) {
            sim.Advance(20.0F, in);
            const auto v = sim.EnemyViews()[0];
            trace.push_back(v.pos.x);
            trace.push_back(v.pos.y);
            trace.push_back(static_cast<float>(sim.Bullets().size()));
        }
        return trace;
    };
    EXPECT_EQ(run(99), run(99));
}
```

- [ ] **Step 3: Implement `AddEnemy`, `MoveControllers` (enemy half), `Cull` (enemy half) in
  `src/sim/Simulation.cpp`.** Replace the `AddEnemy` stub and the `MoveControllers` stub, and
  extend `Cull`:
```cpp
void Simulation::AddEnemy(const Game::EnemyDef &def, glm::vec2 spawn, int roomId, int seed) {
    std::unique_ptr<EnemyController> ec = BrainFactory::MakeEnemyPtr(def, spawn, seed);
    ec->MutableState().roomId = roomId;
    ec->MutableState().stats.hp = kSliceEnemyHp;    // EnemyDef has no hp; seed the slice value.
    ec->MutableState().stats.maxHp = kSliceEnemyHp; // (EnemyController leaves stats.hp == 0).
    ec->Activate(m_Scheduler, m_FireIntents);        // asleep until WakeByRoom().
    m_Enemies.push_back(std::move(ec));
}

void Simulation::MoveControllers() {
    for (auto &e : m_Enemies) {
        if (e->State().dead) {
            continue;
        }
        const glm::vec2 vel = e->ComputeVelocity(); // ONCE per step (decays knockback).
        glm::vec2 pos = e->State().pos;
        // Axis-separated wall slide (same shape as the GameScene player pattern).
        const glm::vec2 tryX{pos.x + vel.x * kFixedStepSeconds, pos.y};
        if (!m_World->Blocks(tryX, kEnemyBodyRadius)) {
            pos.x = tryX.x;
        }
        const glm::vec2 tryY{pos.x, pos.y + vel.y * kFixedStepSeconds};
        if (!m_World->Blocks(tryY, kEnemyBodyRadius)) {
            pos.y = tryY.y;
        }
        e->MutableState().pos = pos;
    }
    // Boss movement added in Task 6.
}
```
And extend `Cull` to kill HP-dead enemies (place BEFORE the bullet erase):
```cpp
void Simulation::Cull() {
    for (auto &e : m_Enemies) {
        if (!e->State().dead && e->State().stats.IsDead()) {
            e->Kill(); // cancels its scheduler cadence; kept in m_Enemies for stable view id.
        }
    }
    m_Bullets.erase(std::remove_if(m_Bullets.begin(), m_Bullets.end(),
                                   [](const BulletState &b) {
                                       return !b.active || b.lifeMs <= 0.0F;
                                   }),
                    m_Bullets.end());
}
```
- [ ] **Step 4: Build + test** `ctest -R "SimulationTest|BrainFactoryTest"` -> SimulationTest
  9/9 and BrainFactoryTest still 6/6. `/W4`-clean.
- [ ] **Step 5: Commit**
```bash
git add include/sim/BrainFactory.hpp src/sim/BrainFactory.cpp src/sim/Simulation.cpp test/SimulationTest.cpp
git commit -m "feat(sim): Simulation enemy spawn + awake-by-room + move + fire (BrainFactory MakeEnemyPtr)"
```

---

## Task 6: Boss spawn + drive (chase-dominant move + fan fire)

**Files:** Modify `src/sim/Simulation.cpp`, `test/SimulationTest.cpp`.

- [ ] **Step 1: Append the test** (before `// NOLINTEND`):
```cpp
TEST(SimulationTest, BossChasesPlayerAndFiresFan) {
    Simulation sim(20240607, &g_NullWorld);
    sim.SetBoss(/*baseShootCd=*/0.2F, glm::vec2{200.0F, 0.0F}, /*maxHp=*/500, /*roomId=*/3,
                /*seed=*/9000);
    ASSERT_TRUE(sim.HasBoss());
    EXPECT_EQ(sim.BossView().id, Simulation::kBossViewId);
    EXPECT_EQ(sim.BossView().maxHp, 500);

    WorldInputs in = Idle();
    in.playerPos = glm::vec2{0.0F, 0.0F};
    in.playerRoomId = 3; // wake the boss
    for (int i = 0; i < 120; ++i) {
        sim.Advance(20.0F, in);
    }
    EXPECT_LT(sim.BossView().pos.x, 200.0F); // chased toward the player
    ASSERT_FALSE(sim.Bullets().empty());     // fired a fan
    // A fan emits >=3 enemy bullets; confirm camp 1 and multiplicity.
    EXPECT_EQ(sim.Bullets().front().camp, 1);
    EXPECT_GE(sim.Bullets().size(), 3U);
}

TEST(SimulationTest, BossReplayIsByteIdentical) {
    auto run = [](int seed) {
        Simulation sim(seed, &g_NullWorld);
        sim.SetBoss(0.2F, glm::vec2{150.0F, 30.0F}, 500, 3, seed + 9000);
        WorldInputs in = Idle();
        in.playerRoomId = 3;
        std::vector<float> trace;
        for (int i = 0; i < 80; ++i) {
            sim.Advance(20.0F, in);
            trace.push_back(sim.BossView().pos.x);
            trace.push_back(sim.BossView().pos.y);
            trace.push_back(static_cast<float>(sim.Bullets().size()));
        }
        return trace;
    };
    EXPECT_EQ(run(123), run(123));
}
```

- [ ] **Step 2: Implement `SetBoss` and the boss half of `MoveControllers` in
  `src/sim/Simulation.cpp`.** Replace `SetBoss`, and append the boss block to `MoveControllers`
  (after the enemy loop, replacing the `// Boss movement added in Task 6.` comment):
```cpp
void Simulation::SetBoss(float baseShootCd, glm::vec2 spawn, int maxHp, int roomId, int seed) {
    m_Boss = std::make_unique<BossController>(baseShootCd, spawn, maxHp, seed);
    m_Boss->MutableState().roomId = roomId;
    m_Boss->Activate(m_Scheduler, m_FireIntents); // asleep until WakeByRoom().
}
```
Boss block appended inside `MoveControllers` (chase-dominant; WanderDir is slice-unused):
```cpp
    if (m_Boss != nullptr && !m_Boss->State().dead && m_Boss->State().awake) {
        const glm::vec2 vel = m_Boss->ChaseDir() * BossController::kSpeed;
        glm::vec2 pos = m_Boss->State().pos;
        const glm::vec2 tryX{pos.x + vel.x * kFixedStepSeconds, pos.y};
        if (!m_World->Blocks(tryX, kBossBodyRadius)) {
            pos.x = tryX.x;
        }
        const glm::vec2 tryY{pos.x, pos.y + vel.y * kFixedStepSeconds};
        if (!m_World->Blocks(tryY, kBossBodyRadius)) {
            pos.y = tryY.y;
        }
        m_Boss->MutableState().pos = pos;
    }
```

- [ ] **Step 3: Build + test** `ctest -R SimulationTest` -> 11/11. `/W4`-clean.
- [ ] **Step 4: Commit** `git commit -am "feat(sim): Simulation boss spawn + chase move + fan fire"`

---

## Task 7: In-sim hit resolution (overlap, ResolveHit, knockback, death)

**Files:** Modify `src/sim/Simulation.cpp`, `test/SimulationTest.cpp`.

- [ ] **Step 1: Append the test** (before `// NOLINTEND`):
```cpp
TEST(SimulationTest, PlayerBulletDamagesAndKillsEnemy) {
    Simulation sim(20240607, &g_NullWorld);
    Game::EnemyDef def{};
    def.shootCd = 99.0F; // keep the enemy from firing back during the test
    def.scoutRate = 99.0F;
    def.friction = 0.9F;
    // Enemy sitting at the origin; player just to its left firing +x straight into it.
    sim.AddEnemy(def, glm::vec2{0.0F, 0.0F}, /*roomId=*/0, /*seed=*/1);
    Game::WeaponDef wdef{};
    wdef.bulletSpeed = 4.0F; // slow so it lingers on the enemy
    wdef.atk = 1;
    wdef.deviation = 0;
    sim.EquipWeapon(wdef, "Gun001", 7);

    WorldInputs in = Idle();
    in.playerPos = glm::vec2{-20.0F, 0.0F};
    in.aimDir = glm::vec2{1.0F, 0.0F};
    in.firing = true;
    in.playerRoomId = 0; // enemy awake (so Kill/scheduler interplay is exercised)
    const int startHp = sim.EnemyViews()[0].hp;
    for (int i = 0; i < 200 && sim.EnemyViews()[0].alive; ++i) {
        sim.Advance(20.0F, in);
    }
    EXPECT_LT(sim.EnemyViews()[0].hp, startHp);   // took damage
    EXPECT_FALSE(sim.EnemyViews()[0].alive);      // died (hp <= 0)
}

TEST(SimulationTest, EnemyBulletDamagesPlayer) {
    Simulation sim(20240607, &g_NullWorld);
    Game::CombatStats player;
    player.hp = 6;
    player.maxHp = 6;
    sim.SetPlayerStats(player);
    Game::EnemyDef def{};
    def.shootCd = 0.1F;
    def.scoutRate = 99.0F; // hold still
    def.friction = 0.9F;
    sim.AddEnemy(def, glm::vec2{30.0F, 0.0F}, /*roomId=*/0, /*seed=*/2);

    WorldInputs in = Idle();
    in.playerPos = glm::vec2{0.0F, 0.0F}; // enemy fires toward origin (the player)
    in.playerRoomId = 0;
    for (int i = 0; i < 200 && sim.PlayerStats().hp == 6; ++i) {
        sim.Advance(20.0F, in);
    }
    EXPECT_LT(sim.PlayerStats().hp, 6); // an enemy bullet reached the player
}

TEST(SimulationTest, HitResolutionReplayIsByteIdentical) {
    auto run = [](int seed) {
        Simulation sim(seed, &g_NullWorld);
        Game::EnemyDef def{};
        def.shootCd = 0.3F;
        def.scoutRate = 0.2F;
        def.friction = 0.9F;
        sim.AddEnemy(def, glm::vec2{40.0F, 0.0F}, 0, seed + 1000);
        Game::WeaponDef wdef{};
        wdef.bulletSpeed = 8.0F;
        wdef.atk = 1;
        sim.EquipWeapon(wdef, "Gun001", seed + 5);
        WorldInputs in = Idle();
        in.playerPos = glm::vec2{-10.0F, 0.0F};
        in.firing = true;
        in.playerRoomId = 0;
        std::vector<float> trace;
        for (int i = 0; i < 100; ++i) {
            sim.Advance(20.0F, in);
            trace.push_back(static_cast<float>(sim.EnemyViews()[0].hp));
            trace.push_back(static_cast<float>(sim.EnemyViews()[0].alive ? 1 : 0));
            trace.push_back(static_cast<float>(sim.Bullets().size()));
        }
        return trace;
    };
    EXPECT_EQ(run(2024), run(2024));
}
```

- [ ] **Step 2: Implement `ResolveHits` in `src/sim/Simulation.cpp`.** Add
  `#include "combat/Damage.hpp"` to the .cpp. Replace the `ResolveHits` stub:
```cpp
void Simulation::ResolveHits() {
    // Deterministic order: ascending bullet id (m_Bullets is append-ordered by id, but
    // sort defensively so a future reordering cannot desync the hit RNG stream).
    std::sort(m_Bullets.begin(), m_Bullets.end(),
              [](const BulletState &a, const BulletState &b) { return a.id < b.id; });

    for (BulletState &b : m_Bullets) {
        if (!b.active) {
            continue;
        }
        if (b.camp == 0) {
            // Player bullet -> enemies, then boss. One target per bullet per step.
            bool consumed = false;
            for (auto &e : m_Enemies) {
                if (e->State().dead || !CirclesOverlap(b.pos, kBulletRadius, e->State().pos,
                                                       kEnemyBodyRadius)) {
                    continue;
                }
                Game::Combat::AttackerInput in;
                in.baseDamage = b.damage;
                in.critical = b.critical;
                in.repelInputMagnitude = b.repel * kRepelScale;
                const Game::Combat::HitResult hr =
                    Game::Combat::ResolveHit(in, m_HitRng, Game::Combat::Defender::ENEMY);
                Game::Combat::ApplyToEnemy(e->MutableState().stats, hr, true);
                if (!e->State().kinematic) {
                    e->ApplyForce(Normalize(b.vel), hr.repelMagnitude);
                }
                consumed = true;
                break;
            }
            if (!consumed && m_Boss != nullptr && !m_Boss->State().dead &&
                CirclesOverlap(b.pos, kBulletRadius, m_Boss->State().pos, kBossBodyRadius)) {
                Game::Combat::AttackerInput in;
                in.baseDamage = b.damage;
                in.critical = b.critical;
                in.repelInputMagnitude = b.repel * kRepelScale;
                const Game::Combat::HitResult hr =
                    Game::Combat::ResolveHit(in, m_HitRng, Game::Combat::Defender::ENEMY);
                Game::Combat::ApplyToEnemy(m_Boss->MutableState().stats, hr, true);
                m_Boss->OnHurt(m_Boss->State().stats.hp, m_Boss->State().stats.maxHp);
                consumed = true;
            }
            if (consumed && b.pierce <= 0) {
                b.active = false;
            } else if (consumed) {
                --b.pierce; // canThrough/pierce: survive and keep going.
            }
        } else {
            // Enemy bullet -> player.
            if (m_Input.playerAlive &&
                CirclesOverlap(b.pos, kBulletRadius, m_Input.playerPos, kPlayerBodyRadius)) {
                Game::Combat::AttackerInput in;
                in.baseDamage = b.damage;
                in.critical = b.critical;
                in.repelInputMagnitude = b.repel * kRepelScale;
                const Game::Combat::HitResult hr =
                    Game::Combat::ResolveHit(in, m_HitRng, Game::Combat::Defender::PLAYER);
                Game::Combat::ApplyToPlayer(m_PlayerStats, hr, true);
                b.active = false;
            }
        }
    }
}
```
The boss-death `Kill()` is handled in `Cull` (extend it to also kill a HP-dead boss):
```cpp
    if (m_Boss != nullptr && !m_Boss->State().dead && m_Boss->State().stats.IsDead()) {
        m_Boss->Kill();
    }
```
Add that block inside `Cull`, right after the enemy-kill loop (before the bullet erase).

- [ ] **Step 3: Build + test** `ctest -R SimulationTest` -> 14/14. `/W4`-clean. Watch for a
  signed/unsigned or `Normalize` ambiguity; `Normalize` is `Game::Sim::Normalize` (in scope).
- [ ] **Step 4: Commit** `git commit -am "feat(sim): Simulation in-sim hit resolution + knockback + death"`

---

## Task 8: End-to-end golden replay + accessor coverage

**Files:** Modify `test/SimulationTest.cpp` (uses the real `RESOURCE_DIR` data, like
`GameDataTest`).

- [ ] **Step 1: Add the headline end-to-end test.** At the top of the file add the data
  include + resource root:
```cpp
#include "data/GameData.hpp"
```
and in the anonymous namespace:
```cpp
const char *kResourceRoot = RESOURCE_DIR; // CMake-injected, as in GameDataTest.
```
Then append (before `// NOLINTEND`):
```cpp
TEST(SimulationTest, EndToEndPlayerEnemyBulletsCollisionByteIdentical) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::EnemyDef *edef = gd.FindEnemy("EnemyAI01");
    const Game::WeaponDef *wdef = gd.FindWeapon("Gun001");
    ASSERT_NE(edef, nullptr);
    ASSERT_NE(wdef, nullptr);

    auto run = [&](int runSeed) {
        Simulation sim(runSeed, &g_NullWorld);
        Game::CombatStats player;
        player.hp = 6;
        player.maxHp = 6;
        sim.SetPlayerStats(player);
        sim.AddEnemy(*edef, glm::vec2{60.0F, 0.0F}, /*roomId=*/0, runSeed + 1000);
        sim.EquipWeapon(*wdef, "Gun001", runSeed + 5);

        WorldInputs in = Idle();
        in.playerPos = glm::vec2{0.0F, 0.0F};
        in.aimDir = glm::vec2{1.0F, 0.0F};
        in.firing = true;
        in.playerRoomId = 0;

        std::vector<float> trace;
        for (int i = 0; i < 150; ++i) {
            sim.Advance(20.0F, in);
            const auto ev = sim.EnemyViews()[0];
            trace.push_back(ev.pos.x);
            trace.push_back(ev.pos.y);
            trace.push_back(static_cast<float>(ev.hp));
            trace.push_back(static_cast<float>(ev.alive ? 1 : 0));
            trace.push_back(static_cast<float>(sim.PlayerStats().hp));
            trace.push_back(static_cast<float>(sim.Bullets().size()));
        }
        return trace;
    };
    const std::vector<float> a = run(20240607);
    const std::vector<float> b = run(20240607);
    EXPECT_EQ(a, b);                       // byte-identical replay
    EXPECT_NE(a, run(20240608));           // a different seed diverges (sanity)
}

TEST(SimulationTest, AccessorsAndEventsAreSane) {
    Simulation sim(1, &g_NullWorld);
    EXPECT_TRUE(sim.EnemyViews().empty());
    EXPECT_FALSE(sim.HasBoss());
    EXPECT_TRUE(sim.DrainEvents().empty()); // emission deferred this cycle.
    sim.Advance(20.0F, Idle());
    EXPECT_TRUE(sim.DrainEvents().empty());
}
```

- [ ] **Step 2: Build + test** `ctest -R SimulationTest` -> 16/16. `/W4`-clean. (The
  `EXPECT_NE(a, run(20240608))` divergence check may, in the unlucky case of identical traces,
  need a different second seed — pick one that visibly diverges if so.)
- [ ] **Step 3: Commit** `git commit -am "feat(sim): Simulation end-to-end golden replay + accessor coverage"`

---

## Task 9: Full-suite regression gate

- [ ] **Step 1:** `cmake --build build --config Debug --target SoulKnightTests` -> `/W4`-clean.
- [ ] **Step 2:** `ctest --test-dir build -C Debug` -> 100% pass (prior suites + SimMath +
  ~16 SimulationTest cases). No game-behaviour change (GameScene still drives the OLD path;
  the new Simulation is headless and unused by the shell until Plan 4b).
- [ ] **Step 3:** Confirm the `Simulation` is move/copy-deleted and that no GameScene file was
  touched: `git diff --stat 5fb5c33 HEAD -- src/scenes include/scenes` is empty.

---

## Self-review notes (author)

- **Spec coverage:** delivers the `Simulation` root (FixedClock + Scheduler + Enemy/Boss/Weapon
  controllers via unique_ptr + FireSystem + bullet motion/collision via `Combat::ResolveHit` +
  WorldCollision) and the SimulationTest replay-equality the spec §8 names. GameScene rewire =
  Plan 4b.
- **Determinism:** every brain stream stays seed-isolated; the one new RNG (`m_HitRng`) is
  seeded `runSeed ^ kHitRngSalt` and consumed in ascending-bullet-id order; the
  `*ReplayIsByteIdentical` / `EndToEnd...ByteIdentical` tests pin it. No wall-clock, no
  `Math.random`.
- **Stable-pointer invariant:** `Simulation` is move/copy-deleted; controllers are `unique_ptr`
  (Boss move-deleted; Enemy also captures `this`), so `Activate`'s raw pointers to
  `m_Scheduler`/`m_FireIntents` never dangle. Dead enemies are retained (Kill cancels their
  cadence) so view ids stay stable; only bullets are erased.
- **Enemy HP gotcha addressed:** `AddEnemy` seeds `stats.hp = maxHp = kSliceEnemyHp` because
  `EnemyController` leaves `stats.hp == 0` and `EnemyDef` has no hp field.
- **No placeholders:** the only slice constants (radii, `kSliceEnemyHp`, `kRepelScale`) are
  named in SimConfig with rationale; boss wander is intentionally slice-unused (RNG still
  locked); SimEvent emission is intentionally deferred (sink present, empty).

## Follow-on

- **Plan 4b — GameScene rewire:** GameScene implements `WorldCollision` over `m_Rooms`/doors,
  constructs one `Simulation`, feeds `WorldInputs` each frame, mirrors `BulletState`/`EntityView`
  to pooled `Game::Bullet`/`Enemy`/`Boss` views by id, gates `firing` on energy (deducting per
  `PlayerShotsLastAdvance()`), and handles weapon-swap via `EquipWeapon`. Removes the OLD
  `enemy->Think`/`boss->Think`/`WeaponInstance` path. Verified by `run`-app playtest + a
  room-collision integration test.
- **Deferred:** EnemyAI06 (kinematic-turret variant), SimEvent anim/sfx emission, the
  400ms-vs-2.0s energy-reload faithfulness reconciliation, boss wander-blend movement fidelity.
