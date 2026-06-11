# Soul Knight Faithful Port — Waves E–J Integration Manifest

Synthesis of the per-module Port + Verify + Fix results for Waves E through J of the
1.7.10 reverse-engineered Soul Knight port. Every module below was independently
re-derived from `game_full.c` (the ranked-truth Ghidra decomp) during verify, with
RNG draw-count/order, gate thresholds, field writes, and test goldens audited
adversarially.

- **Total modules:** 27
- **Disk-verified file triads on disk:** 24 modules (combat + world)
- **Blocked (owner-only / unrecoverable, no files):** 2 — `RGBTRebound`, `ItemRoomEgg`
- **Deepen (edited already-registered files):** 1 — `CombatStats`
- **Remaining must-fix after re-verify:** **0** (see loud-flag section below)

All 27 modules returned `finalFaithful = true`. No `remainingMustFix` survived
re-verification for any module.

---

## Module Table

| Module | Wave | Area | Status | Faithful | Files on disk | RNG draws (summary) | Fabrication flags |
|---|---|---|---|---|---|---|---|
| NpcSummon01 | E | combat | ported | yes | src+test (hpp pre-existing) | Shoot/Run: 1× Range(0,10) max-excl, pre-gate (gated-out still draws); EndCycle/GetHurt/OnGameStateChange: 0 | none |
| RGBatteryController | E | combat | ported | yes | src+test (hpp pre-existing) | 0 draws all methods | none |
| NpcMercenaryController | E | combat | full | yes | hpp+src+test | Melee/Remote Shoot + RemoteRun: 1× Range(0,10) each, pre-gate; Scout/EndCycle/GetHurt/Dead: 0 | none |
| Gun019 | F | combat | tested | yes | hpp+src+test | ScatterAngle: 1 float Range(-s,+s) on main shot; burst path: 0 | FIXED: inverted burst-reschedule gate `cCount>0` → `cCount<0` (decomp 965357 `x<-x` ⇔ x<0) |
| Gun007 | F | combat | tested | yes | hpp+src+test | 0 draws all (deterministic charge math + coroutine state machine) | FIXED: cleanup burst tick set m_State=kBurst → kEnd so end-step is reachable |
| Gun004 | F | combat | tested | yes | hpp+src+test (test new) | ShotScatterAngle: 1 float Range(-s,+s); others 0 | none |
| Gun014 | F | combat | tested | yes | hpp+src+test (test new) | CreateBullet scatter: 1 float Range(-s,+s); AdjustAngle: 1 int Range(0,360) on gate pass; others 0 | none |
| Gun016 | F | combat | pre-existing (verify) | yes | hpp+src+test | ScatterAngle: 1 float Range(-spread,+spread) per Attack | none |
| Gun002 | F | combat | pre-existing (verify) | yes | hpp+src+test | ScatterPellet: 1 float Range(-half,+half) per pellet (spawn loop owner-side) | none |
| Gun012 | F | combat | pre-existing (verify) | yes | hpp+src+test | Scatter: 1 float Range(-half,+half) per call | none |
| Gun018 | F | combat | pre-existing (verify) | yes | hpp+src+test | Attack: 1 float Range(-s,+s); CreateBullet: 1 int Range(-d,+d) on gate pass; else 0 | none |
| Gun017 | G | combat | tested | yes | hpp+src+test | Attack/ScatterAngle: 1 float Range(-s,+s) on CanFire pass; 0 when drone active | FIXED: StopWeapon case-B no-write; 3× line-citation corrections |
| Gun001 | G | combat | pre-existing (verify) | yes | hpp+src+test | ScatterAngle: 1 float Range(-s,+s) per shot | none |
| Gun009 | G | combat | pre-existing (verify) | yes | hpp+src+test | RollScatter: 1 float Range(-s,+s) unconditional | none |
| Gun013 | G | combat | pre-existing (verify) | yes | hpp+src+test | Scatter: 1 float Range(-s,+s) per call | none |
| Gun011 | G | combat | pre-existing (verify) | yes | hpp+src+test | ScatterAngle: 1 float Range(-half,+half) per shot | none |
| Gun006Paw | G | combat | pre-existing (verify) | yes | hpp+src+test | 0 draws (deterministic buff math) | none |
| RGBDelayDivision | H | combat | full | yes | hpp+src+test | 0 draws all methods | None invented; ClampAngle divisor recovered cross-body (sibling idiv callers); field names from il2cpp asset |
| RGBulletTrigger | H | combat | full | yes | hpp+src+test | 0 draws all (crit roll lives in owner-side dispatch) | ice-buff factor unrecoverable → quarantined `kIceBuffFactorUnverified`, never returned |
| RGBTDivision | H | combat | full | yes | hpp+src+test | 0 draws all methods | none (field names from il2cpp dump; tagMatched resolved bool param) |
| RGBTRebound | H | combat | **BLOCKED** | yes | none | n/a (only `Start()` body, pure Unity owner glue) | none |
| CombatStats | I | combat | deepen | yes | hpp+test (already registered) | 0 draws (HP/armor/energy/speed accumulators) | armorLoad/armorRate defaults data-driven → TODO[verify]; RGEWeapon ctor omits atk/speed (not fabricated) |
| RGRoomX | J | world | ported | yes | hpp+src+test | 0 draws (CloseDoor/OpenDoor/ClearRoom) | none |
| RGAisle | J | world | ported | yes | hpp+src+test | BuildFloorEdges pass1: width*height × Range(0,2); RollAisleWall: 2× Range(0,100) when len>0 else 0; pass3 owner-side | none invented; pass-3 loop bound clobbered → left owner-side |
| RGBox | J | world | ported | yes | hpp+src+test | 0 draws all paths (Hit gated-out / registered / destroy) | none |
| ItemWishingWell | J | world | partial | yes | hpp+src+test | OpenChest: 1 int Range(0,poolSize) max-excl (degenerate pool → 0 draws); Trigger/Classify/NextStep: 0 | poolSize owner-set param (not invented); WaitForSeconds delay owner-side |
| ItemRoomEgg | J | world | **BLOCKED** | yes | none | n/a (8 owner-dispatch bodies; draws discarded at truncated tails; CreatePots uses global Unity RNG) | none |

> **Status legend:** `ported` = new hpp+src+test from decomp; `tested` = pre-existing/new
> port body given decomp-grounded tests (and fixes); `pre-existing (verify)` = body
> already on disk, re-verified faithful this wave; `full` = hpp+src+test all new;
> `deepen` = added mechanics to an already-registered header; `partial` = recoverable
> surface ported, owner-only tails deferred; `BLOCKED` = no recoverable pure logic.

---

## RNG fidelity notes (cross-cutting)

- **Range semantics:** `RGRandom.Range(int,int)` is **max-EXCLUSIVE** (Unity int
  convention); `RGRandom.Range(float,float)` is **max-INCLUSIVE**. Verified correct in
  every module (e.g. NpcSummon01 `Range(0,10)` ⇒ `[0,9]` with `<8` threshold; scatter
  float draws `Range(-spread,+spread)`).
- **Pre-gate draws:** NpcSummon01 and NpcMercenaryController draw their `Range(0,10)`
  **before** the shoot gate — a gated-out tick still advances the stream. Pinned by
  `*GatedOutStillAdvancesStream` tests.
- **Zero-draw classes** (RGBatteryController, Gun007, Gun006Paw, RGBDelayDivision,
  RGBulletTrigger, RGBTDivision, RGRoomX, RGBox, CombatStats): draw-count = 0 is
  genuinely pinned by exercising every path then asserting a same-seeded parallel
  `RGRandom` stays byte-identical on the next draw — a stray draw would desync.
- **Owner-side draws correctly excluded:** the crit roll in `RGBulletTrigger`
  (`Random.Range(0,100)`), the per-child fan spawn draws in `RGAisle` pass-3 and
  `RGBDelayDivision`, and `ItemRoomEgg`'s `CreatePots` (global non-seeded
  `UnityEngine.Random`) all live in truncated owner tails and are not modelled — modeling
  them would fabricate stream semantics.

---

## BLOCKED / owner-only modules

Two modules returned **blocked** with **no files written** — this is the faithful
outcome (writing a body would be fabrication). Both were independently re-confirmed
during verify.

### RGBTRebound (Wave H, combat)
- The decomp contains exactly **one** `RGBTRebound__` body: `RGBTRebound__Start`
  (`game_full.c:469273-469333`). No constructor, no `<>c__Iterator`/`MoveNext`
  coroutine, no `Update`/`AdjustmentAngle`/decision body. No recreation `.cs` exists for
  `RGBTRebound` or `ReboundEffectTrigger`.
- Every statement in `Start()` is owner-side Unity glue: cctor static-init guard,
  `GetComponentInChildren<EnemyAI08>/<ReboundEffectTrigger>`,
  `GetComponents<Collider2D>`, `op_Implicit` null checks, `ResourcesUtil.Load<Material>`,
  `get_transform`, a single undeterminable component-field copy
  (`*(child+0x18) = *(self+0x48)`), and a `Collider2D` enable/disable walk
  (`set_enabled(collider,false)` for each non-trigger).
- **Zero** RNG draws, zero recoverable threshold/constant/cadence/state math. No port
  files exist on disk (`include/combat/RGBTRebound.hpp`, `src/combat/RGBTRebound.cpp`,
  `test/RGBTReboundTest.cpp` all absent) — confirmed.

### ItemRoomEgg (Wave J, world)
- All **8** bodies (`OnItemTriggerSuccess` 257084, `CreateObject` 257103,
  `CreateWeapon` 257116, `CreateGem` 257133, `CreateBoom` 257148, `CreatePots` 257177,
  `MoveNext` 257217, `Reset` 257258) bottom out in a cctor guard + null-check + a single
  truncated "Subroutine does not return" tail into an owner concern
  (`Animator.SetBool`, coroutine alloc, `Object.Instantiate<RGWeapon>`, `get_transform`,
  register-only `FUN_010798e8`, `NotSupportedException`, or `UnityEngine.Random`).
- The only seeded `RGRandom` draws are `CreateWeapon`'s `Range(0, field@0x44)` and
  `MoveNext`'s `Range(0,100)` — both are the final expression before a non-returning
  call, with **discarded results, owner-supplied/truncated bounds, and no field write**
  ⇒ no recoverable decision/cadence/scalar/state. `CreatePots` uses the **global
  non-deterministic `UnityEngine.Random`**, outside the seeded model.
- No recreation `.cs` exists ⇒ any reconstructed body would be invention. No port files
  exist on disk — confirmed. Writing no files is faithful.

---

## REMAINING MUST-FIX ITEMS (orchestrator attention)

**NONE.** Every module's `remainingMustFix` array is empty and every module returned
`finalFaithful = true` after independent re-derivation.

Must-fix items that were raised during port/verify were all **fixed in place** before
synthesis (these are recorded for the audit trail, not open):

- **Gun019** — inverted burst-reschedule gate (`cCount > 0` → `cCount < 0`,
  decomp 965357 `x < -x` ⇔ `x < 0`). Fixed in `include/combat/Gun019.hpp`.
- **Gun007** — `Gun007BurstIterator::MoveNext` cleanup tick latched `m_State=kBurst`,
  making the `TurnActivate` end-step unreachable. Fixed to `m_State=kEnd` in
  `src/combat/Gun007.cpp:98-106`.
- **Gun017** — `StopWeapon` case-B wrote `m_Deployed=false` where the decomp
  (964967-964973) returns with no state write; fixed to preserve `m_Deployed`. Plus
  three decomp line-citation corrections.
- **CombatStats** — a fabricated/unverifiable decomp anchor in the `ApplyPlayerDamage`
  docstring (non-existent `HurtArmor`/`0x5B9810` symbol) was corrected to
  `FUN_005c984c@471692` + `HurtHp@471725`; citation-only, no behavior change.

**Non-blocking residue (shouldFix, documentation only — safe to defer):** header
Doxygen line-citation drift in several modules (NpcSummon01, RGBatteryController,
NpcMercenaryController, Gun012, Gun018, Gun011, Gun006Paw); the corresponding `.cpp`
citations are correct. These carry zero runtime impact.

---

## Build registration impact

- **24 modules** need NEW registration in `files.cmake` (all combat/world ported,
  tested, and verify modules — none were previously registered). See the orchestrator's
  returned `srcFiles` / `includeFiles` / `testFiles` lists.
- **CombatStats** (Wave I deepen) is header-only and **already registered**
  (`combat/CombatStats.hpp` files.cmake:172, `CombatStatsTest.cpp` files.cmake:265) —
  no new registration; edited files tracked as `deepenedFiles`.
- **RGBTRebound** and **ItemRoomEgg** are blocked with no files — excluded from
  registration entirely.
