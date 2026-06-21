---
name: scenes
description: "Skill for the Scenes area of Soul-Knight-ai. 22 symbols across 8 files."
---

# Scenes

22 symbols | 8 files | Cohesion: 80%

## When to Use

- Working with code in `src/`
- Understanding how AllHostilesDead, FloorCleared, SetActiveViewMatrix work
- Modifying scenes-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `src/scenes/GameScene.cpp` | BlocksAny, SyncBulletViews, RoomHasLiveHostile, Update, ConsumeSimEvents (+5) |
| `src/scenes/EndScene.cpp` | MakeText, Build, Render |
| `include/scenes/GameScene.hpp` | GameScene, Blocks, BlocksAny |
| `include/game/FloorClear.hpp` | AllHostilesDead, FloorCleared |
| `PTSD/include/Util/TransformUtils.hpp` | SetActiveViewMatrix |
| `PTSD/include/Core/Scene.hpp` | Scene |
| `PTSD/test/SceneManagerTest.cpp` | TestScene |
| `include/scenes/EndScene.hpp` | OnEnter |

## Entry Points

Start here when exploring this area:

- **`AllHostilesDead`** (Function) — `include/game/FloorClear.hpp:24`
- **`FloorCleared`** (Function) — `include/game/FloorClear.hpp:45`
- **`SetActiveViewMatrix`** (Function) — `PTSD/include/Util/TransformUtils.hpp:35`
- **`root`** (Function) — `src/scenes/GameScene.cpp:1021`
- **`Scene`** (Class) — `PTSD/include/Core/Scene.hpp:22`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `Scene` | Class | `PTSD/include/Core/Scene.hpp` | 22 |
| `GameScene` | Class | `include/scenes/GameScene.hpp` | 51 |
| `AllHostilesDead` | Function | `include/game/FloorClear.hpp` | 24 |
| `FloorCleared` | Function | `include/game/FloorClear.hpp` | 45 |
| `SetActiveViewMatrix` | Function | `PTSD/include/Util/TransformUtils.hpp` | 35 |
| `root` | Function | `src/scenes/GameScene.cpp` | 1021 |
| `BlocksAny` | Method | `src/scenes/GameScene.cpp` | 608 |
| `SyncBulletViews` | Method | `src/scenes/GameScene.cpp` | 683 |
| `RoomHasLiveHostile` | Method | `src/scenes/GameScene.cpp` | 717 |
| `Update` | Method | `src/scenes/GameScene.cpp` | 732 |
| `ConsumeSimEvents` | Method | `src/scenes/GameScene.cpp` | 1020 |
| `Build` | Method | `src/scenes/EndScene.cpp` | 33 |
| `Render` | Method | `src/scenes/EndScene.cpp` | 88 |
| `Render` | Method | `src/scenes/GameScene.cpp` | 1070 |
| `OnEnter` | Method | `include/scenes/EndScene.hpp` | 36 |
| `SpawnEffect` | Method | `src/scenes/GameScene.cpp` | 1053 |
| `Blocks` | Method | `include/scenes/GameScene.hpp` | 70 |
| `BlocksAny` | Method | `include/scenes/GameScene.hpp` | 78 |
| `TestScene` | Class | `PTSD/test/SceneManagerTest.cpp` | 16 |
| `MakeText` | Function | `src/scenes/EndScene.cpp` | 22 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `Render → MakeText` | intra_community | 3 |

## Connected Areas

| Area | Connections |
|------|-------------|
| Util | 5 calls |
| Entities | 3 calls |
| Test | 1 calls |

## How to Explore

1. `gitnexus_context({name: "AllHostilesDead"})` — see callers and callees
2. `gitnexus_query({query: "scenes"})` — find related execution flows
3. Read key files listed above for implementation details
