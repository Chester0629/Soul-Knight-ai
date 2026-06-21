---
name: data
description: "Skill for the Data area of Soul-Knight-ai. 42 symbols across 9 files."
---

# Data

42 symbols | 9 files | Cohesion: 88%

## When to Use

- Working with code in `include/`
- Understanding how TEST, TEST, TEST work
- Modifying data-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `include/data/GameData.hpp` | FindWeapon, FindBullet, FindEnemy, FindEnemyGun, FindBuff (+11) |
| `src/data/RGRandom.cpp` | InitState, NextState, NextFloat01, EnsureSeeded, SetRandomSeed (+1) |
| `src/data/GameData.cpp` | GetI, GetF, GetS, LoadAll, FindWeapon (+1) |
| `src/data/LootTable.cpp` | GetI, ReadDropId, LoadAll, GetTier, Roll |
| `include/data/LootTable.hpp` | LoadAll, Roll, GetTier, TierCount |
| `test/DesignRoomConnectivityTest.cpp` | BuildDesign, TEST |
| `test/GameDataTest.cpp` | TEST |
| `test/LootIntegrationTest.cpp` | TEST |
| `test/LootTableTest.cpp` | TEST |

## Entry Points

Start here when exploring this area:

- **`TEST`** (Function) — `test/GameDataTest.cpp:13`
- **`TEST`** (Function) — `test/LootIntegrationTest.cpp:17`
- **`TEST`** (Function) — `test/LootTableTest.cpp:21`
- **`TEST`** (Function) — `test/DesignRoomConnectivityTest.cpp:45`
- **`FindWeapon`** (Method) — `include/data/GameData.hpp:163`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `TEST` | Function | `test/GameDataTest.cpp` | 13 |
| `TEST` | Function | `test/LootIntegrationTest.cpp` | 17 |
| `TEST` | Function | `test/LootTableTest.cpp` | 21 |
| `TEST` | Function | `test/DesignRoomConnectivityTest.cpp` | 45 |
| `FindWeapon` | Method | `include/data/GameData.hpp` | 163 |
| `FindBullet` | Method | `include/data/GameData.hpp` | 177 |
| `FindEnemy` | Method | `include/data/GameData.hpp` | 178 |
| `FindEnemyGun` | Method | `include/data/GameData.hpp` | 179 |
| `FindBuff` | Method | `include/data/GameData.hpp` | 180 |
| `PlayerTemplate` | Method | `include/data/GameData.hpp` | 181 |
| `Bullets` | Method | `include/data/GameData.hpp` | 184 |
| `Enemies` | Method | `include/data/GameData.hpp` | 185 |
| `EnemyGuns` | Method | `include/data/GameData.hpp` | 186 |
| `Buffs` | Method | `include/data/GameData.hpp` | 187 |
| `RoomLayouts` | Method | `include/data/GameData.hpp` | 190 |
| `FindDesignRoom` | Method | `include/data/GameData.hpp` | 194 |
| `ResolveDropWeapon` | Method | `include/data/GameData.hpp` | 175 |
| `Weapons` | Method | `include/data/GameData.hpp` | 183 |
| `LoadAll` | Method | `include/data/LootTable.hpp` | 62 |
| `Roll` | Method | `include/data/LootTable.hpp` | 76 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `Range → InitState` | intra_community | 4 |
| `Range → NextState` | intra_community | 3 |

## Connected Areas

| Area | Connections |
|------|-------------|
| Test | 2 calls |

## How to Explore

1. `gitnexus_context({name: "TEST"})` — see callers and callees
2. `gitnexus_query({query: "data"})` — find related execution flows
3. Read key files listed above for implementation details
