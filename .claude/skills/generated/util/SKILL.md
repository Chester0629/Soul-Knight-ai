---
name: util
description: "Skill for the Util area of Soul-Knight-ai. 113 symbols across 45 files."
---

# Util

113 symbols | 45 files | Cohesion: 92%

## When to Use

- Working with code in `PTSD/`
- Understanding how TEST, SdlFormatToGlFormat, GetMissingFontTextureSDLSurface work
- Modifying util-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `PTSD/include/Util/Input.hpp` | GetScrollDistance, GetCursorPosition, IsKeyPressed, IfScroll, IsMouseMoving (+5) |
| `PTSD/include/Util/ObjectPool.hpp` | Acquire, Release, FreeCount, ActiveCount, Capacity (+1) |
| `PTSD/src/Util/FilledImage.cpp` | VerticalBottomFilledQuad, FilledImage, SetFraction, RebuildQuad, InitProgram (+1) |
| `PTSD/include/Util/Collider.hpp` | Min, Max, OverlapAABB, OverlapAABBCircle, Overlap (+1) |
| `PTSD/include/Util/DataStore.hpp` | Load, Has, Remove, Clear, Size |
| `PTSD/src/Util/Image.cpp` | SetImage, Image, InitProgram, InitVertexArray, LoadSurface |
| `PTSD/include/Util/FixedTimestep.hpp` | Advance, StepMs, Accumulator, Alpha, Reset |
| `PTSD/src/Util/Text.cpp` | Text, InitProgram, InitVertexArray, ApplyTexture |
| `PTSD/include/Util/Text.hpp` | Text, SetText, SetColor, ApplyTexture |
| `PTSD/src/Util/BGM.cpp` | GetVolume, SetVolume, VolumeUp, VolumeDown |

## Entry Points

Start here when exploring this area:

- **`TEST`** (Function) — `PTSD/test/DataStoreTest.cpp:29`
- **`SdlFormatToGlFormat`** (Function) — `PTSD/include/Core/TextureUtils.hpp:8`
- **`GetMissingFontTextureSDLSurface`** (Function) — `PTSD/src/Util/MissingTexture.cpp:20`
- **`TEST`** (Function) — `PTSD/test/ObjectPoolTest.cpp:17`
- **`main`** (Function) — `src/main.cpp:35`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `Context` | Class | `PTSD/include/Core/Context.hpp` | 10 |
| `Drawable` | Class | `PTSD/include/Core/Drawable.hpp` | 13 |
| `Animation` | Class | `PTSD/include/Util/Animation.hpp` | 16 |
| `FilledImage` | Class | `PTSD/include/Util/FilledImage.hpp` | 48 |
| `Image` | Class | `PTSD/include/Util/Image.hpp` | 24 |
| `Text` | Class | `PTSD/include/Util/Text.hpp` | 24 |
| `TEST` | Function | `PTSD/test/DataStoreTest.cpp` | 29 |
| `SdlFormatToGlFormat` | Function | `PTSD/include/Core/TextureUtils.hpp` | 8 |
| `GetMissingFontTextureSDLSurface` | Function | `PTSD/src/Util/MissingTexture.cpp` | 20 |
| `TEST` | Function | `PTSD/test/ObjectPoolTest.cpp` | 17 |
| `main` | Function | `src/main.cpp` | 35 |
| `TEST` | Function | `PTSD/test/FixedTimestepTest.cpp` | 8 |
| `VerticalBottomFilledQuad` | Function | `PTSD/src/Util/FilledImage.cpp` | 15 |
| `GetMissingImageTextureSDLSurface` | Function | `PTSD/src/Util/MissingTexture.cpp` | 7 |
| `OverlapAABB` | Function | `PTSD/include/Util/Collider.hpp` | 129 |
| `OverlapAABBCircle` | Function | `PTSD/include/Util/Collider.hpp` | 156 |
| `LoadTextFile` | Function | `PTSD/include/Util/LoadTextFile.hpp` | 9 |
| `Overlap` | Function | `PTSD/include/Util/Collider.hpp` | 178 |
| `ResolveMTV` | Function | `PTSD/include/Util/Collider.hpp` | 203 |
| `VerticalBottomFilledQuad` | Function | `PTSD/include/Util/FilledImage.hpp` | 33 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `FilledImage → GetMissingImageTextureSDLSurface` | cross_community | 3 |
| `FilledImage → VerticalBottomFilledQuad` | intra_community | 3 |

## How to Explore

1. `gitnexus_context({name: "TEST"})` — see callers and callees
2. `gitnexus_query({query: "util"})` — find related execution flows
3. Read key files listed above for implementation details
