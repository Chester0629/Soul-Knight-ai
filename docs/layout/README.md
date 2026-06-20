# UI Layout Reconstruction (Route A: read the data, don't guess the screen)

Soul Knight's UI is stored as Unity prefabs/scenes inside the asset bundles.
Every screen ships its exact layout: the GameObject hierarchy, each element's
`RectTransform` (anchored position, size, pivot, anchors, scale) and the sprite
each `Image`/`SpriteRenderer` references. AssetRipper already exported all of it
to disk as Unity YAML, so the layout is a **read**, not a screenshot guess.

```
prefab/scene .unity/.prefab (RectTransform + Image.m_Sprite{guid})
      │  tools/extract_layout.py
      ▼
structured layout JSON  ──►  feed to the port / to Opus  ──►  faithful UI
      │  tools/render_layout.py (optional visual check)
      ▼
composited PNG  (eyeball it against the real game)
```

## Data-layer coverage (and where it stops)

This pipeline is the **static UI data layer**: everything serialized in the
scene/prefab YAML. **Covered:** GameObject hierarchy, RectTransform geometry
(validated to sub-pixel), sprite resolution (incl. 9-slice borders + atlas
crops), Image render mode + tint, Text content/typography, Selectable states
(ColorTint / SpriteSwap / Slider), ScrollRect + Mask, layout-driven nodes,
cross-Canvas sorting, and the i2 `Localize` **term** on each localized node (the
`mTerm` key itself — see [Localization](#localization-localize); resolving it
against the i2 table is the one piece that stays in the code layer, item 4).
Totals: **9 scenes = 1106 nodes / 799 sprites / 127 texts / 133 buttons /
3 scroll-rects / 9 masks / 12 canvases / 24 localize; 42 prefabs = 686 / 388 /
95 / 73 / 1 / 6 / 0 / 23 localize** (47 localized nodes total). Fonts:
`fonts_manifest.json` (6; the one bitmap font now carries its character table).

> **Counting unit & the 23-vs-47 scope.** A *localize node* = one node carrying an
> i2 `Localize` component = exactly one `mTerm` = one `localize.term` (1:1, so
> "nodes" and "term occurrences" are the same number). The figures split by source:
> **scenes 24 + prefabs 23 = 47** combined. **`docs/layout/prefabs/_manifest.json`
> is the *prefab-only* batch manifest**, so its `totals.localize` is the **prefab
> figure (23)**, *not* the combined total — the 9 scenes are not in that manifest
> (they have no batch manifest). The combined **47** lives here in the README. So
> README `47` and manifest `23` are both correct in their own scope: `23 (prefab) +
> 24 (scene) = 47`.

**NOT covered — these live in the Il2Cpp / code layer (next phase), do not infer
them from YAML:**
1. **Runtime instantiation / mounting** — which prefab is spawned under which
   canvas, by what code, when. The YAML has no answer; don't guess.
2. **Filled value binding** — `image_type:3` bars carry their *static* fill; the
   live value (skill cooldown %, HP) is driven by gameplay code.
3. **Event handling** — `m_OnClick` / `m_OnValueChanged` targets+methods are
   recorded by name only; the behavior is code.
4. **Language-table resolution** — *boundary now sharp.* The node→term half is
   **done and in the YAML**: each of the 47 localized nodes carries its i2
   `Localize.mTerm` verbatim (`localize.term`). What stays in the code layer is
   only the **term→translation** half: parsing `I2Languages.prefab` (701 terms ×
   13 langs) and joining on `localize.term` to pick the device-locale string.
   That join is a clean one-key lookup (see [Localization](#localization-localize)),
   but the i2 table parse itself is the next stage, not this data layer. (The
   ~175 text nodes with no `localize.term` are either final static text or are
   set at runtime by code via a term that is **not** serialized in the YAML —
   also code-layer; don't fabricate a term for them.)

## Sources on disk

| What | Where |
|------|-------|
| Layout (prefab/scene YAML) | `_reverse/asset soul/ExportedProject/Assets/**/*.{prefab,unity}` |
| `guid → asset` map | the sibling `*.meta` files (cached to `ExportedProject/.guid_map.json`) |
| Sprite metadata (atlas rect, pivot, source texture) | the `Sprite` `.asset` (class 213) |
| Decoded pixels — per-sprite | `_reverse/extracted/assets/<same relpath>.png` (path-parallel mirror) |
| Decoded pixels — atlas | `ExportedProject/Assets/Texture2D/SpriteAtlasTexture-*.png` (cropped by `sprite_rect`) |

## tools/extract_layout.py

Turns one `.prefab` or `.unity` into a layout JSON.

```bash
python tools/extract_layout.py \
  "D:/Soul Knight/_reverse/asset soul/ExportedProject/Assets/_RGScene/Title.unity" \
  -o docs/layout/Title.layout.json \
  --svg docs/layout/Title.svg \
  --crop-dir docs/layout/crops          # cut atlas sprites to PNGs (needs cv2)
```

Per node it emits: `name`, `active`, `rt_fid`/`go_fid`, `unity_rect` (Y-up,
origin bottom-left), `screen_rect` (Y-down, origin top-left — screenshot space),
`anchor_min/max`, `pivot`, `anchored_pos`, `size_delta`, `scale`, a `visual[]`
list resolving each sprite to `{name, sprite_rect, png, atlas_png, needs_crop,
border}`, and (where present) `layout_driven`, `selectable`, `scroll_rect`, and
`mask`. Text, interaction, scroll/mask, and canvas-sorting fields are documented
below. Font references are inventoried in `fonts_manifest.json`
(`tools/build_fonts_manifest.py`).

### Image render fields (per `visual[]` entry)

- `color` — `{r,g,b,a}` tint (Unity `m_Color`). Applied multiplicatively; many
  sprites are white masters colored only by tint, so omitting it is wrong.
- `image_type` — `0` Simple / `1` Sliced / `2` Tiled / `3` Filled. Sliced uses
  the sprite `border` (9-slice). Tiled/Filled are rendered as Simple and flagged
  `unsupported_render`.
- `fill_method` / `fill_amount` / `fill_origin` / `preserve_aspect` — Filled
  controls + aspect lock.
- `sprite.border` — `{left,bottom,right,top}` 9-slice margins (from the sprite
  `.asset` `m_Border`, with the `.meta` `spriteBorder` as fallback).

### Text (`visual[]` entry, `kind: "Text"` or `"TMP"`)

- `text` — the string content, original (Chinese preserved); `\n` marks newlines.
- `color` — text color (UGUI graphic `m_Color`, or TMP `m_fontColor`).
- `text_style` — `{framework: "UGUI"|"TMP", font (resolved asset name), font_guid,
  font_size, font_style, alignment, rich_text, best_fit, min_size, max_size,
  h_overflow, v_overflow}`. TMP adds `auto_size`+`auto_size_min/max` and, if any
  material effects are present, `tmp_effects: [names]` (flagged only). This game
  is 100% UGUI Text — TMP support is defensive.

> **⚠ Localization — for the `text` value, know which of two cases a node is in.**
> Soul Knight uses **i2 Localization** (`Assets/i2/resources/I2Languages.prefab`):
> **701 terms × 13 languages** (en-US, zh-TW, **zh-CN**, ja, ko, es, de, pt, fr, ru,
> pl …). **Not every Text node goes through the i2 table** — the split is exact and
> now recorded in the JSON:
>
> - **47 nodes carry a `localize.term`** (an i2 `Localize` component — see the
>   [Localization](#localization-localize) section). *Only these* are resolved at
>   runtime against the device locale: the port looks up `localize.term` in the i2
>   table and **ignores the JSON `text`** for them (it is just the zh-CN editor
>   preview). The `mTerm` is kept verbatim, whether a key (`I_multiplayer_tips_1`)
>   or an English string (`New Game`). Verified: `新游戏`→`New Game`,
>   `多人游戏`→`I_multiplayer`, `Up to 4 Players`→`I_multiplayer_tips_1` all match a
>   real term in `I2Languages.prefab`. (1 of the 47, the Scene_Splash permission
>   text, has a **blank** `mTerm` — the i2 component is present but the term is set
>   by code at runtime.)
> - **The other 175 text nodes have no `localize.term`.** Either (a) the JSON
>   `text` is the real static content (numbers, symbols, already-final strings), or
>   (b) gameplay code sets the string at runtime via a term — but **that node→term
>   mapping is not in the scene/prefab YAML** (it lives in code), so it is a
>   **code-layer (Il2Cpp) problem, not a gap in this data layer**. Do not invent a
>   term for these from the YAML.
>
> Because the JSON now carries `term`, resolving the i2 table later is **one clean
> join** keyed by `localize.term` (no fragile reverse-lookup on the editor `text`,
> which is ambiguous — e.g. term `I_mul_tip1` appears on two nodes with two
> different editor previews). Parsing the i2 table itself remains a follow-up
> (Il2Cpp / data-layer) task, out of scope here.

### Selectable (node field, `kind`-less — it's a component on the node)

A node with a Button/Toggle/Slider gets `"selectable": {...}`:
- `type` (`Button`/`Toggle`/`Slider`/`Selectable`), `transition`
  (`0` None / `1` ColorTint / `2` SpriteSwap / `3` Animation), `interactable`.
- `color_tint` (transition 1) — `normal/highlighted/pressed/disabled` colors +
  `color_multiplier` + `fade_duration`.
- `sprite_swap` (transition 2) — `highlighted/pressed/disabled`, each a full
  sprite resolution (guid → PNG via the same chain). *This game uses ColorTint
  for all 301 buttons; SpriteSwap is implemented + synthetically validated.*
- `target_graphic` — `{fileID, go_fid, name}` of the graphic it tints.
- Toggle adds `is_on` + `toggle_graphic`.
- Slider adds `slider` — `{fill_rect, handle_rect` (each `{fileID, go_fid,
  name}`), `direction` (`0` LtR / `1` RtL / `2` BtT / `3` TtB), `min_value`,
  `max_value`, `whole_numbers`, `value}`. *This game has **no UGUI Slider in
  scope** (the settings volume is a custom control, not a `Slider`); the field
  is a defensive implementation, synthetically validated end-to-end. Type
  detection requires a Slider-unique field (`m_FillRect`/`m_MinValue`/
  `m_MaxValue`/`m_WholeNumbers`) so a Scrollbar — which also has `m_Value` +
  `m_HandleRect` — is never mislabeled `Slider`. `render_layout.py` draws the
  fill graphic proportional to `value` (direction-aware).*

### Localization (`localize`)

A node carrying an i2 `Localize` component gets `"localize": {...}`. It is keyed by
GameObject, so it attaches whether the component sits on a Text node **or** — i2
also term-swaps sprites/fonts — a non-Text node.

- `term` — the `mTerm` string **verbatim**, whether a key (`I_multiplayer_tips_1`,
  `cloudsave_btn_save`) or an English string (`New Game`, `TAP TO START`). This is
  the i2 lookup key. It can be `""` when the component is present but the term is
  assigned by code at runtime (1 such node: the Scene_Splash permission text).
- `secondary_term` — the `mTermSecondary` string, only when non-empty.
- `localize_target` — the component type, resolved from its `m_Script` guid (here
  always `Localize`, i.e. `I2/Loc/Localize.cs`).

**Scope:** this records only the serialized `Localize` fields. It does **not** parse
`I2Languages.prefab` or resolve `term`→translation (item 4 above; next stage).

**node→term join (how the port consumes it).** At runtime i2 overwrites the Text
with the device-locale translation of `term`; the JSON `text` is only the zh-CN
editor preview. So the port's text source is:

```
display_string(node) =
    i2_table[node.localize.term][device_locale]   if node has localize.term
    node.text                                      otherwise (real static content)
```

Resolving `i2_table` is the next-stage task, **but the join is now trivial**:
`localize.term` is a stable key → a single dictionary lookup, not a fragile reverse
match on the ambiguous editor `text` (e.g. term `I_mul_tip1` labels two nodes whose
editor previews differ). **47 of the 222 text nodes carry a term** (24 scenes + 23
prefabs); the other **175 have none** (static content or code-set, item 4); **0**
localize sits on a non-Text node in this build (the sprite/font-swap path is
supported, just unused).

### `layout_driven`

A node carrying a `HorizontalLayoutGroup` / `VerticalLayoutGroup` /
`GridLayoutGroup` / `ContentSizeFitter` / `AspectRatioFitter` / `LayoutElement`
gets `"layout_driven": [component names]`. **Its serialized rect is the editor
value; Unity recomputes it at runtime** — downstream should treat those rects as
"needs verification" rather than ground truth. (H vs V is resolved by the
`m_Script` fileID; the H/V field signature is unique to those two subclasses.)

### ScrollRect + Mask (node fields)

- `scroll_rect` — `{horizontal, vertical, content, viewport (each {fileID,
  go_fid, name}), movement_type, elasticity, inertia, deceleration_rate,
  scroll_sensitivity, h_scrollbar/v_scrollbar (+visibility)}`. The `content` node
  is typically also `layout_driven` (a Grid/H/V group) — its rect is the
  editor-time extent, not the runtime one.
- `mask` — `{type:"Mask", show_mask_graphic}` or `{type:"RectMask2D", padding,
  softness}`. `render_layout.py` clips a mask node's subtree to its rect
  (rectangular only; a non-rect sprite-shaped Mask would degrade to its rect).
  This game uses 12 `Mask`, 0 `RectMask2D`.

### Canvas sorting (`canvas` per root + `canvas_draw_order`)

Each canvas root's `canvas` adds `{render_mode(+_name), sorting_order,
sorting_layer_id(+_name, from `ProjectSettings/TagManager.asset`),
override_sorting, plane_distance, nested}`. The scene's top-level
`canvas_draw_order` lists root indices sorted by **sorting layer → sorting order
→ hierarchy** (later = on top); `render_layout.py --all-canvases` composites in
that order. In this game 3 scenes (HeroRoom/Scene_Game/Scene_Tutorial) have 2
canvases — a WorldSpace **Effect**-layer canvas (order 0) under a
ScreenSpaceCamera **UI**-layer canvas (order 2); the other 6 are single-canvas.
No nested canvases (`override_sorting` is 0 everywhere). Note: in the *static*
scene the world canvas is near-empty (runtime-populated), so the two canvases
barely overlap — the order is correctly encoded for runtime but visually moot
for the snapshot.

### Fonts (`fonts_manifest.json`)

`tools/build_fonts_manifest.py` inventories every font referenced by extracted
text: **6 total** — 4 TTF (`pixel_bold`, `LockClock`, `galacticstorm`, `m04`,
all in `Assets/rgother/`), 1 Unity-builtin Arial, and **1 bitmap font**
(`number.asset`, used in 3 places). Per font: `guid`, `usage_count`, `name`,
`file_path`, `type`, and (TTF) `exists`.

A bitmap font needs **two** things to render and the manifest now tracks both
separately — an atlas alone is *not* "ready":

- `atlas_png` — the glyph image (`material → texture`).
- `character_table` — the per-glyph `char → UV/advance` map, **without which the
  C++ renderer cannot place glyphs from the image**. Parsed from the Font
  `.asset` `m_CharacterRects`: each entry has `index` (codepoint), `char`
  (printable ASCII), `uv` `{x,y,w,h}` (normalized atlas rect; Unity's negative
  `h` = flipped V), `vert` `{x,y,w,h}` (glyph quad in font units), `advance`,
  `flipped`. Plus font-level `font_metrics` (line/character spacing, padding,
  tracking, ascii_start_offset).
- `status` — explicit: `ready` (atlas + char table both present), or
  `atlas_only_needs_char_table` (`needs_char_table: true` + a note that the port
  may substitute a TTF). **`number` is `ready`: its char table is extracted (41
  glyphs — `+ - .`, `0-9`, `A-Z`, `g`, `s`).** The fallback branch exists so a
  future bitmap font with no extractable table is flagged, never silently
  presented as complete on the strength of its atlas alone.

## ⚠ Porting warnings (C++ side)

1. **`unsupported_render: "Filled"` / `"Tiled"` is a *renderer* limitation, not a
   data one.** `render_layout.py` draws those as Simple for the placeholder
   preview, but the JSON still carries `image_type`, `fill_method`, `fill_amount`,
   `fill_origin`. The C++ port **must implement Filled** wherever `image_type:3`
   appears. **But do not assume the HUD bars are Filled** — the Il2Cpp behavior
   trace (`docs/UI_BEHAVIOR_SPEC.md` §3a, verified) shows only the **skill-cooldown
   mask** uses `Image.fillAmount`; the **HP / energy / armor bars are width-scaled
   `RectTransform`s** (`set_sizeDelta`) bound to `RoleAttributePlayer`, *not* Filled
   images. So: implement Filled for the cooldown mask (and any `image_type:3` node),
   and implement the health/energy/armor bars as width-clipped rects.
2. **`degenerate_root_size` widgets have no intrinsic size.** The 14 full-screen
   prefabs whose root `sizeDelta` is 0 fall back to a `1280×720` frame *for
   extraction only*. Their true size is decided by the host canvas at runtime —
   the port must **stretch them to their parent (anchors 0–1)**, never hardcode
   `1280×720`. The frame is a placeholder, not a layout constant.

### Coordinate space

`coordinate_space` is `"screen"` for scenes (absolute `screen_rect` on the canvas
reference resolution) or `"prefab_local"` for a Canvas-less prefab — see below.

Key correctness rules baked in (validated, see below):
- **Canvas frame.** A `Canvas` root only establishes the `(0,0)–(refW,refH)`
  coordinate frame. Its own `localScale` (ScreenSpace-Camera canvases use a tiny
  one, e.g. `1/90`), `pivot`, and `anchoredPosition` map the whole canvas into
  world/screen space **uniformly** and are **excluded** from child layout.
  Descendant `localScale` still applies normally.
- **Reference resolution** is auto-detected per Canvas from its `CanvasScaler`
  (`m_ReferenceResolution`); `--canvas WxH` overrides.
- **RectTransform math:** `size = (anchorMax−anchorMin)·parentSize + sizeDelta·S`,
  pivot positioned at `parentBL + anchorRef + anchoredPos·S`, where `S` is the
  cumulative descendant scale.

## tools/render_layout.py

Composites a layout JSON back into a PNG (alpha-aware, hierarchy draw order) so
you can verify the reconstruction visually.

```bash
python tools/render_layout.py docs/layout/Title.layout.json -o docs/layout/Title.png
#   --include-inactive   also draw runtime-activated panels (many scenes assemble
#                        their UI in code, so the static scene render is sparse)
#   --root N             render a specific canvas root
```

## Validation

- The math was hand-checked exactly against raw YAML (e.g. `BtnMulSkin`), then a
  multi-agent workflow re-derived the rects independently for a diverse node
  sample (stretch anchors, deep nesting, off-screen modal panels). The first
  pass exposed a uniform `(640, 360)` offset — the Canvas-localScale/centering
  bug — which the canvas-frame rule above fixes.
- `Title.unity` renders pixel-faithfully (logo, ribbon/robot, corner buttons);
  `Scene_Net_Menu` renders the multiplayer lobby (player slots, star banner,
  start/ready/cancel). Both are reconstructed from data alone.

## Route B (fallback): `match_sprites.py`

Multi-scale OpenCV template matching against a screenshot. Use **only** when no
scene/prefab data exists for a screen — Route A is exact and supersedes it
wherever the bundle is available.

## Standalone prefabs (`prefab_local`)

A prefab has no Canvas, so there is **no absolute screen position**. The extractor
lays children out in the prefab root's own frame (the root `sizeDelta`) and emits
`local_rect` (relative to the root, Y-down) instead of `screen_rect`. The root's
own `anchors / pivot / anchoredPosition / sizeDelta / scale` are kept verbatim so a
caller can place the widget once its host canvas is known. Full-screen-stretch
roots (root `sizeDelta` 0) fall back to a 1280×720 frame and are flagged
`degenerate_root_size`.

```bash
python tools/extract_ui_prefabs.py            # Assets/rgprefab/ui + Assets/res/ui
#   --dirs <Assets/...> --out docs/layout/prefabs --crop-dir docs/layout/crops
```

Mirrors the source tree into `docs/layout/prefabs/<relpath>.layout.json`, writes
`_manifest.json` (success / skip / fail with categorized reasons, plus
`totals.{nodes,sprites,texts,buttons,scroll_rects,masks,canvases,localize}` and
per-prefab counts). Render a widget at its native size with
`render_layout.py ... --include-inactive`.

> **Scope of `_manifest.json` (see its `scope` + `totals_definition` keys).** This
> manifest covers the **42 prefabs only** — the 9 scenes have no batch manifest. So
> `totals.localize` here is the **prefab figure (23)**, the prefab share of the
> **47** scenes+prefabs total reported in the [coverage section](#data-layer-coverage-and-where-it-stops)
> above (`23 prefab + 24 scene = 47`). `localize` counts nodes carrying an i2
> `Localize` (1 node = 1 `mTerm`).

## Batch (scenes)

All 9 real scenes are extracted under `docs/layout/scenes/*.layout.json`; the 42
UI prefabs under `docs/layout/prefabs/` (see `_manifest.json`). Scenes are extracted
individually (`extract_layout.py <scene>.unity ... --crop-dir docs/layout/crops`)
and have **no** batch manifest — their localize counts (24) and the scenes+prefabs
combined total (47) are recorded in the coverage section above, not in any
`_manifest.json`.
