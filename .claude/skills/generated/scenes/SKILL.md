---
name: scenes
description: "Skill for the Scenes area of Soul-Knight-ai. 19 symbols across 7 files."
---

# Scenes

19 symbols | 7 files | Cohesion: 79%

## When to Use

- Working with code in `src/`
- Understanding how AllHostilesDead, FloorCleared, SetActiveViewMatrix work
- Modifying scenes-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `src/scenes/GameScene.cpp` | SyncBulletViews, RoomHasLiveHostile, Update, ConsumeSimEvents, Render (+4) |
| `src/scenes/EndScene.cpp` | MakeText, Build, Render |
| `include/game/FloorClear.hpp` | AllHostilesDead, FloorCleared |
| `include/scenes/GameScene.hpp` | Blocks, BlocksAny |
| `PTSD/include/Util/Input.hpp` | IsKeyPressed |
| `src/entities/Player.cpp` | Update |
| `PTSD/include/Util/TransformUtils.hpp` | SetActiveViewMatrix |

## Entry Points

Start here when exploring this area:

- **`AllHostilesDead`** (Function) — `include/game/FloorClear.hpp:24`
- **`FloorCleared`** (Function) — `include/game/FloorClear.hpp:45`
- **`SetActiveViewMatrix`** (Function) — `PTSD/include/Util/TransformUtils.hpp:35`
- **`root`** (Function) — `src/scenes/GameScene.cpp:626`
- **`IsKeyPressed`** (Method) — `PTSD/include/Util/Input.hpp:66`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `AllHostilesDead` | Function | `include/game/FloorClear.hpp` | 24 |
| `FloorCleared` | Function | `include/game/FloorClear.hpp` | 45 |
| `SetActiveViewMatrix` | Function | `PTSD/include/Util/TransformUtils.hpp` | 35 |
| `root` | Function | `src/scenes/GameScene.cpp` | 626 |
| `IsKeyPressed` | Method | `PTSD/include/Util/Input.hpp` | 66 |
| `Update` | Method | `src/entities/Player.cpp` | 33 |
| `SyncBulletViews` | Method | `src/scenes/GameScene.cpp` | 347 |
| `RoomHasLiveHostile` | Method | `src/scenes/GameScene.cpp` | 381 |
| `Update` | Method | `src/scenes/GameScene.cpp` | 396 |
| `ConsumeSimEvents` | Method | `src/scenes/GameScene.cpp` | 625 |
| `Build` | Method | `src/scenes/EndScene.cpp` | 33 |
| `Render` | Method | `src/scenes/EndScene.cpp` | 88 |
| `Render` | Method | `src/scenes/GameScene.cpp` | 675 |
| `SpawnEffect` | Method | `src/scenes/GameScene.cpp` | 658 |
| `Blocks` | Method | `include/scenes/GameScene.hpp` | 68 |
| `BlocksAny` | Method | `include/scenes/GameScene.hpp` | 73 |
| `MakeText` | Function | `src/scenes/EndScene.cpp` | 22 |
| `EffectFrames` | Function | `src/scenes/GameScene.cpp` | 52 |
| `PlaySfx` | Function | `src/scenes/GameScene.cpp` | 62 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `Render → MakeText` | intra_community | 3 |

## Connected Areas

| Area | Connections |
|------|-------------|
| Util | 4 calls |
| Entities | 1 calls |
| World | 1 calls |

## How to Explore

1. `gitnexus_context({name: "AllHostilesDead"})` — see callers and callees
2. `gitnexus_query({query: "scenes"})` — find related execution flows
3. Read key files listed above for implementation details
