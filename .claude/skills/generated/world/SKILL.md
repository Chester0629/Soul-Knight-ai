---
name: world
description: "Skill for the World area of Soul-Knight-ai. 71 symbols across 20 files."
---

# World

71 symbols | 20 files | Cohesion: 92%

## When to Use

- Working with code in `src/`
- Understanding how TEST, WeaponEnergyCost, floor work
- Modifying world-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `src/world/RoomGen.cpp` | Get, Set, At, SetUpRoom, CreateAisle (+8) |
| `src/world/RGMaze.cpp` | NewNode, Walkable, FindPath, FindMinFPoint, SurrroundPoints (+3) |
| `include/world/RGRoomX.hpp` | State, DoorOpen, CloseDoor, OpenDoor, RoomType (+2) |
| `include/world/ItemWishingWell.hpp` | SetSeed, TriggerCount, Trigger, ClassifyFail, NextStep (+1) |
| `include/world/RGBox.hpp` | SetSeed, SetHp, Hp, SourceObject, SetSourceObject (+1) |
| `include/world/RGAisle.hpp` | SetSeed, GridDims, BuildFloorEdges, RollAisleWall, FloorDrawsAreOwnerSide |
| `src/world/Room.cpp` | Room, IsSolidCell, CellToWorld, FromRoomGen |
| `src/world/MapManager.cpp` | MapManager, HasRoom, RoomType, Connected |
| `test/RGAisleTest.cpp` | ClassifyCell, TEST |
| `src/world/RGAisle.cpp` | Index, BuildFloorEdges |

## Entry Points

Start here when exploring this area:

- **`TEST`** (Function) — `PTSD/test/CollisionTest.cpp:18`
- **`WeaponEnergyCost`** (Function) — `include/data/GameData.hpp:46`
- **`floor`** (Function) — `src/scenes/GameScene.cpp:96`
- **`TEST`** (Function) — `test/ItemWishingWellTest.cpp:14`
- **`TEST`** (Function) — `test/RGAisleTest.cpp:45`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `TEST` | Function | `PTSD/test/CollisionTest.cpp` | 18 |
| `WeaponEnergyCost` | Function | `include/data/GameData.hpp` | 46 |
| `floor` | Function | `src/scenes/GameScene.cpp` | 96 |
| `TEST` | Function | `test/ItemWishingWellTest.cpp` | 14 |
| `TEST` | Function | `test/RGAisleTest.cpp` | 45 |
| `TEST` | Function | `test/RGBoxTest.cpp` | 12 |
| `TEST` | Function | `test/RGRoomXTest.cpp` | 13 |
| `TEST` | Function | `test/MapManagerTest.cpp` | 24 |
| `reward` | Function | `test/RGRoomXTest.cpp` | 22 |
| `Get` | Method | `src/world/RoomGen.cpp` | 90 |
| `Set` | Method | `src/world/RoomGen.cpp` | 97 |
| `At` | Method | `src/world/RoomGen.cpp` | 104 |
| `SetUpRoom` | Method | `src/world/RoomGen.cpp` | 112 |
| `CreateAisle` | Method | `src/world/RoomGen.cpp` | 143 |
| `CreateFloor` | Method | `src/world/RoomGen.cpp` | 203 |
| `CreateWall` | Method | `src/world/RoomGen.cpp` | 284 |
| `IsWallIntersect` | Method | `src/world/RoomGen.cpp` | 311 |
| `StampBigObstacle` | Method | `src/world/RoomGen.cpp` | 329 |
| `CreateObstacle` | Method | `src/world/RoomGen.cpp` | 361 |
| `Room` | Method | `src/world/Room.cpp` | 6 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `TEST → Index` | cross_community | 5 |
| `TEST → Set` | cross_community | 5 |
| `TEST → CreateFloor` | cross_community | 4 |
| `TEST → CreateWall` | cross_community | 4 |
| `CreateObstacle → Get` | intra_community | 3 |
| `CreateObstacle → Set` | intra_community | 3 |
| `Connected → Index` | intra_community | 3 |
| `TEST → SizeFromIndex` | intra_community | 3 |
| `TEST → ComputeBaseLevel` | intra_community | 3 |
| `FindPath → Walkable` | intra_community | 3 |

## Connected Areas

| Area | Connections |
|------|-------------|
| Entities | 14 calls |

## How to Explore

1. `gitnexus_context({name: "TEST"})` — see callers and callees
2. `gitnexus_query({query: "world"})` — find related execution flows
3. Read key files listed above for implementation details
