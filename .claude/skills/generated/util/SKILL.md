---
name: util
description: "Skill for the Util area of Soul-Knight-ai. 71 symbols across 31 files."
---

# Util

71 symbols | 31 files | Cohesion: 96%

## When to Use

- Working with code in `PTSD/`
- Understanding how SdlFormatToGlFormat, GetMissingFontTextureSDLSurface, LoadTextFile work
- Modifying util-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `PTSD/include/Util/Input.hpp` | GetScrollDistance, GetCursorPosition, IsKeyPressed, IsKeyDown, IsKeyUp (+5) |
| `PTSD/src/Util/Image.cpp` | SetImage, Image, InitProgram, InitVertexArray, LoadSurface |
| `PTSD/src/Util/Text.cpp` | Text, InitProgram, InitVertexArray, ApplyTexture |
| `PTSD/include/Util/Text.hpp` | Text, SetText, SetColor, ApplyTexture |
| `PTSD/src/Util/BGM.cpp` | GetVolume, SetVolume, VolumeUp, VolumeDown |
| `PTSD/src/Util/Color.cpp` | FromRGB, FromHex, FromHSL, FromHSV |
| `PTSD/src/Util/SFX.cpp` | GetVolume, SetVolume, VolumeUp, VolumeDown |
| `PTSD/include/Util/Time.hpp` | GetDeltaTimeMs, Update, GetElapsedTimeMs |
| `PTSD/src/Util/Animation.cpp` | Draw, Play, Update |
| `PTSD/src/Core/Shader.cpp` | Shader, Compile, CheckStatus |

## Entry Points

Start here when exploring this area:

- **`SdlFormatToGlFormat`** (Function) — `PTSD/include/Core/TextureUtils.hpp:8`
- **`GetMissingFontTextureSDLSurface`** (Function) — `PTSD/src/Util/MissingTexture.cpp:20`
- **`LoadTextFile`** (Function) — `PTSD/include/Util/LoadTextFile.hpp:9`
- **`GetMissingImageTextureSDLSurface`** (Function) — `PTSD/src/Util/MissingTexture.cpp:7`
- **`GetMissingImageTextureSDLSurface`** (Function) — `PTSD/include/Util/MissingTexture.hpp:25`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `Context` | Class | `PTSD/include/Core/Context.hpp` | 10 |
| `Drawable` | Class | `PTSD/include/Core/Drawable.hpp` | 13 |
| `Animation` | Class | `PTSD/include/Util/Animation.hpp` | 16 |
| `Image` | Class | `PTSD/include/Util/Image.hpp` | 24 |
| `Text` | Class | `PTSD/include/Util/Text.hpp` | 24 |
| `SdlFormatToGlFormat` | Function | `PTSD/include/Core/TextureUtils.hpp` | 8 |
| `GetMissingFontTextureSDLSurface` | Function | `PTSD/src/Util/MissingTexture.cpp` | 20 |
| `LoadTextFile` | Function | `PTSD/include/Util/LoadTextFile.hpp` | 9 |
| `GetMissingImageTextureSDLSurface` | Function | `PTSD/src/Util/MissingTexture.cpp` | 7 |
| `GetMissingImageTextureSDLSurface` | Function | `PTSD/include/Util/MissingTexture.hpp` | 25 |
| `LoadSurface` | Function | `PTSD/src/Util/Image.cpp` | 12 |
| `ConvertToUniformBufferData` | Function | `PTSD/include/Util/TransformUtils.hpp` | 22 |
| `Update` | Method | `PTSD/example/src/App.cpp` | 23 |
| `Update` | Method | `PTSD/example/src/Cat.cpp` | 22 |
| `GetScrollDistance` | Method | `PTSD/include/Util/Input.hpp` | 43 |
| `GetCursorPosition` | Method | `PTSD/include/Util/Input.hpp` | 53 |
| `IsKeyPressed` | Method | `PTSD/include/Util/Input.hpp` | 66 |
| `IsKeyDown` | Method | `PTSD/include/Util/Input.hpp` | 79 |
| `IsKeyUp` | Method | `PTSD/include/Util/Input.hpp` | 92 |
| `IfScroll` | Method | `PTSD/include/Util/Input.hpp` | 99 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `Draw → GetElapsedTimeMs` | intra_community | 3 |
| `Draw → GetDeltaTimeMs` | cross_community | 3 |
| `Draw → Play` | intra_community | 3 |

## How to Explore

1. `gitnexus_context({name: "SdlFormatToGlFormat"})` — see callers and callees
2. `gitnexus_query({query: "util"})` — find related execution flows
3. Read key files listed above for implementation details
