## Summary

Works the **A–F backlog** from the post-engine-port review (the "之後還有哪些要做" list), then
**re-investigated every blocked item against the Ghidra decomp** once it was located (it lives at
`D:\Soul Knight\_reverse\ghidra_export\` — the workspace PARENT, not inside the repo). Branch
`feat/phase3-presentation-content` off `main`. Discipline throughout: recover only real code
immediates, never fabricate a constant or guess a sprite mapping; everything else is a documented,
evidence-cited block.

## Done — initial backlog

- **F3** `00bffcd` — energy reload: replace GameScene's hardcoded 400ms accumulator (5× too fast)
  with the faithful 2.0s `CombatStats::EnergyReloadTick` (decomp @432415).
- **F1** `ed6d42b` — EnemyAI06 kinematic turret: `ComputeVelocity` only gated knockback, so the
  "fixed" turret drifted under its wander heading; zero translation when kinematic.
- **A** `61b386d`+`877f6ee` — SimEvent emission (stub → live): sim emits fire/attack/hurt/death
  (deterministic, golden-replay byte-identical); GameScene drains them into effect sprites + real
  `fx_*.wav`. Per-state BODY anim stays asset-blocked (flat manifest, unresolved clip pathids).
- **B3** `9a61316` — multi-shot guns: `WeaponController` honors `WeaponDef.count/angle` → `Fan`.

## Done — decomp re-investigation (10-way parallel)

**Recovered real code-immediates → implemented faithfully:**

- **C — CombatStats armor** `1775b88`: `armorLoad`/`armorRate` were placeholder `0.0/0.0` (which
  made armor regen *every tick*). They ARE code immediates — RoleAttributePlayer..ctor @
  game_typed.c:33618-33619 sets `3.0`/`1.0` and SetUpChar never overwrites them. First armor point
  now takes 4.0s, every later point 1.0s. (The "set by data" assumption was simply wrong.)
- **C — GunWaken fields** `48ca7f1`: `kCtorField70/74/78` (12/50/45.0), "meaning not determinable,"
  were identified via the ctor + Il2CppDumper (dump.cs:270938-270940) as GunWaken's OWN awakened-mode
  fields `atk_mode2`/`critical_mode2`/`speed_mode2` (not "RGWeapon base config"). Renamed, TODO dropped.
- **F2 — boss move** `4880712`: pure chase → the recovered RunReflection **state-switch**
  (game_named.c:122666, which game_full.c truncated): ~60% chase (RETREAT when closer than a
  Range(5,10) threshold), ~20% strafe mirror-X, ~20% mirror-Y. Threshold drawn before selector;
  both int draws, so the boss's firing stream is byte-identical — only movement changes.

**Corrected factually-wrong claims (still blocked, but the comments were misleading):**

- **Gun014** — `FUN_001ceef4` was called an "unknown global random helper"; it is `__aeabi_idiv`
  (integer division): `angle = 360 / <divisor>`, no RNG. The repo's "don't draw m_Rng" was right.
- **RGRandom** — cited the wrong addresses (`0x4EC620`/`0x4FACFC`); real are `0x4FC620`/`0x50ACFC`.
  The Xorshift math is a native il2cpp icall (no decomp body) — validated against an external Unity
  ref, not the decomp; provenance comments made honest.
- **RGBulletTrigger** — fixed the `0x5A9BBC`→`0x5B9BBC` typo; clarified the ice factor is a
  `DAT_005b9c88` slot behind a non-returning singleton tail (the repo correctly returns 1.0, never 0.5).
- **BossAI01 ChooseAttack** — retagged TODO→BLOCKED: the roll→attack dispatch is truncated by the
  no-return stub `FUN_010b7dcc` (the angry constants 0.5/0.5/1.2 ARE confirmed immediates).

## Genuinely blocked — confirmed WITH the decomp open (the real reasons)

The decomp's availability did NOT unblock these; the export itself shows why:

- **C — ice factor / armor done, but ChooseAttack thresholds**: truncated by `FUN_010b7dcc` no-return
  stub — the dispatch code is absent from the export.
- **D — RoomGen** (partial): the height-banding `+0.5`/`+1.0` is confirmed code-literal, but the width
  weights (`DAT_0050f930/f934`) and size table `{15,21,25}` (`DAT_015edca8`) are **data-segment** values.
- **E — lasers/swords/homing/buffs, boss/enemy StartAtkNN dispatch**: unresolved indirect jumptables
  ("Could not recover jumptable at 0x…, Too many branches") + register-only continuations + coroutine
  thunks — confirmed unresolved in all three exports. Needs a `.data` jumptable dump or a dynamic trace.
- **F2 cadence**: the boss think-interval is a per-boss prefab field (`+0x98`) — kept at the 0.5s stand-in.
- **GunThrow spread, RGBulletTrigger ice, RGRandom PRNG math**: `DAT_` data-segment values / native
  il2cpp icalls — need a binary `.data` dump or the libunity runtime, not the code export.

→ The remaining true unblock path is a binary `.data`/jumptable dump or live dynamic analysis.

## Test Plan

- [x] Full suite **green** — baseline 1890 → **1897** (+F1 turret, +A SimEvent stream, +B3 multi-shot,
      +C armor, +F2 boss switch); both targets `/W4`-clean
- [x] Golden replay-equality / RNG draw-count preserved (A adds no draws; F2 preserves the boss
      draw-count so firing is byte-identical; F1/F3/B3/C don't perturb seeded streams)
- [ ] Interactive `/run` playtest — NOT done: the "cannot move" bug (prior session, `wip(scene)`) is
      still open and was left shelved, so A's effects + F2's movement are code-verified, not eyeballed.

## Notes

- First commit `wip(scene)` captures the prior session's shelved G1 work (floor/spawn/diagnostic).
- Not pushed / no PR opened — integration is the owner's call.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
