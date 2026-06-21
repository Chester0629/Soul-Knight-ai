---
name: physics
description: "Skill for the Physics area of Soul-Knight-ai. 10 symbols across 1 files."
---

# Physics

10 symbols | 1 files | Cohesion: 100%

## When to Use

- Working with code in `PTSD/`
- Understanding how CellRange, InsertIntoGrid, RemoveFromGrid work
- Modifying physics-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `PTSD/src/Physics/CollisionWorld.cpp` | CellRange, InsertIntoGrid, RemoveFromGrid, Add, Update (+5) |

## Entry Points

Start here when exploring this area:

- **`CellRange`** (Method) — `PTSD/src/Physics/CollisionWorld.cpp:13`
- **`InsertIntoGrid`** (Method) — `PTSD/src/Physics/CollisionWorld.cpp:23`
- **`RemoveFromGrid`** (Method) — `PTSD/src/Physics/CollisionWorld.cpp:38`
- **`Add`** (Method) — `PTSD/src/Physics/CollisionWorld.cpp:62`
- **`Update`** (Method) — `PTSD/src/Physics/CollisionWorld.cpp:74`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `CellRange` | Method | `PTSD/src/Physics/CollisionWorld.cpp` | 13 |
| `InsertIntoGrid` | Method | `PTSD/src/Physics/CollisionWorld.cpp` | 23 |
| `RemoveFromGrid` | Method | `PTSD/src/Physics/CollisionWorld.cpp` | 38 |
| `Add` | Method | `PTSD/src/Physics/CollisionWorld.cpp` | 62 |
| `Update` | Method | `PTSD/src/Physics/CollisionWorld.cpp` | 74 |
| `Remove` | Method | `PTSD/src/Physics/CollisionWorld.cpp` | 84 |
| `Query` | Method | `PTSD/src/Physics/CollisionWorld.cpp` | 103 |
| `Raycast` | Method | `PTSD/src/Physics/CollisionWorld.cpp` | 265 |
| `RayAABB` | Function | `PTSD/src/Physics/CollisionWorld.cpp` | 188 |
| `RayCircle` | Function | `PTSD/src/Physics/CollisionWorld.cpp` | 232 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `Update → CellRange` | intra_community | 3 |

## How to Explore

1. `gitnexus_context({name: "CellRange"})` — see callers and callees
2. `gitnexus_query({query: "physics"})` — find related execution flows
3. Read key files listed above for implementation details
