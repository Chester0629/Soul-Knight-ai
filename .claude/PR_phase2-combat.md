## Summary

Ports Soul Knight 1.7.10 gameplay from its Ghidra decompilation into a **deterministic,
headless `Game::Sim` engine** and rewires `GameScene` to render from it — replacing the old
entity-owned AI/bullet paths. Three layers:

- **Faithful LOGIC layer (complete).** Per-content decision/cadence/scalar/state brains ported
  directly from `_reverse/ghidra_export/game_full.c` under a strict anti-fabrication discipline:
  RNG draw *count + order* preserved exactly, never write a field the decomp doesn't, unrecoverable
  constants left as `// TODO[verify]` (never guessed), genuinely owner-side bodies left un-ported.
  Covers RNG/maze/room-gen/damage/loot (Phase 3), enemy/boss/hero/weapon brains (Phase 4, Waves
  A–D), ally AI + gun fire-patterns + bullet-triggers + world gen (Phase 5, Waves E–J), and the
  bullet brains + gap closure (Waves K/L + `GunHeroBow`). Final completeness gate: **COMPLETE** —
  the skeptical sweep found no recoverable body left off the DEAD list.
- **Deterministic headless `Game::Sim`** (`src/sim/`): `Simulation` aggregator with a fixed-step
  `Advance`, `FixedClock`, tick-based `Scheduler` (Invoke/InvokeRepeating), `FireSystem`,
  `Enemy/Boss/WeaponController`, and `BrainFactory`. In-sim hit resolution + knockback + death;
  golden replay-equality. Fully unit-testable without PTSD.
- **GameScene rewire (Plan 4b):** `GameScene` becomes a `Sim::WorldCollision` provider + input/
  render shell driving one `Simulation`. PTSD `Enemy`/`Boss`/`Bullet` objects are reduced to
  **views** mirrored from `EntityView`/`BulletState` each frame (enemies/boss 1:1 by index and
  never erased; bullets reconciled by monotonic never-reused id). Player owns its vitals; the sim
  borrows them per frame. The old entity-brain + inline bullet/damage path is deleted. Weapon
  crit/repel/pierce now flow `WeaponDef → FireIntent → bullets`.

## Test Plan

- [x] Full suite **1890 / 1890** pass (`ctest -C Debug`, 152s, 0 failures)
- [x] Both targets build **`/W4`-clean** (0 warnings citing any new `sim/`/`combat/`/`world/` TU)
- [x] Golden replay-equality + RNG draw-count/order pinned via parallel same-seeded streams
- [x] Bullet-id non-recycling pinned under rapid spawn/despawn churn
      (`RapidBulletChurnKeepsLiveIdsUniqueAndMonotone`)
- [x] **Interactive `/run` playtest signed off** — clear a room, swap to Gun016, beat the boss,
      rapid spawn/destroy stress pass (the one human-side check that can't be headless)

## Known deferred (non-blocking, tracked)

- Owner-side effect wiring exposed-but-not-emitted: SimEvent anim/sfx emission.
- Fidelity nits: EnemyAI06 kinematic turret, boss wander-blend movement, 400ms-vs-2.0s energy
  reload faithfulness.
- Flagged-unrecovered constants (`// TODO[verify]`): `RGBulletTrigger` ice-buff factor,
  `CombatStats` armor defaults, `RGAisle` pass-3 fan bound — need a binary `.data`/trace baseline.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
