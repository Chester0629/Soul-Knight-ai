---
name: test
description: "Skill for the Test area of Soul-Knight-ai. 150 symbols across 34 files."
---

# Test

150 symbols | 34 files | Cohesion: 90%

## When to Use

- Working with code in `test/`
- Understanding how TEST, TEST, m work
- Modifying test-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `test/RealBodyDriver.hpp` | AddEnemy, AddBoss, EquipPlayer, SetPlayer, Blocks (+20) |
| `test/FloorConnectivityTest.cpp` | Walkable, BuildRoomSized, Floor1DesignPool, InteriorCell, Code (+7) |
| `PTSD/include/Core/Camera2D.hpp` | SetPosition, GetPosition, SetZoom, GetZoom, Follow (+6) |
| `PTSD/include/Core/SceneManager.hpp` | Push, Pop, Replace, Clear, Update (+4) |
| `PTSD/test/SceneManagerTest.cpp` | TEST, Update, OnEnter, OnExit, OnPause (+4) |
| `include/world/FloorBlock.hpp` | OffsetX, OffsetY, Build, At, ConnectedFloorCells (+2) |
| `test/BoxDestructionTest.cpp` | BreakableBoxWorld, Blocks, DamageObstacle, Alive, FirePlusX (+1) |
| `test/DoorSealTest.cpp` | MakeRoom, MakeRoomWH, BlockWorld, AddFlanks, BasePair (+1) |
| `test/DoorTransitionTest.cpp` | MakeCleanRoom, BlockWorld, AddFlanks, BlockedBy, WalkIntoRoom (+1) |
| `test/RoomGenGoldenTest.cpp` | Fnv1a, GridHash, FloorHash, ExplicitRoom, CheckGolden (+1) |

## Entry Points

Start here when exploring this area:

- **`TEST`** (Function) — `test/FloorBlockTest.cpp:75`
- **`TEST`** (Function) — `test/FloorConnectivityTest.cpp:192`
- **`m`** (Function) — `test/FloorConnectivityTest.cpp:343`
- **`TEST`** (Function) — `test/RoomGenDesignTest.cpp:54`
- **`TEST`** (Function) — `test/RealBodyDriverTest.cpp:40`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `WorldCollision` | Class | `include/sim/WorldCollision.hpp` | 9 |
| `NullWorldCollision` | Class | `include/sim/WorldCollision.hpp` | 23 |
| `RealBodyDriver` | Class | `test/RealBodyDriver.hpp` | 56 |
| `TEST` | Function | `test/FloorBlockTest.cpp` | 75 |
| `TEST` | Function | `test/FloorConnectivityTest.cpp` | 192 |
| `m` | Function | `test/FloorConnectivityTest.cpp` | 343 |
| `TEST` | Function | `test/RoomGenDesignTest.cpp` | 54 |
| `TEST` | Function | `test/RealBodyDriverTest.cpp` | 40 |
| `TEST` | Function | `PTSD/test/Camera2DTest.cpp` | 22 |
| `TEST` | Function | `PTSD/test/SceneManagerTest.cpp` | 47 |
| `TEST` | Function | `test/CorridorNeutralityTest.cpp` | 48 |
| `WeaponEnergyCost` | Function | `include/data/GameData.hpp` | 46 |
| `floor` | Function | `src/scenes/GameScene.cpp` | 131 |
| `TEST` | Function | `test/EnemyBrainDispatchTest.cpp` | 58 |
| `TEST` | Function | `test/BoxDestructionTest.cpp` | 61 |
| `TEST` | Function | `test/DoorSealTest.cpp` | 106 |
| `TEST` | Function | `test/DoorTransitionTest.cpp` | 201 |
| `TEST` | Function | `test/RoomGenGoldenTest.cpp` | 106 |
| `w` | Function | `test/WeaponInstanceTest.cpp` | 42 |
| `ws` | Function | `test/WeaponInstanceTest.cpp` | 75 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `TEST → Set` | cross_community | 6 |
| `M → Set` | cross_community | 5 |
| `M → CreateFloor` | cross_community | 5 |
| `M → CreateWall` | cross_community | 5 |
| `TEST → Set` | cross_community | 5 |
| `TEST → CreateFloor` | cross_community | 5 |
| `TEST → CreateWall` | cross_community | 5 |
| `TEST → SizeFromIndex` | cross_community | 5 |
| `TEST → ComputeBaseLevel` | cross_community | 5 |
| `TEST → Set` | cross_community | 5 |

## Connected Areas

| Area | Connections |
|------|-------------|
| World | 22 calls |
| Entities | 3 calls |

## How to Explore

1. `gitnexus_context({name: "TEST"})` — see callers and callees
2. `gitnexus_query({query: "test"})` — find related execution flows
3. Read key files listed above for implementation details
