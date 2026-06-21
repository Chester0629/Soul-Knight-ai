---
name: entities
description: "Skill for the Entities area of Soul-Knight-ai. 29 symbols across 19 files."
---

# Entities

29 symbols | 19 files | Cohesion: 90%

## When to Use

- Working with code in `src/`
- Understanding how Cat, GameObject, Boss work
- Modifying entities-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `src/entities/Boss.cpp` | GetCollider, Norm, Think, BossFrames, Boss |
| `src/entities/Enemy.cpp` | Think, Position, GetCollider, MakeBatFrames, Enemy |
| `src/entities/Bullet.cpp` | GetCollider, Update, Deactivate |
| `PTSD/example/include/Cat.hpp` | Cat |
| `PTSD/example/include/Giraffe.hpp` | Update |
| `PTSD/example/include/GiraffeText.hpp` | Start |
| `PTSD/include/Util/GameObject.hpp` | GameObject |
| `include/entities/Boss.hpp` | Boss |
| `include/entities/Bullet.hpp` | Bullet |
| `include/entities/Chest.hpp` | Chest |

## Entry Points

Start here when exploring this area:

- **`Cat`** (Class) — `PTSD/example/include/Cat.hpp:8`
- **`GameObject`** (Class) — `PTSD/include/Util/GameObject.hpp:19`
- **`Boss`** (Class) — `include/entities/Boss.hpp:26`
- **`Bullet`** (Class) — `include/entities/Bullet.hpp:26`
- **`Chest`** (Class) — `include/entities/Chest.hpp:21`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `Cat` | Class | `PTSD/example/include/Cat.hpp` | 8 |
| `GameObject` | Class | `PTSD/include/Util/GameObject.hpp` | 19 |
| `Boss` | Class | `include/entities/Boss.hpp` | 26 |
| `Bullet` | Class | `include/entities/Bullet.hpp` | 26 |
| `Chest` | Class | `include/entities/Chest.hpp` | 21 |
| `Enemy` | Class | `include/entities/Enemy.hpp` | 27 |
| `Player` | Class | `include/entities/Player.hpp` | 23 |
| `WeaponPickup` | Class | `include/entities/WeaponPickup.hpp` | 22 |
| `Update` | Method | `PTSD/example/include/Giraffe.hpp` | 12 |
| `Start` | Method | `PTSD/example/include/GiraffeText.hpp` | 16 |
| `GetCollider` | Method | `src/entities/Boss.cpp` | 63 |
| `GetCollider` | Method | `src/entities/Bullet.cpp` | 97 |
| `GetCollider` | Method | `src/entities/Chest.cpp` | 22 |
| `GetCollider` | Method | `src/entities/WeaponPickup.cpp` | 23 |
| `DamageObstacle` | Method | `src/scenes/GameScene.cpp` | 661 |
| `Blocks` | Method | `src/world/Room.cpp` | 80 |
| `Think` | Method | `src/entities/Boss.cpp` | 44 |
| `Think` | Method | `src/entities/Enemy.cpp` | 32 |
| `Position` | Method | `src/entities/Enemy.cpp` | 48 |
| `GetCollider` | Method | `src/entities/Enemy.cpp` | 52 |

## How to Explore

1. `gitnexus_context({name: "Cat"})` — see callers and callees
2. `gitnexus_query({query: "entities"})` — find related execution flows
3. Read key files listed above for implementation details
