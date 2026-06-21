---
name: world
description: "Skill for the World area of Soul-Knight-ai. 75 symbols across 20 files."
---

# World

75 symbols | 20 files | Cohesion: 90%

## When to Use

- Working with code in `src/`
- Understanding how TEST, TEST, TEST work
- Modifying world-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `src/world/RoomGen.cpp` | Get, Set, At, SetUpRoom, CreateAisle (+5) |
| `include/world/RGRoomX.hpp` | SetSeed, State, DoorOpen, CloseDoor, OpenDoor (+4) |
| `src/world/RGMaze.cpp` | NewNode, Walkable, FindPath, FindMinFPoint, SurrroundPoints (+3) |
| `include/world/ItemWishingWell.hpp` | SetSeed, TriggerCount, Trigger, ClassifyFail, NextStep (+1) |
| `src/world/MapManager.cpp` | MapManager, SelectDesignRooms, HasRoom, RoomType, Connected |
| `include/world/RGAisle.hpp` | SetSeed, GridDims, BuildFloorEdges, RollAisleWall, FloorDrawsAreOwnerSide |
| `src/world/Room.cpp` | Room, IsSolidCell, CellToWorld, FromRoomGen |
| `include/world/RGBox.hpp` | SetSeed, Hp, SourceObject, SetSourceObject |
| `src/world/FloorBlock.cpp` | OffsetX, OffsetY, CorridorStrip, Build |
| `src/world/RGRoomX.cpp` | CloseDoor, StartRoom, OpenDoor, ClearRoom |

## Entry Points

Start here when exploring this area:

- **`TEST`** (Function) — `test/MapManagerTest.cpp:27`
- **`TEST`** (Function) — `PTSD/test/CollisionTest.cpp:18`
- **`TEST`** (Function) — `test/ItemWishingWellTest.cpp:14`
- **`TEST`** (Function) — `test/RGAisleTest.cpp:45`
- **`TEST`** (Function) — `test/RGRoomXTest.cpp:13`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `TEST` | Function | `test/MapManagerTest.cpp` | 27 |
| `TEST` | Function | `PTSD/test/CollisionTest.cpp` | 18 |
| `TEST` | Function | `test/ItemWishingWellTest.cpp` | 14 |
| `TEST` | Function | `test/RGAisleTest.cpp` | 45 |
| `TEST` | Function | `test/RGRoomXTest.cpp` | 13 |
| `TEST` | Function | `test/RGBoxTest.cpp` | 12 |
| `reward` | Function | `test/RGRoomXTest.cpp` | 22 |
| `MapManager` | Method | `src/world/MapManager.cpp` | 21 |
| `SelectDesignRooms` | Method | `src/world/MapManager.cpp` | 113 |
| `HasRoom` | Method | `src/world/MapManager.cpp` | 168 |
| `RoomType` | Method | `src/world/MapManager.cpp` | 175 |
| `Connected` | Method | `src/world/MapManager.cpp` | 182 |
| `Index` | Method | `src/world/RGAisle.cpp` | 4 |
| `BuildFloorEdges` | Method | `src/world/RGAisle.cpp` | 37 |
| `Get` | Method | `src/world/RoomGen.cpp` | 96 |
| `Set` | Method | `src/world/RoomGen.cpp` | 103 |
| `At` | Method | `src/world/RoomGen.cpp` | 110 |
| `SetUpRoom` | Method | `src/world/RoomGen.cpp` | 118 |
| `CreateAisle` | Method | `src/world/RoomGen.cpp` | 149 |
| `CreateFloor` | Method | `src/world/RoomGen.cpp` | 209 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `TEST → Set` | cross_community | 6 |
| `M → Set` | cross_community | 5 |
| `M → CreateFloor` | cross_community | 5 |
| `M → CreateWall` | cross_community | 5 |
| `TEST → Index` | intra_community | 5 |
| `TEST → Set` | cross_community | 5 |
| `TEST → CreateFloor` | cross_community | 5 |
| `TEST → CreateWall` | cross_community | 5 |
| `TEST → Set` | cross_community | 5 |
| `TEST → CreateFloor` | cross_community | 5 |

## Connected Areas

| Area | Connections |
|------|-------------|
| Test | 26 calls |
| Entities | 14 calls |

## How to Explore

1. `gitnexus_context({name: "TEST"})` — see callers and callees
2. `gitnexus_query({query: "world"})` — find related execution flows
3. Read key files listed above for implementation details
