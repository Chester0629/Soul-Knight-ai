---
name: combat
description: "Skill for the Combat area of Soul-Knight-ai. 1058 symbols across 176 files."
---

# Combat

1058 symbols | 176 files | Cohesion: 99%

## When to Use

- Working with code in `include/`
- Understanding how TEST, TEST, TEST work
- Modifying combat-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `include/combat/BossAI09.hpp` | SetSeed, SetDead, SetDizzy, SetCanShoot, SetShooting (+20) |
| `include/combat/BossAI03.hpp` | SetSeed, Dizzy, Angry, Shooting, CanShoot (+19) |
| `include/combat/BossAI11.hpp` | SetSeed, Awake, Dizzy, Angry, WeaponLockTarget (+18) |
| `include/combat/BossAI13.hpp` | SetSeed, Awake, Dizzy, Angry, HasTarget (+18) |
| `include/combat/EnemyAI11.hpp` | SetSeed, Awake, SetAwake, Dead, SetDead (+16) |
| `include/combat/BossAI05.hpp` | SetSeed, SetAwake, SetDead, Angry, Dizzy (+15) |
| `include/combat/BossAI07.hpp` | SetSeed, Awake, CanShoot, Angry, TargetCleared (+15) |
| `include/combat/EnemyAI10.hpp` | SetSeed, SetDead, SetDizzy, Awake, SetAwake (+15) |
| `include/combat/EnemyAIShark.hpp` | SetSeed, SetDead, SetDizzy, CanShoot, SetDashing (+15) |
| `include/combat/BossAI14.hpp` | SetSeed, SetDead, Dizzy, CanShoot, SetCanShoot (+14) |

## Entry Points

Start here when exploring this area:

- **`TEST`** (Function) — `test/BossAI09Test.cpp:34`
- **`TEST`** (Function) — `test/BossAI03Test.cpp:27`
- **`TEST`** (Function) — `test/BossAI11Test.cpp:28`
- **`TEST`** (Function) — `test/BossAI13Test.cpp:30`
- **`TEST`** (Function) — `test/EnemyAI11Test.cpp:19`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `TEST` | Function | `test/BossAI09Test.cpp` | 34 |
| `TEST` | Function | `test/BossAI03Test.cpp` | 27 |
| `TEST` | Function | `test/BossAI11Test.cpp` | 28 |
| `TEST` | Function | `test/BossAI13Test.cpp` | 30 |
| `TEST` | Function | `test/EnemyAI11Test.cpp` | 19 |
| `TEST` | Function | `test/BossAI05Test.cpp` | 32 |
| `TEST` | Function | `test/BossAI07Test.cpp` | 25 |
| `TEST` | Function | `test/BossAI14Test.cpp` | 41 |
| `TEST` | Function | `test/EnemyAI10Test.cpp` | 17 |
| `TEST` | Function | `test/EnemyAISharkTest.cpp` | 17 |
| `TEST` | Function | `test/BossAI10Test.cpp` | 31 |
| `TEST` | Function | `test/EnemyAI01Test.cpp` | 17 |
| `TEST` | Function | `test/EnemyAI03Test.cpp` | 17 |
| `TEST` | Function | `test/BossAI06Test.cpp` | 27 |
| `TEST` | Function | `test/EnemyAI09Test.cpp` | 17 |
| `TEST` | Function | `test/EnemyAI14Test.cpp` | 17 |
| `TEST` | Function | `test/EnemyAI02Test.cpp` | 17 |
| `TEST` | Function | `test/BossAI02Test.cpp` | 13 |
| `TEST` | Function | `test/BossAI12Test.cpp` | 27 |
| `TEST` | Function | `test/NpcMercenaryControllerTest.cpp` | 15 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `Advance → SkillCd` | cross_community | 5 |
| `Advance → CooldownRemaining` | cross_community | 5 |
| `TEST → SkillCd` | intra_community | 3 |
| `TEST → CooldownRemaining` | intra_community | 3 |
| `ApplyToPlayer → TakeDamage` | intra_community | 3 |
| `ApplyToPlayer → IsDead` | intra_community | 3 |
| `ApplyToEnemy → IsDead` | intra_community | 3 |

## Connected Areas

| Area | Connections |
|------|-------------|
| Sim | 5 calls |

## How to Explore

1. `gitnexus_context({name: "TEST"})` — see callers and callees
2. `gitnexus_query({query: "combat"})` — find related execution flows
3. Read key files listed above for implementation details
