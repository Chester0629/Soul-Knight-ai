---
name: test
description: "Skill for the Test area of Soul-Knight-ai. 68 symbols across 20 files."
---

# Test

68 symbols | 20 files | Cohesion: 100%

## When to Use

- Working with code in `PTSD/`
- Understanding how TEST, TEST, TEST work
- Modifying test-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `PTSD/include/Core/Camera2D.hpp` | SetPosition, GetPosition, SetZoom, GetZoom, Follow (+6) |
| `PTSD/test/SceneManagerTest.cpp` | TEST, Update, TestScene, OnEnter, OnExit (+5) |
| `PTSD/include/Core/SceneManager.hpp` | Push, Pop, Replace, Clear, Update (+4) |
| `test/RoomGenGoldenTest.cpp` | Fnv1a, GridHash, FloorHash, ExplicitRoom, CheckGolden (+1) |
| `test/WeaponInstanceTest.cpp` | MakeWeapon, Seeded, Angle2D, w, ws |
| `test/SimulationTest.cpp` | RightWall, Idle, TEST |
| `test/RGMazeTest.cpp` | OpenGrid, SetWall, TEST |
| `PTSD/test/Camera2DTest.cpp` | ApplyView, TEST |
| `include/sim/WorldCollision.hpp` | WorldCollision, NullWorldCollision |
| `test/FireSystemTest.cpp` | AngleDeg, TEST |

## Entry Points

Start here when exploring this area:

- **`TEST`** (Function) — `PTSD/test/Camera2DTest.cpp:22`
- **`TEST`** (Function) — `PTSD/test/SceneManagerTest.cpp:47`
- **`TEST`** (Function) — `test/RoomGenGoldenTest.cpp:106`
- **`w`** (Function) — `test/WeaponInstanceTest.cpp:42`
- **`ws`** (Function) — `test/WeaponInstanceTest.cpp:75`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `Scene` | Class | `PTSD/include/Core/Scene.hpp` | 22 |
| `GameScene` | Class | `include/scenes/GameScene.hpp` | 49 |
| `WorldCollision` | Class | `include/sim/WorldCollision.hpp` | 9 |
| `NullWorldCollision` | Class | `include/sim/WorldCollision.hpp` | 16 |
| `TEST` | Function | `PTSD/test/Camera2DTest.cpp` | 22 |
| `TEST` | Function | `PTSD/test/SceneManagerTest.cpp` | 47 |
| `TEST` | Function | `test/RoomGenGoldenTest.cpp` | 106 |
| `w` | Function | `test/WeaponInstanceTest.cpp` | 42 |
| `ws` | Function | `test/WeaponInstanceTest.cpp` | 75 |
| `TEST` | Function | `test/FireSystemTest.cpp` | 21 |
| `TEST` | Function | `test/RGRoomXEndlessTest.cpp` | 27 |
| `TEST` | Function | `test/RGMazeTest.cpp` | 31 |
| `TEST` | Function | `test/EnemyAI07Test.cpp` | 17 |
| `TEST` | Function | `test/EnemyAITest.cpp` | 34 |
| `TEST` | Function | `test/EnemyControllerTest.cpp` | 27 |
| `TEST` | Function | `test/RoomGenTest.cpp` | 43 |
| `TEST` | Function | `test/SimulationTest.cpp` | 31 |
| `SetPosition` | Method | `PTSD/include/Core/Camera2D.hpp` | 26 |
| `GetPosition` | Method | `PTSD/include/Core/Camera2D.hpp` | 28 |
| `SetZoom` | Method | `PTSD/include/Core/Camera2D.hpp` | 31 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `TEST → Fnv1a` | intra_community | 4 |

## How to Explore

1. `gitnexus_context({name: "TEST"})` — see callers and callees
2. `gitnexus_query({query: "test"})` — find related execution flows
3. Read key files listed above for implementation details
