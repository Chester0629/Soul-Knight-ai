# Engine Port — Sim Infrastructure (Plan 1 of 3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the pure, deterministic, engine-decoupled foundation of the
`Game::Sim` adapter layer — a fixed-step clock, a tick-based Invoke/coroutine
scheduler, the shared POD state types + interfaces, and the brain-driven fire
geometry system — all unit-tested headlessly, with no game-behaviour change yet.

**Architecture:** New `Game::Sim` area under `include/sim/` + `src/sim/`,
registered in `files.cmake`. Depends only on `glm`, `data/RGRandom.hpp`,
`combat/CombatStats.hpp` — **never** PTSD. This is Plan 1 of 3 (infrastructure);
Plan 2 wires controllers + `BrainFactory`, Plan 3 builds the `Simulation` root and
rewires `GameScene`. Reference spec:
`docs/superpowers/specs/2026-06-09-engine-port-vertical-slice-design.md`.

**Tech Stack:** C++17, MSVC `/W4` warning-clean, GoogleTest, CMake (the existing
`SoulKnightTests` target; tests registered in `files.cmake`'s `TEST_FILES`).

**Conventions (every file):** ASCII-only; header guard `GAME_SIM_<NAME>_HPP`;
`namespace Game::Sim`; CamelCase types/functions, camelBack locals, `m_` members,
`kFoo` file-scope constants; initialise every member; Doxygen-light on public APIs.

**Build + test commands (used throughout):**
```bash
cmake --build build --config Debug --target SoulKnightTests
ctest --test-dir build -C Debug -R <SuiteName>
```
(Changing `files.cmake` auto-triggers a CMake reconfigure on the next build; if a
fresh configure is ever needed: `cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Debug`.)

---

## File structure (created by this plan)

| File | Responsibility |
|---|---|
| `include/sim/SimConfig.hpp` | Fixed-step constants (`kFixedStepSeconds = 0.02`, `kFixedStepMs = 20`). Header-only. |
| `include/sim/FixedClock.hpp` + `src/sim/FixedClock.cpp` | Variable `dtMs` → count of whole fixed steps, with remainder carry + a max-steps cap. |
| `include/sim/Scheduler.hpp` + `src/sim/Scheduler.cpp` | Deterministic tick-based `Invoke`/`InvokeRepeating`/`Cancel`/`Tick`; `SecondsToTicks`. |
| `include/sim/BulletState.hpp` | POD: a logical bullet (id, pos, vel, life, stats, camp, active). Header-only. |
| `include/sim/EntityState.hpp` | POD: shared controller gameplay state (pos, vel, facing, `CombatStats`, flags). Header-only. |
| `include/sim/FireIntent.hpp` | POD + `FirePattern` enum: a brain's request to spawn bullets. Header-only. |
| `include/sim/SimEvent.hpp` | POD: a render/audio event (anim-trigger / sfx). Header-only. |
| `include/sim/WorldCollision.hpp` | Abstract wall-blocking query + a `NullWorldCollision` test stub. Header-only. |
| `include/sim/FireSystem.hpp` + `src/sim/FireSystem.cpp` | Expand a `FireIntent` → `BulletState`s (Single + Fan), zero RNG of its own. |
| `test/FixedClockTest.cpp`, `test/SchedulerTest.cpp`, `test/SimPodsTest.cpp`, `test/FireSystemTest.cpp` | Unit tests. |

After this plan: 4 new test suites, all green, no behaviour change in the running
game (nothing wires the new code yet — that's Plan 3).

---

## Task 1: SimConfig + FixedClock

**Files:**
- Create: `include/sim/SimConfig.hpp`
- Create: `include/sim/FixedClock.hpp`, `src/sim/FixedClock.cpp`
- Test: `test/FixedClockTest.cpp`
- Modify: `files.cmake` (add the new src/include/test entries)

- [ ] **Step 1: Create the fixed-step constants header**

`include/sim/SimConfig.hpp`:
```cpp
#ifndef GAME_SIM_SIMCONFIG_HPP
#define GAME_SIM_SIMCONFIG_HPP

namespace Game::Sim {

/// The simulation's fixed timestep, matching Unity's default FixedUpdate (0.02s).
/// All brains + cadence advance in whole multiples of this; the runtime is
/// seed-reproducible because nothing reads wall-clock time.
inline constexpr float kFixedStepSeconds = 0.02F;

/// The fixed timestep in milliseconds (the unit GameScene passes as dtMs).
inline constexpr float kFixedStepMs = 20.0F;

/// Upper bound on fixed steps run for one Advance() call, so a huge frame hitch
/// (e.g. a debugger pause) cannot spiral into thousands of catch-up steps.
inline constexpr int kMaxStepsPerAdvance = 8;

} // namespace Game::Sim

#endif /* GAME_SIM_SIMCONFIG_HPP */
```

- [ ] **Step 2: Write the failing FixedClock test**

`test/FixedClockTest.cpp`:
```cpp
#include <gtest/gtest.h>

#include "sim/FixedClock.hpp"
#include "sim/SimConfig.hpp"

using Game::Sim::FixedClock;

// NOLINTBEGIN(readability-magic-numbers)

TEST(FixedClockTest, OneFullStep) {
    FixedClock clk;
    EXPECT_EQ(clk.Advance(20.0F), 1); // exactly one 20ms step
    EXPECT_FLOAT_EQ(clk.RemainderMs(), 0.0F);
}

TEST(FixedClockTest, AccumulatesAcrossFramesWithRemainderCarry) {
    FixedClock clk;
    EXPECT_EQ(clk.Advance(12.0F), 0); // 12 < 20 -> no step yet
    EXPECT_FLOAT_EQ(clk.RemainderMs(), 12.0F);
    EXPECT_EQ(clk.Advance(12.0F), 1); // 24 total -> one step, 4 remainder
    EXPECT_FLOAT_EQ(clk.RemainderMs(), 4.0F);
}

TEST(FixedClockTest, MultipleStepsInOneFrame) {
    FixedClock clk;
    EXPECT_EQ(clk.Advance(50.0F), 2); // 50ms -> 2 steps (40), 10 remainder
    EXPECT_FLOAT_EQ(clk.RemainderMs(), 10.0F);
}

TEST(FixedClockTest, CapsRunawayFrame) {
    FixedClock clk;
    // 1000ms would be 50 steps; capped to kMaxStepsPerAdvance, remainder cleared
    // so we do not bank 49 steps of debt.
    EXPECT_EQ(clk.Advance(1000.0F), Game::Sim::kMaxStepsPerAdvance);
    EXPECT_FLOAT_EQ(clk.RemainderMs(), 0.0F);
}

TEST(FixedClockTest, IgnoresNonPositiveDt) {
    FixedClock clk;
    EXPECT_EQ(clk.Advance(0.0F), 0);
    EXPECT_EQ(clk.Advance(-5.0F), 0);
    EXPECT_FLOAT_EQ(clk.RemainderMs(), 0.0F);
}

// NOLINTEND(readability-magic-numbers)
```

- [ ] **Step 3: Create the FixedClock header + impl**

`include/sim/FixedClock.hpp`:
```cpp
#ifndef GAME_SIM_FIXEDCLOCK_HPP
#define GAME_SIM_FIXEDCLOCK_HPP

namespace Game::Sim {

/// Turns a variable real-time dtMs stream into a count of whole fixed steps,
/// carrying the sub-step remainder across frames so cadence stays exact.
class FixedClock {
public:
    /// Accumulate @p dtMs and return how many whole fixed steps elapsed (capped at
    /// kMaxStepsPerAdvance). Non-positive dt contributes nothing.
    int Advance(float dtMs);

    /// The unconsumed sub-step time (ms) carried to the next Advance.
    float RemainderMs() const { return m_AccumMs; }

private:
    float m_AccumMs = 0.0F;
};

} // namespace Game::Sim

#endif /* GAME_SIM_FIXEDCLOCK_HPP */
```

`src/sim/FixedClock.cpp`:
```cpp
#include "sim/FixedClock.hpp"

#include "sim/SimConfig.hpp"

namespace Game::Sim {

int FixedClock::Advance(float dtMs) {
    if (dtMs > 0.0F) {
        m_AccumMs += dtMs;
    }
    int steps = 0;
    while (m_AccumMs >= kFixedStepMs) {
        m_AccumMs -= kFixedStepMs;
        ++steps;
        if (steps >= kMaxStepsPerAdvance) {
            m_AccumMs = 0.0F; // drop the backlog rather than bank step-debt
            break;
        }
    }
    return steps;
}

} // namespace Game::Sim
```

- [ ] **Step 4: Register the files + build + run the test**

Add to `files.cmake` a new grouping (place the `# Engine port (sim infrastructure)`
block right before the `# Phase 2 entities + scenes` block in each list):
- `SRC_FILES`: `sim/FixedClock.cpp`
- `INCLUDE_FILES`: `sim/SimConfig.hpp`, `sim/FixedClock.hpp`
- `TEST_FILES`: `FixedClockTest.cpp`

Run:
```bash
cmake --build build --config Debug --target SoulKnightTests
ctest --test-dir build -C Debug -R FixedClockTest
```
Expected: builds warning-clean; `FixedClockTest` 5/5 pass.

- [ ] **Step 5: Commit**

```bash
git add include/sim/SimConfig.hpp include/sim/FixedClock.hpp src/sim/FixedClock.cpp test/FixedClockTest.cpp files.cmake
git commit -m "feat(sim): FixedClock + SimConfig (fixed-timestep accumulator)"
```

---

## Task 2: Scheduler (deterministic Invoke/InvokeRepeating)

**Files:**
- Create: `include/sim/Scheduler.hpp`, `src/sim/Scheduler.cpp`
- Test: `test/SchedulerTest.cpp`
- Modify: `files.cmake`

- [ ] **Step 1: Write the failing Scheduler test**

`test/SchedulerTest.cpp`:
```cpp
#include <gtest/gtest.h>

#include <vector>

#include "sim/Scheduler.hpp"

using Game::Sim::Scheduler;

// NOLINTBEGIN(readability-magic-numbers)

TEST(SchedulerTest, SecondsToTicksRounds) {
    EXPECT_EQ(Scheduler::SecondsToTicks(0.02F), 1);
    EXPECT_EQ(Scheduler::SecondsToTicks(0.10F), 5);
    EXPECT_EQ(Scheduler::SecondsToTicks(0.0F), 0);
    EXPECT_EQ(Scheduler::SecondsToTicks(0.03F), 2); // 1.5 -> 2 (round half up via lround)
}

TEST(SchedulerTest, InvokeFiresOnceAtTheDueTick) {
    Scheduler s;
    int fired = 0;
    int firedAt = -1;
    s.Invoke(3, [&] { ++fired; firedAt = s.CurrentTick(); });
    for (int i = 0; i < 6; ++i) {
        s.Tick();
    }
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(firedAt, 3); // tick 1,2,3 -> fires on the 3rd
}

TEST(SchedulerTest, InvokeRepeatingFiresOnCadence) {
    Scheduler s;
    std::vector<int> ticks;
    s.InvokeRepeating(1, 3, [&] { ticks.push_back(s.CurrentTick()); });
    for (int i = 0; i < 8; ++i) {
        s.Tick();
    }
    EXPECT_EQ(ticks, (std::vector<int>{1, 4, 7}));
}

TEST(SchedulerTest, CancelBeforeDueSuppresses) {
    Scheduler s;
    int fired = 0;
    const Scheduler::Handle h = s.Invoke(3, [&] { ++fired; });
    s.Tick(); // tick 1
    s.Cancel(h);
    for (int i = 0; i < 5; ++i) {
        s.Tick();
    }
    EXPECT_EQ(fired, 0);
}

TEST(SchedulerTest, FifoOrderWithinATick) {
    Scheduler s;
    std::vector<int> order;
    s.Invoke(1, [&] { order.push_back(1); });
    s.Invoke(1, [&] { order.push_back(2); });
    s.Invoke(1, [&] { order.push_back(3); });
    s.Tick();
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3})); // insertion order, deterministic
}

TEST(SchedulerTest, CallbackMayScheduleAnotherCallbackSafely) {
    Scheduler s;
    int fired = 0;
    s.Invoke(1, [&] {
        ++fired;
        s.Invoke(1, [&] { ++fired; }); // due next tick, not this one
    });
    s.Tick(); // tick 1: outer fires (fired=1), inner scheduled for tick 2
    EXPECT_EQ(fired, 1);
    s.Tick(); // tick 2: inner fires
    EXPECT_EQ(fired, 2);
}

// NOLINTEND(readability-magic-numbers)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --config Debug --target SoulKnightTests`
Expected: FAIL to compile — `sim/Scheduler.hpp` not found.

- [ ] **Step 3: Create the Scheduler header + impl**

`include/sim/Scheduler.hpp`:
```cpp
#ifndef GAME_SIM_SCHEDULER_HPP
#define GAME_SIM_SCHEDULER_HPP

#include <cstdint>
#include <functional>
#include <vector>

namespace Game::Sim {

/// Deterministic tick-based emulation of Unity Invoke / InvokeRepeating /
/// coroutine cadence. One Tick() == one fixed step; callbacks fire in insertion
/// order within a tick. No wall-clock time, no RNG -- fully replay-safe.
class Scheduler {
public:
    using Handle = std::uint32_t; ///< 0 == invalid.
    using Callback = std::function<void()>;

    /// Convert a real-second delay to a whole tick count (round-half-up).
    static int SecondsToTicks(float seconds);

    /// Fire @p cb once, @p delayTicks ticks from now (>=1 means next-or-later).
    Handle Invoke(int delayTicks, Callback cb);

    /// Fire @p cb after @p firstDelayTicks, then every @p intervalTicks (>=1).
    Handle InvokeRepeating(int firstDelayTicks, int intervalTicks, Callback cb);

    /// Suppress a pending callback (safe to call on an already-fired handle).
    void Cancel(Handle handle);

    /// Advance one fixed step, firing all callbacks due at or before the new tick.
    void Tick();

    /// The current tick index (incremented by Tick()).
    int CurrentTick() const { return m_Tick; }

private:
    struct Entry {
        Handle id = 0;
        int dueTick = 0;
        int intervalTicks = 0;
        Callback cb;
        bool repeating = false;
        bool cancelled = false;
    };

    std::vector<Entry> m_Entries;
    int m_Tick = 0;
    Handle m_NextId = 1;
};

} // namespace Game::Sim

#endif /* GAME_SIM_SCHEDULER_HPP */
```

`src/sim/Scheduler.cpp`:
```cpp
#include "sim/Scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "sim/SimConfig.hpp"

namespace Game::Sim {

int Scheduler::SecondsToTicks(float seconds) {
    if (seconds <= 0.0F) {
        return 0;
    }
    return static_cast<int>(std::lround(seconds / kFixedStepSeconds));
}

Scheduler::Handle Scheduler::Invoke(int delayTicks, Callback cb) {
    const int delay = delayTicks < 1 ? 1 : delayTicks;
    Entry e;
    e.id = m_NextId++;
    e.dueTick = m_Tick + delay;
    e.cb = std::move(cb);
    e.repeating = false;
    m_Entries.push_back(std::move(e));
    return m_Entries.back().id;
}

Scheduler::Handle Scheduler::InvokeRepeating(int firstDelayTicks, int intervalTicks,
                                             Callback cb) {
    const int first = firstDelayTicks < 1 ? 1 : firstDelayTicks;
    const int interval = intervalTicks < 1 ? 1 : intervalTicks;
    Entry e;
    e.id = m_NextId++;
    e.dueTick = m_Tick + first;
    e.intervalTicks = interval;
    e.cb = std::move(cb);
    e.repeating = true;
    m_Entries.push_back(std::move(e));
    return m_Entries.back().id;
}

void Scheduler::Cancel(Handle handle) {
    for (Entry &e : m_Entries) {
        if (e.id == handle) {
            e.cancelled = true;
            return;
        }
    }
}

void Scheduler::Tick() {
    ++m_Tick;
    // Index-based loop: a callback may push_back new entries (reallocating the
    // vector); we copy the callback out before invoking and never touch the entry
    // reference afterwards, so a realloc is safe. New entries are appended with a
    // future dueTick, so they will not fire this tick.
    for (std::size_t i = 0; i < m_Entries.size(); ++i) {
        if (m_Entries[i].cancelled || m_Entries[i].dueTick > m_Tick) {
            continue;
        }
        Callback cb = m_Entries[i].cb; // copy before any reentrant realloc
        if (m_Entries[i].repeating) {
            m_Entries[i].dueTick += m_Entries[i].intervalTicks;
        } else {
            m_Entries[i].cancelled = true;
        }
        cb();
    }
    m_Entries.erase(
        std::remove_if(m_Entries.begin(), m_Entries.end(),
                       [](const Entry &e) { return e.cancelled; }),
        m_Entries.end());
}

} // namespace Game::Sim
```

- [ ] **Step 4: Register + build + run**

Add to `files.cmake`: `SRC_FILES` `sim/Scheduler.cpp`; `INCLUDE_FILES`
`sim/Scheduler.hpp`; `TEST_FILES` `SchedulerTest.cpp`.

Run:
```bash
cmake --build build --config Debug --target SoulKnightTests
ctest --test-dir build -C Debug -R SchedulerTest
```
Expected: warning-clean; `SchedulerTest` 6/6 pass.

- [ ] **Step 5: Commit**

```bash
git add include/sim/Scheduler.hpp src/sim/Scheduler.cpp test/SchedulerTest.cpp files.cmake
git commit -m "feat(sim): deterministic tick-based Scheduler (Invoke/InvokeRepeating)"
```

---

## Task 3: Shared POD state types + interfaces

**Files:**
- Create: `include/sim/BulletState.hpp`, `include/sim/EntityState.hpp`,
  `include/sim/FireIntent.hpp`, `include/sim/SimEvent.hpp`,
  `include/sim/WorldCollision.hpp`
- Test: `test/SimPodsTest.cpp`
- Modify: `files.cmake`

- [ ] **Step 1: Create the POD headers**

`include/sim/BulletState.hpp`:
```cpp
#ifndef GAME_SIM_BULLETSTATE_HPP
#define GAME_SIM_BULLETSTATE_HPP

#include <cstdint>

#include <glm/glm.hpp>

namespace Game::Sim {

/// A logical bullet owned by the Simulation. GameScene mirrors each to a pooled
/// PTSD Bullet by stable id for rendering.
struct BulletState {
    std::uint32_t id = 0;
    glm::vec2 pos{0.0F, 0.0F};
    glm::vec2 vel{0.0F, 0.0F}; ///< pixels/second.
    float lifeMs = 0.0F;
    int damage = 0;
    int camp = 0; ///< 0 = player bullet, 1 = enemy bullet.
    float repel = 0.0F;
    int critical = 0;
    bool canThrough = false;
    int pierce = 0;
    bool active = false;
};

} // namespace Game::Sim

#endif /* GAME_SIM_BULLETSTATE_HPP */
```

`include/sim/EntityState.hpp`:
```cpp
#ifndef GAME_SIM_ENTITYSTATE_HPP
#define GAME_SIM_ENTITYSTATE_HPP

#include <glm/glm.hpp>

#include "combat/CombatStats.hpp"

namespace Game::Sim {

/// Gameplay state a controller owns; the PTSD entity is a view onto it.
struct EntityState {
    glm::vec2 pos{0.0F, 0.0F};
    glm::vec2 vel{0.0F, 0.0F};
    glm::vec2 facing{1.0F, 0.0F};
    CombatStats stats{};
    bool awake = false;
    bool dead = false;
    bool kinematic = false;
    int roomId = -1;
};

} // namespace Game::Sim

#endif /* GAME_SIM_ENTITYSTATE_HPP */
```

`include/sim/FireIntent.hpp`:
```cpp
#ifndef GAME_SIM_FIREINTENT_HPP
#define GAME_SIM_FIREINTENT_HPP

#include <glm/glm.hpp>

namespace Game::Sim {

/// Bullet-spawn shapes. Single + Fan are implemented in Plan 1; Burst/Charge/
/// Parabola are reserved for the guns wired in later cycles.
enum class FirePattern { Single, Fan, Burst, Charge, Parabola };

/// A brain's request to spawn bullets this tick. For Single the brain has already
/// baked any scatter into `dir` (so all RNG stays brain-side); for Fan, `dir` is
/// the centre and the FireSystem spreads `count` bullets over `spreadDeg`.
struct FireIntent {
    glm::vec2 origin{0.0F, 0.0F};
    glm::vec2 dir{1.0F, 0.0F}; ///< unit aim direction.
    FirePattern pattern = FirePattern::Single;
    int count = 1;             ///< number of bullets (Fan).
    float spreadDeg = 0.0F;    ///< total fan spread (Fan only).
    float speedPxPerSec = 0.0F;
    float lifeMs = 0.0F;
    int damage = 0;
    float repel = 0.0F;
    int critical = 0;
    bool canThrough = false;
    int pierce = 0;
    int camp = 0;
};

} // namespace Game::Sim

#endif /* GAME_SIM_FIREINTENT_HPP */
```

`include/sim/SimEvent.hpp`:
```cpp
#ifndef GAME_SIM_SIMEVENT_HPP
#define GAME_SIM_SIMEVENT_HPP

#include <cstdint>
#include <string>

namespace Game::Sim {

enum class SimEventType { AnimTrigger, Sfx };

/// A render/audio cue a controller emits; GameScene drains these each frame.
/// Audio is out of scope this cycle -- the shell may ignore Sfx events.
struct SimEvent {
    SimEventType type = SimEventType::AnimTrigger;
    std::uint32_t entityId = 0;
    std::string name; ///< anim-trigger name or sfx id.
};

} // namespace Game::Sim

#endif /* GAME_SIM_SIMEVENT_HPP */
```

`include/sim/WorldCollision.hpp`:
```cpp
#ifndef GAME_SIM_WORLDCOLLISION_HPP
#define GAME_SIM_WORLDCOLLISION_HPP

#include <glm/glm.hpp>

namespace Game::Sim {

/// Wall-blocking query the Simulation calls before committing a move. GameScene
/// implements it over the room AABBs + sealed doors; tests use NullWorldCollision.
class WorldCollision {
public:
    virtual ~WorldCollision() = default;
    virtual bool Blocks(glm::vec2 pos, float radius) const = 0;
};

/// A WorldCollision that never blocks (open arena), for headless tests.
class NullWorldCollision : public WorldCollision {
public:
    bool Blocks(glm::vec2 /*pos*/, float /*radius*/) const override { return false; }
};

} // namespace Game::Sim

#endif /* GAME_SIM_WORLDCOLLISION_HPP */
```

- [ ] **Step 2: Write the compile/smoke test**

`test/SimPodsTest.cpp`:
```cpp
#include <gtest/gtest.h>

#include "sim/BulletState.hpp"
#include "sim/EntityState.hpp"
#include "sim/FireIntent.hpp"
#include "sim/SimEvent.hpp"
#include "sim/WorldCollision.hpp"

using namespace Game::Sim;

// NOLINTBEGIN(readability-magic-numbers)

TEST(SimPodsTest, DefaultsAreInert) {
    BulletState b;
    EXPECT_FALSE(b.active);
    EXPECT_EQ(b.camp, 0);

    EntityState e;
    EXPECT_FALSE(e.awake);
    EXPECT_FALSE(e.dead);
    EXPECT_EQ(e.roomId, -1);

    FireIntent f;
    EXPECT_EQ(f.pattern, FirePattern::Single);
    EXPECT_EQ(f.count, 1);

    SimEvent ev;
    EXPECT_EQ(ev.type, SimEventType::AnimTrigger);
}

TEST(SimPodsTest, NullWorldCollisionNeverBlocks) {
    NullWorldCollision w;
    EXPECT_FALSE(w.Blocks(glm::vec2{0.0F, 0.0F}, 16.0F));
    EXPECT_FALSE(w.Blocks(glm::vec2{1000.0F, -1000.0F}, 64.0F));
}

// NOLINTEND(readability-magic-numbers)
```

- [ ] **Step 3: Register + build + run**

Add to `files.cmake`: `INCLUDE_FILES` `sim/BulletState.hpp`,
`sim/EntityState.hpp`, `sim/FireIntent.hpp`, `sim/SimEvent.hpp`,
`sim/WorldCollision.hpp`; `TEST_FILES` `SimPodsTest.cpp`. (No new `.cpp`.)

Run:
```bash
cmake --build build --config Debug --target SoulKnightTests
ctest --test-dir build -C Debug -R SimPodsTest
```
Expected: warning-clean; `SimPodsTest` 2/2 pass.

- [ ] **Step 4: Commit**

```bash
git add include/sim/BulletState.hpp include/sim/EntityState.hpp include/sim/FireIntent.hpp include/sim/SimEvent.hpp include/sim/WorldCollision.hpp test/SimPodsTest.cpp files.cmake
git commit -m "feat(sim): shared POD state types + WorldCollision interface"
```

---

## Task 4: FireSystem (brain-driven fire geometry)

**Files:**
- Create: `include/sim/FireSystem.hpp`, `src/sim/FireSystem.cpp`
- Test: `test/FireSystemTest.cpp`
- Modify: `files.cmake`

- [ ] **Step 1: Write the failing FireSystem test**

`test/FireSystemTest.cpp`:
```cpp
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "data/RGRandom.hpp"
#include "sim/BulletState.hpp"
#include "sim/FireIntent.hpp"
#include "sim/FireSystem.hpp"

using namespace Game::Sim;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
float AngleDeg(glm::vec2 v) {
    return std::atan2(v.y, v.x) * 180.0F / 3.14159265358979F;
}
} // namespace

TEST(FireSystemTest, SingleEmitsOneBulletAlongDir) {
    FireSystem fs;
    std::vector<BulletState> out;
    std::uint32_t nextId = 1;

    FireIntent in;
    in.pattern = FirePattern::Single;
    in.origin = glm::vec2{10.0F, 20.0F};
    in.dir = glm::vec2{1.0F, 0.0F};
    in.speedPxPerSec = 300.0F;
    in.lifeMs = 1500.0F;
    in.damage = 7;
    in.camp = 0;

    fs.Expand(in, nextId, out);

    ASSERT_EQ(out.size(), 1U);
    EXPECT_EQ(out[0].id, 1U);
    EXPECT_EQ(nextId, 2U);
    EXPECT_TRUE(out[0].active);
    EXPECT_FLOAT_EQ(out[0].pos.x, 10.0F);
    EXPECT_FLOAT_EQ(out[0].vel.x, 300.0F);
    EXPECT_NEAR(out[0].vel.y, 0.0F, 1e-4F);
    EXPECT_EQ(out[0].damage, 7);
}

TEST(FireSystemTest, FanSpreadsCountBulletsEvenly) {
    FireSystem fs;
    std::vector<BulletState> out;
    std::uint32_t nextId = 1;

    FireIntent in;
    in.pattern = FirePattern::Fan;
    in.dir = glm::vec2{1.0F, 0.0F}; // 0 degrees
    in.count = 3;
    in.spreadDeg = 30.0F; // -> bullets at -15, 0, +15
    in.speedPxPerSec = 100.0F;

    fs.Expand(in, nextId, out);

    ASSERT_EQ(out.size(), 3U);
    EXPECT_NEAR(AngleDeg(out[0].vel), -15.0F, 1e-3F);
    EXPECT_NEAR(AngleDeg(out[1].vel), 0.0F, 1e-3F);
    EXPECT_NEAR(AngleDeg(out[2].vel), 15.0F, 1e-3F);
    EXPECT_EQ(nextId, 4U);
}

TEST(FireSystemTest, FanOfOneIsCentre) {
    FireSystem fs;
    std::vector<BulletState> out;
    std::uint32_t nextId = 1;

    FireIntent in;
    in.pattern = FirePattern::Fan;
    in.dir = glm::vec2{0.0F, 1.0F}; // 90 degrees
    in.count = 1;
    in.spreadDeg = 40.0F;
    in.speedPxPerSec = 100.0F;

    fs.Expand(in, nextId, out);

    ASSERT_EQ(out.size(), 1U);
    EXPECT_NEAR(AngleDeg(out[0].vel), 90.0F, 1e-3F);
}

TEST(FireSystemTest, ExpandTakesNoRngDraw) {
    // The FireSystem is a pure geometry function: all randomness is brain-side.
    // A parallel same-seeded RGRandom must be untouched across many expansions.
    FireSystem fs;
    Game::RGRandom ref;
    ref.SetRandomSeed(123);
    Game::RGRandom probe;
    probe.SetRandomSeed(123);

    std::vector<BulletState> out;
    std::uint32_t nextId = 1;
    for (int i = 0; i < 32; ++i) {
        FireIntent in;
        in.pattern = (i % 2 == 0) ? FirePattern::Single : FirePattern::Fan;
        in.count = 3;
        in.spreadDeg = 20.0F;
        in.dir = glm::vec2{1.0F, 0.0F};
        in.speedPxPerSec = 100.0F;
        fs.Expand(in, nextId, out);
        out.clear();
    }
    EXPECT_EQ(ref.Range(0, 1000000), probe.Range(0, 1000000)); // both unadvanced
}

// NOLINTEND(readability-magic-numbers)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --config Debug --target SoulKnightTests`
Expected: FAIL to compile — `sim/FireSystem.hpp` not found.

- [ ] **Step 3: Create the FireSystem header + impl**

`include/sim/FireSystem.hpp`:
```cpp
#ifndef GAME_SIM_FIRESYSTEM_HPP
#define GAME_SIM_FIRESYSTEM_HPP

#include <cstdint>
#include <vector>

#include "sim/BulletState.hpp"
#include "sim/FireIntent.hpp"

namespace Game::Sim {

/// Expands brain-issued FireIntents into BulletStates. Pure geometry: it takes
/// NO RNG draws (the brain already drew any scatter and baked it into the intent).
class FireSystem {
public:
    /// Append the bullets for @p intent to @p out, assigning ids from @p nextId
    /// (advanced per bullet). Implements Single + Fan; other patterns currently
    /// emit a single bullet along intent.dir (reserved for later cycles).
    void Expand(const FireIntent &intent, std::uint32_t &nextId,
                std::vector<BulletState> &out) const;
};

} // namespace Game::Sim

#endif /* GAME_SIM_FIRESYSTEM_HPP */
```

`src/sim/FireSystem.cpp`:
```cpp
#include "sim/FireSystem.hpp"

#include <cmath>

namespace Game::Sim {
namespace {

constexpr float kDegToRad = 3.14159265358979F / 180.0F;

glm::vec2 RotateDeg(glm::vec2 v, float deg) {
    const float r = deg * kDegToRad;
    const float c = std::cos(r);
    const float s = std::sin(r);
    return glm::vec2{v.x * c - v.y * s, v.x * s + v.y * c};
}

BulletState MakeBullet(const FireIntent &in, glm::vec2 dir, std::uint32_t &nextId) {
    BulletState b;
    b.id = nextId++;
    b.pos = in.origin;
    b.vel = dir * in.speedPxPerSec;
    b.lifeMs = in.lifeMs;
    b.damage = in.damage;
    b.camp = in.camp;
    b.repel = in.repel;
    b.critical = in.critical;
    b.canThrough = in.canThrough;
    b.pierce = in.pierce;
    b.active = true;
    return b;
}

} // namespace

void FireSystem::Expand(const FireIntent &intent, std::uint32_t &nextId,
                        std::vector<BulletState> &out) const {
    if (intent.pattern == FirePattern::Fan && intent.count > 1) {
        // Evenly spread `count` bullets across [-spread/2, +spread/2] about dir.
        const float step =
            intent.spreadDeg / static_cast<float>(intent.count - 1);
        const float start = -intent.spreadDeg * 0.5F;
        for (int i = 0; i < intent.count; ++i) {
            const float ang = start + step * static_cast<float>(i);
            out.push_back(MakeBullet(intent, RotateDeg(intent.dir, ang), nextId));
        }
        return;
    }
    // Single (and any not-yet-implemented pattern): one bullet along dir.
    out.push_back(MakeBullet(intent, intent.dir, nextId));
}

} // namespace Game::Sim
```

- [ ] **Step 4: Register + build + run**

Add to `files.cmake`: `SRC_FILES` `sim/FireSystem.cpp`; `INCLUDE_FILES`
`sim/FireSystem.hpp`; `TEST_FILES` `FireSystemTest.cpp`.

Run:
```bash
cmake --build build --config Debug --target SoulKnightTests
ctest --test-dir build -C Debug -R FireSystemTest
```
Expected: warning-clean; `FireSystemTest` 4/4 pass.

- [ ] **Step 5: Commit**

```bash
git add include/sim/FireSystem.hpp src/sim/FireSystem.cpp test/FireSystemTest.cpp files.cmake
git commit -m "feat(sim): FireSystem -- brain-driven Single/Fan bullet geometry"
```

---

## Task 5: Full-suite regression gate

**Files:** none (verification only).

- [ ] **Step 1: Build the whole test target**

Run: `cmake --build build --config Debug --target SoulKnightTests`
Expected: links warning-clean under `/W4` (0 errors, 0 warnings on any `sim/` file).

- [ ] **Step 2: Run the entire suite**

Run: `ctest --test-dir build -C Debug`
Expected: 100% pass (the prior 1813 plus the new FixedClock/Scheduler/SimPods/
FireSystem cases; ~1830+ total, 0 failures).

- [ ] **Step 3: Confirm no game behaviour changed**

Rationale: nothing references `Game::Sim` from `GameScene`/entities yet, so the
playable game is byte-identical to before this plan. (Plan 3 does the rewire.) No
commit needed; this is a checkpoint before Plan 2 (controllers).

---

## Self-review notes (author)

- **Spec coverage (section 4 components):** `FixedClock`, `Scheduler`, `FireIntent`,
  `FireSystem`, `BulletState`, `EntityState`, `WorldCollision`, `SimEvent` — all
  created here. `EnemyController`/`BossController`/`WeaponController`/`BrainFactory`/
  `Simulation` are intentionally Plan 2 + Plan 3 (the decomposition stated above).
- **Determinism (spec section 2/6):** `Scheduler` + `FireSystem` take no wall-clock
  time and no RNG; `FireSystemTest.ExpandTakesNoRngDraw` and `SchedulerTest` FIFO/
  cadence tests pin it.
- **No placeholders:** every step shows complete code or an exact command +
  expected result.
- **Type consistency:** `Scheduler::Handle`/`Callback`, `FireSystem::Expand(intent,
  nextId, out)`, `FirePattern`, `BulletState` fields are used identically across
  tasks and tests.

## Follow-on plans (not in this document)

- **Plan 2 — Controllers + BrainFactory:** `EnemyController` (EnemyAI01 + EnemyAI06
  on the base `EnemyAI` physics), `BossController` (BossAI01), `WeaponController`
  (Gun001 + Gun016), `BrainFactory`; golden per-tick trace tests.
- **Plan 3 — Simulation + GameScene rewire:** the `Simulation` root (ties clock +
  scheduler + controllers + FireSystem + bullets + collisions), then rewire
  `GameScene` to drive it and render from its state; `SimulationTest` replay
  equality; playtest.
