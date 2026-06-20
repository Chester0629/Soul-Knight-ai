export const meta = {
  name: 'confirm-logic-completeness',
  description: 'Finish the faithful logic layer: port recoverable Wave K bullet accumulators and run a completeness-critic sweep proving no other recoverable game logic remains un-ported',
  whenToUse: 'Gate before the engine-port phase: confirm every faithfully-recoverable decision/cadence/scalar/state body is ported; port Wave K; classify the rest owner-only/blocked.',
  phases: [
    { title: 'Port', detail: 'port recoverable Wave K bullet motion/timing math from the decomp' },
    { title: 'Verify', detail: 'adversarial fidelity re-derivation per Wave K module' },
    { title: 'Finalize', detail: 'fix + re-verify only where verify failed' },
    { title: 'Sweep', detail: 'completeness critics enumerate remaining content classes, flag any recoverable gap' },
    { title: 'Synthesis', detail: 'write LOGIC_COMPLETENESS_REPORT.md + registration list' },
  ],
};

const REPO = 'D:/Soul Knight/Soul-Knight-ai';
const DECOMP = 'D:/Soul Knight/_reverse/ghidra_export/game_full.c';
const RECREATION = 'D:/Soul Knight/_reverse/recreation';

const SOURCES = [
  'SOURCES (read-only, source of truth ranked):',
  '- Decompilation (TRUTH, ~50MB): ' + DECOMP + '. Find bodies with the Grep tool (pattern "<Class>__", path = that file, output_mode content, -n true), then Read the cited line ranges (offset/limit). Never load the whole file.',
  '- Recreation C# (field/logic reference where present): ' + RECREATION + ' (e.g. Weapon/RGBullet.cs).',
  '- IL2CPP dump is body-empty: names/signatures only; porting logic from it is fabrication.',
].join('\n');

const CONVENTIONS = [
  'CONVENTIONS (mandatory):',
  '- C++17, warning-clean under MSVC /W4 AND -Wall -Wextra -pedantic: initialize every member, omit unused parameter names, no signed/unsigned mismatch, no narrowing.',
  '- ASCII-ONLY source bytes in .hpp/.cpp.',
  '- Naming: CamelCase types/functions/namespaces; camelBack locals/params; m_ members; kFoo file-scope constants; UPPER_CASE enum constants. Header guard GAME_<NAME>_HPP. namespace Game.',
  '- Depend ONLY on "data/RGRandom.hpp" (+ <glm/glm.hpp>; <gtest/gtest.h> in tests). NO engine/Unity/PTSD headers. Includes use the include/ root.',
  '- Doxygen-light per public method; every ported body cites // FAITHFUL: <Class>__<Method> @ game_full.c:<line>.',
  '- RGRandom: void SetRandomSeed(int); int Range(int min,int maxExclusive); float Range(float min,float maxInclusive); bool Seeded(). Brains wrap SetSeed(int).',
].join('\n');

const FIDELITY = [
  'FAITHFULNESS (non-negotiable):',
  '- RNG draw COUNT+ORDER sacred (Range(int) max-EXCLUSIVE, Range(float) max-INCLUSIVE; gated-out path takes NO draw).',
  '- NEVER write a field/state the decomp does not. NEVER invent a mechanic/threshold.',
  '- get_transform / Rigidbody2D velocity / Instantiate / Invoke / Animator / Time.deltaTime-driven owner writes are OWNER-side: model the recoverable SCALAR (e.g. progress = timer/duration, the ratio/lerp factor, the decay), not the transform/velocity write itself. deltaTime accumulation may be modelled as a Tick(dt) input.',
  '- Unrecoverable constants (uninitialised DAT_*, jumptable targets): named constant // TODO[verify]; never guess.',
  '- A faithful "owner-only / unrecoverable" outcome (status=blocked) is CORRECT for the homing/seek bullets. Fabrication is the only failure.',
].join('\n');

const TEST_GUIDE = [
  'TEST GUIDANCE: gtest, file test/<Name>Test.cpp, suite <Name>Test, NOLINTBEGIN/END(readability-magic-numbers). For any RNG draw, pin count+order via a parallel same-seeded Game::RGRandom; zero-draw bodies assert a non-advancing stream. For deltaTime accumulators, drive Tick(dt) with fixed dt sequences and assert the exact progress/ratio/decay values from the decomp formula. Match test/Gun016Test.cpp and src/combat/BulletRoundabout.cpp style.',
].join('\n');

const HARD_RULES = [
  'HARD RULES: DO NOT edit files.cmake. DO NOT run cmake/build/ctest/git (orchestrator does that centrally; parallel MSBuild corrupts build/). Write ONLY this module\'s files. Repo root: ' + REPO + '.',
].join('\n');

const TEMPLATES = 'TEMPLATES (Read first): src/combat/BulletRoundabout.cpp, include/combat/BulletRoundabout.hpp, test/Gun016Test.cpp; field map ' + RECREATION + '/Weapon/RGBullet.cs.';

// ---- Wave K items (thin bullet motion/timing accumulators) ----
const ITEMS = [
  { name: 'Bullet03', allowBlocked: true,
    anchors: 'Bullet03__Start @962152 (owner get_transform only). Bullet03__Update @962183: active gate byte 0x20; timer 0x50 += Time.deltaTime WHILE 0x50 < duration 0x4c; progress = (0x50/0x4c); owner write uses progress*scale(0x48) via get_transform (the transform write is OWNER -- model progress + scaled value, not the write); second gate byte 0x1c. NO RNG. Model a Tick(dt) accumulator returning clamped progress and progress*scale.',
    note: 'Thin lifetime-progress lerp accumulator. The get_transform call is owner; the recoverable scalar is progress=timer/duration and progress*scale.' },
  { name: 'BulletParabola', allowBlocked: true,
    anchors: 'BulletParabola__Awake @963473, BulletParabola__FixedUpdate @963502, BulletParabola__SetTargetPosition @960637. Recover the parabola/arc scalar math (height/time interpolation, target lerp) and any deltaTime accumulator; Rigidbody2D/Transform writes are owner.',
    note: 'Parabolic arc motion: recover the deterministic arc factor / interpolation, not the owner velocity write.' },
  { name: 'BulletBoom', allowBlocked: true,
    anchors: 'BulletBoom__StartBoom @962698, BulletBoom__SoonExplode @962712, BulletBoom__ExplodeStart @962731 (also @680484/680498). Recover the explosion-timing cadence (delay constants, the SoonExplode->ExplodeStart state transition); Instantiate/Invoke are owner.',
    note: 'Explosion timing state machine: recover the delay/cadence + state transition, not the spawn.' },
  { name: 'BulletColor', allowBlocked: true,
    anchors: 'BulletColor__Start @962850, BulletColor__FixedUpdate @962903. Recover any deterministic color/scale lerp or deltaTime accumulator; the SpriteRenderer/Transform write is owner.',
    note: 'Color/scale lerp accumulator. If every body is an owner renderer write with no recoverable scalar, return blocked.' },
  { name: 'BulletFollow', allowBlocked: true,
    anchors: 'BulletFollow__FixedUpdate @963057, BulletFollow__UpdateAttribute @963070, BulletFollow__ReSetDestoryTime @963084. ReSetDestoryTime/UpdateAttribute may hold a recoverable timer/scalar reset; the seek/retarget in FixedUpdate is owner homing (a Physics2D/Transform target chase). Port only the recoverable scalar; blocked if none.',
    note: 'Homing bullet -- the seek is owner. Recover only ReSetDestoryTime/UpdateAttribute scalars if present.' },
  { name: 'BulletLaterFixedTarget', allowBlocked: true,
    anchors: 'BulletLaterFixedTarget__Start @963396, BulletLaterFixedTarget__FindTarget @963420. FindTarget is an owner raycast (Physics2D); Start may set a delay timer. Port only a recoverable delay/timer scalar; otherwise blocked.',
    note: 'Delayed-target seek -- FindTarget is owner raycast. Likely blocked except a delay constant.' },
];

const PORT_SCHEMA = {
  type: 'object', additionalProperties: false,
  required: ['name', 'status', 'filesWritten', 'summary'],
  properties: {
    name: { type: 'string' },
    status: { type: 'string', enum: ['ported', 'partial', 'blocked'] },
    blocked: { type: 'boolean' },
    blockedReason: { type: 'string' },
    filesWritten: { type: 'array', items: { type: 'string' } },
    rngSummary: { type: 'string' },
    fabricationFlags: { type: 'array', items: { type: 'string' } },
    summary: { type: 'string' },
  },
};
const VERIFY_SCHEMA = {
  type: 'object', additionalProperties: false,
  required: ['name', 'faithful', 'mustFix', 'notes'],
  properties: {
    name: { type: 'string' }, faithful: { type: 'boolean' },
    mustFix: { type: 'array', items: { type: 'string' } },
    shouldFix: { type: 'array', items: { type: 'string' } },
    notes: { type: 'string' },
  },
};
const FIX_SCHEMA = {
  type: 'object', additionalProperties: false,
  required: ['name', 'applied', 'notes'],
  properties: {
    name: { type: 'string' }, applied: { type: 'array', items: { type: 'string' } },
    filesChanged: { type: 'array', items: { type: 'string' } }, notes: { type: 'string' },
  },
};
const SWEEP_SCHEMA = {
  type: 'object', additionalProperties: false,
  required: ['area', 'recoverableGaps', 'confirmedComplete', 'notes'],
  properties: {
    area: { type: 'string' },
    recoverableGaps: {
      type: 'array',
      items: {
        type: 'object', additionalProperties: false,
        required: ['className', 'method', 'decompLine', 'why'],
        properties: {
          className: { type: 'string' }, method: { type: 'string' },
          decompLine: { type: 'string' }, why: { type: 'string' },
        },
      },
    },
    confirmedComplete: { type: 'boolean' },
    notes: { type: 'string' },
  },
};
const SYNTH_SCHEMA = {
  type: 'object', additionalProperties: false,
  required: ['srcFiles', 'includeFiles', 'testFiles', 'blocked', 'logicComplete', 'summary'],
  properties: {
    reportPath: { type: 'string' },
    srcFiles: { type: 'array', items: { type: 'string' } },
    includeFiles: { type: 'array', items: { type: 'string' } },
    testFiles: { type: 'array', items: { type: 'string' } },
    blocked: { type: 'array', items: { type: 'string' } },
    logicComplete: { type: 'boolean' },
    remainingGaps: { type: 'array', items: { type: 'string' } },
    summary: { type: 'string' },
  },
};

function portPrompt(item) {
  return [
    'You are a FAITHFUL-PORT agent for the Soul Knight 1.7.10 -> C++ reimplementation. SINGLE module: ' + item.name + ' (backlog Wave K -- thin bullet motion/timing accumulators).',
    '', SOURCES, '',
    'DECOMP ANCHORS: ' + item.anchors,
    'MODULE NOTE: ' + item.note,
    '', TEMPLATES, '',
    'TASK = FULL PORT. Enumerate all ' + item.name + '__ bodies, classify pure-logic vs owner-side, port ONLY the recoverable deterministic scalar/timer/state math into include/combat/' + item.name + '.hpp + src/combat/' + item.name + '.cpp + test/' + item.name + 'Test.cpp. deltaTime accumulation -> model as Tick(dt). If EVERY body is an owner transform/velocity/Instantiate write with no recoverable scalar, set status=blocked, write NO files, give blockedReason with cited evidence.',
    '', CONVENTIONS, '', FIDELITY, '', TEST_GUIDE, '', HARD_RULES,
    '', 'Return the PORT result (status, filesWritten absolute paths, rngSummary, fabricationFlags, summary).',
  ].join('\n');
}
function verifyPrompt(item, port) {
  return [
    'You are an ADVERSARIAL FIDELITY VERIFIER. MODULE: ' + item.name + ' (Wave K). Re-derive each method INDEPENDENTLY from the decomp; assume the port is wrong until proven faithful.',
    'Read on-disk include/combat/' + item.name + '.hpp, src/combat/' + item.name + '.cpp, test/' + item.name + 'Test.cpp (if they exist).',
    SOURCES, 'DECOMP ANCHORS: ' + item.anchors,
    (port && port.status === 'blocked' ? 'NOTE: port declared BLOCKED (reason: ' + (port.blockedReason || '') + '). Independently confirm there is genuinely NO recoverable deterministic scalar/timer/state; if there IS, that is a mustFix.' : ''),
    'CHECK: (1) miscounted/reordered RNG draws; (2) fields/state written the decomp does not; (3) wrong gate/threshold; (4) fabricated owner-only motion (a transform/velocity write modelled as if recoverable); (5) invented constants; (6) tests asserting ungrounded behavior or not pinning draw/tick math. faithful=true ONLY if zero mustFix. Do NOT edit files. Cite line numbers.',
  ].filter(Boolean).join('\n');
}
function fixPrompt(item, verify) {
  return [
    'FIX agent. MODULE: ' + item.name + '. Apply ONLY these must-fixes to the on-disk files; re-read the decomp (' + DECOMP + ', anchors: ' + item.anchors + ') before each edit. No new behavior. Do NOT edit files.cmake or build.',
    'MUST-FIXES:', ...(verify.mustFix || []).map((m, i) => (i + 1) + '. ' + m),
    '', CONVENTIONS, FIDELITY, 'Return applied[], filesChanged[].',
  ].join('\n');
}

function mopts(label, phase, schema) { return { label, phase, schema }; }

log('Confirm-logic: porting ' + ITEMS.length + ' Wave K modules + completeness sweep.');

const results = await pipeline(
  ITEMS,
  (item) => agent(portPrompt(item), mopts('port:' + item.name, 'Port', PORT_SCHEMA)),
  (port, item) => agent(verifyPrompt(item, port), mopts('verify:' + item.name, 'Verify', VERIFY_SCHEMA)).then((v) => ({ port, verify: v })),
  async (vr, item) => {
    const { port, verify } = vr;
    const base = { name: item.name, item, port, verify };
    if (!verify || verify.faithful || (verify.mustFix || []).length === 0) { base.finalFaithful = verify ? verify.faithful : null; base.fixed = false; return base; }
    const fix = await agent(fixPrompt(item, verify), mopts('fix:' + item.name, 'Finalize', FIX_SCHEMA));
    const re = await agent(verifyPrompt(item, port) + '\n\nNOTE: a fix was just applied; re-verify INDEPENDENTLY -- prior defects gone AND no new fabrication.', mopts('reverify:' + item.name, 'Finalize', VERIFY_SCHEMA));
    base.fix = fix; base.reverify = re; base.finalFaithful = re ? re.faithful : false; base.fixed = true; return base;
  }
);
const done = results.filter(Boolean);
log('Wave K done: ' + done.filter((r) => r.port && r.port.status !== 'blocked').length + ' ported, ' + done.filter((r) => r.port && r.port.status === 'blocked').length + ' blocked. Running completeness sweep...');

// ---- Completeness critic sweep ----
phase('Sweep');
const SWEEP_COMMON = [
  SOURCES,
  'ALREADY-PORTED set (do not re-flag these as gaps): read the file lists under ' + REPO + '/src/combat and ' + REPO + '/src/world to see every class already ported (EnemyAI*, BossAI*, CharSkill*, Gun*, Npc*, RG* allies, RGBDelayDivision/RGBulletTrigger/RGBTDivision, RGRoomX/RGAisle/RGBox, ItemWishingWell, the Wave K bullets just added, plus core RGRandom/RGMaze/RoomGen/MapManager/Damage/LootTable). Also read ' + REPO + '/docs/PORT_GAP_AUDIT.md and ' + REPO + '/docs/PORT_WAVE_EJ_MANIFEST.md for the prior classification + the confirmed-DEAD list.',
  'GOAL: find any GAME-CONTENT class with a FAITHFULLY-RECOVERABLE decision/cadence/scalar/state body that is NOT yet ported. A body is a gap ONLY if it has recoverable pure logic (RNG draws, gates, thresholds, deterministic formulas, state transitions). It is NOT a gap if it is owner-only (get_transform/Rigidbody/Instantiate/Invoke/Animator/Physics2D tails), a truncated jumptable, a register-only FUN_ helper, or already ported. Confirm the DEAD categories (Buffs, Lasers/Swords/Melee, homing/seek bullets, owner-only bullets, hands/mounts, most bullet-triggers, owner/net interactables) are genuinely owner-only/unrecoverable.',
  'Be precise and skeptical of yourself: only report a gap you can prove has recoverable logic at a cited line. Set confirmedComplete=true if you find NO recoverable gaps in your area.',
].join('\n');

const sweep = await parallel([
  () => agent('You are a COMPLETENESS CRITIC. AREA = enemies / bosses / heroes / weapons / pets-NPCs.\n\n' + SWEEP_COMMON + '\n\nSweep all EnemyAI*, BossAI*, CharSkill*/Cnn, Gun*/Weapon*, Pet/RGPet/Npc* content classes in the decomp. For each NOT in the ported set, classify. Report recoverableGaps (with className/method/decompLine/why) or confirm complete.',
    { label: 'sweep:combat-actors', phase: 'Sweep', schema: SWEEP_SCHEMA }),
  () => agent('You are a COMPLETENESS CRITIC. AREA = bullets / bullet-triggers / buffs / world grid-gen / interactables / items.\n\n' + SWEEP_COMMON + '\n\nSweep all Bullet*, RGB*/RGBT* trigger, *Buff*, RGRoom*/RGAisle/RGBox/RGMaze, Item*/RGItem/RGChest/RGCoin/RGDoor content classes in the decomp. For each NOT in the ported set, classify. Report recoverableGaps or confirm complete (re-affirm the DEAD list).',
    { label: 'sweep:bullets-world', phase: 'Sweep', schema: SWEEP_SCHEMA }),
]);
const sweeps = sweep.filter(Boolean);

// ---- Synthesis ----
phase('Synthesis');
const compact = done.map((r) => ({
  name: r.name, status: r.port ? r.port.status : 'n/a', blocked: r.port ? r.port.status === 'blocked' : false,
  blockedReason: r.port ? (r.port.blockedReason || '') : '', files: r.port ? (r.port.filesWritten || []) : [],
  rng: r.port ? (r.port.rngSummary || '') : '', flags: r.port ? (r.port.fabricationFlags || []) : [],
  finalFaithful: r.finalFaithful, fixed: r.fixed === true,
}));
const gaps = sweeps.flatMap((s) => (s.recoverableGaps || []).map((g) => ({ area: s.area, ...g })));

const synthPrompt = [
  'You are the INTEGRATION SYNTHESIZER + completeness reporter for the Soul Knight faithful LOGIC layer.',
  'Wave K port results JSON:', JSON.stringify(compact, null, 1),
  'Completeness-sweep results JSON:', JSON.stringify(sweeps, null, 1),
  'Flattened recoverable GAPS found by the critics:', JSON.stringify(gaps, null, 1),
  '',
  'DO:',
  '1) Confirm on disk which Wave K files now exist (include/combat/<n>.hpp, src/combat/<n>.cpp, test/<n>Test.cpp).',
  '2) Write ' + REPO + '/docs/LOGIC_COMPLETENESS_REPORT.md: state whether the faithful LOGIC layer is COMPLETE (every recoverable decision/cadence/scalar/state body ported). Include: a Wave K table (status/files/rng/flags), the completeness-sweep conclusion per area, the list of any remaining recoverable GAPS (loudly, if any), and a re-affirmation of the DEAD/owner-only categories. This is the gate before the engine-port phase.',
  '3) Return registration lists for files.cmake (NEW non-blocked Wave K files only): srcFiles like "combat/Bullet03.cpp", includeFiles like "combat/Bullet03.hpp", testFiles like "Bullet03Test.cpp"; blocked = module names with no files; logicComplete = true ONLY if there are zero remaining recoverable gaps (Wave K done + sweeps clean); remainingGaps = any gap descriptions.',
  '',
  'DO NOT edit files.cmake. DO NOT build.',
].join('\n');
const synth = await agent(synthPrompt, { label: 'synthesis', phase: 'Synthesis', schema: SYNTH_SCHEMA });

return { waveK: compact, sweeps, gaps, synthesis: synth };
