# Soul-Knight-ai — Harness Engineering

How we rebuild Soul Knight on the **PTSD** engine: a repeatable, verifiable
production line from the extracted Unity data to a playable C++ game. Three
layers. See the approved plan at `~/.claude/plans/dynamic-puzzling-spring.md`.

> Source of truth for game logic/balance: **`1.07/`** (readable JSON/XML).
> **`1.7.10/`** is a structural blueprint + a reverse-engineering track that may
> later overwrite values. Both dumps are local-only (git-ignored).

---

## Layer 1 — Data pipeline (`tools/`)

Offline tools turn the 1.07 dump into engine-facing tables. The C++ game reads
**only** the generated outputs under `Resources/data/`, never raw Unity files.

| Tool | Input | Output |
|------|-------|--------|
| `import_assets.py` | `1.07/Sprite,AudioClip,Font` | `Resources/{sprites,audio,fonts}/` + `Resources/data/sprite_manifest.json` |
| `build_gamedata.py` | `1.07/MonoBehaviour/*.json`, `1.07/TextAsset/{chest_data,weapon_introduce}` | `Resources/data/{weapons,enemy_guns,bullets,enemies,bosses,buffs,characters,weapon_catalog,droptables}.json` + `_report.json` |
| `validate_data.py` | `Resources/data/*.json` | exit 0 = green gate; non-zero = blocked |

```bash
python tools/build_gamedata.py     # transpile 1.07 -> data tables
python tools/import_assets.py       # copy art/audio + build sprite manifest
python tools/import_assets.py --manifest-only   # fast: rebuild manifest only
python tools/validate_data.py       # Phase-0 gate
```

**Known gap (tracked, not hidden):** Unity cross-references are `m_PathID`s into
the original serialized files. `build_gamedata.py` auto-resolves what it can via
each file's `m_GameObject.m_PathID` and lists the rest in
`Resources/data/_report.json`. Scalar balance values (atk, bullet_speed,
deviation, repel, consume, shoot_cd, …) extract cleanly. The gun→bullet and
Gun*↔weapon_NNN↔sprite linkages are filled by a hand-authored mapping (for the
vertical slice) and/or the IL2CPP RE track.

---

## Layer 2 — Engine primitives (PTSD core)

Generic, reusable systems sink into `PTSD/` (decision #3); game-specific logic
stays in this repo's `src/`. Planned additions (Phase 1):

`Core::Scene` / `Core::SceneManager` · `Core::Camera2D` (+shake) ·
`Util::Collider` (AABB+Circle) / `Physics::CollisionWorld` ·
`Util::ObjectPool<T>` · `Util::ParticleEmitter` · fixed-step update in
`Util::Time`/`Core::Context` · `Util::DataStore` (wraps `nlohmann_json` +
`Util::AssetStore<T>`) · virtual `Util::GameObject::Update(float dtMs)`.

Reuse, don't rebuild: `Util::{AssetStore,Animation,Renderer,Input,Keycode,SFX,
BGM,Text,Image,Time,Position,Transform}`, `Core::{Context,Drawable}`,
`nlohmann_json`, ImGui.

---

## Layer 3 — Execution harness (AI orchestration)

### Validation gates (every phase must pass before the next)
`data schema (validate_data.py)` → `unit tests (GoogleTest/ctest)` →
`build (cmake)` → `smoke (headless scene boots)` → `playtest checklist`.

### GitNexus protocol (per the repo CLAUDE.md)
- Before editing any code symbol: `gitnexus_impact({target, direction:"upstream"})`;
  report blast radius; **warn on HIGH/CRITICAL**.
- Before committing: `gitnexus_detect_changes()`.
- Engine edits → index `practical-tools-for-simple-design`; game-layer edits →
  index `Soul-Knight-ai`. Explore with `gitnexus_query` instead of grep.
- **Status:** the GitNexus **MCP server is not connected** to the agent session
  right now. Until it is, impact analysis is done manually (read the call graph /
  headers) and noted in the change description. To enable automated gating,
  connect the GitNexus MCP server (or run `npx gitnexus analyze` to refresh the
  index). This matters most for the Phase-1 core edits to `GameObject` /
  `Renderer` / `Context`.

### Parallel construction (Phase 1)
Phase-1 engine systems are independent → build with a `Workflow` fanning out one
subagent per system (collision / camera / pooling / scene / datastore), with
`isolation:"worktree"` when agents touch files concurrently. Each result is
**adversarially verified** (an independent reviewer tries to refute it) before
merge. Gates above run per system.

---

## Build & run

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug      # relative RESOURCE_DIR is WIP -> Debug required
cmake --build build
./build/SoulKnight        # (path depends on generator)
```
Requires a C++17 toolchain (Visual Studio / CLion / MinGW). FetchContent pulls
PTSD + SDL2 stack on first configure.
