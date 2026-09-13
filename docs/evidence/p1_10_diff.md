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
| bullets.json | 25 | 203 | 178/256 rgprefab/bullet prefabs (+25 kept 1.07 class rows) | 78 prefabs use bullet-ish classes outside the RGBullet family (BulletFire, BulletGas, ...) -- out of scope this pass |
| bosses.json | 16 | 16 | 16/16 classes | every BossAI01-14/06Child/12Parent class resolves to a real prefab; BossAI14 -> `GameObject/boss14.prefab` (outside res/enemy/**, the seasonal Nian-adjacent boss) |
| buffs.json | 12 | 12 | 12/12 classes | all resolve: 6 under rgprefab/buff, 2 (\*EffectTrigger) under rgprefab/bullet, 4 (BuffStatueNN) under res/effect |
| enemy_guns.json | 10 | 10 | 10/10 classes | EGun007 has no 1.07 row either (dump never had it) and is absent from both |
| droptables.json | 7 tiers | 7 tiers | byte-identical | `EP/Resources/chest_data.txt` == `1.07/TextAsset/chest_data` (diff clean); only the source path + `_source` tag changed |
| weapon_catalog.json | 90 | 90 | byte-identical | `EP/Resources/weapon_introduce.txt` == `1.07/TextAsset/weapon_introduce` (diff clean); only the source path + `_source` tag changed |
| enemies.json (T-27) | 145 | 146 | +1 (ex_orc05) | `ex_orc05` lives at `GameObject/ex_orc05.prefab` (outside `res/enemy/**`, so it has no `enemies.csv` row); its EnemyAI01 + RoleAttribute components are read directly |

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
2. `rgprefab/buff/buff_statue_1.prefab:BuffStatue01.speed` -> `12` (1.07
   `BuffStatue01.json` had `24.0`)
3. `rgprefab/buff/buff_statue_1.prefab:BuffStatue01.count` -> `4` (1.07
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
  boss bullet refs point at prefabs outside bullets.json (Gas/item effects,
  another weapon prefab used as a thrown projectile, or one of the 78
  non-RGBullet-family bullet variants -- real 1.7.10 data, out of this pass's
  scope, downgraded from FAIL to WARN since it is not a data-loss regression).
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
  rewired to call the new builders with 1.07 fallback.
- `tools/validate_data.py`: loads `enemy_guns.json` (wasn't loaded before);
  `has_source()` gate for all six tables; bullet/boss cross-reference checks
  (weapon `bullet_prefab`, boss `bullet01-06`, enemy `enemy_class` ->
  bosses.json) as warnings; T-27 `known_gaps` exemption removed (any unknown
  level-pool enemy id is now a hard failure).
- `include/data/GameData.hpp`: `EnemyDef` doc comment count 145 -> 146.
- `test/GameDataTest.cpp`: `Bullets().size()` 25u -> 203u, `Enemies().size()`
  145u -> 146u, `EnemyGun001ScalarStats` `bulletSpeed` 10.0F -> 16.0F.
- `test/EnemyStatsTest.cpp`: `Enemies().size()` 145u -> 146u.
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
