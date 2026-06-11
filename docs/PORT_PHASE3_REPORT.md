# Phase 3 Core-Logic Port Report

Faithful port of reverse-engineered Soul Knight 1.7.10 logic into the C++ game,
driven by a 14-agent workflow (port + adversarial fidelity verify) and integrated
by hand. Source of truth: `D:\Soul Knight\_reverse` (typed decompilation
`ghidra_export/game_typed.c`, field map, `recreation/*.cs`) + `Resources/data/*.json`.

## What was ported (7 modules, all build-integrated + unit-tested)

| Module | Files | Faithful? | Notes |
|---|---|---|---|
| `Game::RGRandom` | `data/RGRandom.{hpp,cpp}` + test | logic yes / **PRNG unvalidated** | deterministic per-instance Unity-style RNG; the determinism root |
| `Game::RGMaze` (+`RGPoint`) | `world/RGMaze.{hpp,cpp}` + test | **yes (high)** | 4-dir A*, Manhattan H, 50-iter cap, faithful "bail-not-null" quirk reproduced |
| `Game::RoomGen` | `world/RoomGen.{hpp,cpp}` + test | yes (medium) | pure 41x41 grid generator: 3-pass floor, perimeter+doors, 2-phase obstacles |
| `Game::Combat` (damage) | `combat/Damage.{hpp,cpp}`, `combat/CombatStats.hpp` (additive) + test | **yes (high)** | crit roll, repel double-on-crit + caps (player 30 / enemy 28), player armor->hp vs enemy straight-hp, ice/co-op 0.5 factor |
| `Game::LootTable` | `data/LootTable.{hpp,cpp}` + test | **yes (high)** | weighted cumulative-subtraction roll over `droptables.json`, per-chest RGRandom |
| `Game::EnemyAI` (deepened) | `combat/EnemyAI.{hpp,cpp}` (additive) + test | yes (high) | faithful Scout/shoot cadence, knockback cap 28 + friction decay, wall reflection |
| `Game::WeaponInstance` (deepened) | `combat/WeaponInstance.{hpp,cpp}` (additive) + test | yes (high) | fire-rate/weapon_speed, energy cost, deviation-spread fire-plan, through_count |

## Validation gates (all GREEN)

- **Data**: `tools/validate_data.py` -> GREEN (0 errors, 3 known warnings).
- **Unit tests**: `ctest` -> **218/218 pass** (81 new: RGRandom golden+determinism, RGMaze A*, RoomGen determinism/doors/floor-list, Damage, Loot).
- **Build**: `SoulKnight.exe` + `SoulKnightTests.exe` link, warning-clean under MSVC `/W4`.

## Two adversarial-verify "bugs" were FALSE POSITIVES (confirmed against the binary)

- RoomGen `IsWallIntersect` "off-by-one": the binary (`game_full.c:428824`) loops
  `[rx-1, rx+rw+1] x [ry-1, ry+rh+1]` (countdown from `rw+3`/`rh+3`). The port matches; the
  reviewer miscounted the fragmented decompilation. **No change.**
- Damage "enemy crit divergence": `DamageSystem.cs` + `GetDamageFactor` confirm crit/repel
  are computed once on the attacker side; enemies take the already-final damage straight to HP.
  The port matches; defender-specific repel caps give the equivalent result. **No change.**

Only one real nit was fixed: `WeaponDef.angle` `int` -> `float` (RE models it as float).

---

# MANUAL INTERVENTION BOUNDARY (what needs you / decisions)

The faithful **pure-logic core** is done. Everything below needs a decision, an
asset/architecture choice, captured reference data, or per-content work.

## 1. RNG validation -- DONE (integer path bit-exact to Unity)
RESOLVED. `RGRandom` was validated against the reverse-engineered Unity 2017.4 reference
(macklinb gist) and a real bug was found + fixed:
- **Integer path VERIFIED bit-exact:** `InitState(1234)` -> first XORShift word `3463400838`
  -> `Range(0, INT_MAX)` `1315917191`, reproduced exactly (test `UnityParityInt1234`).
  InitState constant 1812433253 (`0x6C078965`), recurrence `s_{i+1}=K*s_i+1`, the 11/8/19
  xorshift triple, and `Range(int)=min + r%(max-min)` all confirmed. => RoomGen / Loot /
  enemy-scout / crit rolls (all int-driven) are Unity-faithful.
- **Float path FIXED:** was `(word & 0xFFFFFF)/2^24` (wrong); now Unity's
  `(uint32)(word<<9)/0xFFFFFFFF` with `Range(float)=(min-max)*value+max`. seed-7 golden
  regenerated; WeaponInstance spread re-verified (cone bounds + determinism hold).
- Goldens locked in `RGRandomTest.cpp` (seeds 12345, 21345, 1, 42, seed-7 float, + the
  Unity-parity 1234 vector). 220/220 tests green.
- **Residual (minor, optional):** the no-seed default init and exact float-max inclusivity are
  not confirmed against the shipped binary; immaterial to the all-int generation chain.

## 2. Scene + asset integration  (combat wiring DONE; dungeon/pickups remaining)

DONE -- faithful combat is wired into `GameScene` and verified (exe links, 220/220 green):
- **Damage:** both hit paths now go through `Combat::ResolveHit` (damage factor, crit roll,
  crit scaling, repel doubled-on-crit then capped 28/30) + `ApplyToEnemy` (straight HP) /
  `ApplyToPlayer` (armor->HP). `Bullet` carries repel/critical/canThrough/pierce.
- **Weapon:** `TryFirePlayerWeapon` uses `WeaponInstance::BuildFirePlan` -> N deviation-jittered
  bullets per pull (single/spread/shotgun), each with full attributes.
- **Enemy physics:** movement uses `EnemyAI::IntegrateVelocity` (decaying knockback fed by
  `GetForce` on bullet hit); the AI stream is seeded at spawn from the run seed.
- **Determinism:** `GameScene` owns an `RGRandom m_Rng` seeded from `m_RunSeed` (combat crit +
  enemy stream; dungeon will share it).
- New minor flags: piercing bullets can re-hit the same single target (no per-bullet hit-set
  yet); the player has no knockback physics (RGController dash/force is a #5 item); `kRepelScale`
  (=30) is a unit-reconciliation tunable (data repel ~1-3 -> saturates the cap).

DONE -- **Dungeon: grid -> world (single room).** `GameScene` now generates the room with
`RoomGen` (deterministic from `m_RunSeed`), builds collision via `Room::FromRoomGen` (every
non-walkable cell -> a `cellSize` AABB; walkable = floor 0 / aisle -2 / door 11), renders the
solid cells as `box01` tiles (scaled to the cell), and spawns the enemy on a `FloorList` cell so
it never starts inside a wall. Verified (warning-clean, exe links, 220/220 green).
- Visual note: all solid cells use `box01.png` for now (border + obstacles look uniform); a
  per-cell-code sprite mapping (distinct wall vs box vs decor, floor tiles) is a polish decision.

REMAINING (needs new modules + asset/design decisions):
- **Multi-room dungeon layout.** (2a DONE) `Game::MapManager` ports the random-walk room graph
  (deterministic, per-room `entrance[4]`, BFS connectivity, special/badass cap; 8 tests).
  (2b CORE DONE) `GameScene` now builds a full floor: a `MapManager` 7-room walk; each room
  generated by `RoomGen` with neighbour-facing doors, fixed 15x15 so rooms tile edge-to-edge
  and door gaps align; combined collision across all rooms (`BlocksAny`); all tiles rendered;
  one enemy per non-start room with the faithful combat/AI applied to every enemy; multi-enemy
  death handling. Verified (warning-clean, exe links, 228/228 green).
  (2b REMAINING) clear-room door gating (lock doors until a room's enemies die), active-room
  tracking + camera handling on transitions, and per-cell tile sprites (distinct wall/obstacle).
- **Loot/chests.** (DONE) `Chest` entities spawn in non-start rooms; on player proximity they
  open once, `LootTable::Roll(tier, m_Rng)` picks a drop, `GameData::ResolveDropWeapon` maps it
  to a real `WeaponDef`, a `WeaponPickup` drops, and walking over it swaps the player's weapon.
  3 loot-integration tests added (231 total green). NOTE: the exact weapon_NNN -> Gun* identity
  is the stand-in resolver pending #3; the loot DISTRIBUTION (tier + weights) is faithful.
- **Clear-room door gating.** (DONE) Each room's door-gap cells become colliders that seal while
  the player is inside a room with live enemies (`m_LockedRoom`), and open when it is cleared.
  Enemies are tagged with their room id.

#2 STATUS: COMPLETE for the playable dungeon loop (move -> multi-room with sealed combat rooms ->
clear -> loot -> equip -> proceed). Verified: warning-clean /W4, exe links, 231/231 tests.
Remaining are pure polish, not gameplay:
- **Per-cell-code tile sprites** (visual): distinct wall/obstacle/decor + a floor-tile layer
  instead of the single `box01`/`box02`/`weapons_9` placeholders.
- **Active-room camera snap** (optional): currently a smooth follow, which already works across rooms.
- **EnemyAI "awake" lifecycle**: the original gates AI on spawn-anim-complete; minor.

## 3. Data-pipeline gaps  (achievable parts DONE)
Correction to the earlier assumption: `build_gamedata.py`'s `build_table` already extracts
EVERY source field generically, so:
- **Weapon fan params ARE present.** 9 weapons (Gun002/004/007/008/012/014/019/GunHeroBow, ...)
  carry `count`+`angle`; the C++ `WeaponDef` loads them (`angle` fixed to float), so
  `BuildFirePlan` already produces real shotgun/multi-shot fans for those weapons. No pipeline
  change was needed; this works today.
- **`EnemyDef.kinematic` now loaded + wired** (DONE). The field was already in `enemies.json`
  (EnemyAI06=1); the C++ side now loads it and `GameScene` calls `EnemyAI::SetKinematic`, so
  turret-style enemies stay put + ignore knockback. 231 tests green.
- **REMAINING (documented hard gap):** the **136 unresolved bullet/audio PathID refs** (the
  gun->bullet PREFAB + audio linkage). Scalar bullet stats (speed/damage/repel) already extract
  cleanly and the fire-plan uses them; the unresolved refs are mostly the bullet *prefab*
  (visual sprite) and SFX linkage. Resolving them needs a cross-file Unity PathID map (objects
  not in the per-file `m_GameObject` index) and/or the IL2CPP RE track -- this is the gap
  HARNESS.md already calls out as hand-authored/RE-track, not a quick win. Gameplay is
  unaffected (stats are data-driven); it is a visual/audio fidelity item.

## 4. Unrecovered constants -- prerequisite met; deep validation still external
RNG validation (the prerequisite) is DONE (#1): the int stream is bit-exact to Unity, so a
captured real-game grid could now be reproduced exactly IF we had one. The remaining unknowns
below live in unrecovered Ghidra static data (`DAT_*`) or need captured game grids -- i.e. an
external-reference blocker (no public cross-check exists, unlike #1's gist reference). RoomGen
output is already determinism-locked by RoomGenTest; what is NOT yet validated against the
shipped game:
- RoomGen `ComputeBaseLevel` float weights {1,1.5,2}/{+0.5,+1} + thresholds 16/22 live in
  unrecovered static data (`DAT_0050F930`...); from the doc/C#, not byte-recovered.
- RoomGen size-roll / difficulty-roll **order** (highest-risk RNG ordering) is from the doc;
  the `SetRGRandomSeed @0x4FF350` body is inlined/truncated.
- Big/small obstacle placement loops are the C# reconstruction (count bands + render pass ARE
  verbatim from the binary). Capture a golden grid for a fixed seed and lock it in RoomGenTest.
- `critic_factor` (crit damage x) is a runtime bullet field; defaulted to 2.0 -- validate/override.

## 5. Per-content scope (Phase 4 -- behavior + data)

PROOF-OF-PATTERN DONE: `Game::BossAI01` (`combat/BossAI01.{hpp,cpp}` + 6 tests) ports the first
boss's content layer faithfully and unit-testably -- the single angry-phase transition at
hp/max_hp < 0.5 (halve shoot_cd, anim speed 1.2x, once), the `Range(0,100)` attack selection,
and the `Range(-1,1)` wander -- all on the deterministic RGRandom base. This is the template
for the rest: a small per-content logic class layered on the shared RGEController/RGRandom core,
with the animation-event bullet spawns wired by the owning entity.

DONE so far (4 faithful modules, all green): `BossAI01`, `BossAI04` (5-attack state machine +
angry windup), `BossAINian` (invisibility + damage-immunity window), and `PlayerDash` (hero
dash: skill_cd cooldown + in_skill_time window + GetForce impulse capped 30).

LIVE IN-GAME: a `Boss` entity (driven by `BossAI01`) now spawns in the floor's farthest room
(the boss room): it chases, fires a 3/5-bullet fan on its shoot cadence, enters the angry phase
at < 50% HP (halving the cadence), the room stays door-sealed until it dies, and player bullets
damage it through the faithful `Combat::ResolveHit` chain. This completes the vertical slice
(start -> sealed combat rooms -> boss room -> defeat -> loot). Verified: exe links, 281/281 tests.

REJECTION TALLY (the verify gate working): of 8 batch-ported content modules, 4 were REJECTED by
the adversarial fidelity pass for `fabrication_found` -- EnemyAI02/04 (invented keep-distance /
lunge mechanics) and PlayerCombat/PetController (invented melee-gate condition / attack-cooldown
state machine). Their real behavior was either ~= the base or not recoverable from the truncated
decomp. KEY SIGNAL for the bulk: most per-content bodies are truncated in the IL2CPP export, so
naive parallel porting yields ~50% faithful; the bulk needs per-method decomp RECOVERY (or
recreation .cs) first, with the adversarial gate mandatory on every module.

PROCESS NOTE (important): a parallel batch also attempted `EnemyAI02`/`EnemyAI04`, but the
adversarial fidelity pass REJECTED both -- lacking a recreation .cs and facing truncated
decompilation, the agents FABRICATED behavior (EnemyAI02 invented a keep-distance/can-shoot
system; EnemyAI04 invented a lunge/charge state machine) that has no basis in the binary; their
real behavior is ~= the already-ported base EnemyAI. They were discarded rather than shipped as
faithful. Lesson for the bulk: enemy/boss variants WITHOUT a recreation .cs need the decompiled
body actually recovered (many EnemyAINN FixedUpdate/Scout bodies are short and recoverable);
do NOT let an agent invent an archetype to fill a gap -- verify every module against the decomp.

The BULK remains (each is the same shape as BossAI01, repeated):
- **Enemies:** generic `RGEController` loop only. `EnemyAI01-15` specifics, multi-player
  nearest-target casting, exact `InvokeRepeating` initial delays -> TODO[verify].
- **Bosses:** `BossAI01-14` multi-phase patterns not ported.
- **Weapons:** exotic subclasses need dedicated ports -- Laser/ShortLaser (type 8/9,
  continuous sweep), Charge (type 10), Bow, summon/throw. Generic count/angle/deviation path
  covers single/spread/shotgun.
- **Characters/skills:** `C01-C13` controllers, dash (RGDash), active skills not ported.
