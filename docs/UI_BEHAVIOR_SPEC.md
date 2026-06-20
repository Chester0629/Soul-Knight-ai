# Soul Knight — UI Behavior Spec (Il2Cpp layer, for the C++ reimplementation)

This document is the **code-layer companion** to the static UI data layer
(`docs/layout/`). The YAML layer answers *what is on screen and where*; this layer
answers the four things the YAML cannot:

1. **Instantiation** — which `MonoBehaviour`/method spawns a prefab, under what trigger, onto which parent.
2. **Data binding** — where dynamic values (prices, counts, `Filled` bar `fillAmount`) come from.
3. **Events** — what method a button's `onClick` actually runs, and what that method does.
4. **i2 term → translation** — how a `localize.term` becomes the on-screen string at runtime.

It is a **behavior spec written from reading the decompiled logic**, not a copy of
it: logic is described in prose, with only the factual identifiers a re-implementer
needs (method names, field names, string constants, resource paths, field offsets).
Il2Cpp is imperative code with no byte-diffable ground truth, so every claim is
**cross-corroborated** (multiple call sites, matching string/resource constants,
agreement with known UI behavior and with the YAML structure) and tagged with a
confidence level. Anything not firmly established is marked **[needs-confirm]**.

---

## Step 0 — Dump environment & quality (ready)

**Toolchain (all artifacts already on disk, no re-dump needed):**

| Artifact | Path | Use |
|---|---|---|
| Il2CppDumper output | `_reverse/tools/il2cpp_out/` | `dump.cs` (signatures + **field offsets** + RVAs), `il2cpp.h`, `DummyDll/` (Assembly-CSharp, UnityEngine.UI…), `script.json` (name↔address), `stringliteral.json` (string table) |
| Ghidra decompile | `_reverse/ghidra_export/` | `game_full.c` (1.72M lines, named `Class__Method`, raw types), `game_typed.c` (**typed** `Class_o *__this` + field names), `game_typed_index.txt` (Class$$Method → address) |
| Ghidra project | `_reverse/soul.gpr` | interactive re-analysis if needed |

**Unity version:** `2017.4.1f1` (IL2CPP, 32-bit — Ghidra shows `undefined4`/4-byte
pointers). UGUI + i2 Localization of that era.

**Dump completeness:** 61,839 named methods, 75,462 addresses, 9,517 string
literals, 4,356 type-metadata entries. Complete.

**Obfuscation:** **none** (re-verified — see the reconciliation note below). Class/
method names are fully preserved (`I2_Loc_LocalizationManager__GetTermTranslation`,
`UIWinodwShop`, …) and **every one of the 9,517 string literals is plaintext** in
`stringliteral.json` — all scene names included (`Title`, `HeroRoom`, `Loading`, …),
and they are used in readable form in code (e.g. `String.Equals(StringLiteral_8600
/*HeroRoom*/, …)`, `LoadScene(StringLiteral_8735 /*Title*/)`). No string encryption,
no name mangling beyond Ghidra's own.

> **Reconciliation — there is no "encrypted string" anywhere (corrects an earlier
> Step-3 note).** Some functions show `**(undefined4 **)(&UNK_00xxxxxx + _UNK_00yyyyyy)`
> and a prior breadth-scan note called this "encrypted `&UNK_*` string indirection."
> That was a misread. The string-literal data lives at `0x15ED7E0–0x15F6C90`; the
> `&UNK_00xxxxxx` sites sit at `0x0074xxxx` — the **code / metadata-usage region** —
> and the values they load are `MethodInfo*` / `TypeInfo*` / class-init flags (the
> function signatures literally type them `MethodInfo*`), **not strings**. So the
> obfuscation verdict ("none") and the next-scene gap are *both* correct and
> consistent: nothing is encrypted; where a scene target looks "missing" it is simply
> a **truncated tail** (below), and the scene name is a plaintext literal once the tail
> is recovered.

**Navigation recipes (validated):**
- *Logic by name:* grep `game_full.c` for `ClassName__Method(`. Functions missing
  there (some `Awake`/`Start`/`ShowWindow`) are present in `game_typed.c` with real
  types — the two exports cover **different** function sets, so check both.
- *Field offsets:* read the class in `dump.cs` (e.g. `Localize.mTerm // 0xC`); a raw
  access `*(param_1 + 0xC)` in `game_full.c` is then that field. `game_typed.c`
  often already shows `__this->fields.<name>`.
- *String literals:* Ghidra labels them `StringLiteral_N`; **`N` maps to
  `stringliteral.json[N-1]`** (1-based, verified). Helper: `_reverse/tools/strlit`
  pattern — `sl[N-1].value`. This turns every `StringLiteral_N` back into its text.
- *Address ↔ name:* `game_typed_index.txt` and `script.json`.

**Friction & its fix (litmus-tested):** Ghidra did not recover types in `game_full.c`
(`undefined4` + offset access), and — the big one — the shared-generic helper
`Singleton<>.get_Inst` is rendered as a self-calling stub and **flagged
non-returning**, so in `game_full.c` / `game_typed.c` **every caller truncates at the
`Singleton<T>.get_Inst(...)` tail** (`// WARNING: Subroutine does not return`), hiding
the method actually invoked on the singleton.

> **Recovery recipe — the truncated tails are already on disk; no Ghidra re-run
> needed.** The *earliest* raw export **`libil2cpp.c`** was produced before that
> noreturn flag was set, so it contains the **full** bodies. To recover a truncated
> tail: find the function's address (`game_typed_index.txt`), read it in `libil2cpp.c`
> as `FUN_<addr>`, and resolve each `FUN_<callee>` back to a name by integer address
> lookup in `game_typed_index.txt` (mind the 8-digit zero-padding). Verified end-to-end
> on `UITitle.BtnNewGameClick`: the part truncated in `game_typed.c` is, in
> `libil2cpp.c`, `RGGameProcess.ReSetInfo()` + `NetBattleData.SaveEmptyData()` +
> `RGMusicManager.GetInstance()` — i.e. new-game reset semantics (cross-corroborated by
> intent). Caveat: the *shared* `get_Inst` helper resolves to a `+offset` inside a
> shared code region (it's the generic dispatch thunk, not a distinct method) — that's
> expected; the meaningful callees around it resolve exactly.

Other mitigations: read the **typed** `game_typed.c` where available, decode offsets
via `dump.cs`, and mark genuinely-unrecovered tails **[needs-confirm]** rather than
guess.

---

## Step 1 — Vertical slice: `window_shop` (the gem/IAP store)

The pilot screen. It exercises all four unknowns: instantiation, dynamic values,
buy-button events, and localized text.

### Identity (cross-corroborated three ways)

The YAML prefab `res/ui/window_shop.prefab` ⇔ the class **`UIWinodwShop : RGUIWindow`**
(class name is a real in-game typo, *Winodw*). Evidence, three independent lines:

1. **Resource path:** `UICanvas.ShowUIWinowShop` loads `StringLiteral_9077`, which
   resolves to the literal **`"window_shop"`** — the exact prefab name.
2. **Structure ↔ methods:** the prefab's 5 `btn_buy` + `btn_back` match the class's
   `BtnOption1Click…BtnOption5Click`, `BuyCoin1/2/3`, `BuyRebornCard`, `BtnBackClick`.
3. **Values:** the prefab's price-value texts `5000 / 12000 / 25000` equal the gem
   amounts hard-coded in `BuyCoin1 / BuyCoin2 / BuyCoin3`.

Confidence: **high.**

### 1a. Instantiation — who spawns it, when, where

- **Spawner:** `UICanvas.ShowUIWinowShop()`. It calls `ResourcesUtil.Load("window_shop")`
  (a `Resources.Load` wrapper — **not** an AssetBundle path for this prefab) and then
  `UnityEngine.Object.Instantiate(prefab)`. So the load is by **name from a Resources
  folder**, and the prefab is instantiated whole. Confidence: **high.**
- **Parent:** `Instantiate` here is called **without an explicit parent transform**;
  the instantiated root then takes its transform. The prefab is a `degenerate_root_size`
  full-screen widget in the YAML (no Canvas of its own), so the host Canvas parenting
  is established by the `RGUIWindow` base / the instantiated object's own setup rather
  than by an argument at this call site. Exact parent Canvas: **[needs-confirm]**
  (candidate: the singleton `UICanvas`'s own canvas, since `UICanvas` is the spawner).
- **Trigger:** `ShowUIWinowShop` has **no direct caller** in the decompile — it is
  invoked as a **button `onClick` delegate** (a `System.Action`/`UnityAction`),
  matching the YAML's "`m_OnClick` recorded by name only". A second entry point,
  `UIChoseHero.ShowShopUI`, routes through `UICanvas.GetInstance()` (→ the same show
  path), i.e. the hero-select screen opens the same store. Confidence: **high** for
  "opened via a UI button delegate"; the precise owning button is in the scene wiring,
  not the code.

> Re-impl note: model this as `UICanvas::ShowWindow(ResourceName)` →
> `Instantiate(Resources::Load(name))` → parent under the UI canvas. The trigger is a
> button event bound in the scene/prefab, surfaced in the YAML by target name.

### 1b. Lifecycle & input (from `game_typed.c`, typed fields)

- **`Awake`** sets `fields.use35000 = true` (a config flag — see 1d) and caches its
  transform.
- **`Start`** initializes the platform layer: `Singleton<GoogleGameCenter>.Inst`
  (Google Play Games / IAP backbone).
- **`Update`** handles dismissal: on the back **input axis** (`Input.GetButtonUp(...)`)
  or **Esc** (`Input.GetKeyDown(0x1B)`), if the window is active (`fields.awake`) it
  runs the close path (`BtnBackClick`). Confidence: **high.**

Discovered fields (names from `game_typed.c`): `awake` (active gate), `adShown` (an
"animation/ad is showing" flag), `anim` (the window `Animator`), `use35000`.

### 1c. Events — what each button runs

| Prefab node | Handler | Behavior (prose) | Confidence |
|---|---|---|---|
| `btn_back` (返回) | `BtnBackClick` | If active and `adShown`, play the close animation via `Animator.SetBool("show", false)` on `fields.anim`, then route through the platform singleton to finish closing. | high |
| `btn_buy` ×5 | `BtnOption1Click … BtnOption5Click` | Gate on active, then hand off to the platform store (`Singleton<SDKManager>.Inst …`) to **initiate** the in-app purchase for that option. The per-option SDK product id is a platform detail — **out-of-scope stub** (§4), not a gap. | high (UI) |
| (purchase success callback) | `BuyCoin1 / BuyCoin2 / BuyCoin3` | Called with a success flag; on success: close the window (`BtnBackClick`) and log analytics `FirebaseUtil.LogEvent("buy_gem", number = <amount>)`, where amount = `5000 / 12000 / 25000` (tier 3 → `35000` when `use35000`). | high |
| (revival card) | `BuyRebornCard` | Purchase path for the `免广告复活卡` (RevivalCard term) item. | med |
| (rewarded video) | `GetCoinVideo(success)` | On success, set the bonus flag (`+0x29`) and read `Singleton<PlayerSaveData>.Inst` (credit the watch-video reward). | med |
| (after purchase) | `HideUILoading` | `GameObject.Find("/Canvas/ui_loading")` → if present, `Destroy` it (removes the loading spinner). | high |

> The split is the standard IAP shape: `BtnOptionNClick` **initiates** the purchase
> through the SDK; the SDK calls back into `BuyCoinN` on success, which credits/logs
> and closes.

> **⚠ Port boundary — the real-money path is a deliberate stub, not a gap to solve.**
> The C++ reimplementation does **not** connect to Google Play billing, so the SDK
> purchase call, the receipt validation, the `FirebaseUtil.LogEvent` analytics, and the
> exact SDK-side gem-credit method have **zero porting value**. Reimplement only the
> **UI flow**: button → confirm/animation → close window → refresh the displayed gem
> balance. Model the SDK call as a stub — *fake success* → directly credit the local
> gem balance by the (known, hard-coded) package amount; *fake failure/cancel* → take
> the cancel path. Accordingly, the former gem-credit `[needs-confirm]` is **reclassified
> as out-of-scope (intentional platform stub)**, not an open gap — see the stub-boundary
> list in §4. The same applies to `GetCoinVideo` (rewarded-ad SDK) and any `*ShareClick`
> → share-SDK tail: stub them.

### 1d. Data binding

- **Gem package amounts** (`5000 / 12000 / 25000`, and `35000` when `use35000` is set
  in `Awake`) are **hard-coded in `BuyCoinN`** and shown **statically** by the prefab
  texts. The USD prices (`USD 1/2/3`) are likewise static prefab text (the real price
  comes from the store product, but the displayed label is static). Confidence: **high.**
- The prefab's `"200"` text near the `宝石商店` header is most likely the **current gem
  balance** read from `PlayerSaveData`, but its binding site was **not** visible in
  `Awake/Start/Update/ShowWindow` (truncated tails / not in this export) →
  **[needs-confirm]** (candidate: `ShowWindow` tail or a separate gem-counter widget
  reading `PlayerSaveData`).
- **`Filled` bars:** this screen has **none** (it is a purchase popup). The `Filled`
  binding pattern (skill cooldown / HP) belongs to in-game HUD screens and is scoped to
  Step 3.

### 1e. Text → see Step 2

`window_shop` carries three `localize.term`s from the YAML: `Gem shop` (宝石商店),
`RevivalCard` (免广告复活卡), `I_back` (返回). At runtime each is resolved by its
`Localize` component through the i2 path below. The static `text` in the YAML is the
zh-CN editor preview; the runtime string is the i2 lookup.

---

## Step 2 — i2 term → translation resolution

### Load / query entry points

- `LocalizationManager` (static) is the query hub. `LocalizationManager.InitializeIfNeeded`
  loads the registered sources (the `LanguageSource` on `I2Languages.prefab`) and the
  current language; `get_CurrentLanguage()` returns the active language name (a static
  field set from the saved/device locale).
- A `Localize` component drives its own node: `Localize.OnLocalize()` reads its
  `mTerm` field (object offset `0xC`) and asks `LocalizationManager.GetTermTranslation(term)`,
  then writes the result into the target graphic (the `Text`).
- `LocalizationManager.GetTermTranslation(term)` → `TryGetTermTranslation` iterates the
  registered sources and calls each source's `LanguageSource.TryGetTermTranslation`.

### The join (the heart of it)

`LanguageSource.TryGetTermTranslation(source, term, out translation)`:

1. `lang = LocalizationManager.get_CurrentLanguage()`.
2. `langIndex = LanguageSource.GetLanguageIndex(source, lang)` — language **name → 0-based
   index** into the source's language list.
3. `termData = LanguageSource.GetTermData(source, term)` — **term → `TermData`** via the
   source's dictionary; null if the term is unknown.
4. If `langIndex < 0` or `termData == null` → output **empty**, return fail.
5. Else `translation = TermData.GetTranslation(termData, langIndex)`, which indexes the
   **parallel string array** `TermData.Languages[]` (object offset `0x18`; the array's
   length is bounds-checked) at `langIndex`.
6. Missing/empty handling by the source's mode field (`+0x40`): mode 2 → return the
   placeholder `String.Format("<!-Missing Translation [{0}]-!>", term)`; mode 1 → fall
   back to the first non-empty translation in **any** language.

So the data model is exactly a **join keyed by term**:

```
term ──GetTermData──▶ TermData
                       └─ Languages : string[]   // one slot per language, fixed order
currentLanguage ──GetLanguageIndex──▶ langIndex
runtime_string = TermData.Languages[langIndex]
```

### Language order (empirical, from `I2Languages.prefab`)

`[0]=en, [1]=zh-TW, [2]=zh-CN, [3]=ja, [4]=ko, [5]=es, [6]=de, [7]=pt, [8]=fr, [9]=ru,
[10]=pl` (the `TermData.Languages` array order; some terms also carry `it`/extra slots).
**zh-CN is index 2** — which is why the YAML editor previews are the zh-CN strings.

### Worked examples (join validated end-to-end)

- `term = "New Game"` → `TermData.Languages = [New Game, 新遊戲, 新游戏, ニューゲーム, 새로운 게임,
  Nuevo juego, Neues Spiel, Jogo novo, Nouveau jeu, Новая игра, Nowa Gra]` → zh-CN
  (index 2) → **`"新游戏"`**. Matches the YAML node's editor `text` for the `New Game`
  localize node. ✓
- `term = "Gem shop"` (the `window_shop` header) → zh-CN slot → **`"宝石商店"`**, matching
  the prefab's editor text. ✓

### How the 47 schema-v5 `node→term` rows join

For each YAML node with `localize.term`, the runtime string is
`GetTermData(localize.term).Languages[ GetLanguageIndex(deviceLocale) ]`. The schema-v5
extraction already supplies the **left** side of this join (the term as a stable key);
the **right** side is a single dictionary-plus-array lookup at the language index above.
No reverse-match on the ambiguous editor `text` is needed. (Dumping the full
701-term × N-language table to a file is separate data-engineering, out of scope here —
this step establishes the *method*.)

---

## Step 3 — UI behavior map (breadth scan of the remaining screens)

The Step-1 template applied across the 8 other scenes + the 16 `RGUIWindow` popups +
the in-game HUD, cross-corroborated against the YAML layer. **Coverage: ~24 UI
surfaces — 16 `high`, 8 `partial`, 0 `low`.** Every `partial` shares one honest root
cause (below), not scattered guesswork.

### 3a. Filled / dynamic-binding findings — VERIFIED (the highest-value gap)

The static layer could resolve *no* runtime value. The HUD bindings all live on
`UICanvas` (Scene_Game / Scene_Tutorial), fed from
`RGGameSceneManager.GetInstance().controller.role_attribute` (a `RoleAttributePlayer`).
**Independently verified by re-reading the functions** (not just trusting the scan):

| HUD element | Bound to | Mechanism | Method |
|---|---|---|---|
| HP bar (`img_hp`) | `role_attribute.hp / max_hp`, `Mathf.Min(·,1)` | **`RectTransform.sizeDelta` (width)** | `UICanvas.UpdateHpBar` |
| Energy/mana bar (`img_energy`) | `energy / max_energy` | **`sizeDelta` (width)** | `UICanvas.UpdateEnergyBar` |
| Armor bar (`img_armor`) | `armor / max_armor` | **`sizeDelta` (width)** | `UICanvas.UpdateArmorBar` |
| **Skill cooldown** (`skill_icon_mask`) | cooldown elapsed/total | **`Image.fillAmount`** (the *only* HUD fill) | `UICanvas.UpdateSkillMask` |
| Floating combat text | the `int amount` arg | spawns `UITextInfo` under `/Canvas_world/{hp_,coin_,energy_,armor_}` | `UICanvas.ShowTextHp/Coin/Energy/Armor/Hurt` |

> **⚠ Corrects a data-layer assumption.** The layout README warned the port "MUST
> implement `Filled` … skill cooldown, HP/mana." Reality: **only the skill-cooldown
> mask is `Image.fillAmount`.** HP / energy / armor are **width-scaled `RectTransform`
> bars** (`set_sizeDelta`), not Filled images. Evidence: of the binary's three
> `set_fillAmount` sites, one is the setter itself, one is `Slider.UpdateVisuals`
> (dead — this game has zero UGUI Sliders, see the layout README's Slider section),
> and the lone HUD one is `UICanvas.UpdateSkillMask` (`fillAmount = value1/value2`).
> `UpdateHpBar` ends in `img_hp.RectTransform.set_sizeDelta(...)`. → The C++ port
> should render HP/energy/armor as width-clipped bars and reserve radial/linear
> `Filled` for the skill-cooldown mask.

Other resolved dynamic bindings: `UIWindowReborn.BtnCardClick` is the genuine revive
(`RGController.Reborn()`); revive/try-again gem costs are literals (`reborn_gem` = 200
reborn / 1000 try-again); `UISlot` (gacha) reward is driven by a `reslut[]` symbol
array with pity `slot_rate_level*10 + 40`.

### 3b. Screen map

Instantiation column: "scene root" = a scene-resident controller (`Awake`/`Start` on
load); "Resources.Load" = a genuine `ResourcesUtil.Load(name)+Instantiate`; "canvas
RGUIWindow" = a pre-placed window under the persistent `UICanvas`, shown by toggling
its Animator `show`/`awake` rather than instantiated.

| Screen | Class | Instantiation (+trigger) | Main events | Special binding | Conf |
|---|---|---|---|---|---|
| Title | `UITitle` | scene root (from Splash `LoadScene("Title")`) | `BtnNewGame/Continue/Multiplayer/Quit Click`, `ShowSettingBar`, `SwitchLanguage`, `BtnWindowClick(int)` | `level_info` "0-0"; lang→i2 `set_CurrentLanguage` | high |
| Scene_Splash | `UISplash` | scene root (boot) | auto-advance: `NextScene`→`LoadScene("Title")` | delay←`logoTime` | high |
| Loading | `UILoading` | scene root (between scenes) | `StartLoad`→async load coroutine | target←`RGGameProcess` | partial |
| Statements | `UIStatement` | scene root (post-run via Loading) | `OnBtnClick`→save+advance; `AddGem`/`SetUpDetialInfo`/`UnLockObject` | coin/kill/time/gem texts←run stats | high |
| Scene_Game (HUD) | `UICanvas` | scene root | `ShowWindowPause`, `UpdateSkillMask`, `SwitchAtkBtn`, `ShowText*`, `Update{Coin,GemText}` | **§3a** (HP/energy/armor + skill fill) | high |
| Pause window | `UIWindowPause` | canvas child (`ui_window_pause`) | `BtnContinue/Home/Setting Click`, `BtnYes/No` (quit confirm) | `UpdateInfo` img_hero/buff_list | high |
| Scene_Tutorial | `UICanvas` (+overlay) | scene root | same HUD + `object_tap`/`message_bar` | same as Scene_Game | high |
| Scene_Plot | (no UI controller surfaced) | scene root (cutscene) | external plot sequencer | term `I_plot1` only | partial |
| HeroRoom | `UIChoseHero`(:RGUIWindow) | real scene | `UIBtnChoseHero.BtnClick`→`GetChoice`, `SkinBtnNext`, `BtnConfirm`, `ShowShopUI`, `UnLock*` | hero/skin index→sprites; lock←`RGSaveManager` | high |
| Scene_Net_Menu | `UIMulMenu`(:NetworkBehaviour) | real scene (lobby) | `OnBtnPlayer/CharSkin/Ring Click`, `OnBtnYes/No` (state 0→1→2), `OnBtnStartClick` | p1..p4←`SetUpInfo`; rooms←broadcast list | high |
| UIWindowFurniture | `UIWindowFurniture` | held ref; owner `ObjectBook.SetInfo+ShowWindow` | `BtnYesClick`→`RGSaveManager`, `CloseBtnClick`, `SetInfo` | gems/info←owner `gem_list[]`/`info_list[]` | high |
| UIWindowPractice | `UIWindowPractice` | pre-placed; `ShowWinodw` Animator `show` | `BtnPlayerClick(0..7)`, `BtnWeaponClick`, `BtnBuffClick`, `BtnContinue/Home` | `img_player`←`player_sprite_list`; weapon="weapon_"+i | high |
| UISlot (gacha) | `UISlot` | pre-placed; `ShowWindow` pauses+Animator | `RollBtnClick`→staggered `RollTap`, `EndRoll`, `GetWeapon`, `On{Add,Sub}Click` | reward←`reslut[]`; pity=`slot_rate_level*10+40` | high |
| UIWinodwTryAgain | `UIWinodwTryAgain` | canvas RGUIWindow; trigger `RGGameProcess.TryAgain` | `BtnGemClick` (gem revive), `CloseBtnClick` | `btn_gem`←`reborn_gem`=1000 | high |
| UIWindowReborn | `UIWindowReborn` | canvas RGUIWindow; shown on death | `BtnCardClick`→`controller.Reborn()`, `BtnGemClick`, `BtnYesClick` (ad/free) | `btn_gem`←`reborn_gem`=200 | high |
| UIWinodwShowObject | `UIWinodwShowObject` | canvas; `SetUpWindow(Sprite,string)` | `BtnClick` (dismiss) | icon/name←args | high |
| UIWindowShareUnlockSkin | `UIWindowShareUnlockSkin` | `ShowWindow` (CN/EN by `systemLanguage`) | `BtnShareClick`→`CaptureCamera`, `UnLockSkin` | art←`is_cn` sprite lists | high |
| UIWindowStatistics | `UIWindowStatistics` | canvas; forwarded | `CloseBtnClick` | `text_info`←external caller | partial |
| UIWindowCertificate | `UIWindowCertificate` | canvas; `ShowWindowGet(int)`/`ShowWindow(int)` | `BtnCloseClick`, `BtnShareClick`→capture | fields←`f_index` (helper, not in body) | partial |
| UIWindowPlutus | `UIWindowPlutus` | `ObjectPlutus.ItemTrigger` Load+Instantiate | `BtnYes/No/OptionClick`, `BuyFishClip1`, `UpdateTextInfo` | text←`PlayerSaveData` | partial |
| Exhibition: Skin / generic / MagicStone / Weapon | `UIWindowExhibitionSkin`, `UIWinodwExhibition(+MagicStone/Weapon)` | pedestal `Object*.ItemTrigger` Load+Instantiate; `ShowWindow(args)` | `CloseBtnClick`, `Update` | icon/name/info←args; weapon stats←`WeaponIntroduceXmlReader` (field-writes truncated) | partial |

(The `window_shop` / `UIWinodwShop` row is the fully-worked anchor in Step 1.)

### 3c. Priority for deeper dives — re-ranked after the verification pass

**Root cause, corrected.** The earlier note ("loaders sit *outside both exports*;
*re-run Ghidra* to unblock") was wrong on both counts:
- The death-flow loaders are **in** the exports and already readable —
  `UICanvas.ShowUIWinodReBorn` (→ `Resources.Load("window_reborn")`+Instantiate),
  `UICanvas.ShowUIWinowObject`, and `RGController.Reborn` (→ un-freeze the player
  Rigidbody2D + `Resources.Load("effect_reborn")`+Instantiate). So the old priority
  #3 is **not a gap** — the death/revive spawn mechanism is recovered.
- The real blocker is the **`Singleton<>.get_Inst` noreturn-truncation** (Step 0
  friction note). **Re-running Ghidra would not fix it** (same noreturn flag → same
  truncation). The working fix is the **`libil2cpp.c` cross-read recipe** (Step 0
  note), which needs no re-run. Litmus-proven: the Title "New Game" next-step,
  truncated in `game_typed.c`, recovers in `libil2cpp.c` as
  `RGGameProcess.ReSetInfo()` + `NetBattleData.SaveEmptyData()` (+ BGM).

So the `partial`s are **more solvable than the map implied** — most are reachable by
the `libil2cpp.c` cross-read — *except* the parts that bottom out in a platform SDK,
which are deliberately not solved (§4). The two are now listed separately.

**A. Real gaps with re-impl value (recoverable via the `libil2cpp.c` cross-read).**
Each is tagged with an **MVP grade**: is it on the *current* MVP combat-loop path?

> **⚠ The MVP grades are a snapshot (as of 2026-06-13), keyed to the port's state at
> that moment — not a verdict on the gap.** At snapshot the C++ port's `scenes/` has
> only `GameScene` (a **single floor** — no floor-progression, no win/lose, no scene
> switch, no death→results), `ui/` has only a placeholder **ImGui** `Hud`
> (`fillAmount`: 0, no faithful bars), and there is **no `Reborn`/`Localiz` module**.
> The grades answer *"is this on the current MVP combat-loop path?"* — they will shift
> as the port grows (a main menu / hero-select especially flips the `#3b` **menu**
> subset from post-MVP). **MVP-value ≠ re-impl-value:** `not-MVP` does **not** mean
> "don't build it" — only "not on the critical path now → defer to the post-loop
> *content & meta* milestone."

1. **`#3a` Scene next-target — *in-run* subset** — `[MVP-critical]` — floor-clear →
   (Loading?) → next floor; death → retry / results. *This* is what turns the single
   playable floor into a *run*; it's the next meta-behaviour the port needs.
   **Resolved this round — see §3e.**
2. **`#3b` Scene next-target — *menu* subset** — `[post-MVP]` — Title `New Game`→HeroRoom,
   `Continue`→last run, Statements→next. Needs a main menu + hero-select to exist first.
   *Recipe demonstrated on `BtnNewGameClick`* (recovers `RGGameProcess.ReSetInfo()` +
   `NetBattleData.SaveEmptyData()`); finish when those screens are built.
3. **Exhibition `ShowWindow` field-writes** (4 windows) — `[not-MVP]` — args→`set_text`/
   `set_sprite`; Weapon variant's `info1..4`→stat mapping is richest. Highest cluster,
   but pedestal-triggered gallery popups the combat loop never opens. **Deferred.**
4. **`UIWindowCertificate` `f_index`→population** — `[not-MVP]` — the full-game
   **victory** card. **Deferred.**
5. **`UIWindowStatistics.text_info`** — `[not-MVP]` — lifetime-stats menu popup,
   composed by the external caller (`UIStatement`/`RGGameProcess`). **Deferred.**
6. **`UIWindowPlutus`** prices/grants — `[not-MVP]` — optional in-dungeon NPC; a floor
   clears without ever touching it. (Prefab name is a plaintext literal once the
   `ItemTrigger` tail is cross-read — *not* obfuscated, per Step 0.) **Deferred.**

> **This round:** only `#3a` is dug out (it's on the MVP critical path). `#3b` and
> items 3–6 stay deferred.

**B. Intentional platform stubs — OUT OF SCOPE for the re-impl (do not invest):** see
§4. These are *not* "to-confirm gaps"; they were reclassified out.

**C. Genuinely hard / suggest behaviour observation instead of code:**
6. **Scene_Plot cutscene controller** — no UI controller surfaced in the grep; the page-sequencing/skip/advance may be driven by a non-`UI*` plot system. Low gameplay-binding value; if a code trace stays empty, **resolve by running the scene and observing** (page cadence, skip behaviour) rather than forcing a decompile.

### 3d. Confidence note

The map is breadth-first (entry + main events + binding, not full excavation). `high`
rows are cross-corroborated (prefab-name string ↔ loader, button nodes ↔ handler
methods, YAML structure). The remaining `partial` rows are **located** (class/methods/
fields named); their innermost field-writes are recoverable via the `libil2cpp.c`
recipe (list A) or are intentional stubs (§4). The §3a Filled finding was re-verified
by direct re-reading; the obfuscation reconciliation and the recovery recipe in Step 0
were litmus-tested this pass.

### 3e. `#3a` In-run scene transitions — RESOLVED (recovered via the `libil2cpp.c` recipe)

The MVP-critical gap: what turns the single `GameScene` floor into a *run*. Owner of the
queued scene is **`RGGameProcess`** (a `Singleton<RGGameProcess>`). It has **no
dedicated next-scene field** — the floor counter `this_index` (field `+0x14`) plus the
path taken pick one of two **static scene-name slots** (`DAT_01605fb0` "normal",
`DAT_01605fb8` "overflow/run-end"). All four transition methods truncate at
`Singleton<>.get_Inst` in the named exports and were read from `libil2cpp.c`.

**Floor-clear → next floor — `RGGameProcess.NextScene()`** *(mechanism: high)*:
1. `this_index += 1`; `RGGetPath.ResetDic()` (reset the room/path cache).
2. **5-floor cycle**: `this_index % 5 == 1` → boss/special-floor `PrefabPool` prep
   (one call); else normal-floor prep (two calls). *(Cross-corroborated: the same
   `% 5` boundary appears independently in `GameFail`'s `this_index == (this_index/5)*5`
   check.)*
3. progress/unlock counters bumped; `start_time (+0x34)` stamped on floor 1.
4. **Floor cap = 16**: `this_index >= 17` clamps it to `16` and switches the queued
   scene slot `fb0 → fb8`.
5. **Load the queued scene**, branching on `NetControllerManager.get_playerCount()`:
   `== 0` (single-player) → a `SceneManager`-style load on the scene-manager singleton;
   else (multiplayer host, gated by `get_isClient`) → `RGGameInfo.SendCliendReadyToNextScence(scene)`.

**Death → run-end — `RGGameProcess.GameFail()`** *(transition: high; stat math: out of
scope)*: updates the run save/stats (gem/coin/kill via the save singleton — *not traced,
out of scope*), then loads `DAT_01605fb8` (the **same** slot as the floor-cap overflow).
The death **window** itself (`UIWindowReborn`/`UIWinodwTryAgain`) is already `high` and is
shown *before* this — the player either revives (`UIWindowReborn.BtnCardClick` →
`RGController.Reborn()`, which stays in `Scene_Game`, already spec'd) or declines into
`GameFail`/`StateMents`.

**Retry — `RGGameProcess.TryAgain()`** *(high)*: `this_index -= 1` (step back one floor),
then re-enters the load path via the save singleton.

**Results — `RGGameProcess.StateMents()`**: routes to the **`Statements`** results scene
(that scene's own button→save→advance logic is already `high`).

**Scene identities (cross-corroborated):** the gameplay floor scene is **`Scene_Game`**
— confirmed by the *readable* literal in `RGGameInfo.SendCliendReadyToNextScence(…,
StringLiteral_8694 /*"Scene_Game"*/)` on this transition family — and **`Loading`** is the
inter-scene (used in an active-scene `String.Equals(StringLiteral_8693 /*"Loading"*/)`
gate). **`Statements`** is the results scene.

**`[needs-confirm]` (honest limit — not fabricated):** the exact string held by
`DAT_01605fb0` (normal next-floor) vs `DAT_01605fb8` (overflow + death) are **runtime
string-literal *cache slots*** (region `0x1605xxx`, above the literal-data region) and
are **not** resolved to a literal in any export. Reasoned candidates from the evidence:
`fb0` = the per-floor scene (`Scene_Game`, possibly via `Loading`); `fb8` = the run-end
scene (`Statements`, since *both* dying and clearing the 16-floor cap route there). To
finish: resolve the il2cpp string-literal cache init for those two slots, **or** confirm
by behaviour observation (clear a floor / die and watch which scene loads). The *grades
above do not depend on this* — the transition methods, the `this_index` counter, the
5-floor boss cycle, the 16-floor cap, and the SP/MP split are all established.

**Evidence:** `RGGameProcess` fields (`this_index` 0x14, `start_time` 0x34, `game_type`
0x10) — `dump.cs`; method addrs — `game_typed_index.txt` (`NextScene` 0x5df264,
`TryAgain` 0x5df864, `StateMents` 0x5dfc6c, `GameFail` 0x5cc948); bodies — `libil2cpp.c`
(`FUN_005df264` etc.); callees resolved exact: `RGGetPath.ResetDic`,
`NetControllerManager.get_playerCount`/`get_isClient`,
`RGGameInfo.SendCliendReadyToNextScence`; scene literals `StringLiteral_8693`="Loading",
`8694`="Scene_Game", `8695`="Statements".

## §4 — Stub boundaries (platform/back-end calls the re-impl does NOT reproduce)

These tails were previously tagged `[needs-confirm]`; they are **reclassified as
intentionally-unresolved** because the C++ reimplementation does not wire real
storefronts, ads, social, achievements, or netcode. Reimplement the **UI flow around
them**; replace the call itself with a local stub. Do **not** spend trace time here.

| Stub boundary | Where | Re-impl substitute |
|---|---|---|
| IAP purchase + receipt (`Singleton<SDKManager>`) | `window_shop` `BtnOptionNClick`/`BuyCoinN`, `UIWindowPlutus` buy | fake success → credit the hard-coded gem amount locally; fake cancel → cancel path |
| Purchase analytics (`FirebaseUtil.LogEvent("buy_gem", …)`) | `BuyCoinN` | no-op |
| Rewarded-video ad SDK | `UIWinodwShop.GetCoinVideo` | fake "ad watched" → grant the reward locally |
| Share sheet SDK (after the portable `CaptureCamera` screenshot) | `UIWindowShareUnlockSkin.BtnShareClick`, `UIWindowCertificate.BtnShareClick` | keep the screenshot; stub the OS share invocation |
| Google Play Games / Game Center (sign-in, achievements, cloud entry) | `Singleton<GoogleGameCenter>` tails across many windows | stub (always-signed-out or a local no-op) |
| Netcode host/join internals | `UIMulMenu.OnBtnStartClick` and lobby net calls | out of UI scope (separate netcode layer) |

> Note: the screenshot capture (`CaptureCamera`) itself *is* portable and worth
> reproducing; only the subsequent platform **share** call is the stub.

## Methodology (the template Step 3 reuses)

For each screen: (1) find the prefab name string → the `ShowX`/loader method that
`Resources.Load`s it (instantiation + parent); (2) map prefab button nodes → class
`BtnX`/`OnX` handlers and read what they do (events); (3) find where dynamic texts and
any `Filled.fillAmount` are written (binding); (4) list its `localize.term`s (text →
Step 2 join). Corroborate every "X spawned on Y" with a second signal (a second call
site, a matching resource/string constant, or agreement with the YAML structure), and
tag unproven tails **[needs-confirm]**. Scope is the **UI behavior layer** only; core
combat/gen/save logic is out of scope unless a UI binding reaches into it.
