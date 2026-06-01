---
name: doxygen-awesome
description: "Skill for the Doxygen-awesome area of Soul-Knight-ai. 7 symbols across 1 files."
---

# Doxygen-awesome

7 symbols | 1 files | Cohesion: 100%

## When to Use

- Working with code in `PTSD/`
- Understanding how userPreference, enableDarkMode, onSystemPreferenceChanged work
- Modifying doxygen-awesome-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `PTSD/docs/doxygen-awesome/doxygen-awesome-darkmode-toggle.js` | userPreference, enableDarkMode, onSystemPreferenceChanged, onUserPreferenceChanged, init (+2) |

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `userPreference` | Method | `PTSD/docs/doxygen-awesome/doxygen-awesome-darkmode-toggle.js` | 103 |
| `enableDarkMode` | Method | `PTSD/docs/doxygen-awesome/doxygen-awesome-darkmode-toggle.js` | 121 |
| `onSystemPreferenceChanged` | Method | `PTSD/docs/doxygen-awesome/doxygen-awesome-darkmode-toggle.js` | 133 |
| `onUserPreferenceChanged` | Method | `PTSD/docs/doxygen-awesome/doxygen-awesome-darkmode-toggle.js` | 138 |
| `init` | Method | `PTSD/docs/doxygen-awesome/doxygen-awesome-darkmode-toggle.js` | 57 |
| `toggleDarkMode` | Method | `PTSD/docs/doxygen-awesome/doxygen-awesome-darkmode-toggle.js` | 142 |
| `updateIcon` | Method | `PTSD/docs/doxygen-awesome/doxygen-awesome-darkmode-toggle.js` | 147 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `UserPreference → EnableDarkMode` | intra_community | 3 |

## How to Explore

1. `gitnexus_context({name: "userPreference"})` — see callers and callees
2. `gitnexus_query({query: "doxygen-awesome"})` — find related execution flows
3. Read key files listed above for implementation details
