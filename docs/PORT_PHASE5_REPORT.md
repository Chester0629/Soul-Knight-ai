# Phase 5 Per-Content Port Report (Backlog Waves E-J)

Faithful port of the remaining reverse-engineered Soul Knight 1.7.10 backlog
identified in `docs/PORT_GAP_AUDIT.md` -- **Waves E through J**. Phase 4 ported the
enemy/boss/hero/weapon brains (Waves A-D); this phase ports the ally AIs, the new
gun fire-pattern shapes, the spawn-pattern bullet-trigger math, a CombatStats
deepening, and the dungeon interactables.

**Source of truth (ranked):** `_reverse/ghidra_export/game_full.c` (1.72M lines,
named line-numbered bodies). Recreation C# (`_reverse/recreation/*.cs`) used as a
field/logic cross-check where present (RGAisle, RGRoomX, RGBulletTrigger, RGWeapon);
the IL2CPP metadata dump is body-empty (names/signatures only). Same anti-fabrication
discipline as Phases 3-4.

## Methodology -- same adversarial pipeline (one module per item)

A single background workflow (`.claude/wf-port-ej.mjs`, 60 subagents, ~20 min,
2.85M agent tokens) drove every module through **Port -> Verify -> Fix -> Re-verify**:

1. **Port** -- enumerate all `<Class>__` bodies, classify pure-logic vs owner-side,
   write a `namespace Game` brain depending only on `data/RGRandom.hpp` (+ `glm`),
   annotating every method `// FAITHFUL: <Class>__<Method> @ game_full.c:<line>`.
2. **Verify** -- an independent adversarial agent re-derives each method from the
   decomp and tries to break it (miscounted/reordered RNG draws, invented field
   writes, wrong gates, fabricated truncated tails, false test goldens).
3. **Fix** -- applies must-fixes only (re-reading the decomp first); runs only when
   verify found a must-fix.
4. **Re-verify** -- a third independent pass confirms the fix and checks for newly
   introduced fabrication.

Agents never touched `files.cmake` and never built. The **orchestrator** registered
all files centrally and ran the single shared build + `ctest` (concurrent MSBuild on
the shared `build/` corrupts it). The central `ctest` is the final gate.

The full per-module audit table lives in `docs/PORT_WAVE_EJ_MANIFEST.md`.

## What was ported (24 faithful modules + 1 deepen; 2 faithfully BLOCKED)

| Wave | Theme | Modules | Outcome |
|---|---|---|---|
| **E** | Pet / NPC / summon allies | `NpcSummon01` (impl), `RGBatteryController` (impl), `NpcMercenaryController` (full) | 3 ported |
| **F** | New gun fire-pattern shapes | `Gun016 Gun019 Gun007 Gun004 Gun002 Gun012 Gun014 Gun018` | 8 ported/verified |
| **G** | Single-shot / secondary spread + drone parent | `Gun001 Gun009 Gun013 Gun011 Gun017 Gun006Paw` | 6 ported/verified |
| **H** | Spawn-pattern bullet-trigger math | `RGBDelayDivision RGBulletTrigger RGBTDivision` (+ `RGBTRebound` BLOCKED) | 3 ported, 1 blocked |
| **I** | CombatStats deepening (additive) | `CombatStats` (RoleAttributePlayer regen / RoleAttribute speed / RGWeapon-RGEWeapon default-stat tables) | 1 deepened |
| **J** | World grid-gen + interactables | `RGRoomX RGAisle RGBox ItemWishingWell` (+ `ItemRoomEgg` BLOCKED) | 4 ported, 1 blocked |

### Wave specifics
- **Allies (E)** -- RGEController-derived Scout/Shoot/Run reflections on the proven
  Wolf/Snowman offset table. `NpcSummon01`/`NpcMercenaryController` draw their
  `Range(0,10)` *before* the 8/10 shoot gate (a gated-out tick still advances the
  stream -- pinned by `*GatedOutStillAdvancesStream` tests). `RGBatteryController` is
  a genuine zero-draw turret (charge latch + StopShooting cadence, no RNG).
  `NpcMercenaryController` is the richest: melee/remote shoot+scout branches with
  weapon-type dispatch and Invoke cadence formulas; pick-weapon/talk/setup bodies were
  correctly left owner-side.
- **Guns (F/G)** -- recoverable scalar/state only: scatter `Range(-spread,+spread)`
  (one max-inclusive float draw per shot), heat/spin-up spread (`Gun016`), charge
  cannon bullet-count + size-lerp (`Gun007`), burst counter/Invoke cadence
  (`Gun019`). Bullet spawn / muzzle transform stay owner-side.
- **Bullet-triggers (H)** -- `RGBDelayDivision` angle/division/timer math and
  `RGBTDivision` fan division are deterministic (zero draws; the crit roll and child
  spawns live in owner dispatch). `RGBulletTrigger` ports through_count accessors +
  `GetDamageFactor`; the ice-buff factor was unrecoverable and quarantined behind a
  never-returned `kIceBuffFactorUnverified` constant rather than guessed.
- **CombatStats (I)** -- additive HP/armor/energy/speed accumulators on the existing
  header; `armorLoad`/`armorRate` defaults are data-driven and flagged `// TODO[verify]`,
  not fabricated; `RGEWeapon` ctor omits atk/speed (not invented).
- **World (J)** -- `RGRoomX` door open/close + ClearRoom state (zero draws); `RGAisle`
  floor/wall grid-stamping (`BuildFloorEdges` pass-1 draws, `RollAisleWall`
  `Range(0,100)` pair; the clobbered pass-3 loop bound was left owner-side, not
  reconstructed); `RGBox.Hit` gate/destroy; `ItemWishingWell.OpenChest`
  `Range(0,poolSize)`. `RoomGen`'s already-ported grid generation was NOT duplicated.

## Must-fixes the adversarial harness caught (and fixed before integration)
- **Gun019** -- inverted burst-reschedule gate (`cCount > 0` should be `cCount < 0`;
  decomp 965357 `x < -x` <=> `x < 0`). Fixed.
- **Gun007** -- `MoveNext` cleanup tick latched `m_State=kBurst`, making the
  `TurnActivate` end-step unreachable; fixed to `kEnd`.
- **Gun017** -- `StopWeapon` case-B wrote `m_Deployed=false` where the decomp
  (964967-964973) returns with no state write; fixed to preserve state.
- **CombatStats** -- a non-existent decomp anchor in the `ApplyPlayerDamage` docstring
  corrected to `FUN_005c984c@471692` + `HurtHp@471725` (citation-only).

## Faithfully BLOCKED (no files written -- a port would be fabrication)
- **RGBTRebound** (H) -- the decomp has only `RGBTRebound__Start`; every statement is
  owner Unity glue (component fetches, material load, collider enable/disable). Zero
  recoverable threshold/constant/cadence/state. No recreation `.cs`.
- **ItemRoomEgg** (J) -- all 8 bodies bottom out in a cctor guard + truncated
  non-returning owner tail (`Instantiate`, coroutine alloc, `Animator.SetBool`). The
  only seeded draws have discarded results / owner-supplied bounds / no field write;
  `CreatePots` uses the global non-seeded `UnityEngine.Random`. No recreation `.cs`.

## Validation gates (all GREEN)
- **Build:** `SoulKnightTests.exe` links; the 24 new translation units are
  **warning-clean** (0 errors, 0 warnings cite any new `combat/`/`world/` file under
  MSVC `/W4`). One integration fix was required: `NpcMercenaryController` declared
  `Dead()` both as a state getter and as the faithful method -- the getter was renamed
  `IsDead()` (the faithful `Dead()` keeps the decomp name).
- **Unit tests:** `ctest` -> **1673 / 1673 pass** (up from 1323; **+350** new across
  Waves E-J). Every module's test re-derives RNG goldens from a parallel same-seeded
  `RGRandom` (or asserts a non-advancing stream for the zero-draw modules), so any
  extra/missing/reordered draw would diverge.

---

# MANUAL INTERVENTION BOUNDARY (unchanged in kind from Phase 4)

The faithful per-content decision/cadence brains are done and green. Still not
modelled (owner-side wiring or genuinely unrecoverable):

1. **Owner-side effects (by design)** -- Animator/sprite, bullet `Instantiate` /
   coroutine scheduling, Rigidbody/Transform, audio, `Invoke` cadence *execution*.
   The brains expose the decisions; wiring each into its `Entity`/`Player`/`Weapon`
   is the remaining integration.
2. **Blocked modules** -- `RGBTRebound`, `ItemRoomEgg` (above); plus the Wave-D/earlier
   blocked laser/bow/homing/thunder mechanics.
3. **Flagged-but-unrecovered constants** -- `RGBulletTrigger` ice-buff factor,
   `CombatStats` armor defaults, `RGAisle` pass-3 fan bound: exposed as named
   `// TODO[verify]` constants, never guessed; need a binary `.data` pass or captured
   trace to baseline.
4. **RoomGen size-roll / ComputeBaseLevel** -- still external-blocked (carried from
   Phase 3-4); `RoomGenGoldenTest` locks current behaviour.
