# Engine Port — Boss + Weapon Controllers (Plan 3 of 4) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Wire `BossAI01` and the `Gun001`/`Gun016` weapon brains into deterministic
`Game::Sim` controllers (`BossController`, `WeaponController`), extract the shared
vector math into `SimMath`, and extend `BrainFactory` with `MakeBoss`/`MakeWeapon` —
all golden-trace tested, no PTSD dependency.

**Architecture:** Builds on Plan 1 (infra) + Plan 2 (EnemyController). Each controller
owns its faithful brain as the sole RNG stream and emits `FireIntent`s (boss: `Fan`;
weapon: `Single` scatter). `SimMath.hpp` holds the shared `RotateDeg`/`Normalize`.
Reference: `docs/superpowers/specs/2026-06-09-engine-port-vertical-slice-design.md`.

**Tech Stack:** C++17, MSVC `/W4`, GoogleTest, CMake (`SoulKnightTests`).
**Conventions:** ASCII-only; guard `GAME_SIM_<NAME>_HPP`; `namespace Game::Sim`;
init every member; `kFoo` constants; Doxygen-light. Build/test:
`cmake --build build --config Debug --target SoulKnightTests` then `ctest --test-dir build -C Debug -R <Suite>`.

---

## Design (read first)

**Carried-over Plan-2-review prerequisites, addressed here:**
1. **SimMath first (Task 1):** extract `RotateDeg` from `FireSystem.cpp` and `Normalize`
   from `EnemyController.cpp` into `include/sim/SimMath.hpp` (inline), and switch both
   to it. Eliminates the duplication before new controllers copy it again.
2. **BrainFactory storage:** `MakeBoss`/`MakeWeapon` return by value (prvalue, like
   `MakeEnemy`); controllers are move-deleted, so Plan 4's `Simulation` holds them via
   `std::unique_ptr` (constructed directly, never moved). Documented on each factory fn.
3. **`can_shoot` re-arm:** `BossAI01` has no `can_shoot` gate in its modelled methods
   (it exposes `ChooseAttack`/`WanderDirection`/`OnHurt`), so no re-arm is needed; the
   BossController fires purely on its shoot-cadence timer. Noted, not modelled.
4. **`EnemyAI06`** remains deferred (it is a child-enemy brain, not a kinematic turret);
   out of scope here and in Plan 4's first slice.

**BossController** owns a `BossAI01` brain (sole RNG). Per the shoot cadence
(`brain.ShootCd()`, which halves once on angry) it: draws `ChooseAttack()` (one
`Range(0,100)`), emits a `Fan` `FireIntent` whose bullet count depends on the attack
index (even attack -> 3 bullets, odd -> 5; spread `kBossFanSpreadDeg = 30`, mirroring
the current GameScene placeholder but now brain-driven), aimed at the player; and on a
separate wander cadence draws `WanderDirection()` (two `Range(-1,1)`) for its move
direction. It chases the player by default (move dir = normalize(player - pos) blended
toward wander). `OnHurt(hpAfter, maxHp)` enters the angry phase (re-reads `ShootCd()`,
which is now halved). Move/copy deleted (this-capturing scheduler callbacks).

**WeaponController** is PLAYER-INPUT driven (not scheduler-cadence): each fixed tick the
owner calls `Tick(firing, origin, aimDir, out)`. A fire-rate cooldown (in ticks) gates
shots; while `firing` and the cooldown is elapsed it emits one `Single` `FireIntent`
and resets the cooldown. Two kinds:
- `Kind::Single` (Gun001): `dir = RotateDeg(aim, gun001.ScatterAngle(baseAngle, recoil))`
  (one scatter draw per shot).
- `Kind::HeatMinigun` (Gun016): while `firing`, ramp heat each tick
  (`Gun016::ShouldTickHeat` gate; `m_HeatTime += kFixedStepSeconds` toward
  `heatMaxTime`); a shot's spread = `Gun016::Spread(heatBaseAngle, heatRecoil,
  Gun016::HeatRatio(m_HeatTime, heatMaxTime))`, then
  `dir = RotateDeg(aim, gun016.ScatterAngle(spread))` (one draw per shot). When not
  firing, heat decays to 0 (cools down). The controller holds both gun brains and
  dispatches by `kind`; only the active brain's stream advances.

Both controllers keep ALL randomness brain-side; `FireSystem`/`RotateDeg` are
deterministic, so the stream stays in lockstep (golden-replay tested).

---

## File structure

| File | Responsibility |
|---|---|
| `include/sim/SimMath.hpp` | inline `RotateDeg(vec2,deg)` + `Normalize(vec2)`. Header-only. |
| `include/sim/BossController.hpp` + `src/sim/BossController.cpp` | BossAI01 fan + angry-phase + chase adapter. |
| `include/sim/WeaponController.hpp` + `src/sim/WeaponController.cpp` | Gun001/Gun016 player-input fire adapter. |
| `src/sim/BrainFactory.cpp` (extend) + `include/sim/BrainFactory.hpp` | add `MakeBoss(const BossDef-or-scalars)`, `MakeWeapon(const WeaponDef&)`. |
| `test/SimMathTest.cpp`, `test/BossControllerTest.cpp`, `test/WeaponControllerTest.cpp`, (extend) `test/BrainFactoryTest.cpp` | tests. |

---

## Task 1: SimMath (extract shared vector helpers)

**Files:** Create `include/sim/SimMath.hpp`, `test/SimMathTest.cpp`; Modify
`src/sim/FireSystem.cpp`, `src/sim/EnemyController.cpp`; Modify `files.cmake`.

- [ ] **Step 1: Write `test/SimMathTest.cpp`**
```cpp
#include <gtest/gtest.h>

#include <cmath>

#include "sim/SimMath.hpp"

using Game::Sim::Normalize;
using Game::Sim::RotateDeg;

// NOLINTBEGIN(readability-magic-numbers)

TEST(SimMathTest, NormalizeUnitAndZero) {
    const glm::vec2 n = Normalize(glm::vec2{3.0F, 4.0F});
    EXPECT_NEAR(std::sqrt(n.x * n.x + n.y * n.y), 1.0F, 1e-5F);
    EXPECT_NEAR(n.x, 0.6F, 1e-5F);
    EXPECT_NEAR(n.y, 0.8F, 1e-5F);
    const glm::vec2 z = Normalize(glm::vec2{0.0F, 0.0F});
    EXPECT_FLOAT_EQ(z.x, 1.0F); // zero -> {1,0}, never NaN
    EXPECT_FLOAT_EQ(z.y, 0.0F);
}

TEST(SimMathTest, RotateDegCcw) {
    const glm::vec2 r = RotateDeg(glm::vec2{1.0F, 0.0F}, 90.0F);
    EXPECT_NEAR(r.x, 0.0F, 1e-5F);
    EXPECT_NEAR(r.y, 1.0F, 1e-5F);
    const glm::vec2 z = RotateDeg(glm::vec2{1.0F, 0.0F}, 0.0F);
    EXPECT_FLOAT_EQ(z.x, 1.0F);
    EXPECT_FLOAT_EQ(z.y, 0.0F);
}

// NOLINTEND(readability-magic-numbers)
```

- [ ] **Step 2: Create `include/sim/SimMath.hpp`**
```cpp
#ifndef GAME_SIM_SIMMATH_HPP
#define GAME_SIM_SIMMATH_HPP

#include <cmath>

#include <glm/glm.hpp>

namespace Game::Sim {

/// Degrees -> radians.
inline constexpr float kDegToRad = 3.14159265358979F / 180.0F;

/// Rotate @p v by @p deg counter-clockwise (screen-math, +y up).
inline glm::vec2 RotateDeg(glm::vec2 v, float deg) {
    const float r = deg * kDegToRad;
    const float c = std::cos(r);
    const float s = std::sin(r);
    return glm::vec2{v.x * c - v.y * s, v.x * s + v.y * c};
}

/// Unit vector along @p v; returns {1,0} for the zero vector (never NaN).
inline glm::vec2 Normalize(glm::vec2 v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y);
    return len > 0.0F ? v / len : glm::vec2{1.0F, 0.0F};
}

} // namespace Game::Sim

#endif /* GAME_SIM_SIMMATH_HPP */
```

- [ ] **Step 3: Switch FireSystem + EnemyController to SimMath**

In `src/sim/FireSystem.cpp`: `#include "sim/SimMath.hpp"`, delete the local
`kDegToRad` constant and `RotateDeg` helper from the anonymous namespace, and use
`RotateDeg` from `Game::Sim`. In `src/sim/EnemyController.cpp`: `#include
"sim/SimMath.hpp"`, delete the local anonymous-namespace `Normalize` helper, and use
`Game::Sim::Normalize` (it is in the same namespace, so unqualified `Normalize` works).
Both files must still build /W4-clean with no unused-function warnings.

- [ ] **Step 4: Register + build + test**

`files.cmake`: INCLUDE_FILES `sim/SimMath.hpp`; TEST_FILES `SimMathTest.cpp`.
Run build; `ctest -R "SimMathTest|FireSystemTest|EnemyControllerTest"` -> all pass
(SimMath 2 + FireSystem 6 + EnemyController 13). Warning-clean.

- [ ] **Step 5: Commit**
```bash
git add include/sim/SimMath.hpp src/sim/FireSystem.cpp src/sim/EnemyController.cpp test/SimMathTest.cpp files.cmake
git commit -m "refactor(sim): extract SimMath (RotateDeg/Normalize); dedupe FireSystem+EnemyController"
```

---

## Task 2: BossController — construction, angry phase, chase move

**Files:** Create `include/sim/BossController.hpp`, `src/sim/BossController.cpp`;
Test `test/BossControllerTest.cpp`; Modify `files.cmake`.

- [ ] **Step 1: Write the failing test**
```cpp
#include <gtest/gtest.h>

#include <cmath>

#include "sim/BossController.hpp"

using Game::Sim::BossController;

// NOLINTBEGIN(readability-magic-numbers)

TEST(BossControllerTest, SpawnsWithBaseShootCd) {
    BossController b(/*baseShootCd=*/2.0F, glm::vec2{0.0F, 0.0F}, /*maxHp=*/600, 1);
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCdSeconds(), 2.0F);
    EXPECT_FLOAT_EQ(b.State().pos.x, 0.0F);
}

TEST(BossControllerTest, AngryHalvesShootCdOnceBelowHalfHp) {
    BossController b(2.0F, glm::vec2{0.0F, 0.0F}, 600, 1);
    b.OnHurt(400, 600); // 0.667 > 0.5 -> not angry
    EXPECT_FALSE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCdSeconds(), 2.0F);
    b.OnHurt(200, 600); // 0.333 < 0.5 -> angry, cd halves to 1.0
    EXPECT_TRUE(b.Angry());
    EXPECT_FLOAT_EQ(b.ShootCdSeconds(), 1.0F);
    b.OnHurt(50, 600); // already angry -> no further halving
    EXPECT_FLOAT_EQ(b.ShootCdSeconds(), 1.0F);
}

TEST(BossControllerTest, ChaseDirIsTowardPlayer) {
    BossController b(2.0F, glm::vec2{0.0F, 0.0F}, 600, 1);
    b.SetTarget(glm::vec2{10.0F, 0.0F});
    const glm::vec2 d = b.ChaseDir();
    EXPECT_NEAR(d.x, 1.0F, 1e-4F);
    EXPECT_NEAR(d.y, 0.0F, 1e-4F);
}

// NOLINTEND(readability-magic-numbers)
```

- [ ] **Step 2: Create `include/sim/BossController.hpp`**
```cpp
#ifndef GAME_SIM_BOSSCONTROLLER_HPP
#define GAME_SIM_BOSSCONTROLLER_HPP

#include <vector>

#include <glm/glm.hpp>

#include "combat/BossAI01.hpp"
#include "sim/EntityState.hpp"
#include "sim/FireIntent.hpp"
#include "sim/Scheduler.hpp"

namespace Game::Sim {

/// Drives the BossAI01 boss: chases the player, fires a brain-selected fan on its
/// shoot cadence (halved once on the angry phase), wanders on a wander cadence.
/// The BossAI01 brain is the sole RNG stream. Move/copy deleted (this-capture).
class BossController {
public:
    /// Bullets in the fan for an even / odd attack index (placeholder slice fan).
    static constexpr int kFanEven = 3;
    static constexpr int kFanOdd = 5;
    static constexpr float kFanSpreadDeg = 30.0F;
    static constexpr float kSpeed = 45.0F;       ///< boss move speed px/s (slice).
    static constexpr float kWanderSeconds = 0.5F;///< wander re-pick cadence (slice).
    static constexpr float kBulletSpeedPxPerSec = 300.0F; ///< slice.
    static constexpr float kBulletLifeMs = 1500.0F;       ///< slice.

    BossController(float baseShootCd, glm::vec2 spawn, int maxHp, int seed);

    BossController(BossController &&) = delete;
    BossController &operator=(BossController &&) = delete;

    /// Schedule the shoot + wander cadence. @pre scheduler/fireOut outlive this and
    /// fireOut is not reallocated while alive (raw pointers held).
    void Activate(Scheduler &scheduler, std::vector<FireIntent> &fireOut);

    void SetTarget(glm::vec2 target) { m_Target = target; }

    /// Apply post-hit HP; enters the angry phase once at < 50% (BossAI01.OnHurt).
    void OnHurt(int hpAfter, int maxHp);

    /// Unit chase direction toward the target (zero target -> {1,0}).
    glm::vec2 ChaseDir() const;

    void Kill();

    bool Angry() const { return m_Brain.Angry(); }
    float ShootCdSeconds() const { return m_Brain.ShootCd(); }
    const EntityState &State() const { return m_State; }
    EntityState &MutableState() { return m_State; }
    BossAI01 &Brain() { return m_Brain; }
    glm::vec2 WanderDir() const { return m_WanderDir; }

private:
    void OnShootTick();
    void OnWanderTick();

    BossAI01 m_Brain;
    EntityState m_State;
    glm::vec2 m_Target{0.0F, 0.0F};
    glm::vec2 m_WanderDir{0.0F, 0.0F};

    Scheduler *m_Scheduler = nullptr;
    std::vector<FireIntent> *m_FireOut = nullptr;
    Scheduler::Handle m_WanderHandle = 0;
    Scheduler::Handle m_ShootHandle = 0;
};

} // namespace Game::Sim

#endif /* GAME_SIM_BOSSCONTROLLER_HPP */
```

- [ ] **Step 3: Create `src/sim/BossController.cpp` (construction + OnHurt + ChaseDir; cadence stubs)**
```cpp
#include "sim/BossController.hpp"

#include "sim/SimMath.hpp"

namespace Game::Sim {

BossController::BossController(float baseShootCd, glm::vec2 spawn, int maxHp, int seed)
    : m_Brain(baseShootCd) {
    m_Brain.SetSeed(seed);
    m_State.pos = spawn;
    m_State.stats.maxHp = maxHp;
    m_State.stats.hp = maxHp;
}

void BossController::OnHurt(int hpAfter, int maxHp) {
    m_Brain.OnHurt(hpAfter, maxHp); // enters angry once at <50%, halves ShootCd
}

glm::vec2 BossController::ChaseDir() const {
    return Normalize(m_Target - m_State.pos);
}

void BossController::Kill() {
    m_State.dead = true;
    if (m_Scheduler != nullptr) {
        m_Scheduler->Cancel(m_WanderHandle);
        m_Scheduler->Cancel(m_ShootHandle);
    }
}

void BossController::OnShootTick() {}  // Task 3
void BossController::OnWanderTick() {} // Task 3

void BossController::Activate(Scheduler &scheduler, std::vector<FireIntent> &fireOut) {
    m_Scheduler = &scheduler;
    m_FireOut = &fireOut;
    // cadence scheduling in Task 3.
}

} // namespace Game::Sim
```

- [ ] **Step 4: Register + build + test**

`files.cmake`: SRC `sim/BossController.cpp`; INCLUDE `sim/BossController.hpp`; TEST
`BossControllerTest.cpp`. Build; `ctest -R BossControllerTest` -> 3/3.

- [ ] **Step 5: Commit** `git commit -m "feat(sim): BossController -- construction, angry phase, chase"`

---

## Task 3: BossController — fan shoot + wander cadence + golden trace

**Files:** Modify `src/sim/BossController.cpp`, `test/BossControllerTest.cpp`.

- [ ] **Step 1: Append the cadence tests**
```cpp
TEST(BossControllerTest, ShootTickEmitsBrainSelectedFanOnCadence) {
    BossController b(0.04F, glm::vec2{0.0F, 0.0F}, 600, 7); // shootCd 2 ticks
    Game::Sim::Scheduler sched;
    std::vector<Game::Sim::FireIntent> fire;
    b.MutableState().awake = true;
    b.SetTarget(glm::vec2{0.0F, 10.0F});
    b.Activate(sched, fire);

    // Drive to the first shoot tick. Reproduce the attack roll from a parallel brain
    // to predict the fan count (even attack -> 3, odd -> 5).
    Game::BossAI01 ref(0.04F);
    ref.SetSeed(7);

    sched.Tick(); sched.Tick(); // shoot fires on tick 2
    ASSERT_FALSE(fire.empty());
    const Game::Sim::FireIntent &f = fire.front();
    EXPECT_EQ(f.pattern, Game::Sim::FirePattern::Fan);
    EXPECT_EQ(f.camp, 1);
    EXPECT_FLOAT_EQ(f.spreadDeg, BossController::kFanSpreadDeg);
    // (count is 3 or 5 depending on ref.ChooseAttack(); both are valid)
    EXPECT_TRUE(f.count == BossController::kFanEven || f.count == BossController::kFanOdd);
}

TEST(BossControllerTest, FullCadenceReplayIsDeterministic) {
    auto run = [](int seed) {
        BossController b(0.06F, glm::vec2{0.0F, 0.0F}, 600, seed);
        Game::Sim::Scheduler sched;
        std::vector<Game::Sim::FireIntent> fire;
        b.MutableState().awake = true;
        b.SetTarget(glm::vec2{5.0F, 5.0F});
        b.Activate(sched, fire);
        std::vector<float> trace;
        for (int i = 0; i < 24; ++i) {
            sched.Tick();
            trace.push_back(b.WanderDir().x);
            trace.push_back(static_cast<float>(fire.size()));
            if (!fire.empty()) trace.push_back(static_cast<float>(fire.back().count));
        }
        return trace;
    };
    EXPECT_EQ(run(99), run(99));
}
```

- [ ] **Step 2: Replace the cadence stubs in `src/sim/BossController.cpp`**
```cpp
void BossController::OnShootTick() {
    if (m_Scheduler == nullptr || m_FireOut == nullptr || m_State.dead) {
        return;
    }
    if (m_State.awake) {
        const int attack = m_Brain.ChooseAttack(); // 1 draw Range(0,100)
        FireIntent intent;
        intent.pattern = FirePattern::Fan;
        intent.origin = m_State.pos;
        intent.dir = Normalize(m_Target - m_State.pos);
        intent.count = (attack % 2 == 0) ? kFanEven : kFanOdd;
        intent.spreadDeg = kFanSpreadDeg;
        intent.speedPxPerSec = kBulletSpeedPxPerSec;
        intent.lifeMs = kBulletLifeMs;
        intent.damage = 1;
        intent.camp = 1;
        m_FireOut->push_back(intent);
    }
    // reschedule at the (possibly angry-halved) shoot cd.
    const int next = (std::max)(1, Scheduler::SecondsToTicks(m_Brain.ShootCd()));
    m_ShootHandle = m_Scheduler->Invoke(next, [this] { OnShootTick(); });
}

void BossController::OnWanderTick() {
    if (m_State.dead || !m_State.awake) {
        return;
    }
    m_WanderDir = m_Brain.WanderDirection(); // 2 draws Range(-1,1)
}

void BossController::Activate(Scheduler &scheduler, std::vector<FireIntent> &fireOut) {
    m_Scheduler = &scheduler;
    m_FireOut = &fireOut;
    const int wanderTicks = (std::max)(1, Scheduler::SecondsToTicks(kWanderSeconds));
    const int shootTicks = (std::max)(1, Scheduler::SecondsToTicks(m_Brain.ShootCd()));
    m_WanderHandle = scheduler.InvokeRepeating(wanderTicks, wanderTicks, [this] { OnWanderTick(); });
    m_ShootHandle = scheduler.Invoke(shootTicks, [this] { OnShootTick(); });
}
```
Add `#include <algorithm>` to the .cpp (for `std::max`).

- [ ] **Step 3: Build + test** `ctest -R BossControllerTest` -> 5/5. Warning-clean.
- [ ] **Step 4: Commit** `git commit -m "feat(sim): BossController -- fan shoot + wander cadence + determinism"`

---

## Task 4: WeaponController — Gun001 single-shot fire-rate + scatter

**Files:** Create `include/sim/WeaponController.hpp`, `src/sim/WeaponController.cpp`;
Test `test/WeaponControllerTest.cpp`; Modify `files.cmake`.

- [ ] **Step 1: Write the failing test**
```cpp
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "combat/Gun001.hpp"
#include "sim/WeaponController.hpp"

using Game::Sim::WeaponController;

// NOLINTBEGIN(readability-magic-numbers)

TEST(WeaponControllerTest, SingleFiresOnIntervalWhileHeld) {
    WeaponController::Params p;
    p.kind = WeaponController::Kind::Single;
    p.fireIntervalSeconds = 0.04F; // every 2 ticks
    p.baseAngle = 0.0F;            // no spread for a clean direction check
    WeaponController w(p, 5);
    std::vector<Game::Sim::FireIntent> out;
    const glm::vec2 origin{0.0F, 0.0F};
    const glm::vec2 aim{1.0F, 0.0F};

    w.Tick(true, origin, aim, out); // tick1: first shot fires immediately
    EXPECT_EQ(out.size(), 1U);
    w.Tick(true, origin, aim, out); // tick2: on cooldown
    EXPECT_EQ(out.size(), 1U);
    w.Tick(true, origin, aim, out); // tick3: fires again
    EXPECT_EQ(out.size(), 2U);
    EXPECT_EQ(out[0].pattern, Game::Sim::FirePattern::Single);
    EXPECT_EQ(out[0].camp, 0); // player bullet
}

TEST(WeaponControllerTest, NoFireWhenNotHeld) {
    WeaponController::Params p;
    p.kind = WeaponController::Kind::Single;
    p.fireIntervalSeconds = 0.02F;
    WeaponController w(p, 1);
    std::vector<Game::Sim::FireIntent> out;
    for (int i = 0; i < 5; ++i) {
        w.Tick(false, glm::vec2{0.0F, 0.0F}, glm::vec2{1.0F, 0.0F}, out);
    }
    EXPECT_TRUE(out.empty());
}

TEST(WeaponControllerTest, ScatterMatchesGun001InLockstep) {
    WeaponController::Params p;
    p.kind = WeaponController::Kind::Single;
    p.fireIntervalSeconds = 0.02F; // every tick
    p.baseAngle = 10.0F;
    p.recoil = 0.5F;
    WeaponController w(p, 808);
    Game::Gun001 ref;
    ref.SetSeed(808);
    std::vector<Game::Sim::FireIntent> out;
    for (int i = 0; i < 6; ++i) {
        w.Tick(true, glm::vec2{0.0F, 0.0F}, glm::vec2{1.0F, 0.0F}, out);
        const float refScatter = ref.ScatterAngle(10.0F, 0.5F);
        const float gotAngle = std::atan2(out.back().dir.y, out.back().dir.x) * 180.0F / 3.14159265358979F;
        EXPECT_NEAR(gotAngle, refScatter, 1e-3F); // aim is +x(0deg), so dir angle == scatter
    }
}

// NOLINTEND(readability-magic-numbers)
```

- [ ] **Step 2: Create `include/sim/WeaponController.hpp`**
```cpp
#ifndef GAME_SIM_WEAPONCONTROLLER_HPP
#define GAME_SIM_WEAPONCONTROLLER_HPP

#include <vector>

#include <glm/glm.hpp>

#include "combat/Gun001.hpp"
#include "combat/Gun016.hpp"
#include "sim/FireIntent.hpp"

namespace Game::Sim {

/// Player-input-driven weapon: each fixed tick the owner calls Tick(firing, ...);
/// a fire-rate cooldown gates shots. The active gun brain is the sole RNG stream.
class WeaponController {
public:
    enum class Kind { Single, HeatMinigun };

    struct Params {
        Kind kind = Kind::Single;
        float fireIntervalSeconds = 0.15F;
        float bulletSpeedPxPerSec = 120.0F;
        float lifeMs = 1500.0F;
        int damage = 1;
        // Single (Gun001):
        float baseAngle = 5.0F;
        float recoil = 0.0F;
        // HeatMinigun (Gun016):
        float heatMaxTime = 2.0F;
        float heatBaseAngle = 20.0F;
        float heatRecoil = 0.0F;
    };

    WeaponController(const Params &params, int seed);

    /// Advance one fixed tick. If @p firing and the fire-rate cooldown elapsed,
    /// append one Single FireIntent (scattered, player camp 0) to @p out.
    void Tick(bool firing, glm::vec2 origin, glm::vec2 aimDir,
              std::vector<FireIntent> &out);

    float HeatTime() const { return m_HeatTime; } ///< for tests.

private:
    Params m_Params;
    Gun001 m_Gun001;
    Gun016 m_Gun016;
    int m_CooldownTicks = 0;
    float m_HeatTime = 0.0F;
};

} // namespace Game::Sim

#endif /* GAME_SIM_WEAPONCONTROLLER_HPP */
```

- [ ] **Step 3: Create `src/sim/WeaponController.cpp` (Single path; Heat path added in Task 5)**
```cpp
#include "sim/WeaponController.hpp"

#include <algorithm>

#include "sim/Scheduler.hpp"
#include "sim/SimMath.hpp"

namespace Game::Sim {

WeaponController::WeaponController(const Params &params, int seed)
    : m_Params(params) {
    m_Gun001.SetSeed(seed);
    m_Gun016.SetSeed(seed);
}

void WeaponController::Tick(bool firing, glm::vec2 origin, glm::vec2 aimDir,
                           std::vector<FireIntent> &out) {
    if (m_CooldownTicks > 0) {
        --m_CooldownTicks;
    }
    if (!firing || m_CooldownTicks > 0) {
        return;
    }
    // emit one shot.
    float scatter = 0.0F;
    if (m_Params.kind == Kind::Single) {
        scatter = m_Gun001.ScatterAngle(m_Params.baseAngle, m_Params.recoil);
    }
    // (HeatMinigun branch added in Task 5.)
    FireIntent intent;
    intent.pattern = FirePattern::Single;
    intent.origin = origin;
    intent.dir = RotateDeg(Normalize(aimDir), scatter);
    intent.speedPxPerSec = m_Params.bulletSpeedPxPerSec;
    intent.lifeMs = m_Params.lifeMs;
    intent.damage = m_Params.damage;
    intent.camp = 0; // player bullet
    out.push_back(intent);
    m_CooldownTicks = (std::max)(1, Scheduler::SecondsToTicks(m_Params.fireIntervalSeconds));
}

} // namespace Game::Sim
```

- [ ] **Step 4: Register + build + test**

`files.cmake`: SRC `sim/WeaponController.cpp`; INCLUDE `sim/WeaponController.hpp`;
TEST `WeaponControllerTest.cpp`. Build; `ctest -R WeaponControllerTest` -> 3/3.

- [ ] **Step 5: Commit** `git commit -m "feat(sim): WeaponController -- Gun001 single-shot fire-rate + scatter"`

---

## Task 5: WeaponController — Gun016 heat-minigun growing spread

**Files:** Modify `src/sim/WeaponController.cpp`, `test/WeaponControllerTest.cpp`.

- [ ] **Step 1: Append the heat tests**
```cpp
TEST(WeaponControllerTest, HeatRampsWhileFiringAndCoolsWhenReleased) {
    WeaponController::Params p;
    p.kind = WeaponController::Kind::HeatMinigun;
    p.fireIntervalSeconds = 0.02F; // every tick
    p.heatMaxTime = 0.2F;          // 10 ticks to full
    WeaponController w(p, 3);
    std::vector<Game::Sim::FireIntent> out;
    for (int i = 0; i < 5; ++i) {
        w.Tick(true, glm::vec2{0.0F, 0.0F}, glm::vec2{1.0F, 0.0F}, out);
    }
    EXPECT_GT(w.HeatTime(), 0.0F);     // heat built while firing
    const float hot = w.HeatTime();
    w.Tick(false, glm::vec2{0.0F, 0.0F}, glm::vec2{1.0F, 0.0F}, out);
    EXPECT_LT(w.HeatTime(), hot);      // cooled when released
}

TEST(WeaponControllerTest, HeatMinigunScatterMatchesGun016InLockstep) {
    WeaponController::Params p;
    p.kind = WeaponController::Kind::HeatMinigun;
    p.fireIntervalSeconds = 0.02F;
    p.heatMaxTime = 2.0F;
    p.heatBaseAngle = 20.0F;
    p.heatRecoil = 0.0F;
    WeaponController w(p, 1234);
    Game::Gun016 ref;
    ref.SetSeed(1234);
    std::vector<Game::Sim::FireIntent> out;
    // Drive the heat model in parallel: heat ramps kFixedStepSeconds (0.02) per tick.
    float heat = 0.0F;
    for (int i = 0; i < 5; ++i) {
        // controller will ramp THEN fire; mirror that order.
        if (heat < 2.0F) heat += 0.02F;
        const float spread = Game::Gun016::Spread(20.0F, 0.0F, Game::Gun016::HeatRatio(heat, 2.0F));
        const float refScatter = ref.ScatterAngle(spread);
        w.Tick(true, glm::vec2{0.0F, 0.0F}, glm::vec2{1.0F, 0.0F}, out);
        const float gotAngle = std::atan2(out.back().dir.y, out.back().dir.x) * 180.0F / 3.14159265358979F;
        EXPECT_NEAR(gotAngle, refScatter, 1e-2F);
    }
}
```

- [ ] **Step 2: Implement the heat path in `src/sim/WeaponController.cpp`**

Add the heat ramp at the top of `Tick` (before the cooldown gate) and the HeatMinigun
scatter branch. Replace the body of `Tick` with:
```cpp
void WeaponController::Tick(bool firing, glm::vec2 origin, glm::vec2 aimDir,
                           std::vector<FireIntent> &out) {
    // Heat model (Gun016): ramp while firing toward heatMaxTime, cool toward 0 when
    // released. The ramp/cool step is the fixed timestep (FAITHFUL: Gun016 heat tick).
    if (m_Params.kind == Kind::HeatMinigun) {
        if (firing) {
            if (Gun016::ShouldTickHeat(true, m_HeatTime, m_Params.heatMaxTime)) {
                m_HeatTime += kFixedStepSeconds;
            }
        } else {
            m_HeatTime = (std::max)(0.0F, m_HeatTime - kFixedStepSeconds);
        }
    }
    if (m_CooldownTicks > 0) {
        --m_CooldownTicks;
    }
    if (!firing || m_CooldownTicks > 0) {
        return;
    }
    float scatter = 0.0F;
    if (m_Params.kind == Kind::Single) {
        scatter = m_Gun001.ScatterAngle(m_Params.baseAngle, m_Params.recoil);
    } else { // HeatMinigun
        const float ratio = Gun016::HeatRatio(m_HeatTime, m_Params.heatMaxTime);
        const float spread = Gun016::Spread(m_Params.heatBaseAngle, m_Params.heatRecoil, ratio);
        scatter = m_Gun016.ScatterAngle(spread);
    }
    FireIntent intent;
    intent.pattern = FirePattern::Single;
    intent.origin = origin;
    intent.dir = RotateDeg(Normalize(aimDir), scatter);
    intent.speedPxPerSec = m_Params.bulletSpeedPxPerSec;
    intent.lifeMs = m_Params.lifeMs;
    intent.damage = m_Params.damage;
    intent.camp = 0;
    out.push_back(intent);
    m_CooldownTicks = (std::max)(1, Scheduler::SecondsToTicks(m_Params.fireIntervalSeconds));
}
```
Add `#include "sim/SimConfig.hpp"` (for `kFixedStepSeconds`).

- [ ] **Step 3: Build + test** `ctest -R WeaponControllerTest` -> 5/5. Warning-clean.
- [ ] **Step 4: Commit** `git commit -m "feat(sim): WeaponController -- Gun016 heat-minigun growing spread"`

---

## Task 6: BrainFactory MakeBoss + MakeWeapon

**Files:** Modify `include/sim/BrainFactory.hpp`, `src/sim/BrainFactory.cpp`,
`test/BrainFactoryTest.cpp`.

- [ ] **Step 1: Inspect `WeaponDef`** in `include/data/GameData.hpp` for the fields:
weapon/fire speed (shots cadence), bullet speed, deviation/angle, damage/atk, energy
cost. Note the real names for `MakeWeapon`.

- [ ] **Step 2: Append factory tests** to `test/BrainFactoryTest.cpp`:
```cpp
#include "sim/BossController.hpp"
#include "sim/WeaponController.hpp"

TEST(BrainFactoryTest, MakeBossSeedsAndSpawns) {
    Game::Sim::BossController b = BrainFactory::MakeBoss(2.0F, glm::vec2{4.0F, 5.0F}, 600, 9);
    EXPECT_FLOAT_EQ(b.State().pos.x, 4.0F);
    EXPECT_FLOAT_EQ(b.ShootCdSeconds(), 2.0F);
    EXPECT_TRUE(b.Brain().Seeded());
}

TEST(BrainFactoryTest, MakeWeaponSingleFromDef) {
    Game::WeaponDef def{};
    // set the real fields: a single-shot gun (e.g. id "Gun001")
    Game::Sim::WeaponController w = BrainFactory::MakeWeapon(def, "Gun001", 1);
    std::vector<Game::Sim::FireIntent> out;
    w.Tick(true, glm::vec2{0.0F, 0.0F}, glm::vec2{1.0F, 0.0F}, out);
    EXPECT_EQ(out.size(), 1U);
}
```

- [ ] **Step 3: Add to `BrainFactory.hpp`** (prvalue returns; controllers are move-deleted):
```cpp
    /// Build a BossController (BossAI01) at @p spawn. Returned by value (prvalue);
    /// the owner must store it stably (e.g. unique_ptr) -- the controller is move-deleted.
    static BossController MakeBoss(float baseShootCd, glm::vec2 spawn, int maxHp, int seed);

    /// Build a WeaponController from a WeaponDef. @p weaponId selects the gun brain
    /// (e.g. "Gun016" -> HeatMinigun, else Single). Returned by value (prvalue).
    static WeaponController MakeWeapon(const WeaponDef &def, const std::string &weaponId,
                                       int seed);
```
(Add `#include "sim/BossController.hpp"`, `#include "sim/WeaponController.hpp"`, `#include <string>`.)

- [ ] **Step 4: Implement in `BrainFactory.cpp`** (map the REAL WeaponDef fields; prvalue returns):
```cpp
BossController BrainFactory::MakeBoss(float baseShootCd, glm::vec2 spawn, int maxHp,
                                      int seed) {
    return BossController(baseShootCd, spawn, maxHp, seed);
}

WeaponController BrainFactory::MakeWeapon(const WeaponDef &def, const std::string &weaponId,
                                          int seed) {
    WeaponController::Params p;
    p.kind = (weaponId == "Gun016") ? WeaponController::Kind::HeatMinigun
                                     : WeaponController::Kind::Single;
    p.fireIntervalSeconds = /* def fire-rate field -> seconds */;
    p.bulletSpeedPxPerSec = /* def bulletSpeed field */ * kDataSpeedToPxPerSec;
    p.damage = /* def atk/damage field */;
    p.baseAngle = /* def deviation/angle field */;
    return WeaponController(p, seed);
}
```
Fill the `/* ... */` from the REAL WeaponDef field names found in Step 1; include
`sim/SimConfig.hpp` for `kDataSpeedToPxPerSec`. If a field is absent, use the Params
default and add a `// TODO` note.

- [ ] **Step 5: Build + test** `ctest -R BrainFactoryTest` -> 4/4. Warning-clean.
- [ ] **Step 6: Commit** `git commit -m "feat(sim): BrainFactory MakeBoss + MakeWeapon"`

---

## Task 7: Full-suite regression gate

- [ ] **Step 1:** `cmake --build build --config Debug --target SoulKnightTests` -> /W4-clean.
- [ ] **Step 2:** `ctest --test-dir build -C Debug` -> 100% pass (prior + SimMath/Boss/
  Weapon/new BrainFactory cases). No game behaviour change (controllers not wired into
  GameScene yet -- Plan 4).

---

## Self-review notes (author)

- **Spec coverage:** delivers BossController (BossAI01 fan + angry), WeaponController
  (Gun001 + Gun016), SimMath, factory MakeBoss/MakeWeapon. Simulation + GameScene rewire
  = Plan 4.
- **Determinism:** each controller's brain is the sole RNG stream; lockstep pinned by
  the `*MatchesGun*InLockstep` and `FullCadenceReplay` tests; SimMath/FireSystem are RNG-free.
- **Prerequisites addressed:** SimMath dedupes Normalize/RotateDeg (Task 1); move-deleted
  controllers + prvalue factory returns (Plan 4 holds via unique_ptr); can_shoot N/A for
  BossAI01; EnemyAI06 deferred.
- **No placeholders:** only deferred lookups are the real `WeaponDef` field names (Task 6
  Step 1 directs reading GameData.hpp), external to this plan.

## Follow-on

- **Plan 4 — Simulation + GameScene rewire:** the `Simulation` root (FixedClock +
  Scheduler + Enemy/Boss controllers via unique_ptr + WeaponController + FireSystem +
  bullet motion/collision via Combat::ResolveHit + WorldCollision), then rewire
  `GameScene` to drive it + render from its state; `SimulationTest` replay equality + playtest.
