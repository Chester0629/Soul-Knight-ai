---
name: ui
description: "Skill for the Ui area of Soul-Knight-ai. 29 symbols across 4 files."
---

# Ui

29 symbols | 4 files | Cohesion: 98%

## When to Use

- Working with code in `src/`
- Understanding how ScreenRectToPtsd, RenderedExtent, Classify work
- Modifying ui-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `src/ui/HudLayout.cpp` | Degenerate, UnionInto, ScreenRectToPtsd, RenderedExtent, Classify (+13) |
| `include/ui/HudLayout.hpp` | Load, ParseString, ok, canvasW, canvasH (+1) |
| `src/ui/Hud.cpp` | Fraction, Build, Draw |
| `test/HudLayoutTest.cpp` | TEST, HpFullTransform |

## Entry Points

Start here when exploring this area:

- **`ScreenRectToPtsd`** (Function) — `src/ui/HudLayout.cpp:150`
- **`RenderedExtent`** (Function) — `src/ui/HudLayout.cpp:164`
- **`Classify`** (Function) — `src/ui/HudLayout.cpp:180`
- **`ClassifyNode`** (Function) — `src/ui/HudLayout.cpp:194`
- **`ParkedDelta`** (Function) — `src/ui/HudLayout.cpp:202`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `ScreenRectToPtsd` | Function | `src/ui/HudLayout.cpp` | 150 |
| `RenderedExtent` | Function | `src/ui/HudLayout.cpp` | 164 |
| `Classify` | Function | `src/ui/HudLayout.cpp` | 180 |
| `ClassifyNode` | Function | `src/ui/HudLayout.cpp` | 194 |
| `ParkedDelta` | Function | `src/ui/HudLayout.cpp` | 202 |
| `ShiftRect` | Function | `src/ui/HudLayout.cpp` | 207 |
| `RenderedSpanX` | Function | `src/ui/HudLayout.cpp` | 215 |
| `FractionFillLeftAnchor` | Function | `src/ui/HudLayout.cpp` | 220 |
| `TEST` | Function | `test/HudLayoutTest.cpp` | 111 |
| `in` | Function | `src/ui/HudLayout.cpp` | 254 |
| `Load` | Method | `include/ui/HudLayout.hpp` | 183 |
| `ParseString` | Method | `include/ui/HudLayout.hpp` | 185 |
| `ok` | Method | `include/ui/HudLayout.hpp` | 187 |
| `canvasW` | Method | `include/ui/HudLayout.hpp` | 188 |
| `canvasH` | Method | `include/ui/HudLayout.hpp` | 189 |
| `Find` | Method | `include/ui/HudLayout.hpp` | 208 |
| `Fraction` | Method | `src/ui/Hud.cpp` | 46 |
| `Build` | Method | `src/ui/Hud.cpp` | 54 |
| `Draw` | Method | `src/ui/Hud.cpp` | 186 |
| `ParseString` | Method | `src/ui/HudLayout.cpp` | 234 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `Draw → Load` | intra_community | 3 |
| `Draw → Ok` | intra_community | 3 |
| `Draw → Find` | intra_community | 3 |

## How to Explore

1. `gitnexus_context({name: "ScreenRectToPtsd"})` — see callers and callees
2. `gitnexus_query({query: "ui"})` — find related execution flows
3. Read key files listed above for implementation details
