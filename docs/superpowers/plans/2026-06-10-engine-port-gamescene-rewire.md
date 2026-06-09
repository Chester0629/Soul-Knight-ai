# Engine Port — GameScene Rewire (Plan 4b of 4) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.
>
> **NOTE ON VERIFICATION:** Unlike Plans 1-4a, `GameScene` is NOT headless-unit-testable (it
> needs PTSD: window, Renderer, Input, Context). The deterministic gameplay logic is ALREADY
> proven by the headless `SimulationTest` (Plan 4a). So each task here is verified by
> **(a) `/W4`-clean build, (b) the full existing test suite staying green, and (c) a manual
> `/run` playtest** — there are no new GoogleTest cases for the scene itself. Treat a clean
> build + green regression + a working playtest as the gate.

**Goal:** Rewire `GameScene` from the OLD entity-owned brain path (`enemy->Think`/`boss->Think`/
`WeaponInstance`/inline `UpdateBullets` damage) to **drive one `Game::Sim::Simulation`** and
render from its state: GameScene becomes a `WorldCollision` provider + input/render shell, with
the PTSD `Enemy`/`Boss`/`Bullet` objects reduced to **views** mirrored from the sim each frame.

**Architecture:** `GameScene` additionally inherits `Sim::WorldCollision` (its existing
`BlocksAny` IS the `Blocks` impl) and holds a `std::optional<Sim::Simulation> m_Sim` constructed
with `(m_RunSeed, this)`. `OnEnter` spawns the same entities AND mirrors each into the sim
(`AddEnemy`/`SetBoss`/`EquipWeapon`) with the identical seeds the headless tests use. `Update`
snapshots input (player movement stays scene-side), pushes player vitals into the sim, runs
`sim.Advance(dtMs, WorldInputs)`, then syncs: enemy/boss view transforms from `EntityView`s (by
index id), bullet pooled views reconciled from `BulletState`s (by id), player hp/armor pulled
back, energy spent per `PlayerShotsLastAdvance()`. The OLD gameplay code is deleted.

**Tech Stack:** C++17, MSVC `/W4`, GoogleTest, CMake (`SoulKnight` exe + `SoulKnightTests`), PTSD.
**Conventions:** ASCII-only; `namespace Game`; `kFoo` constants; init every member; Doxygen-light.
Build: `cmake --build build --config Debug --target SoulKnight` (the game exe) and
`--target SoulKnightTests`. Test: `ctest --test-dir build -C Debug`. Playtest: `/run`.

---

## Design (read first)

**Locked decisions (grounded in the Plan-4 understanding sweep + the read of GameScene.cpp,
Enemy/Boss/Bullet.hpp, Simulation.hpp):**

1. **Entity-as-view, never erased.** `m_Enemies` / `m_Bosses` stay 1:1 with the sim's enemies/
   boss **by index** (same spawn order), so they are NOT erased on death (erasing would desync
   the index↔sim-id mapping). On death (`EntityView.alive == false`) the view is `RemoveChild`-ed
   and its slot **nulled** (`m_Enemies[i] = nullptr`); the sync loop skips null slots. (The sim
   likewise retains dead controllers for stable ids — Plan 4a.)

2. **Player is the authority for its own vitals; the sim borrows them per frame.** Each `Update`:
   `m_Sim->SetPlayerStats(m_Player->Stats())` (push current hp/armor/energy in) → `Advance`
   (the sim's hit resolution damages its copy's hp/armor) → pull `hp`/`armor` back into
   `m_Player->Stats()`. **Energy stays purely scene-side** (regen + spend); the firing input is
   gated on energy and one `SpendEnergy(cost)` is charged per `PlayerShotsLastAdvance()`.

3. **Bullets: reconcile pooled views by id.** A new `std::unordered_map<std::uint32_t,
   std::shared_ptr<Bullet>> m_BulletViews` maps `BulletState.id` -> pooled `Bullet` view. Each
   frame: acquire+`Init`+`AddChild` for new ids, set `m_Transform.translation = bs.pos` for live
   ids, `Deactivate`+`Release`+erase for gone ids. The view's own `Update` is **never called**
   (the sim owns motion); the looping sprite animation still advances via `Renderer.Update()`.
   (Map iteration order affects only which sprite is which — NOT gameplay determinism, which is
   entirely sim-side.)

4. **`WorldCollision`:** `GameScene : public Core::Scene, public Sim::WorldCollision`, with
   `bool Blocks(glm::vec2 pos, float radius) const override { return BlocksAny(pos, radius); }`.
   The `Simulation` is constructed `m_Sim.emplace(m_RunSeed, this)` (the `this->WorldCollision*`
   conversion is implicit). `Simulation` is move-deleted, so `optional::emplace` constructs it
   **in place** (no move) — fine. GameScene is a `Scene` (never moved), so the sim's raw pointers
   to its own members never dangle.

5. **Awake gating via `playerRoomId`.** Each frame compute the room the player is inside (the
   existing AABB test) and feed it as `WorldInputs.playerRoomId`; the sim wakes controllers whose
   `roomId` matches. Clear-room door-sealing (`m_LockedRoom`) now queries **sim liveness**
   (`EnemyViews()`/`BossView().alive`) instead of `m_Enemies[i]->IsDead()`.

6. **Identical seeds** so the wired floor matches the headless tests: enemy `m_RunSeed + 1000 +
   roomIndex`, boss `m_RunSeed + 9000`, weapon `m_RunSeed + 5` (then `+ swapCount` on each swap).

7. **Known carried-over gaps (documented, NOT fixed here):** the sim player weapon drops
   `crit`/`repel`/`pierce` (see the `weapon-crit-repel-pierce-gap` memory) — player bullets won't
   crit/knockback until that weapon-layer fix lands; boss movement is chase-only; SimEvent anim/
   sfx is unemitted; the 400ms-vs-2.0s energy-reload faithfulness nit stays as-is.

**Open decisions surfaced for review (my recommendation in each):**
- **A. Player movement timestep.** The spec keeps player movement scene-side at variable dt
  (recommended, simplest — the sim is deterministic w.r.t. its *inputs*, and the player pos is an
  input). Alternative: integrate the player at fixed step inside the sim for frame-rate-independent
  player motion. **Recommend: keep scene-side variable-dt** for this slice (matches spec §5).
- **B. Multi-shot energy edge.** If >1 player shot fires in one frame's N fixed steps with only
  enough energy for one, the extra bullet still spawns (the sim doesn't know about energy). At the
  real weapon cadence (~8 ticks) and ~1-2 steps/frame this is ~never hit. **Recommend: accept for
  the slice**, note it; tighten later if heat-minigun fire-rate makes it visible.
- **C. Player hp/armor pull granularity.** We pull `hp`+`armor` back from the sim but not energy
  (energy is scene-owned). **Recommend: pull hp+armor only** (as written).

---

## File structure

| File | Responsibility |
|---|---|
| `include/scenes/GameScene.hpp` (modify) | inherit `Sim::WorldCollision`; add `m_Sim`, `m_BulletViews`, weapon cost/swap-seed members, `Blocks`/`SyncBulletViews`/`RoomHasLiveHostile` decls; drop `m_Weapon`/`m_Rng`/`m_Bullets`/`TryFirePlayerWeapon`/`UpdateBullets` (across tasks). |
| `src/scenes/GameScene.cpp` (modify) | `Blocks`; spawn-into-sim in `OnEnter`; rewired `Update`; `SyncBulletViews`; `RoomHasLiveHostile`; delete old AI/bullet/weapon paths. |

No `files.cmake` change (GameScene already registered). No new test files (see the verification note).

---

## Task 1: GameScene becomes a WorldCollision + owns a (populated, not-yet-driven) Simulation

**Files:** Modify `include/scenes/GameScene.hpp`, `src/scenes/GameScene.cpp`.
**Verify:** `/W4`-clean build of `SoulKnight` + `SoulKnightTests`; full `ctest` green; the game
still plays the OLD way (the sim is populated but `Advance` is not yet called — inert).

- [ ] **Step 1: `include/scenes/GameScene.hpp` — add includes, base class, and members.**
  Add includes (with the other project includes):
```cpp
#include <cstdint>
#include <optional>
#include <unordered_map>

#include "sim/Simulation.hpp"
#include "sim/WorldInputs.hpp"
#include "sim/WorldCollision.hpp"
```
  Change the class header to also inherit `WorldCollision`:
```cpp
class GameScene : public Core::Scene, public Sim::WorldCollision {
```
  In the `public:` section add the override:
```cpp
    /// WorldCollision: a circle at @p pos / @p radius overlaps a wall or sealed door.
    bool Blocks(glm::vec2 pos, float radius) const override { return BlocksAny(pos, radius); }
```
  In the `private:` section add the new members (leave the existing ones for now — they are
  removed in Task 4):
```cpp
    /// The deterministic sim this scene drives (Plan 4a). Constructed in OnEnter with
    /// (m_RunSeed, this-as-WorldCollision). Move-deleted -> emplaced in place.
    std::optional<Sim::Simulation> m_Sim;
    /// Pooled Bullet VIEWS keyed by the sim BulletState id (render mirror only).
    std::unordered_map<std::uint32_t, std::shared_ptr<Bullet>> m_BulletViews;
    /// Energy spent per player shot (the equipped WeaponDef.consume).
    int m_WeaponEnergyCost = 1;
    /// Bumps each weapon swap so the rebuilt WeaponController gets a fresh deterministic seed.
    int m_WeaponSwaps = 0;
    /// Helper decls (defined in Task 2/3).
    void SyncBulletViews();
    bool RoomHasLiveHostile(int roomId) const;
```

- [ ] **Step 2: `src/scenes/GameScene.cpp` — populate the sim in `OnEnter`.**
  At the very top of `OnEnter` (right after `m_Rng.SetRandomSeed(m_RunSeed);`), construct the sim:
```cpp
    m_Sim.emplace(m_RunSeed, this); // headless sim driven from Update; `this` is the WorldCollision.
```
  In the room loop, immediately AFTER each entity is created, mirror it into the sim. Replace the
  enemy/boss spawn block (currently `if (roomIndex == bossRoom) { ...Boss... } else if (edef) {
  ...Enemy... }`) so each branch also calls the sim:
```cpp
            if (roomIndex == bossRoom) {
                auto boss = std::make_shared<Boss>(root, spawn, /*maxHp=*/500,
                                                   /*shootCd=*/2.0F, m_RunSeed + 9000);
                boss->SetRoomId(roomIndex);
                m_Bosses.push_back(boss);
                m_Sim->SetBoss(2.0F, spawn, /*maxHp=*/500, roomIndex, m_RunSeed + 9000);
            } else if (edef != nullptr) {
                auto enemy = std::make_shared<Enemy>(*edef, root, spawn, 450.0F, 260.0F);
                enemy->AI().SetSeed(m_RunSeed + 1000 + roomIndex);
                enemy->AI().SetKinematic(edef->kinematic != 0);
                enemy->SetRoomId(roomIndex);
                m_Enemies.push_back(enemy);
                m_Sim->AddEnemy(*edef, spawn, roomIndex, m_RunSeed + 1000 + roomIndex);
            }
```
  After the room loop, where the weapon is equipped, mirror it + record its cost:
```cpp
    if (const WeaponDef *wdef = m_Data.FindWeapon("Gun001")) {
        m_Weapon = std::make_unique<WeaponInstance>(*wdef);   // OLD path, removed in Task 2.
        m_Sim->EquipWeapon(*wdef, "Gun001", m_RunSeed + 5);
        m_WeaponEnergyCost = wdef->consume > 0 ? wdef->consume : 1;
    }
```
  (`m_Enemies` and the sim enemies are now 1:1 by index; the boss is 1:1 with the sim boss.)

- [ ] **Step 3: Build + test (no behaviour change yet).**
```bash
cmake --build build --config Debug --target SoulKnight SoulKnightTests 2>&1 | tail -15
ctest --test-dir build -C Debug
```
  Expected: `/W4`-clean (watch for: `m_Sim`/`m_BulletViews`/`m_WeaponEnergyCost`/`m_WeaponSwaps`
  are written but not yet read — MSVC `/W4` does not warn on unused private data members, so this
  is clean; if a *function* `SyncBulletViews`/`RoomHasLiveHostile` is declared-but-undefined and
  the linker complains, that's expected only if something references them — nothing does yet, and
  an undefined member function that is never odr-used does NOT cause a link error, so leave them
  declared). Full suite still 100% green (no sim-headless test is affected). The game still plays
  the OLD way.

- [ ] **Step 4: Commit**
```bash
git add include/scenes/GameScene.hpp src/scenes/GameScene.cpp
git commit -m "feat(game): GameScene implements WorldCollision + populates a Simulation (inert)"
```

---

## Task 2: Drive the Simulation from Update; sync enemy/boss/bullet views; remove old AI + fire paths

**Files:** Modify `src/scenes/GameScene.cpp` (and a small `GameScene.hpp` include if needed).
**Verify:** `/W4`-clean build; full `ctest` green; **`/run` playtest**: player moves + shoots,
bullets fly + despawn, enemies wake on room-entry + chase/shoot, the player + enemies take damage
and die, the boss fans. This is the atomic gameplay swap.

- [ ] **Step 1: Add the bullet-sync include** to `src/scenes/GameScene.cpp`:
```cpp
#include <unordered_set>
```

- [ ] **Step 2: Implement `SyncBulletViews` + `RoomHasLiveHostile`** (add these two functions to
  `src/scenes/GameScene.cpp`, e.g. just before `Update`):
```cpp
void GameScene::SyncBulletViews() {
    const std::vector<Sim::BulletState> &bullets = m_Sim->Bullets();
    std::unordered_set<std::uint32_t> present;
    present.reserve(bullets.size());
    for (const Sim::BulletState &b : bullets) {
        present.insert(b.id);
    }
    // Release views whose sim bullet is gone.
    for (auto it = m_BulletViews.begin(); it != m_BulletViews.end();) {
        if (present.find(it->first) == present.end()) {
            m_Renderer.RemoveChild(it->second);
            it->second->Deactivate();
            m_BulletPool.Release(it->second);
            it = m_BulletViews.erase(it);
        } else {
            ++it;
        }
    }
    // Acquire new views; mirror every live bullet's position.
    for (const Sim::BulletState &b : bullets) {
        auto found = m_BulletViews.find(b.id);
        std::shared_ptr<Bullet> view;
        if (found == m_BulletViews.end()) {
            view = m_BulletPool.Acquire();
            view->Init(b.pos, b.vel, kBulletLifeMs, b.damage, b.camp); // arms the sprite; not Update()d.
            m_Renderer.AddChild(view);
            m_BulletViews.emplace(b.id, view);
        } else {
            view = found->second;
        }
        view->m_Transform.translation = b.pos;
    }
}

bool GameScene::RoomHasLiveHostile(int roomId) const {
    for (const Sim::Simulation::EntityView &ev : m_Sim->EnemyViews()) {
        if (ev.alive && ev.roomId == roomId) {
            return true;
        }
    }
    if (m_Sim->HasBoss()) {
        const Sim::Simulation::EntityView bv = m_Sim->BossView();
        if (bv.alive && bv.roomId == roomId) {
            return true;
        }
    }
    return false;
}
```

- [ ] **Step 3: Replace the body of `GameScene::Update`** with the sim-driven version. This
  REMOVES the enemy AI loop, the boss AI loop, the `UpdateBullets(dtMs)` call, the `m_Weapon`
  update + `TryFirePlayerWeapon()` call, and the old per-entity death-reap loops; it KEEPS player
  movement, energy regen, chests, pickups (rewired to `EquipWeapon`), and camera. New body:
```cpp
void GameScene::Update(float dtMs) {
    if (Util::Input::IsKeyUp(Util::Keycode::ESCAPE) || Util::Input::IfExit()) {
        Core::Context::GetInstance()->SetExit(true);
        return;
    }

    // --- Player movement with axis-separated wall sliding (stays scene-side) ---
    const glm::vec2 before = m_Player->Position();
    m_Player->Update(dtMs);
    const glm::vec2 after = m_Player->Position();
    glm::vec2 resolved = before;
    if (!BlocksAny(glm::vec2(after.x, before.y), kPlayerRadius)) {
        resolved.x = after.x;
    }
    if (!BlocksAny(glm::vec2(resolved.x, after.y), kPlayerRadius)) {
        resolved.y = after.y;
    }
    m_Player->m_Transform.translation = resolved;

    // --- Energy regen (scene-owned) ---
    m_EnergyRegenAccumMs += dtMs;
    while (m_EnergyRegenAccumMs >= kEnergyRegenMs) {
        m_EnergyRegenAccumMs -= kEnergyRegenMs;
        m_Player->Stats().AddEnergy(1);
    }

    // --- Which room is the player in (awake gating + clear-room) ---
    int playerRoomId = -1;
    for (std::size_t i = 0; i < m_Rooms.size(); ++i) {
        const glm::vec2 c = m_Rooms[i]->Center();
        const glm::vec2 hs = m_Rooms[i]->Size() * 0.5F;
        if (resolved.x >= c.x - hs.x && resolved.x <= c.x + hs.x &&
            resolved.y >= c.y - hs.y && resolved.y <= c.y + hs.y) {
            playerRoomId = static_cast<int>(i);
            break; // player is in exactly one room
        }
    }

    // --- Drive the deterministic sim ---
    Sim::WorldInputs in;
    in.playerPos = resolved;
    in.aimDir = AimDirection();
    in.firing = Util::Input::IsKeyPressed(Util::Keycode::MOUSE_LB) &&
                m_Player->Stats().energy >= m_WeaponEnergyCost;
    in.playerAlive = !m_Player->Stats().IsDead();
    in.playerRoomId = playerRoomId;

    m_Sim->SetPlayerStats(m_Player->Stats());      // push current vitals in
    m_Sim->Advance(dtMs, in);                       // sim damages its player-stats copy
    const CombatStats &simPlayer = m_Sim->PlayerStats();
    m_Player->Stats().hp = simPlayer.hp;            // pull hp/armor back (energy stays scene-side)
    m_Player->Stats().armor = simPlayer.armor;
    for (int s = 0; s < m_Sim->PlayerShotsLastAdvance(); ++s) {
        m_Player->Stats().SpendEnergy(m_WeaponEnergyCost);
    }

    // --- Clear-room gating from sim liveness ---
    m_LockedRoom = (playerRoomId >= 0 && RoomHasLiveHostile(playerRoomId)) ? playerRoomId : -1;

    // --- Sync enemy/boss render views from the sim (entities are never erased: null on death) ---
    const std::vector<Sim::Simulation::EntityView> eviews = m_Sim->EnemyViews();
    for (const Sim::Simulation::EntityView &ev : eviews) {
        if (ev.id >= m_Enemies.size()) {
            continue;
        }
        std::shared_ptr<Enemy> &view = m_Enemies[ev.id];
        if (view == nullptr) {
            continue;
        }
        view->m_Transform.translation = ev.pos;
        if (!ev.alive) {
            m_Renderer.RemoveChild(view);
            view = nullptr;
        }
    }
    if (!m_Bosses.empty() && m_Bosses[0] != nullptr && m_Sim->HasBoss()) {
        const Sim::Simulation::EntityView bv = m_Sim->BossView();
        m_Bosses[0]->m_Transform.translation = bv.pos;
        if (!bv.alive) {
            m_Renderer.RemoveChild(m_Bosses[0]);
            m_Bosses[0] = nullptr;
            LOG_INFO("Boss defeated!");
        }
    }

    // --- Sync bullet views ---
    SyncBulletViews();

    // --- Player death ---
    if (m_Player->Stats().IsDead()) {
        LOG_INFO("Player defeated -- exiting");
        Core::Context::GetInstance()->SetExit(true);
        return;
    }

    // --- Chests: open on proximity, roll loot, drop a weapon pickup ---
    const glm::vec2 playerPos = resolved;
    for (auto &chest : m_Chests) {
        if (chest->Opened() ||
            glm::distance(playerPos, chest->Position()) > kChestOpenRange) {
            continue;
        }
        chest->Open();
        const std::string dropId = m_Loot.Roll(chest->Tier(), m_Rng);
        if (const WeaponDef *def = m_Data.ResolveDropWeapon(dropId)) {
            auto pickup = std::make_shared<WeaponPickup>(
                std::string(RESOURCE_DIR),
                chest->Position() + glm::vec2(0.0F, -kCellPx), def);
            m_Pickups.push_back(pickup);
            m_Renderer.AddChild(pickup);
        }
    }

    // --- Pickups: walk over one to equip it (rebuilds the sim WeaponController) ---
    for (auto it = m_Pickups.begin(); it != m_Pickups.end();) {
        if (glm::distance(playerPos, (*it)->Position()) <= kPickupRange) {
            const WeaponDef *def = (*it)->Def();
            ++m_WeaponSwaps;
            m_Sim->EquipWeapon(*def, def->id, m_RunSeed + 5 + m_WeaponSwaps);
            m_WeaponEnergyCost = def->consume > 0 ? def->consume : 1;
            m_Renderer.RemoveChild(*it);
            it = m_Pickups.erase(it);
        } else {
            ++it;
        }
    }

    // --- Camera follow ---
    m_Camera.Follow(m_Player->Position(), 0.1F);
    m_Camera.Update(dtMs);
}
```
  Notes: `m_Player->Stats()` returns a mutable `CombatStats&` (Player exposes a non-const `Stats()`).
  `CombatStats::energy`/`hp`/`armor` are public fields. `WeaponDef::id`/`consume` are real fields.
  The old `m_Weapon`/`m_Rng` are still referenced ONLY by the now-unused `TryFirePlayerWeapon`/
  `UpdateBullets` (deleted in Task 4) and by the loot roll (`m_Rng` — kept, it is the loot stream
  here) — leave them defined until Task 4.

- [ ] **Step 4: Build + test + playtest.**
```bash
cmake --build build --config Debug --target SoulKnight SoulKnightTests 2>&1 | tail -15
ctest --test-dir build -C Debug
```
  Expected: `/W4`-clean; full suite green. Then **playtest**: `/run` (or
  `cmake --build build --config Debug --target SoulKnight` then launch `build/Debug/SoulKnight.exe`).
  Confirm: WASD moves, mouse aims, LMB fires bullets that travel + vanish, entering an enemy room
  wakes it (it chases + shoots), bullets damage + kill enemies (they vanish) and the player (HUD hp
  drops), the boss room fans. If a behaviour is wrong, fix the sync/feed in this task (the sim
  logic itself is already test-proven — suspect the wiring).

- [ ] **Step 5: Commit**
```bash
git add src/scenes/GameScene.cpp
git commit -m "feat(game): GameScene drives Simulation -- view sync replaces entity AI + bullet paths"
```

---

## Task 3: Remove the dead OLD gameplay code

**Files:** Modify `include/scenes/GameScene.hpp`, `src/scenes/GameScene.cpp`.
**Verify:** `/W4`-clean build (now with `-Wunused`-style cleanliness for real, since the old
members/functions are gone); full `ctest` green; `/run` still works identically.

- [ ] **Step 1: Delete the now-unused functions** from `src/scenes/GameScene.cpp`:
  `TryFirePlayerWeapon()` and `UpdateBullets(float)` (their entire definitions). Keep
  `AimDirection()` (still used) and `BlocksAny()` (the WorldCollision impl).

- [ ] **Step 2: Delete the old bullet container + weapon instance + their state.**
  In `include/scenes/GameScene.hpp` remove: the `void TryFirePlayerWeapon();` and
  `void UpdateBullets(float dtMs);` decls; the `std::unique_ptr<WeaponInstance> m_Weapon;`
  member; the `std::vector<std::shared_ptr<Bullet>> m_Bullets;` member; and the now-unused
  include `#include "combat/WeaponInstance.hpp"` (only if nothing else needs it — `grep`
  `WeaponInstance` first). In `src/scenes/GameScene.cpp`, remove the two `m_Weapon` lines in
  `OnEnter` (the `m_Weapon = std::make_unique<WeaponInstance>(...)` in the equip block AND in the
  pickup branch — the latter is already gone after Task 2's rewrite; confirm). `m_Rng` STAYS (it
  is the loot-roll stream used by `m_Loot.Roll(chest->Tier(), m_Rng)`). The `m_BulletPool` STAYS
  (now feeds `m_BulletViews`).

- [ ] **Step 3: Drop now-dead anonymous-namespace constants** in `src/scenes/GameScene.cpp` that
  no code references anymore: `kBulletSpeedScale`, `kEnemyBulletSpeed`, `kBossFanSpreadDeg`,
  `kEnemyContactDamage` (these fed the deleted inline enemy/boss/player fire + damage). `grep`
  each before removing; KEEP `kPlayerRadius`, `kBulletLifeMs` (SyncBulletViews), `kEnergyRegenMs`,
  `kRepelScale` only-if-still-referenced (it is not, after UpdateBullets is gone -> remove),
  `kCellPx`/`kRoomCells`/`kRoomPitch` (room layout), `kChestOpenRange`/`kPickupRange`,
  `kBossBodyRadius` only-if-referenced (BlocksAny no longer special-cases it -> the old boss-move
  block used it; after removal it may be unused -> remove if so). The local `Normalize` STAYS
  (used by `AimDirection`). Remove any constant the compiler flags as unused under `/W4`
  (MSVC warns C4505/unused for `static`/anon-namespace items only for functions, not constexpr
  vars — but clang-tidy/IWYU would; remove dead ones regardless for cleanliness).

- [ ] **Step 4: Build + test + playtest.**
```bash
cmake --build build --config Debug --target SoulKnight SoulKnightTests 2>&1 | tail -15
ctest --test-dir build -C Debug
```
  Expected: `/W4`-clean with no dead-code warnings; full suite green; `/run` unchanged.

- [ ] **Step 5: Commit**
```bash
git commit -am "refactor(game): remove dead OLD gameplay path (WeaponInstance, UpdateBullets, fire consts)"
```

---

## Task 4: Final regression gate + playtest sign-off

- [ ] **Step 1:** `cmake --build build --config Debug --target SoulKnight SoulKnightTests` ->
  `/W4`-clean.
- [ ] **Step 2:** `ctest --test-dir build -C Debug` -> 100% pass (the headless sim suite is
  unchanged + still green; GameScene has no unit tests by design).
- [ ] **Step 3:** `gitnexus_detect_changes()` (or `git diff --stat main..HEAD -- src/scenes
  include/scenes`) -> confirm only `GameScene.{hpp,cpp}` changed in the shell, with the expected
  symbols.
- [ ] **Step 4: Playtest sign-off** via `/run`: a full loop — clear a room (enemies wake, chase,
  shoot, die; door unseals on clear), pick up a Gun016 (heat-minigun spread visibly grows while
  held), reach + defeat the boss (fan + angry phase at <50% hp), and confirm player death exits.
  Capture a screenshot or note any visual/behaviour gap.

---

## Self-review notes (author)

- **Spec coverage:** delivers the spec §3/§5 GameScene-as-shell: one `Simulation`, `WorldInputs`
  feed, `BulletState`/`EntityView` -> PTSD view sync, energy gating via `PlayerShotsLastAdvance`,
  weapon swap via `EquipWeapon`. The OLD entity-brain path is removed.
- **Determinism:** all gameplay RNG now lives in the sim (already golden-tested headless); the
  scene adds only render mirroring + the loot `m_Rng` (unchanged). The sim is fed an identical
  per-frame input snapshot held across its N fixed steps.
- **Risk / verification:** GameScene is not headless-testable, so the gate is build + full-suite +
  `/run` playtest. The view-sync index/id mappings (enemy index == sim id; bullet id == map key)
  are the main wiring risk; Task 2's playtest is the check.
- **No placeholders:** every step has the real code; the only judgment calls (movement timestep,
  multi-shot energy edge, hp/armor pull) are surfaced above with recommendations.

## Follow-on (after Plan 4b)

- Fix the **weapon crit/repel/pierce gap** (memory `weapon-crit-repel-pierce-gap`): carry
  `critical`/`repel`/`pierce` from `WeaponDef` through `WeaponController` -> `FireIntent` so player
  bullets crit + knock back again.
- Wire **SimEvent** anim/sfx emission (hit flashes, death, muzzle) once the event sink is produced.
- Boss **wander-blend** movement fidelity; **EnemyAI06** kinematic turret; the **energy-reload**
  400ms->2.0s faithfulness reconciliation.
- Optional: fixed-step player integration (decision A) for frame-rate-independent player motion.
