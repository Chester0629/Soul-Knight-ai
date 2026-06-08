# Engine Port — Vertical Slice Design

**Date:** 2026-06-09
**Status:** Approved (brainstorming) — pending implementation plan
**Scope:** First sub-project of the engine port. Builds the owner-side adapter
*infrastructure* and wires a representative *vertical slice* of faithful brains.
The remaining ~70 brains are deferred to follow-on cycles (each its own
spec -> plan -> implementation).

---

## 1. Background & problem

Phases 3-5 ported ~80 faithful, engine-free *decision/cadence brains*
(`EnemyAINN`, `BossAINN`, `CharSkillCNN`, `Gun*`, pets, bullet-triggers, world
interactables) -- all unit-tested and deterministic, depending only on
`data/RGRandom.hpp` (+ `glm`). The faithful **logic layer is complete**
(`docs/LOGIC_COMPLETENESS_REPORT.md`).

These brains expose *decisions*; the owning entity must perform the *effects*
(movement, bullet spawns, animation, audio) on Unity's `FixedUpdate` / `Invoke` /
coroutine cadence. Today none of the per-content brains are wired: `GameScene`
(531 lines) hand-drives entities through the **generic base** `EnemyAI` /
`WeaponInstance` brains and a **hand-coded placeholder** 3/5 boss fan. There is
**no cadence scheduler** -- Unity `Invoke`/coroutine is faked with ad-hoc ms
accumulators, and the runtime is not seed-reproducible.

**This sub-project** builds the adapter layer that turns brain decisions into
deterministic engine effects, and wires a small representative slice end-to-end.

## 2. Goals & non-goals

**Goals**
- A reusable, **engine-decoupled** owner-side adapter layer (`Game::Sim`) that is
  unit-testable headlessly with deterministic golden traces.
- **Deterministic fixed-timestep** runtime: a given run seed reproduces the same
  enemy/boss/bullet sequence end-to-end (the project's RNG-lockstep ethos applied
  to the wired runtime, not just brains in isolation).
- Wire a vertical slice (2 enemies, 1 boss, 2 guns) that exercises every adapter
  subsystem and is visible in the playable dungeon.
- Make the remaining ~70 brains **data-driven follow-on work** (register a brain,
  no new plumbing).

**Non-goals (this cycle)**
- The other ~70 brains (remaining `EnemyAINN`/`BossAINN`/`Gun*`).
- CharSkill hero active-skills + dash wiring; pet/NPC ally entities;
  bullet-trigger / world-interactable wiring; audio.
- Frame-exact animation-event bullet timing (we use the cadence model, not
  per-anim-frame events).
- Bit-exact reproduction of a real-device capture (impossible without a captured
  trace; see the `SecondsToTicks` caveat in section 6).

## 3. Architecture (Approach A: Director + Services)

New area `Game::Sim` in `include/sim/` + `src/sim/`, registered in `files.cmake`.
It depends only on `data/`, `combat/` brains, `world/`, `RGRandom`, and `glm` --
**never** on PTSD. `GameScene` owns one `Simulation` and becomes a render/input
shell.

```
GameScene (PTSD shell: input, player movement, rooms, chests, camera, HUD, render)
   |  feeds player pos + fire input + dtMs
   v
Simulation  (Game::Sim -- pure, deterministic, headless-testable)
   |- FixedClock        dtMs -> N fixed 0.02s steps (+ remainder carry)
   |- Scheduler         deterministic Invoke/InvokeRepeating/coroutine (tick-based)
   |- FireSystem        FireIntent -> BulletState[] (no RNG of its own)
   |- EnemyController[]  EnemyAINN brain + RGEController physics + EntityState
   |- BossController[]   BossAINN brain (attack-phase SM + angry transition)
   |- WeaponController   Gun* brain (fire-control cadence + scatter/heat/charge)
   |- BrainFactory       def id -> concrete brain + controller config (registry)
   |- bullets: BulletState[]
   ^  exposes controller positions/facing + bullet list + emitted anim/SFX events
   |
GameScene syncs renderables (entity transforms, pooled Bullet views, anim/SFX)
```

**Ownership model (key decoupling):** the controller **owns the gameplay state**
(`EntityState`: pos, vel, hp via `CombatStats`, facing, flags, the brain's
`RGRandom` stream). The PTSD `Enemy`/`Boss` `GameObject` becomes a **view** that
each frame copies `controller.Position()` / facing into its transform and plays
emitted anim events. Pure sim, renderable shell.

## 4. Components

Each unit has one purpose, a well-defined interface, and is testable alone.

| Unit | File | Responsibility |
|---|---|---|
| `FixedClock` | `sim/FixedClock.{hpp,cpp}` | `int Advance(float dtMs)` -> number of elapsed fixed 0.02s steps; carries the sub-step remainder across frames. `kFixedStep = 0.02f`. |
| `Scheduler` | `sim/Scheduler.{hpp,cpp}` | Tick-based timer service. `Handle Invoke(int delayTicks, Callback)`, `Handle InvokeRepeating(int firstTicks, int intervalTicks, Callback)`, `void Cancel(Handle)`, `void Tick()` (fires due callbacks, FIFO within a tick). `static int SecondsToTicks(float s)`. |
| `FireIntent` | `sim/FireIntent.hpp` | POD: `origin`, `dir`, `Pattern{Single,Fan,Burst,Charge,Parabola}`, `count`, `spreadDeg`, `speed`, per-bullet stats (`damage,repel,critical,canThrough,pierce`), `camp`. The brain fills it (scatter/fan angles already drawn on the brain's stream). |
| `FireSystem` | `sim/FireSystem.{hpp,cpp}` | `void Expand(const FireIntent&, std::vector<BulletState>& out)` -- deterministic geometry expansion. **Single** (base +/- the brain-computed scatter, incl. Gun016's heat-scaled spread) and **Fan** (N even angles over spreadDeg) are implemented for the slice; `Burst`/`Charge`/`Parabola` are enum-reserved for follow-on guns. **Takes no RNG draws of its own.** |
| `BulletState` | `sim/BulletState.hpp` | POD: stable `id`, `pos`, `vel`, `lifeMs`, stats, `camp`, `active`. Lives in the sim; GameScene mirrors to pooled PTSD `Bullet`s. |
| `EntityState` | `sim/EntityState.hpp` | POD gameplay state shared by controllers: `pos`, `vel`, `facing`, `CombatStats`, flags (awake/dead/kinematic), `roomId`. |
| `WorldCollision` | `sim/WorldCollision.hpp` | Interface the sim queries for wall blocking: `bool Blocks(glm::vec2 pos, float radius) const`. GameScene implements it over the existing room AABBs + sealed doors. |
| `SimEvent` | `sim/SimEvent.hpp` | POD render/audio event (anim-trigger name or SFX id + entity id) controllers push into the sim's event queue; GameScene drains it each frame to play anim/SFX. Keeps the sim PTSD-free. Audio is out of scope this cycle -- SFX events may be ignored by the shell. |
| `EnemyController` | `sim/EnemyController.{hpp,cpp}` | Owns an `EnemyAINN` brain + RGEController base physics + `EntityState`. Per tick: scheduler-driven Scout/ShootReflection/RunReflection; `IntegrateVelocity` move; emits `FireIntent` + anim events. Kinematic enemies skip the move. |
| `BossController` | `sim/BossController.{hpp,cpp}` | Owns a `BossAINN` brain. Attack-phase SM (StartAtk/InAtk/EndAtk), angry-phase transition (shoot_cd halves once at <50% HP), wander, fan `FireIntent`. |
| `WeaponController` | `sim/WeaponController.{hpp,cpp}` | Owns a `Gun*` brain. Fire-control cadence; emits `FireIntent` with the brain's scatter/heat/charge output. Rebuilt by the factory on weapon swap (cold start). |
| `BrainFactory` | `sim/BrainFactory.{hpp,cpp}` | `EnemyController MakeEnemy(const EnemyDef&, int seed, glm::vec2 spawn)`, `BossController MakeBoss(...)`, `WeaponController MakeWeapon(const WeaponDef&, int seed)`. String-id registry -> concrete brain. The extension point for the other ~70. |
| `Simulation` | `sim/Simulation.{hpp,cpp}` | The sim root GameScene owns. Holds `FixedClock`, `Scheduler`, `FireSystem`, controllers, bullets, event queue. `void Update(float dtMs, const WorldInputs&)` runs N fixed `Step()`s. Read accessors for the shell to poll each frame: `Bullets()`, the controller views (pos/facing/hp/alive), and `DrainEvents()`. Headless-testable. |

## 5. Data flow

`GameScene::Update(dtMs)`:
1. Handle input (ESC/exit, WASD, mouse) -> player movement (with axis-separated
   wall slide, **stays in GameScene**) -> player pos.
2. Feed the sim: `sim.SetPlayerState(pos, alive)`, `sim.SetFireInput(mouseDown, aimDir)`.
3. `sim.Update(dtMs, worldInputs)` -- runs `FixedClock.Advance(dtMs)` fixed steps.
4. Sync renderables from sim state: each enemy/boss `GameObject.transform =
   controller.Position()`/facing; reconcile the pooled `Bullet` views to the sim
   bullet list (acquire new ids, release gone ids); apply emitted anim/SFX events.
5. Rooms / chests / pickups / camera / HUD -- unchanged.

`Simulation::Step()` (one 0.02s fixed tick), in order:
1. `Scheduler.Tick()` -- fire callbacks due this tick (scheduled ShootReflection /
   StartAtk / next burst shot).
2. Each controller advances per-`FixedUpdate` state (velocity integration,
   knockback decay, boss windup, gun heat ramp).
3. Apply movement: `pos += IntegrateVelocity(...) * 0.02`; revert on
   `WorldCollision.Blocks` (axis-separated).
4. Drain `FireIntent`s -> `FireSystem.Expand` -> append `BulletState`s.
5. Advance bullets (`pos += vel*0.02`, lifetime decay; despawn at 0 / off-life).
6. Collisions: bullet<->enemy/boss (camp 0) and bullet<->player (camp 1) through
   the existing faithful `Combat::ResolveHit` + `ApplyToEnemy`/`ApplyToPlayer`;
   feed repel via `EnemyAI::GetForce`; flag deaths; cancel a dead entity's
   scheduled callbacks.

## 6. The two hard mechanics

**Invoke / cadence emulation.** Unity `Invoke("ShootReflection", cd)` ->
`scheduler.Invoke(SecondsToTicks(cd), cb)`; when it fires, the controller runs the
faithful brain method and reschedules the next using the brain's returned cadence
(e.g. `EnemyAI shoot_cd`, halved in a boss angry-phase). `InvokeRepeating` and the
coroutine state-machines (Gun008 / BulletRoundabout, already modelled as step
functions) advance the same way. `SecondsToTicks(s) = lround(s / 0.02f)`.

*Faithfulness caveat (recorded, accepted):* Unity's real-second `Invoke` is
quantized onto our 0.02s grid -- gameplay-identical and fully deterministic inside
the sim, but not bit-equal to a real-device capture (which we cannot reproduce
without such a capture anyway).

**FireIntent -> bullets, RNG-lockstep preserved.** The **brain** produces the
faithful geometry and takes the RNG draws on its own stream (Gun016's
`Range(-spread,+spread)` scatter; BossAI01's fan choice). The controller packages
that into a `FireIntent`. `FireSystem` expands it **deterministically with no RNG
of its own** (Fan->N even angles, Single->base+/-scatter, Charge->speed*ratio).
All randomness stays brain-side, so the stream stays in lockstep -- the
`FireSystem` is a pure geometry function.

## 7. Vertical-slice content

Chosen to stress every subsystem:
- **EnemyAI01** -- scout + shoot cadence + wander: full `EnemyController` path
  (Scout/ShootReflection/RunReflection on the scheduler + single-bullet
  `FireIntent` + knockback physics).
- **EnemyAI06** -- kinematic turret: the no-knockback / stationary-shooter path
  (`SetKinematic`, skips the move; still fires).
- **BossAI01** -- replaces the hand-coded 3/5 fan placeholder: faithful
  `Range(0,100)` attack select, the `<50% HP` angry-phase transition (shoot_cd
  halves once), `Range(-1,1)` wander, real fan `FireIntent`.
- **Gun001** (default) + **Gun016** (heat-minigun, dropped as a pickup): single-
  shot *and* the heat-ramp / growing-spread path, plus **weapon-swap rebuilding
  the `WeaponController` via the factory** (data-driven dispatch, cold restart).

## 8. Testing strategy

Headless and deterministic; **no PTSD dependency in any sim test**. Tests read
`sim.Bullets()` and use a stub `WorldCollision` returning no walls (or fixed
AABBs). Golden traces are derived from a fixed seed + fixed tick count, re-run for
byte-identical replay -- the same discipline as the brain tests, at the wired
level.

- `FixedClockTest` -- dtMs accumulation -> correct step counts + remainder carry.
- `SchedulerTest` -- Invoke/InvokeRepeating fire at the right ticks; Cancel;
  deterministic FIFO order within a tick; `SecondsToTicks` rounding.
- `FireSystemTest` -- Single+scatter->base+/-the brain scatter; Fan(N,spread)->N
  correct even angles; asserts zero own RNG draws (parallel stream non-advancing).
- `EnemyControllerTest` -- EnemyAI01 golden position/fire trace from a seed;
  EnemyAI06 kinematic stays put and still shoots.
- `BossControllerTest` -- BossAI01 enters the angry phase once at <50% HP (cadence
  halves); fan fired on a shoot tick.
- `WeaponControllerTest` -- Gun016 heat ramps -> spread shrinks -> one scatter
  draw per shot in lockstep with a parallel stream; Gun001 single shot.
- `SimulationTest` -- end-to-end player + 1 enemy + bullets + collision; the enemy
  takes deterministic damage; two same-seeded runs are byte-identical.

Target ~40-60 new tests; keep the suite 100% green (currently 1813/1813).

## 9. Edge cases & error handling

- **Dead/awake gate** before any scheduled callback acts (brains already model the
  gates); on death, cancel the entity's scheduled callbacks (`CancelInvoke`).
- **Weapon swap** rebuilds the `WeaponController` via the factory -> heat/charge
  resets cold (faithful: a new weapon instance starts uncharged).
- **Wall block** on a scheduled move -> axis-separated slide via `WorldCollision`
  (as today).
- **Determinism guard** -- the sim takes no wall-clock time and no `Math.random`;
  all RNG via the seeded `RGRandom` (existing rule). A test asserts replay
  equality.
- **Bullet despawn sync** -- stable `BulletState` ids; GameScene releases the
  pooled view on despawn; pool exhaustion degrades gracefully (skip spawn, never
  crash).
- **Scheduler/controller lifetime** -- callbacks captured by a controller must not
  outlive it; the `Simulation` owns controllers and clears their handles on
  removal.

## 10. Risks & mitigations

- *GameScene refactor regression* (moving ~150 lines of AI/fire/bullet logic into
  the sim). Mitigation: the sim is built + unit-tested first; GameScene is rewired
  to drive it only once the sim is green; the existing playtest loop is the
  acceptance check.
- *Determinism leaks* (a stray `dtMs` path or unseeded draw). Mitigation: the
  `SimulationTest` replay-equality test is the tripwire; sim code is PTSD-free by
  construction.
- *Over-abstraction* for the slice. Mitigation: interfaces (`WorldCollision`,
  `BulletSink`) are minimal; the `BrainFactory` registry starts with exactly the
  slice's entries.

## 11. Definition of done

- `Game::Sim` infrastructure built, registered in `files.cmake`, warning-clean
  under MSVC /W4.
- The slice (EnemyAI01, EnemyAI06, BossAI01, Gun001+Gun016) runs in `GameScene`
  via the sim; the hand-coded boss fan and generic-enemy path are replaced.
- ~40-60 new headless deterministic tests; `ctest` 100% green.
- A `SimulationTest` proves seed-reproducible replay.
- Spec for the next cycle (data-driven roll-out of the remaining brains) is a
  natural follow-on (register brains in the factory).
