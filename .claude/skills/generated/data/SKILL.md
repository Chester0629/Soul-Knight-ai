---
name: data
description: "Skill for the Data area of Soul-Knight-ai. 37 symbols across 8 files."
---

# Data

37 symbols | 8 files | Cohesion: 87%

## When to Use

- Working with code in `include/`
- Understanding how TEST, TEST, TEST work
- Modifying data-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `include/data/GameData.hpp` | FindWeapon, FindBullet, FindEnemy, FindEnemyGun, FindBuff (+8) |
| `src/data/RGRandom.cpp` | InitState, NextState, NextFloat01, EnsureSeeded, SetRandomSeed (+1) |
| `src/data/GameData.cpp` | GetI, GetF, GetS, LoadAll, FindWeapon (+1) |
| `src/data/LootTable.cpp` | GetI, ReadDropId, LoadAll, GetTier, Roll |
| `include/data/LootTable.hpp` | LoadAll, Roll, GetTier, TierCount |
| `test/GameDataTest.cpp` | TEST |
| `test/LootTableTest.cpp` | TEST |
| `test/LootIntegrationTest.cpp` | TEST |

## Entry Points

Start here when exploring this area:

- **`TEST`** (Function) — `test/GameDataTest.cpp:13`
- **`TEST`** (Function) — `test/LootTableTest.cpp:21`
- **`TEST`** (Function) — `test/LootIntegrationTest.cpp:17`
- **`FindWeapon`** (Method) — `include/data/GameData.hpp:134`
- **`FindBullet`** (Method) — `include/data/GameData.hpp:148`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `TEST` | Function | `test/GameDataTest.cpp` | 13 |
| `TEST` | Function | `test/LootTableTest.cpp` | 21 |
| `TEST` | Function | `test/LootIntegrationTest.cpp` | 17 |
| `FindWeapon` | Method | `include/data/GameData.hpp` | 134 |
| `FindBullet` | Method | `include/data/GameData.hpp` | 148 |
| `FindEnemy` | Method | `include/data/GameData.hpp` | 149 |
| `FindEnemyGun` | Method | `include/data/GameData.hpp` | 150 |
| `FindBuff` | Method | `include/data/GameData.hpp` | 151 |
| `PlayerTemplate` | Method | `include/data/GameData.hpp` | 152 |
| `Bullets` | Method | `include/data/GameData.hpp` | 155 |
| `Enemies` | Method | `include/data/GameData.hpp` | 156 |
| `EnemyGuns` | Method | `include/data/GameData.hpp` | 157 |
| `Buffs` | Method | `include/data/GameData.hpp` | 158 |
| `InitState` | Method | `src/data/RGRandom.cpp` | 28 |
| `NextState` | Method | `src/data/RGRandom.cpp` | 42 |
| `NextFloat01` | Method | `src/data/RGRandom.cpp` | 53 |
| `EnsureSeeded` | Method | `src/data/RGRandom.cpp` | 59 |
| `SetRandomSeed` | Method | `src/data/RGRandom.cpp` | 71 |
| `Range` | Method | `src/data/RGRandom.cpp` | 77 |
| `LoadAll` | Method | `include/data/LootTable.hpp` | 62 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `Range → InitState` | intra_community | 4 |
| `Range → NextState` | intra_community | 3 |

## How to Explore

1. `gitnexus_context({name: "TEST"})` — see callers and callees
2. `gitnexus_query({query: "data"})` — find related execution flows
3. Read key files listed above for implementation details
