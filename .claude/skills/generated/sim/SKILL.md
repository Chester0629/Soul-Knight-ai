---
name: sim
description: "Skill for the Sim area of Soul-Knight-ai. 52 symbols across 16 files."
---

# Sim

52 symbols | 16 files | Cohesion: 88%

## When to Use

- Working with code in `src/`
- Understanding how Normalize, TEST, RotateDeg work
- Modifying sim-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `src/sim/Simulation.cpp` | MoveControllers, WakeByRoom, TickWeapon, EmitAttackEvents, DrainFireIntents (+9) |
| `include/sim/Scheduler.hpp` | SecondsToTicks, Invoke, InvokeRepeating, Cancel, Tick (+1) |
| `src/sim/BossController.cpp` | ChaseDir, OnShootTick, OnWanderTick, Activate, BossController |
| `src/sim/BrainFactory.cpp` | EnemyParams, MakeEnemy, MakeEnemyPtr, MakeBoss, MakeWeapon |
| `src/sim/EnemyController.cpp` | OnScoutTick, OnShootTick, Activate, EnemyController |
| `include/sim/BrainFactory.hpp` | MakeEnemy, MakeBoss, MakeWeapon, MakeEnemyPtr |
| `include/sim/SimMath.hpp` | Normalize, RotateDeg, CirclesOverlap |
| `src/sim/FireSystem.cpp` | MakeBullet, Expand |
| `include/sim/FixedClock.hpp` | Advance, RemainderMs |
| `test/BrainFactoryTest.cpp` | TEST |

## Entry Points

Start here when exploring this area:

- **`Normalize`** (Function) — `include/sim/SimMath.hpp:21`
- **`TEST`** (Function) — `test/BrainFactoryTest.cpp:16`
- **`RotateDeg`** (Function) — `include/sim/SimMath.hpp:13`
- **`CirclesOverlap`** (Function) — `include/sim/SimMath.hpp:28`
- **`TEST`** (Function) — `test/SimMathTest.cpp:11`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `Normalize` | Function | `include/sim/SimMath.hpp` | 21 |
| `TEST` | Function | `test/BrainFactoryTest.cpp` | 16 |
| `RotateDeg` | Function | `include/sim/SimMath.hpp` | 13 |
| `CirclesOverlap` | Function | `include/sim/SimMath.hpp` | 28 |
| `TEST` | Function | `test/SimMathTest.cpp` | 11 |
| `TEST` | Function | `test/SchedulerTest.cpp` | 10 |
| `TEST` | Function | `test/FixedClockTest.cpp` | 9 |
| `TEST` | Function | `test/SimPodsTest.cpp` | 12 |
| `SecondsToTicks` | Method | `include/sim/Scheduler.hpp` | 21 |
| `Invoke` | Method | `include/sim/Scheduler.hpp` | 25 |
| `InvokeRepeating` | Method | `include/sim/Scheduler.hpp` | 29 |
| `ChaseDir` | Method | `src/sim/BossController.cpp` | 20 |
| `OnShootTick` | Method | `src/sim/BossController.cpp` | 32 |
| `OnWanderTick` | Method | `src/sim/BossController.cpp` | 55 |
| `Activate` | Method | `src/sim/BossController.cpp` | 68 |
| `OnScoutTick` | Method | `src/sim/EnemyController.cpp` | 55 |
| `OnShootTick` | Method | `src/sim/EnemyController.cpp` | 65 |
| `Activate` | Method | `src/sim/EnemyController.cpp` | 91 |
| `MoveControllers` | Method | `src/sim/Simulation.cpp` | 137 |
| `WakeByRoom` | Method | `src/sim/Simulation.cpp` | 53 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `Advance → SkillCd` | cross_community | 5 |
| `Advance → CooldownRemaining` | cross_community | 5 |
| `Advance → Normalize` | cross_community | 4 |
| `Advance → TickWeapon` | intra_community | 3 |
| `Advance → EmitAttackEvents` | intra_community | 3 |

## Connected Areas

| Area | Connections |
|------|-------------|
| Combat | 2 calls |

## How to Explore

1. `gitnexus_context({name: "Normalize"})` — see callers and callees
2. `gitnexus_query({query: "sim"})` — find related execution flows
3. Read key files listed above for implementation details
