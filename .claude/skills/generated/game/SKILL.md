---
name: game
description: "Skill for the Game area of Soul-Knight-ai. 15 symbols across 4 files."
---

# Game

15 symbols | 4 files | Cohesion: 88%

## When to Use

- Working with code in `include/`
- Understanding how PerFloorSeed, TEST, b work
- Modifying game-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `include/game/RunController.hpp` | ResetRunState, OnPlayerDied, AdvanceFloor, CurrentFloorSeed, State |
| `src/game/RunController.cpp` | BuildFloorScene, ResetRunState, StartRun, AdvanceFloor, OnFloorCleared |
| `test/RunLoopTest.cpp` | TEST, RoomPositions, b, View |
| `include/game/RunState.hpp` | PerFloorSeed |

## Entry Points

Start here when exploring this area:

- **`PerFloorSeed`** (Function) — `include/game/RunState.hpp:77`
- **`TEST`** (Function) — `test/RunLoopTest.cpp:40`
- **`b`** (Function) — `test/RunLoopTest.cpp:283`
- **`ResetRunState`** (Method) — `include/game/RunController.hpp:49`
- **`OnPlayerDied`** (Method) — `include/game/RunController.hpp:55`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `PerFloorSeed` | Function | `include/game/RunState.hpp` | 77 |
| `TEST` | Function | `test/RunLoopTest.cpp` | 40 |
| `b` | Function | `test/RunLoopTest.cpp` | 283 |
| `ResetRunState` | Method | `include/game/RunController.hpp` | 49 |
| `OnPlayerDied` | Method | `include/game/RunController.hpp` | 55 |
| `AdvanceFloor` | Method | `include/game/RunController.hpp` | 60 |
| `CurrentFloorSeed` | Method | `include/game/RunController.hpp` | 71 |
| `State` | Method | `include/game/RunController.hpp` | 79 |
| `BuildFloorScene` | Method | `src/game/RunController.cpp` | 9 |
| `ResetRunState` | Method | `src/game/RunController.cpp` | 14 |
| `StartRun` | Method | `src/game/RunController.cpp` | 22 |
| `AdvanceFloor` | Method | `src/game/RunController.cpp` | 42 |
| `OnFloorCleared` | Method | `src/game/RunController.cpp` | 48 |
| `RoomPositions` | Function | `test/RunLoopTest.cpp` | 249 |
| `View` | Function | `test/RunLoopTest.cpp` | 294 |

## Connected Areas

| Area | Connections |
|------|-------------|
| Data | 4 calls |
| Test | 1 calls |
| Scenes | 1 calls |

## How to Explore

1. `gitnexus_context({name: "PerFloorSeed"})` — see callers and callees
2. `gitnexus_query({query: "game"})` — find related execution flows
3. Read key files listed above for implementation details
