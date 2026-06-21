---
name: sim
description: "Skill for the Sim area of Soul-Knight-ai. 111 symbols across 21 files."
---

# Sim

111 symbols | 21 files | Cohesion: 94%

## When to Use

- Working with code in `include/`
- Understanding how BossFanCount, Normalize, TEST work
- Modifying sim-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `include/sim/BossBrainAdapters.hpp` | BossFanCount, AttackTick, AttackTick, AttackTick, ShootCd (+37) |
| `src/sim/Simulation.cpp` | MoveControllers, WakeByRoom, TickWeapon, EmitAttackEvents, DrainFireIntents (+9) |
| `include/sim/EnemyBrainAdapters.hpp` | CdShootAdapter, IntShootAdapter, MoveOnlyAdapter, EnemyAI01Adapter, EnemyAI02Adapter (+5) |
| `include/sim/Scheduler.hpp` | SecondsToTicks, Invoke, InvokeRepeating, Cancel, Tick (+1) |
| `src/sim/BrainFactory.cpp` | EnemyParams, MakeEnemy, MakeEnemyPtr, MakeBoss, MakeBossPtr (+1) |
| `src/sim/BossController.cpp` | ChaseDir, OnShootTick, OnWanderTick, Activate, BossController |
| `src/sim/EnemyController.cpp` | OnScoutTick, OnShootTick, Activate, EnemyController |
| `include/sim/BrainFactory.hpp` | MakeEnemy, MakeBoss, MakeWeapon, MakeBossPtr |
| `include/sim/SimMath.hpp` | Normalize, RotateDeg, CirclesOverlap |
| `include/sim/IBossBrain.hpp` | IBossBrain, BossBrainBase |

## Entry Points

Start here when exploring this area:

- **`BossFanCount`** (Function) — `include/sim/BossBrainAdapters.hpp:49`
- **`Normalize`** (Function) — `include/sim/SimMath.hpp:21`
- **`TEST`** (Function) — `test/BrainFactoryTest.cpp:18`
- **`MakeEnemyBrain`** (Function) — `include/sim/EnemyBrainAdapters.hpp:161`
- **`RotateDeg`** (Function) — `include/sim/SimMath.hpp:13`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `BossAI01Adapter` | Class | `include/sim/BossBrainAdapters.hpp` | 57 |
| `BossAI02Adapter` | Class | `include/sim/BossBrainAdapters.hpp` | 85 |
| `BossAI03Adapter` | Class | `include/sim/BossBrainAdapters.hpp` | 112 |
| `BossAI04Adapter` | Class | `include/sim/BossBrainAdapters.hpp` | 137 |
| `BossAI05Adapter` | Class | `include/sim/BossBrainAdapters.hpp` | 163 |
| `BossAI06Adapter` | Class | `include/sim/BossBrainAdapters.hpp` | 192 |
| `BossAI07Adapter` | Class | `include/sim/BossBrainAdapters.hpp` | 222 |
| `BossAI08Adapter` | Class | `include/sim/BossBrainAdapters.hpp` | 254 |
| `BossAI09Adapter` | Class | `include/sim/BossBrainAdapters.hpp` | 279 |
| `BossAI10Adapter` | Class | `include/sim/BossBrainAdapters.hpp` | 303 |
| `BossAI11Adapter` | Class | `include/sim/BossBrainAdapters.hpp` | 332 |
| `BossAI12Adapter` | Class | `include/sim/BossBrainAdapters.hpp` | 360 |
| `BossAI13Adapter` | Class | `include/sim/BossBrainAdapters.hpp` | 387 |
| `BossAI14Adapter` | Class | `include/sim/BossBrainAdapters.hpp` | 416 |
| `IBossBrain` | Class | `include/sim/IBossBrain.hpp` | 33 |
| `BossBrainBase` | Class | `include/sim/IBossBrain.hpp` | 82 |
| `CdShootAdapter` | Class | `include/sim/EnemyBrainAdapters.hpp` | 37 |
| `IntShootAdapter` | Class | `include/sim/EnemyBrainAdapters.hpp` | 51 |
| `MoveOnlyAdapter` | Class | `include/sim/EnemyBrainAdapters.hpp` | 65 |
| `EnemyAI01Adapter` | Class | `include/sim/EnemyBrainAdapters.hpp` | 78 |

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

1. `gitnexus_context({name: "BossFanCount"})` — see callers and callees
2. `gitnexus_query({query: "sim"})` — find related execution flows
3. Read key files listed above for implementation details
