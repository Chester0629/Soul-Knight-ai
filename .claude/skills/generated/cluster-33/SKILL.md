---
name: cluster-33
description: "Skill for the Cluster_33 area of Soul-Knight-ai. 4 symbols across 2 files."
---

# Cluster_33

4 symbols | 2 files | Cohesion: 100%

## When to Use

- Working with code in `PTSD/`
- Understanding how GlFormatToGlInternalFormat, Texture, UpdateData work
- Modifying cluster_33-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `PTSD/src/Core/Texture.cpp` | Texture, UpdateData, UseAntiAliasing |
| `PTSD/src/Core/TextureUtils.cpp` | GlFormatToGlInternalFormat |

## Entry Points

Start here when exploring this area:

- **`GlFormatToGlInternalFormat`** (Function) — `PTSD/src/Core/TextureUtils.cpp:22`
- **`Texture`** (Method) — `PTSD/src/Core/Texture.cpp:7`
- **`UpdateData`** (Method) — `PTSD/src/Core/Texture.cpp:51`
- **`UseAntiAliasing`** (Method) — `PTSD/src/Core/Texture.cpp:67`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `GlFormatToGlInternalFormat` | Function | `PTSD/src/Core/TextureUtils.cpp` | 22 |
| `Texture` | Method | `PTSD/src/Core/Texture.cpp` | 7 |
| `UpdateData` | Method | `PTSD/src/Core/Texture.cpp` | 51 |
| `UseAntiAliasing` | Method | `PTSD/src/Core/Texture.cpp` | 67 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `Texture → GlFormatToGlInternalFormat` | intra_community | 3 |

## How to Explore

1. `gitnexus_context({name: "GlFormatToGlInternalFormat"})` — see callers and callees
2. `gitnexus_query({query: "cluster_33"})` — find related execution flows
3. Read key files listed above for implementation details
