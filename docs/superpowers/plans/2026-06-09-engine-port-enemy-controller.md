# Engine Port — Enemy Controller (Plan 2 of 4) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire the faithful `EnemyAI01` decision brain into a deterministic,
engine-decoupled `Game::Sim::EnemyController` that drives an enemy's Scout/wander/
shoot cadence through the `Scheduler`, emits `FireIntent`s, and handles knockback —
proving the controller pattern end-to-end with golden-trace tests.

**Architecture:** Builds on Plan 1's `Game::Sim` infrastructure. `EnemyController`
owns an `EnemyAI01` brain (the SOLE RGRandom stream — all Scout/Run/Shoot draws)
plus an `EntityState` and knockback state, and computes the faithful velocity
composition itself (cited to `EnemyAI01__FixedUpdate`). A `BrainFactory::MakeEnemy`
builds one from an `EnemyDef`. No PTSD dependency; fully headless-testable from
scalars. Reference spec:
`docs/superpowers/specs/2026-06-09-engine-port-vertical-slice-design.md`.

**Tech Stack:** C++17, MSVC `/W4` warning-clean, GoogleTest, CMake
(`SoulKnightTests` target; register new files in `files.cmake`).

**Conventions:** ASCII-only; guard `GAME_SIM_<NAME>_HPP`; `namespace Game::Sim`;
CamelCase/camelBack/`m_`/`kFoo`; initialise every member; Doxygen-light. Build/test:
`cmake --build build --config Debug --target SoulKnightTests` then
`ctest --test-dir build -C Debug -R <Suite>`.

---

## Design (read before implementing)

**The single-RNG-stream rule.** The real enemy has exactly ONE `rg_random` stream.
The per-content `EnemyAI01` brain models all of its draws (Scout: one `Range(0,10)`;
RunReflection: two `Range(-1,1)`; ShootReflection: zero). The controller therefore
takes ALL randomness from `EnemyAI01` and NEVER instantiates a second drawing brain.
Velocity physics is a deterministic formula computed in the controller (zero draws),
so the stream stays in lockstep — pinned by a golden-trace test.

**Velocity composition** (FAITHFUL: `EnemyAI01__FixedUpdate` @ game_full.c:675946,
mirroring `EnemyAI::IntegrateVelocity`):
```
if (kinematic OR inertialVel <= 1.0):   // not-knockback path
    velocity = moveDir * speed * (speedRate + 1)
    // (no inertial decay on this path)
else:                                     // knockback path
    velocity = moveDir * speed * (speedRate + 1) + forceDir * inertialVel
    inertialVel *= friction               // multiplicative decay
```
Knockback is seeded by `ApplyForce(dir, power)` (FAITHFUL: `RGEController__GetForce`
@473583): `forceDir = dir`, `inertialVel = min(power, 28.0)`.

**Cadence wiring** (via the shared `Scheduler`, scheduled in `Activate`):
- `InvokeRepeating(scoutTicks, scoutTicks, OnScoutTick)` where
  `scoutTicks = max(1, SecondsToTicks(scoutRateSeconds))`. `OnScoutTick` calls
  `brain.Scout()` (1 draw) then `brain.RunReflection()` (2 draws) and stores the
  returned wander dir as the current move direction. (Driving wander on the scout
  cadence is a documented slice approximation; the decomp Invokes them on related
  cadences. Flagged, not fabricated — the draw COUNT/ORDER per tick is faithful.)
- `Invoke(shootTicks, OnShootTick)` where
  `shootTicks = max(1, SecondsToTicks(shootCdSeconds))`. `OnShootTick` calls
  `brain.ShootReflection(outCd, shootCdSeconds)`: on a fire (not gated) it pushes a
  Single `FireIntent` aimed at the current target and re-`Invoke`s itself at
  `max(1, SecondsToTicks(outCd))`. A gated shot still reschedules (the enemy keeps
  trying). ShootReflection takes zero draws.

**Targeting.** `SetTarget(glm::vec2 playerPos)` is called by the owner each tick;
`OnShootTick` aims the `FireIntent` at `normalize(target - pos)` (zero vector when
coincident -> aims +x, never NaN).

**Fire output.** `Activate(Scheduler&, std::vector<FireIntent>& fireOut)` stores
references; `OnShootTick` appends to `fireOut`. The owner (Plan 4 `Simulation`)
drains `fireOut` through `FireSystem`. In tests a local vector is passed.

**Gating.** `OnScoutTick`/`OnShootTick` no-op while `m_State.dead` (the brain's
dead/dizzy gates are also honoured: `EnemyAI01::Scout()` returns -1 when gated and
takes no draw, so the controller mirrors `m_State.dead -> brain.SetDead(true)`).

---

## File structure (created by this plan)

| File | Responsibility |
|---|---|
| `include/sim/EnemyController.hpp` + `src/sim/EnemyController.cpp` | One enemy's brain+physics+cadence adapter over `EnemyAI01`. |
| `include/sim/BrainFactory.hpp` + `src/sim/BrainFactory.cpp` | `MakeEnemy(const EnemyDef&, spawn, seed)` -> `EnemyController` (the def->params map; the extension point for the other enemies). |
| `test/EnemyControllerTest.cpp`, `test/BrainFactoryTest.cpp` | Golden per-tick traces + factory mapping. |

---

## Task 1: EnemyController — construction, force, velocity (no cadence yet)

**Files:** Create `include/sim/EnemyController.hpp`, `src/sim/EnemyController.cpp`;
Test `test/EnemyControllerTest.cpp`; Modify `files.cmake`.

- [ ] **Step 1: Write the failing test (construction + force + velocity)**

`test/EnemyControllerTest.cpp`:
```cpp
#include <gtest/gtest.h>

#include <cmath>

#include "sim/EnemyController.hpp"

using Game::Sim::EnemyController;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
EnemyController::Params MakeParams() {
    EnemyController::Params p;
    p.speed = 60.0F;
    p.speedRate = 0.0F;
    p.friction = 0.5F;
    p.shootCdSeconds = 1.0F;
    p.scoutRateSeconds = 0.5F;
    p.kinematic = false;
    return p;
}
} // namespace

TEST(EnemyControllerTest, SpawnsAtPositionAlive) {
    EnemyController e(MakeParams(), glm::vec2{100.0F, 50.0F}, 1234);
    EXPECT_FLOAT_EQ(e.State().pos.x, 100.0F);
    EXPECT_FLOAT_EQ(e.State().pos.y, 50.0F);
    EXPECT_FALSE(e.State().dead);
}

TEST(EnemyControllerTest, SteerVelocityIsMoveDirTimesSpeed) {
    // No knockback: velocity = moveDir * speed * (speedRate+1).
    EnemyController e(MakeParams(), glm::vec2{0.0F, 0.0F}, 1);
    e.SetMoveDir(glm::vec2{1.0F, 0.0F});
    const glm::vec2 v = e.ComputeVelocity();
    EXPECT_FLOAT_EQ(v.x, 60.0F); // 1 * 60 * (0+1)
    EXPECT_FLOAT_EQ(v.y, 0.0F);
}

TEST(EnemyControllerTest, KnockbackAddsForceThenDecays) {
    EnemyController e(MakeParams(), glm::vec2{0.0F, 0.0F}, 1);
    e.SetMoveDir(glm::vec2{0.0F, 0.0F});
    e.ApplyForce(glm::vec2{1.0F, 0.0F}, 10.0F); // inertialVel = min(10,28) = 10
    EXPECT_FLOAT_EQ(e.InertialVel(), 10.0F);
    // knockback path (inertialVel 10 > 1): velocity = 0 + forceDir*10 = (10,0),
    // then inertialVel *= friction(0.5) -> 5.
    const glm::vec2 v = e.ComputeVelocity();
    EXPECT_FLOAT_EQ(v.x, 10.0F);
    EXPECT_FLOAT_EQ(e.InertialVel(), 5.0F);
}

TEST(EnemyControllerTest, ForceIsCappedAt28) {
    EnemyController e(MakeParams(), glm::vec2{0.0F, 0.0F}, 1);
    e.ApplyForce(glm::vec2{0.0F, 1.0F}, 999.0F);
    EXPECT_FLOAT_EQ(e.InertialVel(), 28.0F); // RGEController GetForce cap
}

TEST(EnemyControllerTest, KinematicIgnoresKnockbackTerm) {
    EnemyController::Params p = MakeParams();
    p.kinematic = true;
    EnemyController e(p, glm::vec2{0.0F, 0.0F}, 1);
    e.SetMoveDir(glm::vec2{0.0F, 0.0F});
    e.ApplyForce(glm::vec2{1.0F, 0.0F}, 20.0F);
    const glm::vec2 v = e.ComputeVelocity();
    EXPECT_FLOAT_EQ(v.x, 0.0F); // kinematic: no knockback term applied
    EXPECT_FLOAT_EQ(v.y, 0.0F);
}

// NOLINTEND(readability-magic-numbers)
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --config Debug --target SoulKnightTests`
Expected: FAIL — `sim/EnemyController.hpp` not found.

- [ ] **Step 3: Create the header**

`include/sim/EnemyController.hpp`:
```cpp
#ifndef GAME_SIM_ENEMYCONTROLLER_HPP
#define GAME_SIM_ENEMYCONTROLLER_HPP

#include <vector>

#include <glm/glm.hpp>

#include "combat/EnemyAI01.hpp"
#include "sim/EntityState.hpp"
#include "sim/FireIntent.hpp"
#include "sim/Scheduler.hpp"

namespace Game::Sim {

/// Drives one EnemyAI01 enemy: the brain (sole RNG stream) decides scout/wander/
/// shoot on the Scheduler cadence; the controller computes the faithful velocity
/// and emits FireIntents. Engine-free and deterministic.
class EnemyController {
public:
    /// The RGEController GetForce knockback cap (FAITHFUL @ game_full.c:473583).
    static constexpr float kForceCap = 28.0F;
    /// inertialVel must exceed this for the knockback term (FAITHFUL @ 675976).
    static constexpr float kKnockbackThreshold = 1.0F;

    /// Scalar config (BrainFactory fills this from an EnemyDef).
    struct Params {
        float speed = 60.0F;
        float speedRate = 0.0F;
        float friction = 0.9F;          ///< inertial decay multiplier in [0,1).
        float shootCdSeconds = 1.0F;
        float scoutRateSeconds = 0.5F;
        bool kinematic = false;
    };

    EnemyController(const Params &params, glm::vec2 spawn, int seed);

    /// Schedule the scout + shoot cadence. @p fireOut receives emitted intents.
    void Activate(Scheduler &scheduler, std::vector<FireIntent> &fireOut);

    /// Set the aim target (the player position), updated each tick by the owner.
    void SetTarget(glm::vec2 target) { m_Target = target; }

    /// Seed knockback (FAITHFUL: RGEController__GetForce @473583): forceDir = dir,
    /// inertialVel = min(power, 28).
    void ApplyForce(glm::vec2 dir, float power);

    /// Compose this fixed step's velocity and decay knockback (see design).
    glm::vec2 ComputeVelocity();

    /// Latch death: brain + state stop acting; scheduled callbacks no-op.
    void Kill();

    // ---- state access (the owner/view + tests) ----
    const EntityState &State() const { return m_State; }
    EntityState &MutableState() { return m_State; }
    float InertialVel() const { return m_InertialVel; }
    glm::vec2 MoveDir() const { return m_MoveDir; }
    void SetMoveDir(glm::vec2 dir) { m_MoveDir = dir; } ///< test/seam hook.
    EnemyAI01 &Brain() { return m_Brain; }

private:
    void OnScoutTick();
    void OnShootTick();

    Params m_Params;
    EnemyAI01 m_Brain;
    EntityState m_State;
    glm::vec2 m_MoveDir{0.0F, 0.0F};
    glm::vec2 m_ForceDir{0.0F, 0.0F};
    float m_InertialVel = 0.0F;
    glm::vec2 m_Target{0.0F, 0.0F};

    Scheduler *m_Scheduler = nullptr;        ///< not owned; set in Activate.
    std::vector<FireIntent> *m_FireOut = nullptr; ///< not owned; set in Activate.
};

} // namespace Game::Sim

#endif /* GAME_SIM_ENEMYCONTROLLER_HPP */
```

- [ ] **Step 4: Create the impl (construction + force + velocity; cadence stubs come in Task 2)**

`src/sim/EnemyController.cpp`:
```cpp
#include "sim/EnemyController.hpp"

#include <algorithm>
#include <cmath>

namespace Game::Sim {
namespace {

glm::vec2 Normalize(glm::vec2 v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y);
    return len > 0.0F ? v / len : glm::vec2{1.0F, 0.0F};
}

} // namespace

EnemyController::EnemyController(const Params &params, glm::vec2 spawn, int seed)
    : m_Params(params) {
    m_Brain.SetSeed(seed);
    m_Brain.SetKinematic(params.kinematic);
    m_State.pos = spawn;
    m_State.kinematic = params.kinematic;
}

void EnemyController::ApplyForce(glm::vec2 dir, float power) {
    m_ForceDir = dir;
    m_InertialVel = std::min(power, kForceCap); // FAITHFUL GetForce @473583
}

glm::vec2 EnemyController::ComputeVelocity() {
    // FAITHFUL: EnemyAI01__FixedUpdate @675946. Steer term always; knockback term
    // + decay only when inertialVel > 1 and not kinematic.
    const float scale = m_Params.speed * (m_Params.speedRate + 1.0F);
    glm::vec2 velocity = m_MoveDir * scale;
    if (!m_Params.kinematic && m_InertialVel > kKnockbackThreshold) {
        velocity += m_ForceDir * m_InertialVel;
        m_InertialVel *= m_Params.friction; // multiplicative decay
    }
    m_State.vel = velocity;
    return velocity;
}

void EnemyController::Kill() {
    m_State.dead = true;
    m_Brain.SetDead(true);
}

// Cadence callbacks: implemented in Task 2.
void EnemyController::OnScoutTick() {}
void EnemyController::OnShootTick() {}

void EnemyController::Activate(Scheduler &scheduler,
                               std::vector<FireIntent> &fireOut) {
    m_Scheduler = &scheduler;
    m_FireOut = &fireOut;
    // Cadence scheduling added in Task 2.
    (void)Normalize; // silence unused until Task 2 wires targeting.
}

} // namespace Game::Sim
```

- [ ] **Step 5: Register + build + test**

`files.cmake`: SRC_FILES `sim/EnemyController.cpp`; INCLUDE_FILES
`sim/EnemyController.hpp`; TEST_FILES `EnemyControllerTest.cpp` (under the sim block).
Run: `cmake --build build --config Debug --target SoulKnightTests` then
`ctest --test-dir build -C Debug -R EnemyControllerTest`. Expected: 5/5 pass,
warning-clean.

- [ ] **Step 6: Commit**

```bash
git add include/sim/EnemyController.hpp src/sim/EnemyController.cpp test/EnemyControllerTest.cpp files.cmake
git commit -m "feat(sim): EnemyController -- construction, knockback, velocity"
```

---

## Task 2: EnemyController — Scout/wander/shoot cadence + golden trace

**Files:** Modify `src/sim/EnemyController.cpp` (implement `OnScoutTick`,
`OnShootTick`, wire `Activate`); Modify `test/EnemyControllerTest.cpp` (add cadence
+ determinism tests).

- [ ] **Step 1: Write the failing cadence tests (append to EnemyControllerTest.cpp)**

```cpp
TEST(EnemyControllerTest, ScoutTickAdvancesStreamAndPicksWanderDir) {
    // OnScoutTick draws Scout (1) + RunReflection (2) on the brain stream and sets
    // a (possibly zero) wander move dir. A parallel EnemyAI01 must match draw-for-
    // draw, proving the controller drives exactly the brain's draws in order.
    EnemyController::Params p;
    p.scoutRateSeconds = 0.02F; // 1 tick
    EnemyController e(p, glm::vec2{0.0F, 0.0F}, 777);
    Game::Sim::Scheduler sched;
    std::vector<Game::Sim::FireIntent> fire;
    e.MutableState().awake = true;
    e.Activate(sched, fire);

    Game::EnemyAI01 ref;
    ref.SetSeed(777);

    for (int i = 0; i < 4; ++i) {
        sched.Tick(); // fires OnScoutTick (scout cadence = 1 tick)
        ref.Scout();
        const glm::vec2 refDir = ref.RunReflection();
        EXPECT_FLOAT_EQ(e.MoveDir().x, refDir.x);
        EXPECT_FLOAT_EQ(e.MoveDir().y, refDir.y);
    }
}

TEST(EnemyControllerTest, ShootTickEmitsAimedFireIntentOnCadence) {
    EnemyController::Params p;
    p.shootCdSeconds = 0.04F; // 2 ticks
    p.scoutRateSeconds = 100.0F; // keep scout out of the way
    EnemyController e(p, glm::vec2{0.0F, 0.0F}, 5);
    Game::Sim::Scheduler sched;
    std::vector<Game::Sim::FireIntent> fire;
    e.MutableState().awake = true;
    e.SetTarget(glm::vec2{10.0F, 0.0F}); // player to the +x
    e.Activate(sched, fire);

    sched.Tick(); // tick 1: nothing (shoot due at tick 2)
    EXPECT_TRUE(fire.empty());
    sched.Tick(); // tick 2: shoot fires
    ASSERT_EQ(fire.size(), 1U);
    EXPECT_EQ(fire[0].pattern, Game::Sim::FirePattern::Single);
    EXPECT_EQ(fire[0].camp, 1); // enemy bullet
    EXPECT_NEAR(fire[0].dir.x, 1.0F, 1e-4F); // aimed at +x target
    EXPECT_NEAR(fire[0].dir.y, 0.0F, 1e-4F);
    sched.Tick();
    sched.Tick(); // tick 4: re-scheduled shot fires again
    EXPECT_EQ(fire.size(), 2U);
}

TEST(EnemyControllerTest, DeadEnemyEmitsNoFireAndTakesNoDraw) {
    EnemyController::Params p;
    p.shootCdSeconds = 0.02F;
    p.scoutRateSeconds = 0.02F;
    EnemyController e(p, glm::vec2{0.0F, 0.0F}, 9);
    Game::Sim::Scheduler sched;
    std::vector<Game::Sim::FireIntent> fire;
    e.MutableState().awake = true;
    e.Activate(sched, fire);
    e.Kill();

    Game::EnemyAI01 ref; // never drawn
    ref.SetSeed(9);
    for (int i = 0; i < 8; ++i) {
        sched.Tick();
    }
    EXPECT_TRUE(fire.empty());
    // brain stream did not advance past the dead gate (Scout returns -1, no draw).
    EXPECT_EQ(e.Brain().Rng().Range(0, 1000), ref.Range(0, 1000));
}

TEST(EnemyControllerTest, FullCadenceReplayIsDeterministic) {
    auto run = [](int seed) {
        EnemyController::Params p;
        p.shootCdSeconds = 0.06F;
        p.scoutRateSeconds = 0.04F;
        EnemyController e(p, glm::vec2{0.0F, 0.0F}, seed);
        Game::Sim::Scheduler sched;
        std::vector<Game::Sim::FireIntent> fire;
        e.MutableState().awake = true;
        e.SetTarget(glm::vec2{5.0F, 5.0F});
        e.Activate(sched, fire);
        std::vector<float> trace;
        for (int i = 0; i < 20; ++i) {
            sched.Tick();
            const glm::vec2 v = e.ComputeVelocity();
            trace.push_back(v.x);
            trace.push_back(v.y);
            trace.push_back(static_cast<float>(fire.size()));
        }
        return trace;
    };
    const auto a = run(2024);
    const auto b = run(2024);
    EXPECT_EQ(a, b);
}
```

- [ ] **Step 2: Run to verify the new tests fail**

Run: `ctest --test-dir build -C Debug -R EnemyControllerTest` (after build).
Expected: the 4 new cases FAIL (cadence is stubbed; no fire emitted, move dir stays
zero).

- [ ] **Step 3: Implement the cadence (replace the stubs in EnemyController.cpp)**

Replace the `OnScoutTick`/`OnShootTick`/`Activate` stubs with:
```cpp
void EnemyController::OnScoutTick() {
    if (m_State.dead) {
        m_Brain.SetDead(true);
        return; // dead gate: Scout()/RunReflection() not driven, no draw.
    }
    m_Brain.Scout();                 // 1 draw (Range(0,10)); -1 + no draw if gated
    m_MoveDir = m_Brain.RunReflection(); // 2 draws (Range(-1,1) x2), normalized
}

void EnemyController::OnShootTick() {
    if (m_Scheduler == nullptr || m_FireOut == nullptr) {
        return;
    }
    if (!m_State.dead) {
        m_Brain.SetDead(false);
        float outCd = m_Params.shootCdSeconds;
        const bool fired = m_Brain.ShootReflection(outCd, m_Params.shootCdSeconds);
        if (fired) {
            FireIntent intent;
            intent.pattern = FirePattern::Single;
            intent.origin = m_State.pos;
            intent.dir = Normalize(m_Target - m_State.pos);
            intent.speedPxPerSec = m_Params.speed * 5.0F; // enemy bullet speed (slice constant)
            intent.lifeMs = 1500.0F;
            intent.damage = 1;
            intent.camp = 1; // enemy bullet
            m_FireOut->push_back(intent);
        }
        // reschedule the next shot (a gated shot still keeps trying).
        const int next = (std::max)(1, Scheduler::SecondsToTicks(outCd));
        m_Scheduler->Invoke(next, [this] { OnShootTick(); });
    }
}

void EnemyController::Activate(Scheduler &scheduler,
                               std::vector<FireIntent> &fireOut) {
    m_Scheduler = &scheduler;
    m_FireOut = &fireOut;
    const int scoutTicks = (std::max)(1, Scheduler::SecondsToTicks(m_Params.scoutRateSeconds));
    const int shootTicks = (std::max)(1, Scheduler::SecondsToTicks(m_Params.shootCdSeconds));
    scheduler.InvokeRepeating(scoutTicks, scoutTicks, [this] { OnScoutTick(); });
    scheduler.Invoke(shootTicks, [this] { OnShootTick(); });
}
```
Remove the `(void)Normalize;` line and the cadence-stub comment from Task 1.
(Note: the `enemy bullet speed` and `lifeMs` are slice constants here; Plan 3/4 will
source them from the weapon/bullet data via `kDataSpeedToPxPerSec`.)

- [ ] **Step 4: Run to verify all pass**

Run: `ctest --test-dir build -C Debug -R EnemyControllerTest`
Expected: 9/9 pass (5 from Task 1 + 4 new), warning-clean.

- [ ] **Step 5: Commit**

```bash
git add src/sim/EnemyController.cpp test/EnemyControllerTest.cpp
git commit -m "feat(sim): EnemyController -- scout/wander/shoot cadence + determinism"
```

---

## Task 3: BrainFactory::MakeEnemy

**Files:** Create `include/sim/BrainFactory.hpp`, `src/sim/BrainFactory.cpp`;
Test `test/BrainFactoryTest.cpp`; Modify `files.cmake`.

- [ ] **Step 1: Inspect EnemyDef field names**

Read `include/data/GameData.hpp` and note the exact `EnemyDef` fields for shoot
cooldown, scout rate, friction, speed, and the kinematic flag (the existing
`EnemyAI`/`Enemy`/`GameScene` code reads `def.scoutRate`, `def.friction`,
`def.kinematic`). Use the real field names in `MakeEnemy`. If a field is absent
(e.g. an explicit per-enemy speed), fall back to the documented slice default
(speed 60) and note it.

- [ ] **Step 2: Write the failing factory test**

`test/BrainFactoryTest.cpp`:
```cpp
#include <gtest/gtest.h>

#include "data/GameData.hpp"
#include "sim/BrainFactory.hpp"
#include "sim/EnemyController.hpp"

using Game::Sim::BrainFactory;
using Game::Sim::EnemyController;

// NOLINTBEGIN(readability-magic-numbers)

TEST(BrainFactoryTest, MakeEnemyAppliesDefParamsAndSpawn) {
    Game::EnemyDef def{};
    def.scoutRate = 0.5F;
    def.friction = 0.8F;
    def.kinematic = 0; // not kinematic
    // (set whatever shoot-cooldown field EnemyDef exposes to 1.0)

    EnemyController e = BrainFactory::MakeEnemy(def, glm::vec2{20.0F, 30.0F}, 42);
    EXPECT_FLOAT_EQ(e.State().pos.x, 20.0F);
    EXPECT_FLOAT_EQ(e.State().pos.y, 30.0F);
    EXPECT_FALSE(e.State().kinematic);
    EXPECT_TRUE(e.Brain().Seeded());
}

TEST(BrainFactoryTest, MakeEnemyHonoursKinematicFlag) {
    Game::EnemyDef def{};
    def.kinematic = 1;
    EnemyController e = BrainFactory::MakeEnemy(def, glm::vec2{0.0F, 0.0F}, 1);
    EXPECT_TRUE(e.State().kinematic);
}

// NOLINTEND(readability-magic-numbers)
```
(Adjust the `def` field assignments to the real `EnemyDef` field names found in
Step 1.)

- [ ] **Step 3: Create the factory**

`include/sim/BrainFactory.hpp`:
```cpp
#ifndef GAME_SIM_BRAINFACTORY_HPP
#define GAME_SIM_BRAINFACTORY_HPP

#include <glm/glm.hpp>

#include "data/GameData.hpp"
#include "sim/EnemyController.hpp"

namespace Game::Sim {

/// Builds sim controllers from data definitions. The single place that maps a
/// content def to its controller config; the extension point for the rest of the
/// roster in later cycles.
class BrainFactory {
public:
    /// Build an EnemyController from an EnemyDef at @p spawn, seeded with @p seed.
    static EnemyController MakeEnemy(const EnemyDef &def, glm::vec2 spawn, int seed);
};

} // namespace Game::Sim

#endif /* GAME_SIM_BRAINFACTORY_HPP */
```

`src/sim/BrainFactory.cpp` (fill the `EnemyControllerParams` from the REAL EnemyDef
fields confirmed in Step 1; the body below shows the mapping shape):
```cpp
#include "sim/BrainFactory.hpp"

namespace Game::Sim {

EnemyController BrainFactory::MakeEnemy(const EnemyDef &def, glm::vec2 spawn,
                                        int seed) {
    EnemyController::Params p;
    p.speed = 60.0F;               // slice default (EnemyDef carries no per-enemy speed)
    p.speedRate = 0.0F;
    p.friction = def.friction;
    p.scoutRateSeconds = def.scoutRate;
    p.shootCdSeconds = def.shootCd; // use the real EnemyDef shoot-cooldown field
    p.kinematic = def.kinematic != 0;
    return EnemyController(p, spawn, seed);
}

} // namespace Game::Sim
```

- [ ] **Step 4: Register + build + test**

`files.cmake`: SRC_FILES `sim/BrainFactory.cpp`; INCLUDE_FILES `sim/BrainFactory.hpp`;
TEST_FILES `BrainFactoryTest.cpp`. Run build + `ctest -R BrainFactoryTest` -> 2/2 pass,
warning-clean.

- [ ] **Step 5: Commit**

```bash
git add include/sim/BrainFactory.hpp src/sim/BrainFactory.cpp test/BrainFactoryTest.cpp files.cmake
git commit -m "feat(sim): BrainFactory::MakeEnemy (EnemyDef -> EnemyController)"
```

---

## Task 4: Full-suite regression gate

**Files:** none (verification).

- [ ] **Step 1: Build the whole target**

Run: `cmake --build build --config Debug --target SoulKnightTests`
Expected: warning-clean `/W4` (0 warnings on any `sim/` file).

- [ ] **Step 2: Run the entire suite**

Run: `ctest --test-dir build -C Debug`
Expected: 100% pass (prior 1836 + the new EnemyController/BrainFactory cases). No
game behaviour changed (nothing wires the controller into GameScene yet — that's
Plan 4).

---

## Self-review notes (author)

- **Spec coverage:** delivers the spec's `EnemyController` (EnemyAI01) + the
  `BrainFactory` enemy path + golden traces. `EnemyAI06`/`BossController`/
  `WeaponController` are Plan 3; `Simulation`+GameScene rewire is Plan 4 (stated
  decomposition).
- **Determinism:** single RGRandom stream (EnemyAI01 only); `FullCadenceReplayIsDeterministic`
  + the parallel-`EnemyAI01` scout-draw test pin draw count+order; velocity is a
  pure formula (zero draws).
- **No placeholders:** the only deferred lookup is the exact `EnemyDef` field names
  (Task 3 Step 1 directs reading `GameData.hpp`), because those names are external
  to this plan; everything else is complete code.
- **Type consistency:** `EnemyController::Params`, `ComputeVelocity()`,
  `ApplyForce()`, `Activate(Scheduler&, std::vector<FireIntent>&)`, `Brain()` used
  identically across tasks/tests.

## Follow-on plans

- **Plan 3 — Boss + Weapon controllers:** `SimMath.hpp` (shared `RotateDeg`,
  extracted from FireSystem per Plan 1's review), `BossController` (BossAI01 fan +
  angry phase), `WeaponController` (Gun001/Gun016), factory `MakeBoss`/`MakeWeapon`.
- **Plan 4 — Simulation + GameScene rewire:** the `Simulation` root + bullet motion/
  collision + the GameScene shell rewire + `SimulationTest` replay equality + playtest.
