---
name: tools
description: "Skill for the Tools area of Soul-Knight-ai. 107 symbols across 12 files."
---

# Tools

107 symbols | 12 files | Cohesion: 91%

## When to Use

- Working with code in `tools/`
- Understanding how block_text, find_scalar, find_vec2 work
- Modifying tools-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `tools/extract_layout.py` | block_text, find_scalar, _unquote, find_vec2, find_fileid (+40) |
| `tools/render_layout.py` | _font, draw_texts, measure, collect_slider_fills, render_root (+12) |
| `tools/build_gamedata.py` | add, load_group, parse_weapon_catalog, parse_droptables, write (+5) |
| `tools/build_fonts_manifest.py` | load_guid_map, collect_font_guids, find_png, bitmap_atlas, read_char_table (+2) |
| `tools/validate_data.py` | load, need, expect_keys, numeric, count_unresolved (+1) |
| `tools/hud_check.cpp` | IsBarRed, SaveShot, HpBarSpan, Stats, main |
| `tools/hud_bar_check.cpp` | MakeBar, LitSpan, SaveScreenshot, main |
| `tools/bake_hud_bars.py` | find, resolve, main |
| `tools/gen_tiles.py` | floor_tile, wall_tile, main |
| `tools/import_assets.py` | copy_category, build_sprite_manifest, main |

## Entry Points

Start here when exploring this area:

- **`block_text`** (Function) — `tools/extract_layout.py:71`
- **`find_scalar`** (Function) — `tools/extract_layout.py:75`
- **`find_vec2`** (Function) — `tools/extract_layout.py:95`
- **`find_fileid`** (Function) — `tools/extract_layout.py:104`
- **`find_color`** (Function) — `tools/extract_layout.py:117`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `block_text` | Function | `tools/extract_layout.py` | 71 |
| `find_scalar` | Function | `tools/extract_layout.py` | 75 |
| `find_vec2` | Function | `tools/extract_layout.py` | 95 |
| `find_fileid` | Function | `tools/extract_layout.py` | 104 |
| `find_color` | Function | `tools/extract_layout.py` | 117 |
| `find_float` | Function | `tools/extract_layout.py` | 128 |
| `find_int` | Function | `tools/extract_layout.py` | 138 |
| `find_sprite_ref` | Function | `tools/extract_layout.py` | 143 |
| `find_children` | Function | `tools/extract_layout.py` | 154 |
| `detect_layout_component` | Function | `tools/extract_layout.py` | 448 |
| `has` | Function | `tools/extract_layout.py` | 452 |
| `detect_selectable` | Function | `tools/extract_layout.py` | 469 |
| `detect_scroll_rect` | Function | `tools/extract_layout.py` | 587 |
| `detect_mask` | Function | `tools/extract_layout.py` | 621 |
| `detect_localize` | Function | `tools/extract_layout.py` | 640 |
| `extract_text_style` | Function | `tools/extract_layout.py` | 697 |
| `collect` | Function | `tools/extract_layout.py` | 754 |
| `build_guid_map` | Function | `tools/extract_layout.py` | 179 |
| `load_sorting_layers` | Function | `tools/extract_layout.py` | 205 |
| `node_rect` | Function | `tools/extract_layout.py` | 1051 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `Main → To_bgra` | cross_community | 5 |
| `Main → Make_9slice` | cross_community | 5 |
| `Main → Apply_tint` | cross_community | 5 |
| `Main → Find_scalar` | cross_community | 4 |
| `Detect_selectable → Find_scalar` | intra_community | 4 |
| `Main → _slider_rect` | cross_community | 4 |
| `Main → _font` | intra_community | 4 |
| `Main → Measure` | intra_community | 4 |
| `Detect_scroll_rect → Find_scalar` | intra_community | 4 |
| `Detect_mask → Find_scalar` | intra_community | 4 |

## Connected Areas

| Area | Connections |
|------|-------------|
| Scenes | 2 calls |
| Ui | 2 calls |

## How to Explore

1. `gitnexus_context({name: "block_text"})` — see callers and callees
2. `gitnexus_query({query: "tools"})` — find related execution flows
3. Read key files listed above for implementation details
