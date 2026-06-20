# HUD Build Plan (§3a faithful HUD) — planning only

**Status:** planning document. **No implementation, no build changes, no edits to existing
files** are part of this round — this doc exists to scope the first HUD work, surface one
architecture decision for you to call, and order the prerequisites.

**Inputs (adopted verbatim from the [Port Structure Report]; not re-investigated):**
- Engine **PTSD = SDL2 + OpenGL**; sprite path = `Util::Image(path)` → `GameObject` →
  `Util::Renderer` (`PTSD/include/Util/Image.hpp:34`, `Util/Renderer.hpp:53`). Screen-space
  HUD hook = `src/scenes/GameScene.cpp:534-535` (right after `SetActiveViewMatrix(identity)`).
- **Coordinate mismatch is real:** layout = top-left / Y-down / `(0..1280, 0..720)`; PTSD =
  center / Y-up / `(±640, ±360)` (`PTSD/src/Util/TransformUtils.cpp:24-30`,
  `PTSD/src/config.cpp:11-12`).
- **Three bar values are ready:** `Player::Stats()` → `CombatStats.{hp,armor,energy}(+max)`
  (`include/entities/Player.hpp:32-33,36`, `include/combat/CombatStats.hpp:20-25`).
- **Cooldown value is NOT ready:** `CharSkill` brains not wired into `Player` (gap **b2** —
  a prerequisite dependency, *not* HUD scope).
- **`width-clip / fillAmount / 9-slice / UV-crop` renders: none exist** today (all new).
- **HUD bar textures: not extracted** (gap **d1** — runtime-populated, absent from static crops).

Classification tags carried from the Structure Report: **[HUD-scope]** vs **[prereq
dependency]**. Anything this plan needs that the Structure Report did not establish is marked
**[needs-confirm]** — it is *not* assumed.

---

## 1. Scope split — §3a HUD into two batches

The §3a HUD has four element groups: the three vitals bars, the skill-cooldown mask, and
(separately) the floating combat-text system. **This plan covers Batch 1 + Batch 2; floating
combat text is explicitly out of scope here** (a distinct element, deferred to a later plan).

### Batch 1 — the three width-clip vitals bars (HP / energy / armor) — **[HUD-scope]**
**Why these first, together:** they have no external dependency.
- **Value source ready:** `Player::Stats()` exposes `hp/maxHp`, `armor/maxArmor`,
  `energy/maxEnergy` (`CombatStats.hpp:20-25`). The bar fraction is `cur/max` — exactly the
  §3a rule, and the same width math the port already models for the boss bar
  (`BossAI12Parent.hpp:29`: `sizeDelta.x = current*400/max`).
- **Render hook exists:** `GameScene::Render()` already calls the HUD after switching to
  screen space (`GameScene.cpp:534-535`).
- **No other module needed** to display them. Their only blockers are HUD-internal: the
  `width-clip` render (a1), the textures (d1), and the positioning decision (c1).
- *(`[needs-confirm]`: whether `ArmorReloadTick` is driven live in `GameScene` like
  `EnergyReloadTick` is — `GameScene.cpp:344` drives energy; armor regen wasn't established.
  This does **not** block the bar — `armor/maxArmor` is reachable regardless — but the bar
  will only animate if armor actually changes.)*

### Batch 2 — the skill-cooldown mask (fillAmount) — **blocked on b2**
**Why it cannot ship with Batch 1:** the cooldown value does not exist anywhere reachable.
The cooldown engine (`skill_cd` / `this_skill_time` count-up, `skill_ready`) lives only in
the standalone `CharSkillC01-C13` brains (`include/combat/CharSkillC01.hpp:22-29`) and **none
is wired into `Player`/`GameScene`** (Structure Report §3). Rendering a cooldown mask with no
data source can't be built or validated, so it is a separate milestone behind **b2**.

**b2 unlock conditions (what makes the cooldown value reachable) — [prereq dependency,
gameplay scope, NOT planned here]:**
1. `Player` owns / instantiates the chosen character's `CharSkill` module.
2. That module's per-frame cooldown count-up (the `SkillReload` ticker) is driven from
   `GameScene::Update` (alongside the existing `EnergyReloadTick` at `GameScene.cpp:344`).
3. A reachable **cooldown fraction** getter (`elapsed / total`, clamped 0..1) is exposed
   (e.g. off `Player`), mirroring §3a's `UpdateSkillMask(value1/value2)`.

This plan lists those conditions only; it does **not** design b2. Batch 2's own HUD work
(the `fillAmount` render **a2** + the `skill_icon_mask` texture) is sketched in §5 but starts
only after b2 lands.

---

## 2. ⚠ Architecture fork (c1): build the generic coordinate/layout layer **now**, or hardcode the HUD first?

**This is the decision you must make — the two routes lead to different implementation
instructions.** This plan presents both side by side and gives a lean; it deliberately does
**not** pick one and write its details.

Both routes share one unavoidable fact: **the bar geometry's *source* is the same either
way.** The bar rects are not in the static `Scene_Game.layout.json` (only `btn_skill` is —
Structure Report §4); they must come from **d1** (extracting the HUD prefab) or, failing a
static source, from measurement. Route 1 *reads* that geometry from JSON; Route 2 *bakes* it
as constants. So c1 is about **where the layout math lives**, not about getting the numbers.

### Route 1 — build the generic layout承接 now (HUD = its first client)
**Work / modules:**
- A **`screen_rect → PTSD-translation` converter** (the core coordinate bridge): given a
  layout rect `(left, top, w, h)` in 1280×720 top-left/Y-down, return the PTSD center/Y-up
  sprite translation `(left + w/2 − 640, 360 − (top + h/2))` and a scale `(w/texW, h/texH)`.
  *(Shape only — derived from `TransformUtils.cpp:24-30`; not implemented here.)*
- A **schema-v5 layout-JSON loader** (`docs/layout/**.layout.json`) — uses PTSD's bundled
  `nlohmann_json` (Structure Report §1). Reads the precomputed `screen_rect` per node.
- *(Optional)* a **RectTransform evaluator** in C++. **`[needs-confirm]`:** likely
  *unnecessary* for a fixed 1280×720 HUD — `extract_layout.py` already bakes `screen_rect`
  into the JSON, so the C++ side only needs to read it, not re-derive anchor/pivot. A runtime
  RectTransform evaluator is only needed for resolution-independent re-layout (not an MVP need).

**How the HUD bars change:** each bar's rect is read from a layout JSON node (`img_hp` /
`img_energy` / `img_armor`) instead of a constant; the bar drawable is positioned via the
converter.
**Scope expansion:** large — a reusable layout subsystem (loader + converter [+ evaluator]).
**Benefit:** every future faithful screen (the 222 text nodes, the 16 popups, §3 of the
behavior spec) reuses it; the HUD amortizes the cost as the first of many clients.
**Risk:** (a) **building infra before a second client exists** (the HUD is the *only* current
UI consumer — YAGNI); (b) **contingent on d1** — if the bars turn out to be *code-assembled*
(no static prefab node), there is **no layout JSON to read** and Route 1 gives the HUD nothing
(it would still serve future windows, but not these bars). This contingency is itself a
**`[needs-confirm]` resolved by d1.**

### Route 2 — HUD hardcodes its coordinates now; generic layout deferred
**Work:** bake the three bar rects as constants (either as `screen_rect` constants fed through
the same small converter, or directly as PTSD-space translations/sizes) inside the HUD module
(`src/ui/`). No JSON loader, no subsystem.
**Scope:** minimal — a handful of constants + the one-line converter (or pre-converted values).
**Later rework when generic layout arrives:** the hardcoded constants are replaced by
JSON-driven rects — i.e. the HUD's *positioning* lines are rewritten, but the bar drawables,
the value wiring, and the render hook all stay. Throwaway surface = small (positioning only).
**Risk:** minor duplication / a later rewrite of the positioning constants; the converter math
gets re-homed into the generic layer when a second client (a window) needs it.

### Lean (yours to ratify — **this fork is decided by you; the two routes have different
follow-up implementation instructions**)
**Lean: Route 2 (hardcode now), extract the converter into a shared util only when a second UI
client appears.** Reasons: the HUD is the sole current UI consumer, so Route 1's infra has
nothing to amortize against yet; the bar-geometry source (d1) is unresolved and *may force
hardcoding regardless* if the bars are code-assembled; Route 2 unblocks Batch 1 fastest; and
the coordinate converter is small and cleanly promotable later. **But** if you intend to start
the menu/window screens (§3 of the behavior spec) soon, Route 1's amortization flips the
calculus — so this is a roadmap call, not a purely technical one. **Pick a route before the a4
wiring step (§5); the rest of Batch 1 is route-agnostic.**

> **DECISION (ratified): c1 = Route 1.** The coordinate basis is pinned and the converter is
> designed below (§2.5). One important caveat surfaced during basis-pinning — see §2.5b.

---

## 2.5 Route 1 foundation — coordinate basis (PINNED) + converter + loader

### 2.5a — Coordinate basis verdict (pinned with render + screenshot + working-project evidence)

**`screen_rect` in the layout JSON is FAITHFUL to the static prefab, but it is NOT directly the
final on-screen position for the HUD's top-anchored elements — they are *parked off-screen
above* in the static data.** The negative `top` (d1's contradiction signal) is real design data,
not an extractor bug. Evidence (all runnable-tool / ground-truth, not reasoning):

1. **Extractor is faithful.** Raw `Canvas.prefab` `state_bar` = `m_AnchorMin/Max {0,1}` (top-left),
   `m_AnchoredPosition {30, +100}`. Positive `aPos.y` against a **top** anchor = 100px *above* the
   720 top → the extractor's `top = -100` is the mathematically correct Unity result.
2. **Not a prefab-vs-scene offset.** `Scene_Game.unity`'s own embedded `state_bar` is *identical*
   (`{0,1}`, `{30,100}`, active) and doesn't reference `Canvas.prefab`'s guid. (Possibility 2 ruled out.)
3. **A taller canvas can't explain it either** (possibility 3 as first modeled): a top-anchored
   element keeps the same `screen_rect.top` regardless of canvas height (the anchor moves with the
   top). So the reference resolution is *not* the lever.
4. **Render vs reference screenshot (the decisive test):** rendering `Canvas.layout.json` places
   bottom-anchored buttons correctly (match the HeroRoom screenshot) but the top HUD **off-screen**.
   The gem counter `text_gems`, *visibly on-screen* top-right in the screenshot ("592"), computes to
   `screen_rect.top ≈ -120`. It carries an **Animator** → it slides in from the parked spot.
   `state_bar` has *no* Animator but is parked the same way → repositioned by code at runtime
   (mechanism is `[needs-confirm]`; the verdict stands without it).
5. **Working sibling project ground truth** (`D:\Soul Knight\project`, reference-matched HUD):
   places the *same* `ui_15` panel at the **screen top-left, 6px inset** (`HUD.hpp`: `LEFT=-634,
   TOP=+354`, panel ≈237×117) — i.e. `screen_rect ≈ (6, 6, 237, 117)`, **not** the extractor's
   parked `(30, -143, 247, 122)`.

**Conclusion:** the coordinate *flip math* is correct and verified (§2.5c); only the HUD's static
*absolute* rect is wrong because it is the parked value. The JSON's **relative** data (sprite refs,
sizes, tints, intra-group child offsets, fill direction, borders) is faithful and usable.

### 2.5b — What this means for Route 1 (the caveat to act on)

Route 1's clean premise — "read the precise HUD bar rect straight from the layout JSON" — **holds
for the bars' relative structure but NOT for the HUD group's absolute on-screen position** (that
static value is parked off-screen). This is a *landing-phase correction to upstream*: the c1
decision predated this basis fact. Route 1 stays viable, with this rule:

- **Use the JSON for:** which sprite (`ui_12`/`ui_15`), bar/panel sizes, tints, the three bars'
  offsets *relative to `state_bar`*, fill direction, borders.
- **Do NOT use the JSON's absolute position for the `state_bar` group.** Anchor the group at a
  **known on-screen reference** instead — top-left, ~6px inset (per the working project / the
  reference screenshot). The children then fall into place via their faithful relative offsets.
- (For *non-parked* screens — e.g. bottom-anchored buttons — the static `screen_rect` *is* the
  final position and Route 1 reads it directly. Parking is per-element; flag slide-in/parked
  groups. The general subsystem must not assume every static rect is the final one.)

### 2.5c — Converter design spec (pure function, **verified**)

Pure, side-effect-free util; input one layout rect, output a PTSD transform. Fixed 1280×720
(MVP; no resolution-independent re-layout — `extract_layout.py` already bakes `screen_rect`, the
C++ side only reads, never re-derives anchor/pivot; a runtime RectTransform evaluator is a future
extension, out of MVP).

```
// screen_rect: top-left origin, Y-down, 1280x720   ->   PTSD: center origin, Y-up, ±640 x ±360
// (PTSD draws a sprite CENTERED at its translation — see TransformUtils.cpp:24-35)
PtsdTransform ScreenRectToPtsd(rect, texW, texH):
    translation.x = rect.left + rect.w/2 - 640
    translation.y = 360 - (rect.top + rect.h/2)        // single Y-flip about screen center
    scale.x       = rect.w / texW                       // native sprite px -> slot px
    scale.y       = rect.h / texH
    return { translation, scale }
```

**Verified against ground truth:** feeding the *correct* on-screen panel rect `(6, 6, 237, 117)`
yields `translation = (6+118.5-640, 360-(6+58.5)) = (-515.5, 295.5)` — **exactly** the working
project's `PANEL_CX=-515.5, PANEL_CY=295.5`. So the formula is right; the only requirement is that
its *input* rect be the real (on-screen) rect — which, per §2.5b, the converter gets from the JSON
for non-parked elements and from the known top-left anchor for the parked HUD group.

Hand-check on a bar (sanity): bars are `pivot/anchor [0,0.5]` (left-edge) → §a1 width-clip shrinks
`scale.x` (or clips UV) toward the left; the converter's `scale.x = w/texW` is the *full* bar; the
runtime fraction multiplies it (a1).

### 2.5d — Layout-JSON loader design spec

Read-only loader over `docs/layout/**.layout.json` using PTSD's bundled `nlohmann_json`. **Reads
only the fields `extract_layout.py` already baked — no RectTransform math in C++** (the cost-control
crux of Route 1).

```
LayoutDoc  Load(path)                       // parse once
LayoutNode Find(doc, "state_bar/hp_bar/img")  // by name-path; returns:
   { screen_rect{left,top,w,h},             // faithful (parked for the HUD group; see §2.5b)
     visual[]{ kind, sprite_png, tint{r,g,b,a}, image_type, border } }
```

**HUD as first client (call flow):** on construction, `Hud` loads `Canvas.layout.json`; reads
`state_bar/{hp_bar,armor_bar,energy_bar}/img` for the **`ui_12` png + per-bar tint + slot size +
relative offsets**, and `state_bar/bg` for **`ui_15`**; **anchors the `state_bar` group at the
top-left on-screen reference (§2.5b), not its parked rect**; runs each rect through
`ScreenRectToPtsd`; builds the bar drawables (a1) at those PTSD transforms; per frame sets each
bar's fraction from `CombatStats`.

### 2.5e — d1 refine folded into Batch-1 scope

Batch 1 is **not** "3 rects" — d1 (verified) shows the real `state_bar` group is:
- **`bg`** = one shared `ui_15` panel (79×39 → ~247×122), border `{0,0,0,0}`.
- **3 fills** = one **`ui_12`** sprite (64×7 native), **tinted** per bar (HP red `0.87,0.23,0.23` /
  armor grey `0.55,0.55,0.55` / energy blue `0.12,0.45,0.77`), `image_type 0`, **fill LtR**.
- **3 inline number `Text`s** (one per bar — so the faithful HUD *does* show numbers).
- **3 `warn` icons** (`ui_62`, low-value blink) + a **`badass_mode`** icon (`ui_11`).
- Real paths `/Canvas/state_bar/{hp,armor,energy}_bar/img`; **a3 (9-slice) NOT needed** (all borders zero).

So **Batch-1 deliverable = `bg` panel + 3 width-clip fills (1 tinted sprite) + 3 number texts +
3 warn icons (+ badass)**, all anchored as one top-left group.

---

## 2.6 Generic layout subsystem: parked/slide-in detection + external-anchor override

> **⚠ Cost boundary (do not cross).** Route 1 stays cheap only because `extract_layout.py` bakes
> `screen_rect` and C++ **only reads, never re-derives**. This extension is strictly
> **detect → mark → accept an external anchor**. It must **never** try to *derive where code
> repositions a parked group* — `state_bar` has no Animator and is code-moved, so deriving its
> runtime endpoint means re-opening Il2Cpp reverse *per slide-in group*. That is out of scope.
> The subsystem decides only *"is this group's static absolute position trustworthy?"*; the
> endpoint is **supplied by the client** (for the HUD: a known top-left constant).

### 2.6a — Parked / slide-in detection (pure JSON read, zero reverse)

Detect whether a group's static **absolute** position is trustworthy by its **rendered extent**
(its own `screen_rect` if non-degenerate, else the union bbox of its descendants' rects — needed
because container nodes like `state_bar` are 0×0):

```
classify(group, CW=1280, CH=720):
    bbox = renderedExtent(group)              // (L, T, R, B) from baked screen_rect only
    fullyOutside = B<=0 or T>=CH or R<=0 or L>=CW      // entire extent beyond one edge
    straddles    = (T<0 or B>CH or L<0 or R>CW) and not fullyOutside
    if fullyOutside: return PARKED            // static absolute pos NOT trustworthy
    if straddles:    return AMBIGUOUS         // flag; do NOT force-binary (§ honesty)
    return NORMAL                             // static screen_rect IS the final position
```
Output annotation per group: `{ parked: bool, classification, reason }` (reason = which edge / which
condition). **Detection answers only "trustworthy?", never "where does it go at runtime."**

**Verified on real `Canvas.layout.json` nodes (geometry alone, no reverse):**

| group | bbox (L,T,R,B) | verdict | note |
|---|---|---|---|
| `state_bar` | (30,−143,381,−21) | **PARKED** | fully above (B<0) — the HUD vitals |
| `text_gems` | (992,−130,1300,−30) | **PARKED** | the gem counter (visible in-game via its Animator) |
| `info_bar` | (821,−150,1270,−20) | **PARKED** | top-right cluster |
| `setting_bar` | (330,800,950,930) | **PARKED** | slide-up settings (fully below) |
| `btn_home` | (60,570,180,670) | **NORMAL** | the always-visible bottom-left button — **not mismarked** |
| `control` | (0,400,1280,1120) | **AMBIGUOUS** | virtual-joystick HUD straddles the bottom — correctly flagged |

> *Optional precision enhancement (not required, not this round):* an Animator on a node is a strong
> "slide-in" signal but the JSON does **not** record it today. Adding `has_animator` would be a small
> **additive** `extract_layout.py` field read straight from the prefab's component list (faithful YAML
> read, **zero reverse**) — it would *corroborate* and classify the mechanism (animated vs code-moved),
> but geometry already catches every case above, so it is a nice-to-have, not a dependency.

### 2.6b — External-anchor override (client supplies the endpoint; subsystem rigid-shifts)

A `PARKED` group's children keep their **faithful relative layout** (the parent→child math is
correct; only the group's own anchor against the canvas is parked). So the override is a single
**rigid translation** of the whole subtree that aligns a chosen reference element to a
client-supplied on-screen rect — **no per-child derivation, no runtime guessing**:

```
// client: "place group G so its reference child R sits at this known on-screen rect"
AlignParkedGroup(G, refChildPath, targetScreenTopLeft):
    delta = targetScreenTopLeft - staticScreenRect(refChildPath).topLeft   // 2D shift, screen space
    for each descendant d of G:
        corrected = { d.left+delta.x, d.top+delta.y, d.w, d.h }            // rigid shift; sizes kept
        d.ptsd = ScreenRectToPtsd(corrected, d.texW, d.texH)              // §2.5c verified flip
// NORMAL groups skip this entirely: ScreenRectToPtsd(d.staticScreenRect, ...) directly.
```
The client provides `targetScreenTopLeft` from a **known reference constant** (the subsystem never
computes it). For the HUD that constant is the working project's top-left inset.

**Hand-verified on `state_bar`:** align `bg` to the on-screen top-left `(6,6)` →
`delta = (6,6) − (30,−143) = (−24, +149)`. Rigid-shift the subtree:

| node | static `screen_rect.top` | shifted (`+delta`) → | on-screen? |
|---|---|---|---|
| `bg` panel | −143 | **(6, 6)** | top-left ✓ (= working-project position) |
| `hp_bar/img` | −127.4 | **(56.3, 21.6)** | ✓ |
| `armor_bar/img` | −96 | **(56, 53)** | ✓ |
| `energy_bar/img` | −65 | **(56, 84)** | ✓ |

Bars land at top 21.6 / 53 / 84 — on-screen, with the original ~31px spacing **preserved** (the shift
is rigid). Run through the §2.5c converter, e.g. `hp_bar/img (56.3,21.6,180,22)` → PTSD
`(−493.7, 327.4)` — inside `±640×±360`. ✓

### 2.6c — Integration into the converter/loader + updated HUD call flow

- **Loader** (`§2.5d`): when it reads a group, it also runs `classify()` and returns the
  `{parked, classification}` annotation alongside the node data — one extra pure-JSON pass, no new
  reverse.
- **Converter dispatch:** `NORMAL` group → `ScreenRectToPtsd(static rect)` directly. `PARKED` group →
  must have an `AlignParkedGroup(...)` anchor from the client first, then the shifted-rect path.
  `AMBIGUOUS` → loader surfaces it; the client decides (treat as normal, or supply an anchor).
- **HUD as first client (full flow):** `Hud` loads `Canvas.layout.json` → reads the `state_bar`
  subtree (`bg` + `{hp,armor,energy}_bar/img` + number Texts + `warn` + `badass`), pulling **sprite
  png + per-bar tint + sizes + intra-group offsets** from JSON → loader flags `state_bar` **PARKED**
  → `Hud` calls `AlignParkedGroup(state_bar, "bg", topLeft=(6,6))` with the **known top-left
  constant** → subsystem rigid-shifts + converts the subtree → `Hud` builds each bar's width-clip
  drawable (a1) at its PTSD transform, applies the bar's tint, and per frame sets `fraction = cur/max`.

### 2.6d — Value for future screens, and the honest residual

- **Future slide-in UI is handled automatically:** any later screen with parked/slide-in groups
  (e.g. `setting_bar`, the pause menu, result panels) is **auto-detected** by the same geometry rule
  and routed down the same "external anchor + faithful internal structure" path — **no per-screen
  hand-coding of the internal layout, and no per-screen reverse**. The subsystem scales to many
  screens, which is exactly Route 1's roadmap payoff.
- **Honest residual (bounded, by design):** for a `PARKED` group, the *endpoint anchor* is supplied
  by the client as a **known reference constant** (HUD uses the working project's top-left). **If a
  future parked screen has no ready reference value, obtaining its anchor — by behaviour observation,
  measurement, or, only if unavoidable, a narrow local reverse — is *that screen's* problem at *its*
  build time, not the generic subsystem's.** The subsystem deliberately stops at "detect + mark +
  accept anchor"; it never derives the endpoint. This keeps Route 1's cost bounded.
- **`[needs-confirm]` (ambiguity, not force-binary):** the `control` (virtual-joystick) group
  straddles the bottom edge → classified `AMBIGUOUS`. Whether its visible part is final-on-screen
  while sub-controls are parked is a per-element judgement to resolve when that HUD piece is built;
  the detector flags it rather than guessing.

---

## 3. Batch-1 render gaps (a1 width-clip · a3 9-slice · a4 ImGui→PTSD) — all **[HUD-scope]**

### a1 — width-clip drawable (the core new capability)
**What it must do:** draw a texture showing only the leading `fraction` of its width — a
**clip, not a scale** (show texture pixels `[0 .. w·frac]`, *not* a squashed full texture).
Direction-aware per §3a (`0 LtR / 1 RtL / 2 BtT / 3 TtB`); the vitals bars are presumably
horizontal LtR, **`[needs-confirm]` the exact fill direction per bar (from d1 / the real
asset).**
**Technical shape (description, not code):** PTSD's `Util::Image` draws a fixed unit quad
sampling the full `[0,1]` UV, so it cannot clip. A width-clip needs **both** the quad width
*and* the UV u-max set to `frac` (so the texture is revealed, not stretched). Interface shape:
a drawable taking `(imagePath, fraction, direction)` with a `SetFraction(float)` per frame.
**Landing point — two options (describe; the smaller fork, lean noted, not executed):**
- **(preferred) a Game-side drawable** in `src/ui/` (e.g. a `BarFill : Core::Drawable`) that
  composes PTSD primitives (its own `Core::Program`/`VertexArray` or a UV-uniform shader),
  **without forking the vendored PTSD `Util::Image`**.
- (alt) extend `PTSD/.../Util/Image.hpp` with a fraction/sub-rect param — touches the vendored
  engine; avoid unless PTSD is considered first-party here.
Lean = Game-side drawable (keeps the engine fork-free); flag for confirmation at impl time.

### a3 — 9-slice — **conditional, decided after d1**
9-slice is needed **only if the bar textures are actually sliced** (carry a non-zero
`m_Border`). That can't be known until **d1** extracts the textures and we read the sprite
`.asset` border. **Until then a3 is "TBD — possibly unnecessary."** A thin fill bar that
simple-stretches needs no 9-slice; a bar with rounded caps via slicing does. **Resolve a3 as a
by-product of d1** (the border is visible the moment the sprite is extracted).

### a4 — ImGui → PTSD wiring (the cutover)
After a1 + d1, the HUD changes from the ImGui overlay (`src/ui/Hud.cpp:7-26`:
`SetNextWindowPos/Begin/Text/ProgressBar/End`) to **PTSD screen-space sprites** drawn at the
existing hook (`GameScene.cpp:534-535`). **Change points (description, no code):**
- `Hud` holds the three `BarFill` drawables (+ their frame/background sprites, if any).
- `Hud::Draw(const CombatStats&)` sets each bar's `fraction = cur/max` and positions it
  (constants from Route 2, or JSON rect from Route 1).
- Drawing happens in screen space — either by adding the HUD GameObjects to a screen-space
  renderer pass, or by direct `Draw()` after the identity-view switch at `GameScene.cpp:534`.
- **`[needs-confirm]`:** whether the faithful HUD shows the numeric `HP: x/y` readouts at all
  (the current ImGui text may be debug-only). PTSD has `Util::Text` (Structure Report §1) if
  numbers are wanted; otherwise drop them. Keep the ImGui readouts as a debug toggle until the
  real layout is validated, if useful.

---

## 4. d1 — asset prerequisite: identify & extract the bar textures

**The hard prerequisite for Batch-1 *validation*.** The bar fill textures (and any frame/
background) are not in `docs/layout/crops/` (140 files, none `hp/energy/armor`) nor named in
`Resources/`; the static `Scene_Game.layout.json` has only `btn_skill` because the HUD bars are
**runtime-populated** (Structure Report §4). Plan to find and extract them:

1. **Locate the defining source.** The bars (`img_hp`/`img_energy`/`img_armor` Image fields on
   `UICanvas`, per the behavior spec §3a) are instantiated at runtime — they live in a
   **UICanvas HUD prefab** (or are code-assembled). Find it by grepping the AssetRipper export
   `_reverse/asset soul/ExportedProject/Assets/**` for the node names (`img_hp` etc.) or for the
   in-game HUD prefab referenced by `Scene_Game`'s `UICanvas`. **`[needs-confirm]`:** whether a
   static prefab holds these nodes at all (vs. pure code assembly) — this is the *same* question
   that gates Route 1 in §2.
2. **Extract the PNGs** with the existing pipeline: `tools/extract_layout.py <that prefab>
   --crop-dir docs/layout/crops` (the schema-v5 extractor + crop pipeline already used for the
   222-node layer). The bars' `visual[].sprite.png` become loadable via `Util::Image(path)`.
3. **a3 (9-slice) falls out here:** the sprite `.asset` `m_Border` is read during extraction —
   non-zero ⇒ a3 needed; zero ⇒ a3 unnecessary.

**Sequencing note:** Batch-1 *render code* (a1) can be written against a **placeholder texture**
(a flat-colour PNG) before d1 finishes; only **final visual validation** needs the real
textures. Skill icons already exist (`Resources/Sprite/ui_skill01-06.png`) but are a Batch-2
concern.

---

## 5. Suggested execution order (dependency-sorted)

### Batch 1 (the three bars) — start now
```
        ┌─ d1  extract bar textures ──────────────┐  (also answers a3)
parallel┤                                          ├─► a4  ImGui→PTSD wiring ─► final
        ├─ a1  width-clip drawable (placeholder) ──┤        (needs a1 + c1)      validation
        └─ c1  ROUTE DECISION (user) ──────────────┘                            (needs d1)
                                                     a3  9-slice: only if d1 shows borders
```
- **Parallelizable (no inter-dependency):** **d1** (extract), **a1** (build the width-clip
  drawable against a placeholder), and the **c1 route decision**.
- **a4** (the ImGui→PTSD cutover) depends on **a1** (needs the drawable) and **c1** (needs the
  chosen positioning approach); it works best with **d1** done but can run on placeholders.
- **a3** is conditional and is decided by **d1**; do it only if borders exist.
- **Final visual validation** waits on **d1** (real textures).

### Batch 2 (the cooldown mask) — separate milestone, after Batch 1
```
b2  wire a CharSkill into Player  ─► expose cooldown fraction  ─► a2 fillAmount drawable
   (prereq dependency, gameplay)                                  + mask texture extract
                                                                ─► wire mask into Hud
```
- **b2 [prereq dependency]** must land first (gameplay wiring; not planned here).
- **a2** (the `fillAmount`/Filled drawable — sibling of a1) and the **`skill_icon_mask`
  texture extraction** are Batch-2 [HUD-scope]; a2 *could* be built in parallel with Batch 1
  if desired, but it has nothing to drive until b2 exists.

---

## Open items carried as `[needs-confirm]` (not assumed)
1. Whether the HUD bar nodes have a **static prefab source** (vs. code-assembled) — gates both
   Route 1's usefulness (§2) and d1's method (§4).
2. The **fill direction** of each vitals bar (§3a `0/1/2/3`) — from the real asset (d1).
3. Whether the bars carry a **9-slice border** (a3) — from the sprite `.asset` (d1).
4. Whether `ArmorReloadTick` is **driven live** in `GameScene` (affects whether the armor bar
   animates; does not block display) — `GameScene.cpp:344` drives only energy.
5. Whether the faithful HUD shows **numeric readouts** (keep/drop the ImGui text) — §3a / art.
