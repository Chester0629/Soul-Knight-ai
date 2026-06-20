# Minimal Playable Run Loop — Design Spec (Line 2 / main-loop #3a, Phase 1)

**Status:** Phase-1 minimal run loop **COMPLETE** (steps 1 + 2a + 2b IMPLEMENTED). Step 1 =
RunState/RunController + player-continuation LOAD + GameScene parameterization. Step 2a = the forward loop
**play -> clear -> next floor** (clear detection + snapshot capture + `Replace`). Step 2b = the failure
path **death -> EndScene** (minimal end screen: text + restart/quit; death is no longer an app quit;
restart = a fresh run from the template). All three in-game verified end-to-end; unit-tested; `/W4`-clean
for game-layer code. **Deferred (later UI line):** polished death/results/reborn UI + clear->portal trigger.
Route **(a) fresh-instance `Replace`**. See the **"Phase-1 step-1/2a/2b: IMPLEMENTED"** sections below.

**Inputs (from the structure inventory, [[soul-knight-floorloop-inventory]] / chat 2026-06-16):**
PTSD already provides `Core::Scene` + `Core::SceneManager` (stack `Push`/`Pop`/`Replace`/`Clear`
+ deferred-op queue, `PTSD/include/Core/SceneManager.hpp:28-52`, `.cpp:30-97`). The port pushes
exactly one `GameScene` (`src/main.cpp:41`) and has **no** floor progression, **no** reset, **no**
death/results flow, and **no** cross-floor persistent state (all run state is `GameScene` members
that die with the scene).

---

## Phase-1 step-1: IMPLEMENTED (RunState + continuation + GameScene parameterization)

Landed this step (build-clean `/W4` for game-layer code, 13 unit tests + the 1910 baseline green,
floor-0 in-game smoke verified). The remaining §6 sub-stages (floor-clear detection, transition
wiring, death->EndScene) are unbuilt.

**Files:** `include/game/RunState.hpp` (`PlayerContinuation`, `RunState`, `PerFloorSeed`),
`include/game/RunController.hpp` + `src/game/RunController.cpp` (owns `Core::SceneManager` + `RunState`;
`StartRun`/`OnFloorCleared`/`CurrentFloorSeed`/`Update`/`Render`), `include/data/GameData.hpp`
(`WeaponEnergyCost`), `include/scenes/GameScene.hpp` + `src/scenes/GameScene.cpp` (parameterized ctor +
OnEnter continuation LOAD), `src/main.cpp` (RunController wiring), `test/RunLoopTest.cpp` (13 tests).

**`perFloorSeed = runSeed + floorIndex * 1000003`** (a large prime, `RunState.hpp` `kFloorSeedStride`);
floor 0 -> exactly `runSeed`, so floor 0 stays bit-identical to the legacy single floor. It replaces the
hardcoded `20240607` at every OnEnter use site (the member is now `m_FloorSeed`).

**What this step did NOT do (per scope):** the snapshot CAPTURE at floor-clear and the `Replace`
transition (§3), floor-clear detection (§3a), death->`EndScene` (§5). `OnFloorCleared` currently does
ONLY the pure carry (`carried = snapshot; ++floorIndex`); it does not `Replace` yet. The OnEnter
continuation LOAD path is in place but only the floor-0 (`carried == nullopt`) branch is exercised
in-game so far; the `carried`-set branch is exercised by unit tests at the data layer and will be
in-game-verified when the transition is wired next step.

### Exhaustiveness clarifications folded from the inventory (its §4)

1. **Exactly 3 GameScene members depend on player state -- all 3 covered.** The exhaustive 22-member
   inventory found precisely `m_Player` (CARRY = its `CombatStats`), `m_WeaponEnergyCost` (DERIVE), and
   `m_Sim` (RESET; its player-dependent internals `m_PlayerStats`/`m_Weapon` flow through the CARRY +
   DERIVE). No member depends on player state while sitting outside `CombatStats`/the sim and unflagged
   -- exhaustive confirmation, no omission beyond the already-caught `m_WeaponEnergyCost`. D3 is
   exhaustive, not merely a list.

2. **`m_WeaponEnergyCost` is a definitively scene-owned DERIVE.** `WeaponDef.consume` is read ONLY by the
   scene (now via `Game::WeaponEnergyCost`, `GameData.hpp`); the live sim weapon path
   (`Sim::WeaponController`) NEVER reads it (no energy field). The only other reader is the scene-dead
   `WeaponInstance` (`src/combat/WeaponInstance.cpp:46,66`, used by its own test only -- never by
   GameScene/Simulation). So a rebuilt `m_Sim->m_Weapon` cannot recover the cost; a carried weapon MUST
   be re-equipped and the cost re-derived via `WeaponEnergyCost()` (done in OnEnter + the pickup path).

3. **`m_WeaponSwaps` = RESET-to-0, and the premise is now a TEST, not a comment.** Resetting it to 0 on a
   fresh floor is correct ONLY because each floor's base seed differs. `PerFloorSeed` guarantees that and
   `RunLoopTest.cpp` pins it: `DiffersPerFloor` + `DerivedSeedWindowsDoNotOverlap` (the per-floor derived
   windows `base+5+swaps ... base+9000` are spaced by the 1000003 stride, so they never collide across
   floors). The latent coupling is now a failing-if-broken guard, not a footnote.

4. **`m_CurrentWeaponId` is THE weapon-id carry vehicle.** Added as a real GameScene member, set on EVERY
   equip (initial OnEnter equip + pickup equip), replacing the id being implicit in the `EquipWeapon`
   calls. The floor-clear snapshot `{ m_Player->Stats(), m_CurrentWeaponId }` (capture wired next step)
   reads it.

### The two conditional seams, pinned as explicit decisions (not "acceptable residual" wording)

- **Speed trio / regen accumulators -- decision (ii): full-struct `CombatStats` copy, documented.**
  `PlayerContinuation.stats` copies the WHOLE struct (matches D3; future-proofs `maxHp` for hp-up
  pickups; more faithful -- the original persists `RoleAttributePlayer` across floors rather than
  reconstructing it). The dragged speed trio is **inert today**: movement reads the template-derived
  `Player::m_Speed` (`src/entities/Player.cpp:26,52`), never `CombatStats.speed`; `ChangeSpeed`/`SpeedBack`
  have no non-test callers. **If `Stats().speed` ever drives movement, revisit** (likely reset the trio on
  continuation). Recorded at `RunState.hpp` `PlayerContinuation` doc -- a dormant seam that is documented,
  not silent. (Chosen over copying only `{hp,armor,energy}` because that would drop the maxes and diverge
  from the ratified D3 for no real benefit.)

- **`floorIndex` -> RoomGen: threaded but HONESTLY inert.** `GameScene` now sets
  `ropt.floorIndex = m_FloorIndex`, but this is **inert under `randomRoom=false`** -- RoomGen reads
  `floorIndex` only inside its `randomRoom==true` branch (`RoomGen.cpp:53-69`), and the scene builds with
  `randomRoom=false`. The ONLY live, in-game-observable `floorIndex` effect this step is via `m_FloorSeed`
  (different floors generate different layouts -- unit-proven at the seed->geometry layer by
  `RunLoopFloorGen.DifferentFloorIndexProducesDifferentLayout`). The `%5` size-narrowing under
  `randomRoom=false` is deferred to Phase 2 (§4b). The call site documents the thread as inert -- no fake
  "it's wired" signal (the same anti-pattern §4b warns about with `isBossFloor`).

---

## Phase-1 step-2a: IMPLEMENTED (forward run loop: play -> clear -> next floor)

Landed this step (build-clean `/W4` for game-layer code; 1930 tests green incl. 7 new clear-predicate
tests; **in-game verified end-to-end**). Three pieces wired:

**1. Whole-floor clear detection** -- `Game::FloorCleared(sim)` / `AllHostilesDead(views, hasBoss, boss)`
(`include/game/FloorClear.hpp`), a pure predicate over the sim's **AUTHORITATIVE** views
(`EnemyViews()`/`HasBoss()`/`BossView()`), NOT the render mirrors (`m_Enemies` are nulled a frame later).
**Boss현황 confirmed by reading `GameScene` OnEnter:** a boss spawns on EVERY floor (the farthest room,
unconditional + `m_Sim->SetBoss`), so the clear condition is **all enemies dead AND boss dead**. Armed
only after `m_HadHostiles` (>= 1 hostile existed), so a degenerate floor can't auto-skip. Unit-tested
with fabricated views (live boss / live enemy / all-dead / no-boss).

**2. Snapshot capture -- the timing crux (capture-before-destroy).** At the **END** of `GameScene::Update`
(after the sim hp/armor pull-back AND the pickup->equip block, so it reflects this frame's FINAL state),
on clear the scene builds `PlayerContinuation{ m_Player->Stats(), m_CurrentWeaponId }` **while m_Player is
alive** and hands it to `RunController::OnFloorCleared`. That calls `AdvanceFloor` (pure: copies the
snapshot into `RunState`, `++floorIndex`) and THEN `m_Scenes.Replace(...)`. The `Replace` is **deferred**
by the SceneManager (we are inside its `Update` via the signalling scene), so the old scene + its
`m_Player` are torn down only after `Update` returns -- by then the snapshot is safely in `RunState`. A
`m_Transitioning` guard fires the signal exactly once. (`OnFloorCleared` was split from the step-1 pure
`AdvanceFloor` precisely so the carry stays unit-testable while the GL-bound `Replace` is in-game-verified.)

**3. `Replace` transition.** `RunController::OnFloorCleared` requests `SceneManager::Replace(BuildFloorScene())`
where `BuildFloorScene()` = `make_shared<GameScene>(CurrentFloorSeed(), floorIndex, carried, this)` -- the
new floor carries the `PlayerContinuation` (step-1 LOAD path) and a `RunController*` back-pointer. Fresh
instance, OnEnter once, empty containers (route (a) -- append-only OnEnter untouched, no reset).

### Design decision 1: clear -> AUTO-transition (option (i)), portal deferred
On clear, the scene transitions **immediately** (capture + `Replace`), the minimal-and-fastest way to
prove the forward loop. The faithful **portal/trigger-point** (clear -> spawn a portal -> player walks onto
it -> advance, option (ii)) is **deferred** to polish -- it needs a portal entity + sprite + player-enter
detection, an extra dependency not needed to prove "can reach floor 2 with state continued."

### In-game verification (the real acceptance for this step)
Driven headlessly via the `SK_FORCE_CLEAR=K` test hook (env-gated, NO-OP in normal play -- mirrors
`SK_MAX_FRAMES`/`SK_SHOT`): forces the clear path on frame K of each floor (bleeding 1 hp as a marker, so
cross-floor hp-continuation is observable without combat/input). Observed log:
```
Floor 0 fresh start (template) weapon='Gun001' (seed=20240607)
Floor 0 CLEARED -> snapshot hp=5 armor=3 energy=130 weapon='Gun001' (next floor 1)
Floor 1 continuation LOADED: hp=5 armor=3 energy=130 weapon='Gun001' (seed=21240610)
Floor 1 CLEARED -> snapshot hp=4 ...                Floor 2 LOADED hp=4 (seed=22240613)
... hp 5->5, 4->4, 3->3, 2->2 (each floor loads the PREVIOUS floor's snapshot hp, never reset to 6)
```
This proves end-to-end: clear -> transition -> floor N+1 is a **different floor** (seed `+1000003`/floor)
with **hp + full `CombatStats` continued** (not refilled) and the weapon id carried. (Without
`SK_FORCE_CLEAR` the floor does NOT auto-transition -- boss+enemies alive -> not cleared -- confirming the
hook is inert in normal play.) **In-game caveat (honest):** a real WEAPON SWAP across floors needs input
(walk onto a pickup), so headless shows `Gun001->Gun001` (the id mechanism, captured+loaded); swap-value
continuation is covered by the step-1 unit tests (carry + DERIVE) and the OnEnter LOAD path, and is
interactively verifiable.

### Out of scope (kept faithful to the step boundary)
- **Death side is step 2b** (now done -- see below). In step 2a death stayed `Context::SetExit(true)`; the
  death check early-returns BEFORE the clear check, so a dead player never triggers a floor transition.
- Portal/results/reborn UI -- deferred (decision 1 (i)).

---

## Phase-1 step-2b: IMPLEMENTED (failure path: death -> EndScene)

Landed this step (build-clean `/W4` for game-layer code; 1932 tests green incl. 2 new; **in-game verified
end-to-end**). Death is no longer an app quit: it enters a minimal, restartable end screen.

**1. `EndScene`** (`include/scenes/EndScene.hpp` + `src/scenes/EndScene.cpp`) -- a greenfield `Core::Scene`:
screen-space text ("YOU DIED" + "R: new run     Esc: quit") drawn via a `Util::Renderer` (lazy-built on
first `Render`, like the HUD, since `Util::Text` needs GL), and input: **R** -> `run->StartRun()`,
**Esc/Q** (or window close) -> `Context::SetExit(true)`. Owns no combat state. Holds a `RunController*`.

**2. Death -> `RunController::OnPlayerDied()`** -- `GameScene`'s death check now signals the controller
instead of `SetExit`: `OnPlayerDied` sets `phase = Ended` and `m_Scenes.Replace(make_shared<EndScene>(this))`.
Requested from `GameScene::Update` (deferred-op safe, same timing as a clear); the death check shares the
`m_Transitioning` guard so it signals once, and still early-returns before the clear check.

**3. restart = `RunController::StartRun()` -> a FRESH run.** `StartRun` calls the new pure
`ResetRunState()` (floor 0, **continuation snapshot CLEARED**, `phase = Playing`, `runSeed` kept) and then,
because the stack is non-empty (the EndScene), **`Replace`s** (never `Push`es) it with a fresh floor-0
`GameScene`. So a restart starts a brand-new run from the character template -- NOT a continuation of the
dead floor. (Unit-tested: `ResetRunStateIsAFreshRunFromTemplate` -- after playing into floor 2 with a
carried snapshot, `ResetRunState` returns floor 0 + carried cleared.)

### The two crux constraints, verified
- **restart `Replace` not `Push`** -- proven in-game by the `OnExit` logs: on restart, `EndScene: OnExit`
  fires (the EndScene is replaced, never left at the stack bottom); on death, `Floor N GameScene OnExit`
  fires (the dead floor is torn down). The death->EndScene->restart loop runs indefinitely with no scene
  accumulation/leak.
- **restart = fresh run** -- after restart the log shows `Floor 0 fresh start (template) hp=6` (FULL
  template hp, base seed), not the dead floor's `hp=0` nor a continuation.

### In-game verification
Headless via `SK_FORCE_DIE=K` (kills the player on frame K) + `SK_ENDSCENE=restart|quit` (EndScene
auto-picks the action after ~30 frames) -- both env-gated NO-OPs in normal play. Observed:
```
A (restart): Floor 0 fresh start hp=6 -> Player died -> EndScene -> Floor 0 GameScene OnExit
             -> EndScene: restart -> EndScene: OnExit -> Floor 0 fresh start hp=6 -> ... (clean loop)
B (quit):    Player died -> EndScene -> EndScene: quit -> exit 0 (exits via EndScene, not the frame cap)
C (no act):  Player died -> EndScene -> (persists; death did NOT quit the app) -> exit 0
```
Normal play (no hooks) is unchanged: floor 0 runs, no death, no EndScene, exit 0.

### Out of scope (kept to the step boundary)
- **Reborn (TryAgain) mechanic** -- NOT here (this step has only restart = new run / quit).
- **Results/Statements screen** (score/crystals/coins) -- the economy system does not exist yet; deferred.
- Polished death/results/reborn UI -- the separate later UI line; `EndScene` is the minimal placeholder.

---

## 0. The real problem this phase solves (read first)

Route (a) makes reset trivial — a *fresh* `GameScene` starts with empty containers, so the
append-only `OnEnter` (`src/scenes/GameScene.cpp:131,149,195,203,215`, zero `.clear()`) is no
longer a hazard. **But (a) trades reset-correctness for cross-floor persistence:** every floor is a
new `GameScene` whose `m_Player` is rebuilt from `PlayerTemplate`
(`GameScene.cpp:78` → `Player::Player` sets `m_Stats = CombatStats::FromCharacter(def)`,
`src/entities/Player.cpp:25`). The inventory's §5 confirmed **no** run-level state container exists.
So without new machinery, **every floor resets hp/armor/energy and drops the equipped weapon**
("floor 2: full health, gun gone").

> **Therefore the star of Phase 1 is NOT the `Replace` wiring (that's off-the-shelf). It is a
> run-level state container that outlives any single `GameScene` and the player-continuation
> mechanism that carries hp + equipped weapon from the dying floor into the new floor's Player.**
> This is the port's first analog of the original `RGGameProcess` (floor counter + transitions)
> + `PlayerSaveData` (carried player state), which today exist only as decomp annotations
> (`include/world/RGBox.hpp:18`, `src/combat/BossAI06.cpp:18`), never modelled.

---

## Decisions ratified in this spec (rationale in the sections below)

| # | Decision | Choice | Why (short) |
|---|---|---|---|
| D1 | Where run state lives | **`main`-owned `Game::RunController` that OWNS `RunState` + the `SceneManager`**, not a singleton, not `Core::Context` | explicit ownership + unit-testable; the controller must outlive the scenes holding its back-pointer, so it owns the stack (§1 lifetime); Context is engine-layer (window+exit only) |
| D2 | Player continuation path | **(i)** GameScene is parameterized by an optional continuation and applies it at construction | localized branch at the natural construct point; new scene fully-formed before frame 1; controller never reaches into GameScene internals |
| D3 | What continues across floors | **hp + armor + energy (full `CombatStats`) + equipped weapon id** | faithful (SK keeps hp across floors); minimal mechanism (struct copy + one id) |
| D4 | Floor transition | **`SceneManager::Replace(new GameScene(...))`**, requested from GameScene's `Update` (deferred-op safe) | engine already provides it; no custom transition framework |
| D5 | Floor-clear detection | **aggregate over `Sim::EnemyViews()` + `HasBoss()`/`BossView()`** (all hostiles dead) | data already exists; per-room gating stays for doors |
| D6 | floorIndex trap | **`floorIndex`→seed is live (floors differ); `ropt.floorIndex` threaded but inert under `randomRoom=false`; boss stays unconditional (NO dead `isBossFloor` flag); boss-floor `%5` selection deferred to Phase 2** | RoomGen's `%5` is dead under the port's room-build path; do not fake a live boss cycle (see §4b) |
| D7 | Death → end | **minimal placeholder `EndScene`** (text + restart/quit), polished UI deferred | "death ≠ quit" is in-scope; results/reborn UI is the separate UI line |

---

## 1. `RunState` / `RunController` — the Phase-1 star

Two objects, split so the **state is pure and unit-testable** and the **orchestration owns the
engine seams**:

### `Game::RunState` — pure data (no engine deps; the `PlayerSaveData` + `this_index` analog)
```
struct PlayerContinuation {            // what survives a floor transition
    CombatStats stats;                 // hp/armor/energy (+max, regen accumulators) — full copy
    std::string weaponId;              // equipped WeaponDef id (re-equipped on the next floor)
};
struct RunState {
    int   runSeed   = 20240607;        // base seed for the whole run (per-floor seed derived from it)
    int   floorIndex = 0;              // 0-based; the port analog of RGGameProcess this_index (+0x14)
    enum class Phase { Playing, Ended } phase = Phase::Playing;
    std::optional<PlayerContinuation> carried;  // empty on floor 0 (use template); set after floor 0
};
```
- **Faithful note (D3):** Soul Knight keeps hp **across** floors (no per-floor refill); armor/energy
  regenerate over time and persist. Copying the whole `CombatStats` carries hp/armor/energy and the
  (harmless) regen accumulators; `maxHp/maxArmor/maxEnergy` are character-constant in the MVP (no
  hp-up pickups yet), so a full-struct copy is correct. **Trade-off if you want a simpler MVP:**
  refill hp each floor — *rejected here* because continuation costs the same (one struct copy) and
  faithful is better. (If hp-up/maxHp-raising pickups land later, continuation already carries the
  raised max correctly — see §future.) The full-struct copy also carries `CombatStats`'s speed trio
  (`speed`/`speedRate`/`speedChangeValue`, `CombatStats.hpp:40-46`) and the regen accumulators —
  harmless in the MVP (no buff/debuff is wired to call `ChangeSpeed`/`SpeedBack`, and player movement
  uses `Player::m_Speed` from the template, `Player.cpp:26,52`, not `CombatStats.speed`). If timed
  speed buffs/debuffs get wired later, revisit whether a mid-debuff snapshot should carry the
  half-applied delta (likely reset the trio on continuation); flagged, not designed.
- **Economy deferred:** score / crystals / coins — the inventory confirmed the port has **zero**
  tracking of these. `RunState` intentionally omits them for the minimal loop (add later; greenfield).

### `Game::RunController` — orchestration (the `RGGameProcess` analog)
**Owns** a `RunState` **and** the `Core::SceneManager` (by value — see the lifetime note below).
Responsibilities:
- `StartRun()` → reset `RunState` (floorIndex 0, carried empty, Phase::Playing) → put floor 0 on the
  stack with `m_scenes.Empty() ? Push : Replace` (so the SAME entry handles cold start *and* restart
  from the `EndScene` — see §5; a bare `Push` from `EndScene` would stack a floor *on top* of the
  paused `EndScene` and leak it).
- `OnFloorCleared(const PlayerContinuation& snapshot)` → store `snapshot` into `RunState.carried`,
  `floorIndex++`, build + `Replace` the next `GameScene`.
- `OnPlayerDied()` → `RunState.phase = Ended` → `Replace` with the minimal `EndScene` (§5).
- `Update(dtMs)` / `Render()` → forward to the owned `m_scenes` (the main loop calls these).
- `BuildFloorScene()` → `make_shared<GameScene>(perFloorSeed(), floorIndex, carried, this)`.

### Lifecycle / ownership (D1)
`main.cpp` owns **only** the `RunController`; the controller owns the `SceneManager` (which owns the
`GameScene`s):
```
Game::RunController run;              // owns RunState + (declared-last, so destroyed-first) SceneManager
run.StartRun();                       // replaces the old `scenes.Push(make_shared<GameScene>())`
while (!context->GetExit()) { context->Setup(); run.Update(dtMs); run.Render(); ImGui::Render(); ... }
```
**Why the controller OWNS the SceneManager (not a `main` local + a back-reference):** every
`GameScene`/`EndScene` holds a `RunController*` back-pointer (to signal clear/death). The referent
**must outlive every scene that points at it.** If `main` owned both as locals, reverse-declaration
destruction would kill whichever was declared last first — and a `RunController` that holds a
`SceneManager&` *must* be declared after it (it needs the reference at construction), so the controller
would die **before** the scenes that point at it → dangling `run*` at teardown (latent today since
`GameScene` has no `OnExit`/dtor that derefs `run`, but a trap the design must not leave). Making the
controller own the `SceneManager` as a member declared **last** guarantees the stack (and its scenes)
is torn down *during* `~RunController`, while the controller object still exists → the back-pointer is
valid throughout teardown. **Invariant: `RunController` outlives every scene holding its pointer.**
**Why a controller, not a singleton, not `Context`:** explicit ownership, no hidden global, `RunState`
stays directly unit-testable; `Core::Context` is engine-layer (window+exit only,
`PTSD/include/Core/Context.hpp`). The original uses `Singleton<RGGameProcess>`; the port prefers
explicit ownership (the port already avoids new singletons).

### Responsibility boundary (who owns what)
- **RunController owns run-level decisions:** floor counter, when to transition, what carries over,
  run-ended handling. (Boss-floor *selection* is a Phase-2 rule — §4b; Phase 1 does not decide it.)
- **GameScene is demoted to "one floor in a run":** it is *constructed by* the controller with its
  floor params + continuation, *reads* continuation at build, *detects* its own floor-clear/death and
  *signals* the controller (`run->OnFloorCleared(snapshot)` / `run->OnPlayerDied()`). It does **not**
  own floorIndex, the seed policy, or the transition. GameScene holds a `RunController*` (passed in
  ctor) purely to signal those two events + hand over its player snapshot.

```
 main ── owns ──► RunController ── owns ──► RunState (floorIndex, carried, phase)
                       │  owns ──► SceneManager ── owns ──► GameScene(seed, floorIndex, carried, run*)
                       ▲                                         │
                       └──── signals OnFloorCleared/OnPlayerDied ┘ (run* back-pointer; controller outlives it)
```

---

## 2. ★ Player continuation mechanism (the lifeline of Route (a))

Under (a) the continued thing is **state data**, not the `Player` object (each floor `make_shared`s a
new `Player`). The carried `PlayerContinuation` is loaded into the new floor's Player.

**Candidate (i) — GameScene loads continuation at construction (CHOSEN).** GameScene ctor takes
`std::optional<PlayerContinuation> carried`. In `OnEnter`, *after* the existing
`m_Player = make_shared<Player>(template, root)` (`GameScene.cpp:78`):
```
if (carried) m_Player->Stats() = carried->stats;            // overwrite template vitals (Player.hpp:33 mutable ref)
const std::string equipWeaponId = carried ? carried->weaponId : "Gun001"; // floor 0 -> default (GameScene.cpp:221)
// MIRROR the live equip EXACTLY (GameScene.cpp:221-224 / :456-457): keep the null-guard AND
// re-derive m_WeaponEnergyCost. "set m_CurrentWeaponId AND m_WeaponEnergyCost on every equip" is ONE
// invariant -- the firing gate + energy spend read m_WeaponEnergyCost (GameScene.cpp:362-363,376), which
// defaults to 1, so a carried weapon whose consume != 1 would mis-cost the whole floor if this is skipped.
if (const WeaponDef *w = m_Data.FindWeapon(equipWeaponId)) {
    m_Sim->EquipWeapon(*w, equipWeaponId, perFloorSeed + 5);
    m_WeaponEnergyCost = w->consume > 0 ? w->consume : 1;
    m_CurrentWeaponId  = equipWeaponId;
}   // else: unknown id -> leave default Gun001 (never deref a null FindWeapon, as the live code's guard implies)
```
**Candidate (ii) — RunController injects after construction (REJECTED).** Controller builds GameScene
(template + default Gun001), then reaches into the built scene to overwrite `m_Player->Stats()` and
re-equip `m_Sim`.

**Why (i):** (a) the "template vs continuation" branch lives in **one place at the natural
construction point** (where Player + weapon are already created in `OnEnter`), not as an external
overwrite; (b) the new scene is **fully-formed before its first frame** — (ii) has a transient window
where the default Gun001 is equipped then swapped, and the controller would have to reach into
`GameScene::m_Player` and `GameScene::m_Sim` (tight coupling to internals). (i) keeps the controller
decoupled: it only passes data *in*.

**Mechanical grounding (no change to `Player.hpp` needed):** Player exposes a mutable
`CombatStats &Stats()` (`include/entities/Player.hpp:33`), so `m_Player->Stats() = carried->stats;`
is a direct struct assignment. The equipped weapon is rebuilt via the existing
`Sim::EquipWeapon(const WeaponDef&, weaponId, seed)` (`include/sim/Simulation.hpp:64`), looking the
def up by id with `m_Data.FindWeapon(id)` (already used at `GameScene.cpp:221`; pickups use
`def->id` as the weaponId at `GameScene.cpp:456`, so ids round-trip).

**The first-floor branch:** `carried == nullopt` ⇒ template + default weapon (run start);
`carried` set ⇒ continuation. The controller guarantees this: floor 0 is built with `nullopt`; every
later floor is built with `RunState.carried` populated by the previous floor's snapshot.

**Snapshot capture (the other half):** at floor-clear, GameScene produces a `PlayerContinuation`:
`{ m_Player->Stats(), m_CurrentWeaponId }`. **New GameScene member (ADDED in step 1):** GameScene now
remembers the currently-equipped weapon id via `m_CurrentWeaponId`, set on EVERY equip (the initial
OnEnter equip + the pickup equip), replacing the id being implicit in the `EquipWeapon` calls. (Player
vitals to snapshot = `m_Player->Stats()`, which is authoritative: GameScene syncs hp/armor from the sim
and owns energy each frame.) The capture itself + the `FloorCleared` signal fire **next step**.

> **Ordering contract (load-bearing for D3):** the `FloorCleared` signal — and thus the snapshot —
> must fire at the **END** of `GameScene::Update`, *after* both the hp/armor pull-back
> (`GameScene.cpp:367-377`) **and** the chest/pickup→equip block (`GameScene.cpp:452-463`, which is the
> only place that can change the equipped weapon + `m_CurrentWeaponId` mid-frame, and runs *after* the
> death early-return at `:427`). Then the snapshot reflects this frame's *final* hp/armor/energy and the
> *final* equipped weapon. (The §3b pseudocode's `... existing ...` denotes the whole existing body, so
> the appended check is at the bottom — this contract makes that explicit, not incidental.)

> **What must NOT be carried:** the `Sim::Simulation` (move/copy-deleted, "must never relocate after
> Activate", `Simulation.hpp:53-56`) is floor-local and rebuilt per GameScene — continuation carries
> only the player *snapshot* (stats + weapon id), never sim objects, enemies, rooms, or bullets.

---

## 3. Floor transition — trigger + flow (riding the existing SceneManager)

### 3a. Whole-floor clear detection (D5) — new, but data already exists
The inventory found only **per-room** liveness (`RoomHasLiveHostile(playerRoomId)`,
`GameScene.cpp:305-318`, drives door sealing at `:383-384`). Phase 1 adds a **floor-wide** predicate
over the sim views the scene already polls:
```
bool FloorCleared(const Sim::Simulation& sim):
    for each v in sim.EnemyViews(): if (v.alive) return false   // EntityView.alive, .roomId unused here
    if (sim.HasBoss() && sim.BossView().alive) return false
    return true
```
`EnemyViews()` / `HasBoss()` / `BossView()` are public (`Simulation.hpp:74-76`); `EntityView.alive`
exists (`:43`). This is a **pure predicate over the views** ⇒ unit-testable with a fake view list.

> **Precondition (state it, don't assume):** "no live hostile" must not be confused with "no hostile
> ever existed." A floor with zero spawned hostiles would make `FloorCleared` true on frame 0 and
> (under the immediate-advance fallback) auto-skip the floor. Today this can't happen — `mapLong=7`
> (`GameScene.cpp:85`) and `MapManager` always places that many rooms (`MapManager.cpp:30,35`;
> `MapManagerTest` asserts the walk reaches `mapLong`), so with ≥2 rooms the farthest room always gets
> a boss (`GameScene.cpp:190-196`). The chosen player-initiated-exit trigger also gates advance on
> `playerAtExit`, so even a hostile-less floor would not auto-skip. Still, **arm `FloorCleared` only
> after ≥1 hostile has existed on the floor** (e.g. a `hadHostiles` flag set in `OnEnter` from
> `m_Enemies`/`m_Bosses` non-empty), so the loop can never silently skip a degenerate floor.

**What counts as "this floor is done":** MVP = **all hostiles dead**. Soul Knight is *clear + walk
into the portal*; for the minimal loop, the recommended trigger is a **player-initiated exit**: on
`FloorCleared`, enable a simple exit trigger (proximity zone, mirroring the existing chest/pickup
proximity at `kChestOpenRange`/`kPickupRange`, `GameScene.cpp:33-34,435-463`) placed in the start or
boss room; touching it advances. This avoids a jarring instant scene-swap mid-combat-feel and reuses
the proximity pattern. **Absolute-minimum fallback:** advance immediately on `FloorCleared`. **Lean:
portal-proximity** (player-initiated, ~one placeholder sprite + a proximity check). The faithful
reward-room/portal logic (salvageable `RGRoomX::ClearRoom` reward gate, `src/world/RGRoomX.cpp:40-50`,
currently test-only) is **deferred**.

### 3b. Transition flow (D4) — deferred-op safe
```
GameScene::Update(dt):
    ... existing ...
    if (m_Phase==Playing && FloorCleared(*m_Sim) && playerAtExit):
        m_Phase = Transitioning                      // guard so we signal once
        run->OnFloorCleared({ m_Player->Stats(), m_CurrentWeaponId })

RunController::OnFloorCleared(snapshot):
    m_State.carried = snapshot
    m_State.floorIndex += 1
    m_Scenes.Replace(BuildFloorScene())              // deferred by SceneManager until Update returns
```
`SceneManager::Replace` is **safe to call from inside a scene's `Update`**: the manager queues the op
(`m_Updating` guard) and applies it after `Update` returns (`PTSD/src/Core/SceneManager.cpp:46-53,
65-97`). So GameScene requests its own replacement without the stack being mutated mid-walk. **No
custom transition framework is built** — this is exactly the deferred-op queue's purpose. The new
`GameScene` then loads `RunState.carried` via the §2 (i) path on its `OnEnter`.

---

## 4. GameScene parameterization + the floorIndex trap (D6)

### 4a. Parameterize the scene
Today: `GameScene()` default ctor (`GameScene.cpp:64`); `m_RunSeed` hardcoded `20240607`
(`GameScene.hpp:94`) → every instance generates the **same** floor. New ctor:
```
GameScene(int perFloorSeed, int floorIndex,
          std::optional<PlayerContinuation> carried, RunController* run);
```
(No `isBossFloor` param in Phase 1 — see §4b: the boss already spawns unconditionally, so a flag would
be dead plumbing.)
- `perFloorSeed = RunController::perFloorSeed()` = `f(RunState.runSeed, floorIndex)`
  (e.g. `runSeed + floorIndex * <large prime>`) so each floor differs. `OnEnter` feeds it to
  `MapManager(perFloorSeed, mapOpt)` (the seed-consuming ctor is `GameScene.cpp:87`; `:84` is just the
  `Options` decl) and `RoomGen(perFloorSeed + 1 + roomIndex, …)` (`:129`). ⇒ different `floorIndex` ⇒
  different layout + rooms (in-game verifiable). **Implementer note:** `perFloorSeed` must replace the
  hardcoded `m_RunSeed` at **every** OnEnter use site, not only the two cited — grep gives
  `GameScene.cpp:75,76,87,129,193,196,200,204,222,456` (RNG seed, sim seed, MapManager, RoomGen,
  boss/enemy seeds, weapon-equip seeds).
- `runSeed` itself can stay a fixed default for now (deterministic runs); per-run randomization is a
  defer/nicety.
- `floorIndex` is still passed in (the scene threads it into `ropt.floorIndex` for fidelity, §4b);
  the seed difference (not `floorIndex` directly) is what makes floors differ.

### 4b. ★ The floorIndex half-wiring trap — MUST be handled, not assumed
The inventory found `RoomGen::Options.floorIndex` (`include/world/RoomGen.hpp:70`) consumed at
`src/world/RoomGen.cpp:57` (`if (options.floorIndex % 5 == 1)` narrows the *width* roll). **It is
doubly dead today:**
1. GameScene never sets `ropt.floorIndex` (`GameScene.cpp:123-128` omit it ⇒ default `0`); **and**
2. the `%5` branch lives **inside the `if (options.randomRoom)` block** (`RoomGen.cpp:53-69`), but
   GameScene builds rooms with `ropt.randomRoom = false` (`GameScene.cpp:124`, deliberately — fixed
   size so rooms tile edge-to-edge and door gaps align). Under `randomRoom=false` RoomGen takes the
   `else` branch (`RoomGen.cpp:70-75`) and **never reads `floorIndex` at all**.

**Two different `%5`s — do not conflate:** RoomGen's `%5` narrows *room size* on certain floors; the
original `RGGameProcess.NextScene` `%5` *selects which floor is a boss/special floor*
(`docs/UI_BEHAVIOR_SPEC.md:413-468`). The boss-cycle the run loop cares about is the **latter**.

**Phase-1 reconciliation (wire `floorIndex` correctly + identify the boss-cycle hook; do NOT fake a
live cycle).** The honest split — Phase 1 must not reintroduce the very "looks-live-but-isn't" trap
this section warns about (do not add a flag that the boss spawn then ignores):
- **`floorIndex` → seed is LIVE** (this is the part that actually does something in Phase 1): it feeds
  `perFloorSeed`, so different floors generate differently (§4a). This is the only `floorIndex` effect
  observable in-game in Phase 1.
- **Thread `ropt.floorIndex = floorIndex` for fidelity, but it is INERT under `randomRoom=false`** —
  document it as such. It would only matter if a room-build path used `randomRoom=true` (the
  `%5`-size-narrowing branch). The port must **not** depend on RoomGen's internal `%5` for boss logic.
- **The boss cycle stays as-is in Phase 1: the boss spawns UNCONDITIONALLY in the farthest room
  (`GameScene.cpp:190-196`).** Phase 1 does **not** add an `isBossFloor` flag — that would be dead
  plumbing (the spawn ignores it) and a false "it's wired" signal. The **boss-floor SELECTION** (which
  floors are boss/special — the original `RGGameProcess.NextScene` `%5`) is a **Phase-2 rule**, to be
  computed at the `RunController` (it owns `floorIndex`) and applied by gating the spawn at the
  identified hook (`GameScene.cpp:190-196`). Phase 1's contract is only: **`floorIndex` is live and
  correct (via the seed), nothing relies on the dead RoomGen `%5`, and no flag pretends the boss cycle
  is wired when it isn't.**
- **Defer (Phase 2):** the `%5` boss-floor selection rule + gating the spawn on it; and reconciling
  RoomGen's `%5` *size-narrowing* with `randomRoom=false` (thread `floorIndex` through the `else`
  branch, or lift the boss-floor size policy out of the `randomRoom` gate).

---

## 5. Death → run-end (minimal; UI deferred) (D7)

Today: death = quit the app (`GameScene.cpp:427-431`: `IsDead()` → `Context::SetExit(true)`). New:
```
GameScene::Update: if (m_Player->Stats().IsDead()) { run->OnPlayerDied(); return; }
RunController::OnPlayerDied(): m_State.phase = Ended; m_Scenes.Replace(make_shared<EndScene>(this));
```
**Minimal `EndScene`** — a new `Core::Scene` subclass (greenfield, tiny): clears to a flat color and
draws one line of `Util::Text` ("You Died — R: new run · Esc: quit") in screen space (same render
path the HUD uses). Keys: `R` → `run->StartRun()` (fresh run: floorIndex 0, carried cleared,
template player — and because the stack is non-empty, `StartRun` **`Replace`s** the `EndScene` rather
than `Push`ing over it, §1, so no `EndScene` leaks across runs); `Esc`/`IfExit` →
`Context::SetExit(true)`. This proves **death is no longer an app quit** — it enters a visible end
state with restart — without building the polished screen. (Like floor transitions, this `Replace` is
requested from `EndScene::Update` and is deferred-op safe, §3b.)

> **Scope marker:** the *polished* death / results(Statements) / reborn(TryAgain) / victory windows
> are the **separate UI line** (inventory tagged them `separate line`; spec-graded `high` and ready at
> `docs/UI_BEHAVIOR_SPEC.md:413-468`, but **not** required for a minimal run loop). `EndScene` here is
> an explicit placeholder for that future work. **Deferred this phase.**

---

## 6. Phase-1 sub-stage breakdown (each independently verifiable)

| Step | Build | Verify | Kind |
|---|---|---|---|
| **1. `RunState` + continuation logic** | `RunState`/`PlayerContinuation` structs + RunController's carry/floorIndex logic (pure, no engine) | **Unit test**: simulate two floors — assert hp/armor/energy + weaponId carry from floor N snapshot into floor N+1's build params; floor 0 uses template (carried empty) | pure logic |
| **2. GameScene parameterization** | new ctor `(perFloorSeed, floorIndex, carried, run*)`; feed `perFloorSeed` into MapManager/RoomGen at every use site (§4a); thread `ropt.floorIndex` (inert under `randomRoom=false`, §4b); **no `isBossFloor` flag** (boss stays unconditional) | **In-game**: floor 0 vs floor 1 render **visibly different** floors (seed→layout — the only in-game-observable `floorIndex` effect). **Code/unit (not in-game)**: assert the controller threads `floorIndex`/`perFloorSeed` into the ctor + `ropt`. **Do NOT** "confirm boss-cycle controller-driven" (not a Phase-1 property — Phase 2) nor "confirm floorIndex plumbed" in-game (it's inert by §4b). | mixed (seed→floor mapping unit-testable; layout diff in-game) |
| **3. Floor-clear detection** | `FloorCleared(sim)` pure predicate over `EnemyViews`/`BossView` | **Unit test**: fake view lists — all-dead ⇒ true; one alive enemy or live boss ⇒ false | pure logic |
| **4. Wire the transition** | GameScene detects clear (+ exit trigger) → `run->OnFloorCleared(snapshot)` → `Replace` → new GameScene loads continuation | **In-game**: clear floor 1 → advance to floor 2; **hp + equipped weapon continue**; floor 2 is a different floor | in-game |
| **5. Death → minimal `EndScene`** | death path → `run->OnPlayerDied()` → `EndScene`; R restarts, Esc quits | **In-game**: die → land on `EndScene` (not app quit); R → fresh run at floor 0 with template player | in-game |

**Unit-testable (no GL, ctest):** steps 1 & 3 fully; step 2's seed→floor mapping. **Requires in-game
run:** step 2's visual layout diff, step 4 (continuation across a real `Replace`), step 5 (death
flow). Steps 1–3 are independent and can land in any order; step 4 depends on 1–3; step 5 depends on
1 (RunController) only.

---

## Deferred to later phases (flagged, NOT done this phase)

- **Death / results(Statements) / reborn(TryAgain) / victory windows** — separate **UI line**
  (`docs/UI_BEHAVIOR_SPEC.md:413-468` ready; `EndScene` is the placeholder).
- **Full `%5` boss-cycle rules, 16-floor cap, `NextScene` branches, boss-floor SELECTION + gating** —
  **Phase 2** (spec ready). Phase 1 only makes `floorIndex` live+correct (via the per-floor seed) and
  identifies the boss-spawn gate hook (`GameScene.cpp:190-196`); the boss stays unconditional and **no
  `isBossFloor` flag is added** in Phase 1 (§4b — a flag the spawn ignores would be dead plumbing).
- **RoomGen `%5` size-narrowing vs `randomRoom=false` reconciliation** — **Phase 2** (§4b).
- **Economy** (score / crystals / coins) — greenfield; port has none today. Omitted from `RunState`.
- **The two scene-name slots** `DAT_01605fb0` / `DAT_01605fb8` (normal vs run-end) — resolve by
  behavior observation at impl time, not design.
- **Salvage faithful `RGRoomX::ClearRoom()` reward/portal logic** (test-only today,
  `src/world/RGRoomX.cpp:40-50`) — pull in when the floor-clear reward/portal is built out.
- **SP/MP split** (`NetControllerManager.get_playerCount`) — out of run-loop scope (single-player MVP).

## Scope classification (continuation of the inventory's discipline)

- **Main-loop scope (this line, Phase 1):** RunState/RunController, player continuation, GameScene
  parameterization, floor-clear detection, transition wiring, minimal EndScene, the `floorIndex`
  trap fix.
- **UI line (separate):** polished death/results/reborn/victory windows.
- **Defer (later phase, this line):** full `%5`/16-cap/NextScene rules, RoomGen `%5` reconciliation,
  economy, reward/portal salvage.

## Future considerations (noted, not designed)

- **maxHp-raising pickups:** if added, continuation already carries the raised `maxHp` (full-struct
  copy) correctly; no rework needed.
- **Per-run seed randomization:** `RunState.runSeed` can become random per run (currently fixed
  default) without touching the continuation/transition design.
- **Weapon per-instance state** (charge/heat/ammo): SK weapons are energy-gated (energy already
  continues via `CombatStats`); the sim rebuilds the `WeaponController` cold on equip, so persisting
  the weapon **id** suffices for the MVP. Revisit only if a weapon gains persistent per-instance state.
