# Phase 4+ Coverage Gap Audit

Read-only audit of `_reverse/ghidra_export/game_full.c` (1.72M lines) for content that is
**not yet ported but is faithfully recoverable**, run as an 8-category parallel sweep + synthesis
(9 agents). Of ~221 game-content classes, 49 are ported (Phase 3-4); this audit classifies the
remaining ~115 in-scope classes by recoverability with the same anti-fabrication discipline as the
port itself (a body that is only a `get_transform`/`Instantiate`/`Animator` tail is owner-only, not
a candidate).

## Headline

- **~29 classes/fragments are still faithfully portable** (after dropping owner-only/coroutine/stub
  bodies and deduping already-ported overlaps).
- **2 whole categories are genuinely DEAD** (confirm the prior BLOCKED calls): **Buffs** (all 10 -
  DoT/slow/speed logic lives in stripped `TriggerWith` overrides / `InvokeRepeating` handlers with no
  decompiled body) and **Lasers/Swords/Melee** (all 9 - every damage tick is a `Physics2D.BoxCast`
  beam with the scale/reflect/chain math in register-only `FUN_` helpers).
- The earlier "weapons mostly blocked" call was **too pessimistic about guns**: it judged 4 specific
  *mechanics*, but the gun *fire-pattern family* has **15+ recoverable** classes (spread/fan/burst/
  charge/heat formulas survive Ghidra; only the bullet spawn is owner-side).

## Proposed port backlog (priority order)

| Wave | Theme | Modules | Effort | Value |
|---|---|---|---|---|
| **E** | Pet / NPC / Summon allies (RGEController AI) | RGPetController, NpcMercenaryController, NpcSummon01, RGBatteryController | medium | **highest** |
| **F** | New gun fire-pattern *shapes* | Gun016, Gun019, Gun007, Gun004, Gun002, Gun012, Gun014, Gun018 | medium | high |
| **G** | Single-shot / secondary spread guns + drone parent | Gun001, Gun009, Gun013, Gun011, Gun017, Gun006Paw | small | medium |
| **H** | Spawn-pattern bullet-trigger math | RGBDelayDivision, RGBulletTrigger, RGBTDivision, RGBTRebound | small | medium |
| **I** | CombatStats *deepening* (fold-in, no new classes) | RoleAttributePlayer regen tickers, RoleAttribute speed, RGWeapon/RGEWeapon default-stat tables | small | medium |
| **J** | World grid-gen + interactables (capped by known external block) | RGRoomX (deepen), RGAisle.CreateFloor, RGBox.Hit, ItemWishingWell, ItemRoomEgg | large | medium |
| **K** | Thin bullet motion accumulators (defer/optional) | Bullet03, BulletParabola, BulletBoom, BulletFollow, BulletLaterFixedTarget, BulletColor | small | low |

### Highest-value targets (port first)
- **RGPetController** - the un-ported BASE class of the already-shipped Wolf/Snowman pets. 3 fully
  recoverable bodies (FixedUpdate velocity/decel SM, ReplyingHP heal `maxHP/5`-per-interval regen,
  TurnTo `Vector2.Reflect` bounce). Completes a shipped family; reuses the proven RGEController offsets.
- **NpcMercenaryController** - richest ally AI in the backlog: FixedUpdate knockback-decel + three
  `Random.Range(0,10)<8`-gated shoot/run reflections with weapon-type branches and Invoke-cadence
  formulas (`base + itemLevel*0.25`, `base*(itemLevel*0.3+1)`). Mirrors the proven Wolf RNG pattern.
- **Gun016** - heat/spin-up minigun, a *growing-spread* shape no ported gun has:
  `spread = Max(0, recoil_base + (shoot_time/shoot_max_time)*max_deviation)` + symmetric RGRandom scatter.
- **Gun019** - cleanest self-contained burst: counter/Invoke-cadence loop + per-shot scatter in one body.
- **Gun007** - charge cannon richer than the ported Gun005: `bulletCount = Max(1, FloorToInt(maxCount *
  Min(1, charge)))`, 0.6/1.0 thresholds, charge-scaled muzzle pos, start..end size lerp.

## Confirmed BLOCKED / DEAD (do not attempt - fabrication risk)

- **All Buffs** (BuffChangeSpeed/Fire/Ice/EffectTrigger/RandomEffectTrigger, RG/Sword/Arrow*Buff*) -
  logic in stripped `TriggerWith` overrides; every present body is a return-0 stub or owner tail.
- **All Lasers/Swords/Melee** (RGLaser, RGELaser, RGShortLaser, RGChain, RGSword, *Trigger) - beam
  raycasts with math in `unaff_`-register-only `FUN_` helpers. (Matches Wave D's blocked laser/bow/
  thunder/rebound.)
- **Homing/seek bullets** (Bullet02, Bullet02AngleLimit, BulletBeetle, BulletBat, BulletThunder,
  BulletFollow-seek) - retarget/angle-clamp/chain-hop in `<>c__Iterator0__MoveNext` jumptable
  coroutines; FindTarget is an owner raycast.
- **Owner-only bullets** (Bullet01/04/05, BulletBottle, BulletEPoly, BulletPolymerization,
  BulletCoinExplode, BulletFire, BulletGas, the light family, RGSBullet01/02, BulletImmediately) -
  FixedUpdate is the stamped owner active+rigidbody gate; no surviving math.
- **Hands & mounts** (RGHand, RGEHand, RGBaseHand, RGMountController, PetHand, PetSword, PetGun003) -
  pure owner adapters (null-guard + vtable-forward to the equipped weapon).
- **Most bullet-triggers** (RGBTCreate/Energy/Explode/DelayCreate, RGBulletEffectTrigger,
  RGBulletTriggerWall, RGBulletBuffTrigger) - exploded-bool guard + owner Instantiate / coroutine spawn.
- **Interactables owner/net set** (RGChest*, pots, RGCoin, RGItem, RGDoor, RGTransferGate, RGDrill,
  RGContainer, RGStartRoomEvent, RGBigRoom, ItemEggMachine, ItemGem) - NetController authority gates +
  serialized-field magnitudes; loot rolls are one-line RGRandom index draws (no behavior worth a port).
  Note: **RGGetPath is a path-string cache, NOT pathfinding.**

## Recommendation

Port **Wave E (pet/NPC allies)** and **Wave F (new gun shapes)** next - highest gameplay value, lowest
blocker-risk, both reuse the proven RGEController + gun-scatter patterns. Waves G-I are cheap follow-on
batches; J is the only large one (and it's capped by the same external block as the RoomGen size-roll).
Skip the DEAD categories entirely.
