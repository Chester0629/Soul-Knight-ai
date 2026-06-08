# Phase 4 Per-Content Port Report

Faithful port of the reverse-engineered Soul Knight 1.7.10 **per-content brains**
(enemies, bosses, heroes, weapons) into the C++ game. Where Phase 3 ported the
shared core (RNG, maze, room-gen, damage, loot), Phase 4 ports the individual
content classes -- one faithful, engine-free decision/cadence brain per class.

**Source of truth (ranked):** real logic lives in `_reverse/ghidra_export/game_full.c`
(1.72M lines, named line-numbered bodies). The IL2CPP metadata dump
(`_reverse/asset soul 2/Scripts/Assembly-CSharp`) is **body-empty** -- used for
field names/signatures only; porting logic from it is fabrication. Raw offsets
decoded against `_reverse/recreation/*` field maps.

## Methodology -- adversarial port -> verify -> fix -> re-verify

Every module went through a deterministic multi-agent pipeline (one module per item):

1. **Port** -- an agent enumerates *all* `<Class>__` bodies, classifies each
   pure-logic vs owner-side, and writes a `namespace Game` brain depending only on
   `data/RGRandom.hpp` (+ `glm` for vectors), annotating every method
   `// FAITHFUL: <Class>__<Method> @ game_full.c:<line>`.
2. **Verify** -- an independent *adversarial* agent re-derives each method from the
   decomp and tries to break it (miscounted/reordered RNG draws, invented field
   writes, wrong gates, fabricated truncated tails, omitted bodies).
3. **Fix** -- applies must-fixes only, re-reading the decomp before each edit.
4. **Re-verify** -- a third agent independently confirms the fix is faithful and
   introduced no new fabrication.

Agents never build and never touch `files.cmake`; the orchestrator registers files
and builds centrally (concurrent MSBuild on the shared `build/` corrupts it). The
**central `ctest` is the final gate** -- it catches what logic-only review cannot
(compile errors, runtime test/brain inconsistencies).

### Non-negotiable faithfulness rules
- RNG draw **count + order** preserved exactly (`Range(int)` max-exclusive,
  `Range(float)` max-inclusive); gated-out paths take **no** draw (replay lockstep).
- **Never** write a field/state the decomp does not.
- Truncated / jumptable / vtable tails: model the recoverable head + RNG only; the
  rest is owner-side or `// TODO[verify]` -- never invented.
- Unrecoverable magic constants (uninitialised `DAT_*`, jumptable targets) exposed
  as named constants marked `// TODO[verify]` + a `fabrication_flag` -- never guessed.

## What was ported (46 modules, all build-integrated + unit-tested)

| Wave | Content | Modules | Decomp class -> port |
|---|---|---|---|
| **A** | Enemies | 16 | `EnemyAINN` -> `EnemyAINN` (01,02,04,06-15), `WolfController` (RGPetController), `SnowmanController`, `EnemyAIShark` |
| **B** | Bosses | 14 | `BossAINN` -> `BossAINN` (03,05-14) + companions `BossAI06Child`, `BossAI12Parent`, `BossAINianLantern` |
| **C** | Heroes | 12 | `CNNController` (RGController player) -> `CharSkillCNN` (C02-C13) |
| **D** | Weapons | 4 | `GunThrow`, `Gun005`, `Gun008`, `BulletRoundabout` (recoverable mechanics only) |

(Builds on the Phase 4 pilot already integrated: `EnemyAI03`, `BossAI02`,
`CharSkillC01`, plus the `RoomGenGoldenTest` regression locks for item #4.)

### Wave specifics
- **Enemies** -- Scout/RunReflection/ShootReflection/FixedUpdate state machines:
  wander draws (2x `Range(-1,1)`), scout reroll gates, knockback friction decay,
  awake/dead/dizzy gating. `EnemyAI05` correctly remains a Dead-only stub (no port).
- **Bosses** -- attack-phase state machines (`StartAtkNN`/`InAtkNN`/`EndAtkNN`),
  `BossAngry`/enrage (`shoot_cd *= 0.5`, hp-ratio < 0.5 once), child-dead, dizzy,
  icicle/transfer-gate chains. RNG-light (most draws in RunReflection wander +
  ShootReflection attack roll), attack dispatch via unrecovered vtable jumptables
  modelled as flagged equal-bucket reconstructions (draw preserved regardless).
- **Heroes** -- player skill/atk gates on RGController offsets (`0xC` awake,
  `0x44` role_attribute.skill_ready, `0x55` in_skill, vtable `0x17c`), reusing the
  PlayerDash/`CharSkillC01` cooldown-timer model (explicitly `fabrication_flag`ged).
  Heroes take **zero RNG draws** (player-driven), locked via non-advancing reference
  streams.
- **Weapons** -- *selective*: only the recoverable scalar/state math. GunThrow's
  geometric multi-shot spread fan-out (15deg/30deg branches + parity), Gun005's
  charge ratio (`charge/maxCharge`, `maxCharge=2.0f` recovered from the `0x40000000`
  bit-pattern), and the Gun008 / BulletRoundabout coroutine state machines
  (`<>c__Iterator0__MoveNext`: state `0x2c` / counter / limit). All zero-RNG
  (deterministic geometry, not random scatter).

## Validation gates (all GREEN)

- **Unit tests:** `ctest` -> **1323 / 1323 pass** (up from 351 at the start of this
  phase; **+972** new across the 46 modules).
- **Build:** `SoulKnightTests.exe` links; **warning-clean under MSVC `/W4`** (0 errors,
  0 warnings).
- **Determinism:** every module's test re-derives RNG goldens from a parallel
  same-seeded `RGRandom` (or asserts a non-advancing stream for zero-draw modules),
  so any extra/missing/reordered draw would diverge.

## The adversarial harness earned its keep

- **Caught real rule-2 fabrications that a first-pass fix had passed:** independent
  re-verify found `BossAI10` *still* writing an invented `m_AtkIndex` after a prior
  fix cleared it; `BossAI14`'s first draft invented `m_AtkIndex` + a phantom `0xed`
  write. Both removed and re-confirmed. (Same bug class as the `BossAI02` pilot.)
- **Central ctest caught a faithful-brain / wrong-test inconsistency** logic-only
  review missed: `CharSkillC04Test` asserted a kill -> instant-reactivate chain that
  is not decomp-grounded (`in_skill` 0x55 is cleared only by `RoleSkillEnd`, and that
  also resets the cooldown -- the latch and the combo cooldown-refresh are separate
  events). The brain was faithful; the test was corrected to assert only grounded
  behaviour.
- **Refused to fabricate blocked constants:** GunThrow's 6-7 / 8+ bullet spread
  magnitudes (`DAT_00b24804/808`, uninitialised) were left as `kSpread*=0.0f`
  `// TODO[verify]`; tests assert only branch selection, never magnitude.

---

# MANUAL INTERVENTION BOUNDARY (deferred / blocked)

The faithful per-content **decision/cadence brains** are done and green. The
following are intentionally *not* modelled (owner-side wiring, or genuinely
unrecoverable from this decompilation) and need engine wiring, captured reference
data, or a future binary-data pass:

1. **Owner-side effects (by design)** -- Animator/sprite, bullet `Instantiate` /
   `GetComponent<RGBullet>`, `Invoke`/coroutine scheduling, Rigidbody/Transform,
   audio. The brains expose decisions; the owning entity performs the effect. Wiring
   each brain into its `Entity`/`Boss`/`Player`/`Weapon` is the remaining integration.
2. **Blocked weapon mechanics (not attempted)** -- laser beam, bow draw, homing,
   thunder-chain, rebound: flagged unrecoverable in the pre-port assessment; not
   ported rather than fabricated.
3. **Blocked magic constants** -- GunThrow `DAT_00b24804/808` spread magnitudes
   (6-7 / 8+ bullets) are uninitialised in `game_full.c`; need a binary `.data` pass
   to recover the real degree values (currently `0.0f` / `// TODO[verify]`).
4. **Truncated attack-dispatch jumptables** -- several boss `ShootReflection ->
   StartAtkNN` mappings are unrecovered vtable jumptables; modelled as flagged
   equal-bucket reconstructions. The single dispatch draw is stream-correct; the exact
   bucket thresholds need a captured trace to re-baseline.
5. **RoomGen size-roll / ComputeBaseLevel (carried over from #4)** -- still
   external-blocked (`SetRGRandomSeed` truncated, `DAT_0050F930` absent); needs a
   captured real-game grid. `RoomGenGoldenTest` locks the current behaviour for
   regression; `g4` flagged as reconstructed-order to re-baseline.
