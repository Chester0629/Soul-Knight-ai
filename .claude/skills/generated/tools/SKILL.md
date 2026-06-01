---
name: tools
description: "Skill for the Tools area of Soul-Knight-ai. 19 symbols across 3 files."
---

# Tools

19 symbols | 3 files | Cohesion: 89%

## When to Use

- Working with code in `tools/`
- Understanding how load_group, parse_weapon_catalog, parse_droptables work
- Modifying tools-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `tools/build_gamedata.py` | add, load_group, parse_weapon_catalog, parse_droptables, write (+5) |
| `tools/validate_data.py` | load, need, expect_keys, numeric, count_unresolved (+1) |
| `tools/import_assets.py` | copy_category, build_sprite_manifest, main |

## Entry Points

Start here when exploring this area:

- **`load_group`** (Function) — `tools/build_gamedata.py:92`
- **`parse_weapon_catalog`** (Function) — `tools/build_gamedata.py:113`
- **`parse_droptables`** (Function) — `tools/build_gamedata.py:126`
- **`write`** (Function) — `tools/build_gamedata.py:142`
- **`main`** (Function) — `tools/build_gamedata.py:152`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `load_group` | Function | `tools/build_gamedata.py` | 92 |
| `parse_weapon_catalog` | Function | `tools/build_gamedata.py` | 113 |
| `parse_droptables` | Function | `tools/build_gamedata.py` | 126 |
| `write` | Function | `tools/build_gamedata.py` | 142 |
| `main` | Function | `tools/build_gamedata.py` | 152 |
| `load` | Function | `tools/validate_data.py` | 25 |
| `need` | Function | `tools/validate_data.py` | 33 |
| `expect_keys` | Function | `tools/validate_data.py` | 38 |
| `numeric` | Function | `tools/validate_data.py` | 46 |
| `count_unresolved` | Function | `tools/validate_data.py` | 54 |
| `main` | Function | `tools/validate_data.py` | 63 |
| `clean` | Function | `tools/build_gamedata.py` | 73 |
| `build_table` | Function | `tools/build_gamedata.py` | 104 |
| `copy_category` | Function | `tools/import_assets.py` | 37 |
| `build_sprite_manifest` | Function | `tools/import_assets.py` | 55 |
| `main` | Function | `tools/import_assets.py` | 87 |
| `add` | Method | `tools/build_gamedata.py` | 51 |
| `resolve` | Method | `tools/build_gamedata.py` | 58 |
| `_is_ref` | Function | `tools/build_gamedata.py` | 40 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `Build_table → Resolve` | intra_community | 3 |
| `Build_table → _is_ref` | intra_community | 3 |

## How to Explore

1. `gitnexus_context({name: "load_group"})` — see callers and callees
2. `gitnexus_query({query: "tools"})` — find related execution flows
3. Read key files listed above for implementation details
