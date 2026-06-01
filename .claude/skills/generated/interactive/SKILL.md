---
name: interactive
description: "Skill for the Interactive area of Soul-Knight-ai. 7 symbols across 1 files."
---

# Interactive

7 symbols | 1 files | Cohesion: 100%

## When to Use

- Working with code in `PTSD/`
- Understanding how UserCheck, EXCEPT_INPUT_YES, TEST_F work
- Modifying interactive-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `PTSD/test/Interactive/Audio.cpp` | UserCheck, EXCEPT_INPUT_YES, TEST_F, AudioInit, SetUp (+2) |

## Entry Points

Start here when exploring this area:

- **`UserCheck`** (Function) — `PTSD/test/Interactive/Audio.cpp:10`
- **`EXCEPT_INPUT_YES`** (Function) — `PTSD/test/Interactive/Audio.cpp:28`
- **`TEST_F`** (Function) — `PTSD/test/Interactive/Audio.cpp:63`
- **`AudioInit`** (Function) — `PTSD/test/Interactive/Audio.cpp:37`
- **`AudioQuit`** (Function) — `PTSD/test/Interactive/Audio.cpp:46`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `UserCheck` | Function | `PTSD/test/Interactive/Audio.cpp` | 10 |
| `EXCEPT_INPUT_YES` | Function | `PTSD/test/Interactive/Audio.cpp` | 28 |
| `TEST_F` | Function | `PTSD/test/Interactive/Audio.cpp` | 63 |
| `AudioInit` | Function | `PTSD/test/Interactive/Audio.cpp` | 37 |
| `AudioQuit` | Function | `PTSD/test/Interactive/Audio.cpp` | 46 |
| `SetUp` | Method | `PTSD/test/Interactive/Audio.cpp` | 55 |
| `TearDown` | Method | `PTSD/test/Interactive/Audio.cpp` | 60 |

## How to Explore

1. `gitnexus_context({name: "UserCheck"})` — see callers and callees
2. `gitnexus_query({query: "interactive"})` — find related execution flows
3. Read key files listed above for implementation details
