# Faithful LOGIC Layer — Completeness Report

**Gate status before engine-port phase: NOT COMPLETE.**

Wave K (the bullet brains) shipped cleanly, but two independent completeness
sweeps each found recoverable decision/cadence/scalar/state bodies that are still
un-ported. Until those gaps are closed (or formally reclassified as DEAD), the
faithful LOGIC layer is **not** complete and the engine-port phase must not start.

- **Wave K:** 5 of 6 modules ported, 1 correctly BLOCKED (owner-only). No fabrication.
- **Completeness sweep:** `confirmedComplete = false` in **both** swept areas.
- **Remaining recoverable GAPS: 8** (7 in enemies/weapons/pets-NPCs, 1 in world grid-gen).
- **`logicComplete = false`** — there are open recoverable gaps.

---

## 1. Wave K — Bullet brains (port results)

| Module | Status | Files (hpp / cpp / test) | RNG contract | Key faithfulness flags |
|---|---|---|---|---|
| **Bullet03** | ported | `include/combat/Bullet03.hpp`, `src/combat/Bullet03.cpp`, `test/Bullet03Test.cpp` | ZERO RNG draws. Pure deltaTime/timer accumulator; `RGRandom` carried for interface parity only. Test `MakesNoRngDrawsAcrossFullLifetime` pins zero-draw via parallel same-seeded stream. | `start_size` declared but never read by `Bullet03__Update` → deliberately NOT modelled (no invented lerp). `ScaledSize` is literal `(a_time/max_time)*max_size`. Owner `get_transform` scale/rotate writes NOT modelled. No clamp added (final-step overshoot reproduced exactly). |
| **BulletParabola** | ported | `include/combat/BulletParabola.hpp`, `src/combat/BulletParabola.cpp`, `test/BulletParabolaTest.cpp` | ZERO RNG draws across Awake/SetTargetPosition/FixedUpdate. `RGRandom` parity-only. Test `MakesNoRngDrawsAcrossTicks` pins zero-draw over 50 ticks. | Field semantics (0x20 active, 0x1c counter, 0x4c rate, 0x54 progress) inferred from offset usage only. Parabola/arc-shaping math lives in owner `get_transform` consumer → NOT reconstructed (only the linear deltaTime progress accumulator is recovered). |
| **BulletBoom** | ported (fixed) | `include/combat/BulletBoom.hpp`, `src/combat/BulletBoom.cpp`, `test/BulletBoomTest.cpp` | ZERO RNG draws (StartBoom/SoonExplode/ExplodeStart). Two tests pin parity: `MakesNoRngDrawsAcrossFullLifecycle`, `SeedIsParityOnlyAndNeverConsumed`. | String-literal→method binding INFERRED (literals carried as raw refs): earlier event (boom_time-1.0)→SoonExplode warning, later (boom_time)→ExplodeStart detonation. Timing scalars fully recoverable. `Tick(dt)` is a modelling shim for Unity `Invoke`; detonation-pre-empts-warning rule is the faithful consequence of ExplodeStart's `CancelInvoke`. Instantiate/Animator/get_transform are owner-side, NOT ported. |
| **BulletColor** | ported | `include/combat/BulletColor.hpp`, `src/combat/BulletColor.cpp`, `test/BulletColorTest.cpp` | `PickColorIndex()` makes **exactly ONE** int draw: `RGRandom::Range(0,6)` (max EXCLUSIVE → {0..5}), modelling `Start`'s global `UnityEngine.Random.Range(0,6)` (BossAI06Child precedent). Gate/FixedUpdate make ZERO draws. Tests pin draw count+order and non-advancing gate. | The 6-way switch's per-index colour/sprite VALUE is unrecovered (each case collapses to identical owner `get_transform` stub) → only the selected INDEX is modelled; the colour mapping is NOT fabricated. FixedUpdate gate `awake && rotate_angle!=0` ported; the rotate itself is owner-side. Offsets 0x1C/0x20 from RGBullet base layout. |
| **BulletLaterFixedTarget** | **partial** | `include/combat/BulletLaterFixedTarget.hpp`, `src/combat/BulletLaterFixedTarget.cpp`, `test/BulletLaterFixedTargetTest.cpp` | ZERO **RGRandom** draws. `FindTarget` draws its re-seek delay from the engine GLOBAL `UnityEngine.Random` stream (a different stream) → modelling that via RGRandom would be fabrication. Recovers only the `[min,max]` bounds (min=delay 0x44, max=delay+delay) plus the lerp `min + t*(max-min)`; the `t` sample is owner-supplied. Test `MakesNoRgRandomDraws` pins non-advancing RGRandom. | Partial because the engine-global delay draw is intentionally left to the owner (not faked through RGRandom). Invoked method name `StringLiteral_6654` unrecoverable. Field semantics inferred from offsets (0x44 delay, 0x10 Rigidbody2D, 0x50 bool flag). Owner-side Start `get_transform`, Invoke scheduling, velocity-zero brake, flag clear NOT modelled. |
| **BulletFollow** | **BLOCKED** | _(none — no files written)_ | ZERO RNG draws. No file/brain created. | **Correctly owner-only.** All three bodies are engine writes with no recoverable scalar: FixedUpdate = sole `get_transform` (homing seek = Transform read); UpdateAttribute = `SetSourceObject` pointer store + `get_transform`; ReSetDestoryTime = `CancelInvoke`/`Invoke` re-arm with verbatim `destroy_time` (0x18, confirmed via sibling `RGShield__ReSetDestoryTime` int→float convert vs raw passthrough). Modelling any of them — even an identity passthrough — would invent a mechanic. Per HARD RULES, no files written. |

**On-disk verification (this pass):** all 5 ported modules have hpp + cpp + test present;
BulletFollow has zero files (correct). `files.cmake` does **not** yet register any Wave K
file — registration lists are returned below for the build wave to apply.

---

## 2. Completeness sweep — conclusion per area

### Area A — enemies / bosses / heroes / weapons / pets-NPCs
**`confirmedComplete = false`.** Fully accounted for: EnemyAI01-15+Shark,
BossAI01-14+Nian+NianLantern+06Child+12Parent, CharSkillC01-C13, Npc*. RGEController's
recoverable bodies (TurnTo/GetForce/Dizzy/Scout) are already folded into the EnemyAI base
port; its remaining bodies are owner/NetController glue. **But 7 recoverable bodies are
still un-ported** (see GAPS §3) — most importantly the three `RGPetController` bodies, the
prior audit's stated **#1 highest-value target** that Wave E dropped.

### Area B — bullets / bullet-triggers / buffs / world grid-gen / interactables / items
**`confirmedComplete = false`.** The whole Bullet*/RGBT*/Buff*/RGRoom*/Item* surface
re-affirms as ported-or-DEAD **except one** un-ported recoverable unit:
`RGRoomXEndless::IsOccupied`, a self-contained AABB Rect-overlap validity test that the
base-class `RoomGen` port does **not** cover (the endless subclass uses a different
Rect-list overlap algorithm, not the grid-cell `map==0` scan).

---

## 3. Remaining recoverable GAPS (LOUD — these block completeness)

> These are **provably recoverable** deterministic decision / cadence / scalar / state
> bodies that are **absent from the port**. Each must be ported (or formally reclassified
> DEAD with evidence) before the engine-port phase. **8 gaps total.**

### G1 — `RGPetController::ReplyingHP`  (game_full.c:427317-427364) — HIGHEST VALUE
Recoverable regen cadence + heal formula. Gated `hp[0x1c] < maxHp[0x18]`; accumulate
`timer[0x64] += Time.deltaTime`; when `timer >= interval[0x60]+[0x5c]` heal
`hp += maxHp/5` (integer /5), reset `timer = [0x5c]`, then clamp `hp = min(hp, maxHp)`.
This is the maxHP/5-per-interval pet regen the prior audit named #1 but Wave E DROPPED.
`RGPetController` is the un-ported BASE class of the shipped Wolf/Snowman pets.

### G2 — `RGPetController::FixedUpdate`  (game_full.c:427251-427312)
Recoverable two-branch velocity state machine with a multiplicative decel accumulator.
`decel[0x30] <= 1.0` → follow-master: `velocity = dir[0x50] * master.vel[(0x40)+0x10] *
(master.vel[(0x40)+0x14] + 1.0)`. Else → coast: `velocity = dir[0x34] * decel[0x30]`, then
decay `decel[0x30] *= dampingFactor[0x24]` (written-back state). Only the
`Rigidbody2D.set_velocity` write is owner-side; the branch + decel decay is pure logic.

### G3 — `RGPetController::TurnTo`  (game_full.c:427457-427477)
Recoverable wall-bounce reflection writing back direction state:
`dir[0x50,0x54] = Vector2.Reflect(dir[0x50,0x54], normal(param_2,param_3))`. Pure
deterministic state transition the pets inherit. Completes the shipped pet family.

### G4 — `GunStaffWizard::Attack`  (game_full.c:968615-968675)
Recoverable 4-state cyclic counter written back (`[0x1e]`: 0→1→2→3→0 barrel/phase rotation)
PLUS a sign-gated early-out (`[0x1c] < -[0x1c]`, the burst-exhausted idiom) PLUS standard
scatter `RGRandom.Range(-fVar4,+fVar4)`, `fVar4 = baseDev + baseDev*deviation[(owner)+0x20]`.
Richer than the bare-scatter guns (has a real written-back state machine). Not ported.

### G5 — `GunWaken::Attack`  (game_full.c:970426-970465)
Recoverable mode-gated scatter: spread draw `RGRandom.Range(-fVar4,+fVar4)` applied only
when `wakenFlag[0x84] == 0`; in awakened mode the RNG draw is **SKIPPED entirely** (perfect
accuracy). A recoverable conditional-RNG decision (gate-passed-draw shape). Not ported.

### G6 — `GunMagicBow::Attack`  (game_full.c:967301-967327; charge gate Update @967246-967272)
Recoverable charge-scaled output formula (same class as ported Gun007 charge cannon):
`chargeRatio = currentCharge[0x8c] / maxCharge[0x90]`, then scale bullet base-velocity
components (x[0x80],y[0x84],z[0x88]) by `chargeRatio`. Update holds the charge gate
(accumulate while Animator-bool set and `charge[0x8c] < maxCharge[0x90]`). Deterministic,
zero-RNG, pure scalar; only the `GetComponent<RGBullet>` apply is owner-side. Not ported.

### G7 — `GunMultiBullet::GetAttack / GetSpeed / GetCanThrough / GetCritics`  (game_full.c:967541-967675) — LOWER VALUE
Four recoverable per-bullet deterministic stat selectors. Each checks whether a per-bullet
override array (atk@0x70, spd@0x74, canThrough@0x80, crit@0x78) has the same element
count[0xc] as the bullet-count array[0x6c]; if equal returns the indexed element
(`array + index*4 + 0x10`, bounds `index<count`), else returns a scalar fallback
(atk@0x20, spd@0x28, canThrough@0x38, crit@0x2c). Array-vs-scalar branch + index formula.
Borderline but provably recoverable.

### G8 — `RGRoomXEndless::IsOccupied`  (game_full.c:429286-429422; thunk 429427-429431; caller 429150)
FULLY-RECOVERABLE pure-geometry AABB Rect-overlap validity test, **zero presence in the
port**. Builds a query Rect `(c-1, r-1, w+2, h+2)` (1-cell padding), loops the placed-obstacle
`List<Rect>` at 0x7c doing a per-axis overlap test (x via Rect.x/Rect.width, y via
Rect.y/Rect.height; reconstructs xMax/yMax), returns 1 on first overlap else 0. **Distinct**
from the ported `RoomGen::IsWallIntersect` (grid-CELL `map==0` scan) — the endless subclass
tests geometric Rect overlap against a runtime list, a different algorithm. Only the Rect-list
contents are owner-populated. (Sibling `CreateObstacle` shares the base RNG count cadence but
its placement loop truncates into owner Instantiate tails — `IsOccupied` is the self-contained
recoverable unit.)

---

## 4. DEAD / owner-only categories — RE-AFFIRMED

The prior audit's DEAD classifications hold in full (re-verified at line level this pass).
The only corrections are the 8 GAPS above; everything below remains genuinely
owner-only / unrecoverable and is **correctly absent** from the port.

**Bullets (Wave K + family):**
- `BulletFollow` (all 3 bodies) — owner `get_transform` / `SetSourceObject` / `Invoke` re-arm. BLOCKED, no files.
- `BulletImmediately` (963296-963392) — ctor Color.clear + RGBullet ctor; Start/DestroySelf get_transform; CastRay = RaycastHit2D memclr → owner Physics2D tail; Explode = null-check + get_transform. DEAD.

**Pet weapons / swords / lasers / melee:**
- `PetGun001/002` (383935/383969) — GLOBAL non-seeded `UnityEngine.Random` discarded into Instantiate tails (ItemRoomEgg exclusion). `PetGun003/PetHand/PetSword` owner adapters. DEAD.
- `GunDarkSword.CastPoly`, `GunChain/GunCaliburn/GunDrill/GunHoldSword/GunSpearSword/GunSpearLaser/GunShield` — energy-consume + Animator/PrefabPool spawn; math in owner FUN_/RGLaser. DEAD.
- `GunDragon.Attack` (966535) — owner dispatch with a DISCARDED `RGRandom.Range(0,100)` (non-returning tail, no field write). DEAD.
- `GunPhantom/GunHarmmer/GunBadminton/Gun003/Gun006/Gun010/Gun015` — Instantiate / Animator / name-concat / vtable reads. Owner-only.

**Bare-scatter named guns (NOT gaps — mechanical duplication of an already-ported shape):**
- `GunChannel, GunMultiBullet/02, GunSpearBullet, GunSwordKnight, GunSwordShutgun, GunTNT` — identical ported scatter shape `RGRandom.Range(-fVar4,+fVar4)` on a different spread field. Porting them is duplication, not new logic. (Only `GunMultiBullet`'s *stat selectors* G7 carry logic beyond scatter.)

**Buffs:** `BuffChangeSpeed::GetSourceObject` (return-0 stub); `BuffEffectTrigger/BuffRandomEffectTrigger::AddBuff` (enemy-vs-player type dispatch, no formula); `BuffFire/BuffIce` Start/GetHurt/BuffEnd/Disappear = Singleton/CancelInvoke/get_transform. DoT/slow scalars live in stripped Invoke handlers. DEAD.

**Bullet-triggers:** `RGBTCreate/RGBulletTrigger/RGBTDelayCreate/RGBTEnergy/RGBTExplode` — exploded-bool guards + PrefabPool/get_transform/iterator-state-clear. DEAD.

**Interactables / net / items:** `RGCoin` (GetForce vector setter, FindTarget owner seek, GetItem singleton, serialized ctor defaults); `ItemGem::FindTarget`; `RGBothPot` (NetController authority gates); `ItemEgg` (RGRandom draws DISCARDED into non-returning tails — ItemRoomEgg pattern); `RGStartRoomEvent::SetRGRandomSeed` (thin forwarder); `RGDoor` (owner glue). DEAD.

**Enemies:** `EnemyAI05` Dead body = cctor guard + get_transform tail (owner-only DEAD, correctly absent). `RGEController` EndCycle/GetHurt/SyncGetHurt/HitBack = owner/NetController glue.

---

## 5. Gate decision

| Check | Result |
|---|---|
| Wave K bullet brains ported (or correctly blocked) | YES (5 ported, 1 BLOCKED-owner) |
| Area A sweep clean | NO — 7 recoverable gaps (G1-G7) |
| Area B sweep clean | NO — 1 recoverable gap (G8) |
| **Faithful LOGIC layer COMPLETE** | **NO** |

**Recommendation:** Run one more port wave to close G1-G8 (priority order:
G1-G3 RGPetController family → G4-G6 GunStaffWizard/GunWaken/GunMagicBow →
G8 RGRoomXEndless::IsOccupied → G7 GunMultiBullet selectors). Only after these are
ported (or reclassified DEAD with line-level evidence) should the engine-port phase begin.

---

## Wave L (G1-G8 closure)

Wave L ports the eight recoverable gaps (G1-G8) identified at the bottom of the
previous gate. Each port was verified line-by-line against the decompilation
TRUTH (`game_full.c`); RNG draw count **and** order are pinned via parallel
same-seeded `RGRandom` reference streams where any draw exists, and zero-draw
paths are pinned non-advancing.

### Ported modules

| Module | Gaps | Status | Files (hpp / cpp / test) | RNG | Faithful |
|---|---|---|---|---|---|
| `RGPetController` | G1-G3 | ported | `include/combat/RGPetController.hpp` · `src/combat/RGPetController.cpp` · `test/RGPetControllerTest.cpp` | ZERO-DRAW. FixedUpdate/ReplyingHP/TurnTo are deterministic scalar/state/vector logic. `RGRandom` carried only for family parity; pinned non-advancing in lockstep vs a same-seeded reference (`BaseBodiesTakeNoRngDraw`). | YES |
| `GunStaffWizard` | G4 | ported | `include/combat/GunStaffWizard.hpp` · `src/combat/GunStaffWizard.cpp` · `test/GunStaffWizardTest.cpp` | Exactly ONE seeded max-inclusive float draw per shot: `ScatterAngle -> Range(-spread,+spread)` (@968670). Sign-gated early-out and the 4-state phase counter take ZERO draws. Tests pin count+order (`ScatterDrawsExactlyOneFloatInOrder`, `EarlyOutPathTakesNoScatterDraw`, `NextPhaseTakesNoDraw`, `FiringPathDrawsOnePerShotInLockstep`). | YES |
| `GunWaken` | G5 | ported | `include/combat/GunWaken.hpp` · `src/combat/GunWaken.cpp` · `test/GunWakenTest.cpp` | Conditional-RNG: normal mode (`wakenFlag==0`) draws ONE max-inclusive float `Range(-spread,+spread)` (@970458); awakened mode (`wakenFlag!=0`) draws ZERO and leaves the stream untouched (gate @970452). Tests pin count+order via parallel stream (`AwakenedModeReturnsZeroAndTakesNoDraw`, `AwakenedShotsLeaveStreamUntouchedAcrossSeeds`, `MixedSequenceAdvancesOnlyOnNormalShots`). | YES |
| `GunMagicBow` | G6 | partial* | `include/combat/GunMagicBow.hpp` · `src/combat/GunMagicBow.cpp` · `test/GunMagicBowTest.cpp` | ZERO-DRAW on every path (Update/Attack/MakeConsume/StopWeapon). Charge-to-velocity is a deterministic scalar (ratio `charge/maxCharge` per-axis scale), not random scatter. `RGRandom` carried for owner-side parity, pinned non-advancing. | YES |
| `GunMultiBullet` | G7 | ported | `include/combat/GunMultiBullet.hpp` · `src/combat/GunMultiBullet.cpp` · `test/GunMultiBulletTest.cpp` | ZERO-DRAW in the module. All four selectors (`GetAttack/GetSpeed/GetCanThrough/GetCritics` @967541-967675) are pure array-vs-scalar choosers. The only `Range` in the surrounding owner body (`CreateBullet` @967529) is the already-ported scatter shape (= `Gun019::ScatterAngle`), not re-ported. No `RGRandom` member needed. | YES |
| `RGRoomXEndless` | G8 | ported | `include/world/RGRoomXEndless.hpp` · `src/world/RGRoomXEndless.cpp` · `test/RGRoomXEndlessTest.cpp` | `IsOccupied` is a PURE predicate, ZERO draws (@429286-429422). Owner-side `CreateObstacle` scatter is not part of this unit. Test pins zero-draw contract: same-seeded parallel `RGRandom` non-advancing across 100 `IsOccupied` calls. | YES |

\* `GunMagicBow` reported `status: partial` from the port worker, but the
recoverable charge-scaled-velocity body (Update + Attack + helpers) is fully
ported and `finalFaithful: true`; "partial" reflects only the owner-side spawn
tail being out of scope, not missing logic.

### Notable flags (recorded, not invented)

- `GunWaken` ctor scalars `owner+0x70=12 / 0x74=50 / 0x78=45.0f` recovered VERBATIM
  as named constants (`kCtorField70/74/78`); semantics undeterminable, left
  `// TODO[verify]`. They do not enter the Attack math.
- `GunMultiBullet`: `GetSpeed` int->float widening modeled as a caller-supplied
  representation; ctor fields `0x85=1 / 0x88=0xf` recorded as named constants with
  unknown meaning; equal-count + IL2CPP bounds-throw folded into a total-function
  predicate (`overrideCount==bulletCount && index<overrideCount`) — documented in
  the header, not a guessed mechanic.
- `RGRoomXEndless`: the inlined `Rect.Overlaps` </=/<= cascade reconstructed as the
  closed-interval `qMin<=rMax && rMin<=qMax`; proven algebraically equivalent by
  per-axis case analysis. `List<Rect>` at `+0x7c` is owner-populated -> modeled as an
  explicit `std::vector<PlacedRect>` input per the MODULE NOTE boundary.

### Re-confirm verdict (skeptical final pass)

All eight gaps **G1-G8 are faithfully ported and verified line-by-line** against
the decomp TRUTH. The five new combat modules + `RGRoomXEndless` are correct, with
tests pinning the load-bearing invariants: RNG draw count AND order via parallel
same-seeded streams; integer division (`12/5==2`); timer-reset-to-`reply_time1`
(not 0); AABB boundary inclusivity. `RGPetController` ctor defaults
(100/4/2/1/2/1/1 @427154-427166) and the byte-stride asymmetry in
`GunMultiBullet::GetCanThrough` (`std::vector<bool>`, index not index*4) are both
correct. The G8 overlap predicate received full case analysis and is provably
equivalent to the decomp's nested branch cascade on both axes.

The skeptical sweep also re-confirmed the adjacent pet-subclass family
(`WolfController`, `SnowmanController`) as already ported and correct — incl.
Snowman `OnGameStateChange`'s `+0.5` speed_rate boost and Wolf `RunReflection`'s
recovered `Range(0,10)` draw (stored to `m_LastWanderRoll`, not discarded).
`SnowmanController::FixedRotation` / `WolfController::FixedRotation` remain
DEFENSIBLY owner-only (engine `Vector2.Angle` over Transform-differenced inputs
feeding a rotation field — same owner-Transform fabrication that correctly blocked
`BulletFollow`). `GunBattery`/`GunOnePunch` confirmed owner-only.

### NEW gap found (blocks logic-complete)

| Module | Method | Decomp | Why recoverable |
|---|---|---|---|
| `GunHeroBow` | `Attack` | `game_full.c:967038-967077` | Un-ported G6-class charge-cannon: charge ratio (`charge[0x98]/maxCharge[0x9c]`) scaling base velocity per-axis (`[0x8c]/[0x90]/[0x94]`, @967049-967053) **plus** a recoverable count-gate `if ([0xac] < 1)` (burst/arrow-count early-out: plays SFX and returns without firing, @967054-967061). Identical zero-RNG pure-scalar shape that justified porting G6 `GunMagicBow`. Only the `List.get_Item`/`GetComponent<RGBullet>` spawn tail is owner-side. Absent from the port, the G1-G8 list, AND the DEAD list. |

`GunHeroBow` has **no files on disk** (hpp/cpp/test all MISSING) — confirmed this
pass. It must be ported (clone the `GunMagicBow` ChargeRatio/ScaledVelocity shape +
add a count-gate predicate) or DEAD-reclassified with line-level evidence before
the gate can pass.

### Build-integration debt (non-faithfulness)

None of the six Wave-L modules are registered in `files.cmake` yet (same debt the
Wave K report flagged). All hpp+cpp+test exist on disk but are unbuilt/untested in
CI until wired in. Registration is a separate task from faithfulness and does not
affect the logic-complete verdict; it is tracked for the build-integration wave.

### Wave L gate

| Check | Result |
|---|---|
| G1-G3 `RGPetController` family ported + verified | YES |
| G4 `GunStaffWizard::Attack` ported + verified | YES |
| G5 `GunWaken::Attack` (conditional-RNG) ported + verified | YES |
| G6 `GunMagicBow` charge-cannon ported + verified | YES |
| G7 `GunMultiBullet` selectors ported + verified | YES |
| G8 `RGRoomXEndless::IsOccupied` ported + verified | YES |
| New `GunHeroBow::Attack` gap closed | **NO — un-ported, no files** |
| **Faithful LOGIC layer COMPLETE** | **NO** |

**GATE: NOT COMPLETE.** G1-G8 are all closed and faithful, but the skeptical pass
surfaced one concrete recoverable gap (`GunHeroBow::Attack`, same class as the
already-ported G6 `GunMagicBow`) that is absent from the port, the G-list, and the
DEAD list. One short wave to port `GunHeroBow` (or DEAD-reclassify it with
line-level evidence), then re-run the critic, closes the faithful logic layer.
Separately, register all Wave-K + Wave-L modules in `files.cmake` and run `ctest`.

---

## Final gate -- GunHeroBow closure + completeness

### GunHeroBow verify verdict -- FAITHFUL (yes), no must-fix

Re-derived independently from the decomp TRUTH (`game_full.c`): ctor @966960,
SetAttack @966973, Attack @967038-967077, the per-arrow loop continuation
`FUN_00b138f0` @967081-967112, StopWeapon @967116-967177. All six checks pass at
the line level:

1. **ChargeRatio = charge(0x98)/max_charge(0x9c)** -- confirmed @967052
   (`*(float *)(param_1 + 0x98) / *(float *)(param_1 + 0x9c)`). Port reads the
   right offsets, applies NO clamp, and returns 0 on a non-positive divisor (a
   total-function guard, never a fabricated value). FAITHFUL.
2. **ScaledSpeed = chargeRatio * base(0x94)** -- confirmed: @967049 reads
   `fVar2 = base(0x94)`; @967052 forms `(int)((charge/maxCharge) * fVar2)`. ONLY the
   0x94 component is scaled. The 0x90 (@967050) and 0x8c (@967051) reads have their
   `VectorSignedToFloat` results discarded -> owner pass-through, correctly NOT
   modelled as scaled. The `(int)` truncation is the owner velocity-store; the float
   product is the recoverable scalar. FAITHFUL.
3. **HasArrows = arrow_count(0xac) >= 1** -- confirmed @967054
   (`if (*(int *)(param_1 + 0xac) < 1)` -> dry SFX + return, no fire). Gate is NOT
   inverted (`>= 1` fires). FAITHFUL.
4. **ShouldFireNext(firedIndex, arrowCount) = firedIndex+1 < arrowCount** --
   confirmed `FUN_00b138f0` @967091 (`if (*(int *)(unaff_r4 + 0xac) <= unaff_r7 + 1)`
   stops, `unaff_r7` = index just fired). So the bow fires exactly `arrow_count`
   arrows (indices 0..n-1); the test's loop-walk proves `fired == n` for n in 1..6.
   FAITHFUL.
5. **ZERO RGRandom draws** -- confirmed: no `RGRandom`/`Random__Range` call appears
   anywhere in 966960-967177 (the bodies use only `VectorSignedToFloat`,
   `RGMusicManager`, `List.get_Item`, `GetComponent`, `Animator`, `Object.Destroy`,
   `UICanvas`). The test `MakesNoRngDrawsAcrossEveryPath` pins a non-advancing
   parallel same-seeded stream (`bow.Rng().Range(0,1e6) == ref.Range(0,1e6)` after
   exercising every path -- a real mutating draw, so equality proves zero advance).
   FAITHFUL.
6. **No fabricated state / owner-side correctly excluded** -- the brain writes no
   field the decomp does not, guesses no constant (`kMinArrowsToFire = 1` derives
   directly from `< 1` @967054), and leaves OWNER: the `List.get_Item` /
   `GetComponent<RGBullet>` spawn tail (@967067-967076, @967104-967111), the
   `Animator.GetBool/SetBool` charge-toggle (SetAttack @966983-967024), the cost
   write (`*(iVar1+0x14) += cost[0x44]` @967149), `Object.Destroy` of the charge
   effect (@967156), the charge reset (`+0x98 = 0` @967157), and the cost subtraction
   (@967173). FAITHFUL.

**No must-fix applied; none remain.** Files present and correct on disk:
`include/combat/GunHeroBow.hpp`, `src/combat/GunHeroBow.cpp`,
`test/GunHeroBowTest.cpp`.

### Completeness conclusion -- NO new gaps

Skeptical final sweep across the charge-cannon / named-gun family, pets, summons,
and everything adjacent to the prior 9 gaps:

- **Charge-cannon family CLOSED.** The `charge/maxCharge` ratio division exists in
  EXACTLY two bodies in the whole decomp: GunHeroBow @967052 (`0x98/0x9c`, now
  ported) and Gun005 @316778 (`0x8c/0x90`, already ported). Gun005/Gun007/Gun016/
  GunMagicBow/GunHeroBow are all ported -- no other charge-scaled-velocity body
  exists.
- **GunSummon01** (Attack @968680, StartUseWeapon @968693) -- owner-only:
  `get_transform` + `Animator.SetTrigger`. DEAD.
- **GunTeammate1/2/3 + GunTeamate4** (SetAttack/Update/TurnReady/ctor @969060-969317)
  -- owner-only: the SetAttack bodies are the standard energy-gate
  (`component-resource >= cost[0x34]`) feeding a `NetControllerManager`/`UICanvas`
  singleton tail (the SAME shape GunHeroBow's own SetAttack correctly leaves
  owner-side); TurnReady is a `SpriteRenderer.set_sprite` flag write; Update is
  `Animator.GetBool` + `get_transform`; ctor only sets `+0x78=1` and chains
  `RGWeapon::ctor`. No recoverable formula/gate-with-scalar/state-machine/RNG. DEAD.
- **Pets/summons** -- `RGPetController` (G1-G3) ported; `WolfController`/
  `SnowmanController` ported; `NpcMercenaryController`/`NpcSummon01` ported.
  `NpcSummon01::FixedRotation` @1671597 re-confirmed DEFENSIBLY owner-only (the same
  `Transform.get_position` -> `Vector2.Angle` shape that correctly blocked
  Wolf/Snowman FixedRotation and BulletFollow). `PetGun001/002/003` DEAD
  (global-Random discarded into Instantiate tails).

No body with genuinely NEW recoverable logic (new state machine, formula, gate, or
RNG cadence) remains un-ported and off the DEAD list. Bare-scatter duplication is
correctly NOT flagged per the rules.

### FINAL GATE

| Check | Result |
|---|---|
| GunHeroBow ported + verified line-by-line (6/6 checks) | YES |
| Charge-cannon family fully accounted for | YES (only Gun005 + GunHeroBow divide; both ported) |
| Named-gun / teammate / summon sweep clean | YES (all DEAD/owner-only or ported) |
| Pet / summon family sweep clean | YES (RGPetController + Wolf/Snowman/Npc* ported) |
| New recoverable gaps found | NONE |
| **Faithful LOGIC layer COMPLETE** | **YES** |

**GATE: COMPLETE.** GunHeroBow was the last named gap; it is now faithfully ported
and verified, and the skeptical final sweep surfaces no further recoverable
decision/cadence/scalar/state body off the DEAD list. The faithful LOGIC layer is
**COMPLETE**. (Build-integration debt is unchanged and separate: register all
Wave-K + Wave-L + GunHeroBow modules in `files.cmake` and run `ctest`; this does not
affect the logic-complete verdict.)
