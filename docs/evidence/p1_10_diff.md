# P1.10 upversion diff report

Regenerates bullets / bosses / buffs / enemy_guns / droptables / weapon_catalog
from the 1.7.10 AssetRipper project (`EP = _reverse/asset soul/ExportedProject/Assets`)
instead of the 1.07 dump (`1.07/MonoBehaviour`, `1.07/TextAsset`), and closes T-27
(ex_orc05 missing from enemies.json). Source: `tools/build_gamedata.py`.

## Method

The 1.07 dump only ever had ONE row per AI/behaviour *class* (`BulletNN`,
`BossAINN`, `BuffXxx`, `EGunNNN` -- a shared script asset, never a real
per-instance object). The AssetRipper project resolves this by matching each
class's `<Class>.cs.meta` guid against every 1.7.10 prefab's MonoBehaviour
`m_Script` guid: the FIRST prefab (path order) that uses a class is that
class's real-data source. This keeps every class-keyed id existing C++
consumers/tests look up (`FindBullet("Bullet01")`, `FindEnemyGun("EGun001")`,
`FindBuff("BuffArmor")`) while sourcing the scalars from a real 1.7.10
instance instead of the class template. `bullets.json` additionally gets one
row PER REAL PREFAB (id = prefab stem) so weapon/boss bullet references
resolve to a real bullet.

Every record carries `_source`: `"1.7.10"` (resolved) or `"1.07"` (no 1.7.10
prefab uses that class, or the AssetRipper source is absent entirely -- the
whole table then falls back to the old `build_table()` path and `main()`
prints `FALLBACK: ...`). For the array-shaped tables (bullets/bosses/buffs/
enemy_guns/weapon_catalog) the marker is per-record; `droptables.json` keeps
its `{"0".."6": [...]}` shape (`LootTable::LoadAll` parses every top-level key
as a numeric tier), so the marker is per-item inside each tier instead of a
sibling dict key.

## Per-table outcome

| table | old count | new count | resolved 1.7.10 | notes |
|---|---|---|---|---|
| bullets.json | 25 | 203 | 178/256 rgprefab/bullet prefabs + 18/25 class rows resolved to a real 1.7.10 bullet prefab | 78 prefabs use bullet-ish classes outside the RGBullet family (BulletFire, BulletGas, ...) -- out of scope this pass; 7/25 class rows have no 1.7.10 bullet prefab and stay `_source: "1.07"` -- see the bullet class-row table below |
| bosses.json | 16 | 16 | 16/16 classes | every BossAI01-14/06Child/12Parent class resolves to a real prefab; BossAI14 -> `GameObject/boss14.prefab` (outside res/enemy/**, the seasonal Nian-adjacent boss) |
| buffs.json | 12 | 12 | 12/12 classes | all resolve: 6 under rgprefab/buff, 2 (\*EffectTrigger) under rgprefab/bullet, 4 (BuffStatueNN) under res/effect |
| enemy_guns.json | 10 | 10 | 10/10 classes | EGun007 has no 1.07 row either (dump never had it) and is absent from both |
| droptables.json | 7 tiers | 7 tiers | byte-identical | `EP/Resources/chest_data.txt` == `1.07/TextAsset/chest_data` (diff clean); only the source path + `_source` tag changed |
| weapon_catalog.json | 90 | 90 | byte-identical | `EP/Resources/weapon_introduce.txt` == `1.07/TextAsset/weapon_introduce` (diff clean); only the source path + `_source` tag changed |
| enemies.json (T-27) | 145 | 146 | +1 (ex_orc05) | `ex_orc05` lives at `GameObject/ex_orc05.prefab` (outside `res/enemy/**`, so it has no `enemies.csv` row); its EnemyAI01 + RoleAttribute components are read directly |

## Bullet class-row resolution (T-30 applied to bullets.json's own 25 class rows)

Codex's review of the initial P1.10 pass flagged that bullets.json's 25
CLASS-keyed rows (`Bullet01`, `Bullet02`, ... from the 1.07 dump) were the one
class-keyed table NOT resolved to a real 1.7.10 instance, even though
bosses.json/buffs.json/enemy_guns.json already used
`_first_prefab_by_guid()` (T-30) for their class rows. Fixed by applying the
identical strategy, scoped to `rgprefab/bullet/*.prefab` (path order) instead
of the whole AssetRipper project (a bullet class's real instance should be a
real bullet prefab, not an unrelated prefab reusing the same script) --
`_first_bullet_prefab_by_guid()` in `tools/build_gamedata.py`.

18/25 classes resolve; 7 have no 1.7.10 prefab under `rgprefab/bullet/` using
their script and stay the 1.07 row with `_source: "1.07"`.

| class | resolved | prefab | changed fields (1.07 -> 1.7.10) |
|---|---|---|---|
| Bullet01 | yes | `_bullet_4` | `can_rebound`: 0 -> 1 (speed/destroy_time/camp unchanged: 10/5/0 -- `Bullet01ScalarStats` still passes, no test change needed) |
| Bullet02 | yes | `_bullet_13` | (unchanged) |
| Bullet02Percentage | yes | `bullet_94` | (unchanged; modifier bullet, no speed/destroy_time fields) |
| Bullet03 | yes | `bullet_69` | (unchanged) |
| Bullet04 | yes | `_bullet_71` | `destroy_time`: 5.0 -> 0.75 |
| Bullet05 | yes | `_bullet_8` | `destroy_time`: 5.0 -> 6.66; `rotate_angle`: 0 -> 570 |
| BulletBat | yes | `bullet_bat` | (unchanged; modifier bullet) |
| BulletBeetle | yes | `_bullet_70` | (unchanged) |
| BulletBoom | yes | `bullet_boom` | (unchanged; modifier bullet) |
| BulletBottle | yes | `bullet_bottle_0` | (unchanged; modifier bullet) |
| BulletBuleLight | no | -- | stays 1.07 (`_source: "1.07"`) |
| BulletCoinExplode | no | -- | stays 1.07 (`_source: "1.07"`) |
| BulletColor | yes | `bullet_60` | (unchanged) |
| BulletEPoly | no | -- | stays 1.07 (`_source: "1.07"`) |
| BulletFire | no | -- | stays 1.07 (`_source: "1.07"`) |
| BulletFollow | yes | `bullet_44` | (unchanged) |
| BulletGas | no | -- | stays 1.07 (`_source: "1.07"`) |
| BulletGreenLight | no | -- | stays 1.07 (`_source: "1.07"`) |
| BulletImmediately | yes | `bullet_84` | (unchanged) |
| BulletLaterFixedTarget | yes | `bullet_72` | (unchanged) |
| BulletParabola | yes | `bullet_90` | `camp`: 1 -> 0; `rotate_angle`: 0 -> 17 |
| BulletPolymerization | no | -- | stays 1.07 (`_source: "1.07"`) |
| BulletReboundPercentage | yes | `bullet_23_rebound` | (unchanged; modifier bullet) |
| BulletRoundabout | yes | `bullet_80` | (unchanged) |
| BulletThunder | yes | `thunder` | (unchanged; modifier bullet) |

Counts: 18/25 resolved, 7/25 stayed 1.07. `test/GameDataTest.cpp`'s
`Bullet01ScalarStats` (speed 10 / destroy_time 5 / camp 0) still passes
unchanged -- `Bullet01`'s resolved prefab (`_bullet_4`) happens to carry the
same three pinned values; only `can_rebound` (not asserted) changed.

## Fields changed (real 1.7.10 values vs the 1.07 class template)

- `EnemyGunDef` (EGun001): `bulletSpeed` 10.0 -> 16.0 (atk/deviation unchanged: 3/5).
  Source: `GameObject/ex_orc05.prefab` (the first 1.7.10 prefab, path order,
  using EGun001) -- `test/GameDataTest.cpp` `EnemyGun001ScalarStats` updated.
- `BuffDef` (BuffStatue01): `speed` 24.0 -> 12.0, `atk` 4 -> 3, `count` 1 -> 4,
  `angle` 12 -> 45, `pierced` 1 -> 0, `cd` 1.0 -> 5.0. No pinning test existed
  for this row (bosses.json/buffs.json's per-field values aren't asserted
  outside `BuffArmorLoaded`, which is unaffected -- see below), so no test
  changes needed here.
- `BossDef`-shaped rows (not a real C++ struct -- bosses.json has no loader in
  `GameData.cpp`, only `tools/validate_data.py`'s count/shape gate) now carry
  real per-boss `bullet01..bullet06` (resolved bullet-prefab ids) where the
  1.07 dump could only keep the raw, un-cross-referenceable PathID.
- `BulletDef`/`BuffDef` (BuffArmor, Bullet01 et al.): values that happen to be
  identical between the 1.07 template and their first-resolved 1.7.10
  instance are UNCHANGED (e.g. `Bullet01`: speed 10 / destroy_time 5 / camp 0;
  `BuffArmor`: is_enemy 0 / buff_time 6) -- no test changes needed for those.
- `EnemyDef` (`ex_orc05`, new row): `enemy_class` EnemyAI01, `ai_level` 3,
  `shoot_cd` 3.0, `friction` 0.7, `scout_rate` 1.0, `consume` 2, `e_size` 2,
  `reward_rate` 100, `reward_value` [0,0,4,3], `buff_immune` 1.0, `kinematic`
  0, `max_hp`/`hp` 50, `speed` 4.0, `speed_rate` 0.0, `camp` 1, `critical` 0,
  `deviation` 0, `atk` 0 (no `damage` field on this prefab's EnemyAI
  component). `test/EnemyStatsTest.cpp` and `test/GameDataTest.cpp` bumped
  `Enemies().size()` 145u -> 146u.

## 5 fixed samples (source path:field -> value)

1. `GameObject/ex_orc05.prefab:EGun001.bullet_speed` -> `16` (1.07
   `EGun001.json` had `10.0`)
2. `res/effect/buff_statue_1.prefab:BuffStatue01.speed` -> `12` (1.07
   `BuffStatue01.json` had `24.0`)
3. `res/effect/buff_statue_1.prefab:BuffStatue01.count` -> `4` (1.07
   `BuffStatue01.json` had `1`)
4. `res/enemy/1-forest/boss07.prefab:BossAI07.bullet01` -> `bullet_e_24`
   (guid-resolved via the AssetRipper project index; the 1.07 dump only
   carried the raw PathID with no crosswalk to a bullet class id)
5. `GameObject/ex_orc05.prefab:RoleAttribute.max_hp` -> `50` (new row; T-27 --
   this species previously had no `enemies.json` entry at all, so level pools
   referencing it silently substituted the EnemyAI01-class representative)

## Section 1.2 style samples (for MASTER_PLAN readers, not written into that file)

- bullets: `rgprefab/bullet/bullet_0.prefab:RGBullet.destroy_time` -> `5`
  (same field name/shape as the 1.07 `BulletNN.json` dump; matches
  `test/GameDataTest.cpp`'s `Bullet01ScalarStats`, since `Bullet01`'s row is
  kept 1.07-sourced verbatim -- see "no test changes needed" above).
- bosses: `res/enemy/3-volcano/boss12_1.prefab` and
  `res/enemy/3-volcano/boss12_2.prefab` both use `BossAI12` (two real
  instances, one class row -- the first, `boss12_1`, wins by path order).
- buffs: `rgprefab/bullet/bullet_95.prefab:BuffEffectTrigger.probability` ->
  `0` (this "buff" class is carried by a *bullet* prefab, not one under
  `rgprefab/buff/` -- a real 1.7.10 fact, not a bug in the resolver).
- enemy_guns: `res/enemy/e_battery01.prefab:EGun008.atk` -> `5`.
- droptables/weapon_catalog: `EP/Resources/chest_data.txt` and
  `EP/Resources/weapon_introduce.txt` are byte-identical to their `1.07/
  TextAsset/*` counterparts (`diff` clean) -- confirmed the "upversion" here
  is a source-path switch with no content drift, not a re-parse of changed
  data.

## Verification

- `python tools/build_gamedata.py` -- all six tables + enemies.json rebuilt,
  no FALLBACK lines printed (AssetRipper source present).
- `python tools/validate_data.py` -- `GATE: GREEN -- 0 errors, 6 warning(s)`.
  New warnings (all expected/documented, not regressions): 59 weapons + 18
  boss bullet REFERENCES (14 distinct boss.bulletNN entries across 16 bosses,
  some bosses carrying more than one) point at prefabs outside bullets.json
  (Gas/item effects, another weapon prefab used as a thrown projectile, or
  one of the 78 non-RGBullet-family bullet variants -- real 1.7.10 data, out
  of this pass's scope). T-31 (Codex review): these are no longer a blanket
  downgrade -- every such reference id is checked against the explicit
  `KNOWN_UNRESOLVED_BULLET_REFS` allow-list in `tools/validate_data.py`
  (47 ids); anything in the list stays a WARN, anything else is now a hard
  FAIL, so a newly-broken reference cannot slip through as one of these
  warnings any more.
- `python -m unittest discover -s tools/tests -v` -- `tools/tests/test_validate_data.py`
  (new, T-31 boundary test): 3/3 pass -- the real, unmodified data yields 0
  errors, and injecting a bogus `bullet_prefab`/`bullet01` reference (not in
  `KNOWN_UNRESOLVED_BULLET_REFS`) is reported as an error.
- `cmake --build build --config Debug --target SoulKnightTests` -- 0 errors.
- `ctest --test-dir build -C Debug -E Tests_NOT_BUILT` -- **100% tests
  passed, 0 tests failed out of 2016** (unchanged from baseline; two test
  files touched for the count/value changes above).
- `cmake --build build --config Debug --target SoulKnight` -- 0 errors.
- `SK_SKIP_MENU=1 SK_FORCE_KILL=120 SK_MAX_FRAMES=300 ./build/Debug/SoulKnight.exe`
  -- ran a run to completion; `Reward: ...` lines printed for `e_miner02`,
  `ex_miner03`, `e_yeti01` (x2), `e_miner01`; no `FALLBACK` or `error` lines.

## Files changed

- `tools/build_gamedata.py`: `build_bullet_table`, `build_boss_table`,
  `build_buff_table`, `build_enemy_gun_table`, `build_ex_orc05_row` (new);
  `_class_guid`/`_mono_block`/`_field`/`_first_prefab_by_guid` (new shared
  helpers); `parse_weapon_catalog`/`parse_droptables` (prefer the 1.7.10
  path); `build_enemy_species_table` (appends the ex_orc05 row); `main()`
  rewired to call the new builders with 1.07 fallback. **This pass (T-30
  applied to bullets.json's own class rows):** `_first_bullet_prefab_by_guid`
  (new, `rgprefab/bullet/`-scoped sibling of `_first_prefab_by_guid`);
  `build_bullet_table` rewritten to resolve the 25 1.07 class rows the same
  way bosses/buffs/enemy_guns already do (18/25 resolve, 7 stay 1.07) --
  resolved rows carry `_prefab` (which prefab won) alongside `_source`.
- `tools/validate_data.py`: loads `enemy_guns.json` (wasn't loaded before);
  `has_source()` gate for all six tables; bullet/boss cross-reference checks
  (weapon `bullet_prefab`, boss `bullet01-06`, enemy `enemy_class` ->
  bosses.json); T-27 `known_gaps` exemption removed (any unknown
  level-pool enemy id is now a hard failure). **This pass (T-31, Codex
  review):** `KNOWN_UNRESOLVED_BULLET_REFS` module-level allow-list (47 ids);
  the weapon/boss -> bullet cross-reference check now WARNs only for ids in
  that list and ERRORs for anything else; refactored into `load_all_tables()`
  + `run_checks(data) -> (errors, warnings)` (pure function of an
  already-loaded table dict) so `main()` is a thin load/print/exit wrapper
  and the checks are unit-testable without touching disk.
- `tools/tests/test_validate_data.py` (new): T-31 boundary test -- real data
  yields 0 errors; an injected bogus `bullet_prefab`/`bullet01` reference is
  reported as an error.
- `src/data/GameData.cpp`: one-line `// PORT CHOICE (T-30): class-keyed row =
  first 1.7.10 prefab of that class (P1.13 per-instance)` comment at
  `FindBullet`/`FindEnemyGun`/`FindBuff` (the class-keyed lookups this port
  choice affects). No `bullet_prefab`/bullet-resolution fallback exists yet
  in the C++ weapon/bullet path (`bullet_prefab` is only read by
  `tools/validate_data.py` today), so no T-31 tag was added there.
- `include/data/GameData.hpp`: `EnemyDef` doc comment count 145 -> 146.
- `test/GameDataTest.cpp`: `Bullets().size()` 25u -> 203u, `Enemies().size()`
  145u -> 146u, `EnemyGun001ScalarStats` `bulletSpeed` 10.0F -> 16.0F.
  `Bullet01ScalarStats` needed no change (`Bullet01`'s resolved prefab,
  `_bullet_4`, carries the same pinned speed/destroy_time/camp).
- `test/EnemyStatsTest.cpp`: `Enemies().size()` 145u -> 146u.
- `docs/evidence/p1_10_diff.md`: fixed the `BuffStatue01` sample path
  (`rgprefab/buff/` -> `res/effect/`, its real location), "18 boss" ->
  "18 boss bullet REFERENCES", added the bullet class-row resolution table
  and this pass's counts.
- `Resources/data/{bullets,bosses,buffs,enemy_guns,droptables,weapon_catalog,enemies}.json`:
  regenerated.

## Unresolved / out of scope

- 78 of 256 `rgprefab/bullet` prefabs use a bullet-ish class outside the
  RGBullet inheritance family (`BulletFire`, `BulletGas`, `BulletBoom`, ...)
  and aren't in `bullets.json`; weapons/bosses that reference them show up as
  WARN, not FAIL, in `validate_data.py`.
- A handful of weapon `bullet_prefab`/boss `bulletNN` refs point at prefabs
  outside `rgprefab/bullet` entirely (`rgprefab/item/Gas*.prefab`, another
  `res/weapon/*.prefab` used as a thrown projectile) -- real 1.7.10 game
  mechanics, not part of this pass's per-bullet extraction.
