---
name: include
description: "Skill for the Include area of Soul-Knight-ai. 6 symbols across 6 files."
---

# Include

6 symbols | 6 files | Cohesion: 100%

## When to Use

- Working with code in `PTSD/`
- Understanding how Cat, GameObject, Update work
- Modifying include-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `PTSD/example/include/Cat.hpp` | Cat |
| `PTSD/example/include/Giraffe.hpp` | Update |
| `PTSD/example/include/GiraffeText.hpp` | Start |
| `PTSD/include/Util/GameObject.hpp` | GameObject |
| `PTSD/include/config.hpp` | Init |
| `PTSD/src/Core/Context.cpp` | Context |

## Entry Points

Start here when exploring this area:

- **`Cat`** (Class) — `PTSD/example/include/Cat.hpp:8`
- **`GameObject`** (Class) — `PTSD/include/Util/GameObject.hpp:19`
- **`Update`** (Method) — `PTSD/example/include/Giraffe.hpp:12`
- **`Start`** (Method) — `PTSD/example/include/GiraffeText.hpp:16`
- **`Init`** (Method) — `PTSD/include/config.hpp:65`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `Cat` | Class | `PTSD/example/include/Cat.hpp` | 8 |
| `GameObject` | Class | `PTSD/include/Util/GameObject.hpp` | 19 |
| `Update` | Method | `PTSD/example/include/Giraffe.hpp` | 12 |
| `Start` | Method | `PTSD/example/include/GiraffeText.hpp` | 16 |
| `Init` | Method | `PTSD/include/config.hpp` | 65 |
| `Context` | Method | `PTSD/src/Core/Context.cpp` | 15 |

## How to Explore

1. `gitnexus_context({name: "Cat"})` — see callers and callees
2. `gitnexus_query({query: "include"})` — find related execution flows
3. Read key files listed above for implementation details
