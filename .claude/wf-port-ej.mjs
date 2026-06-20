export const meta = {
  name: 'port-soulknight-waves-ej',
  description: 'Faithful adversarial port of Soul Knight backlog Waves E-J (pet/NPC, gun shapes, bullet-trigger math, CombatStats deepen, world interactables) into the C++ reimplementation',
  whenToUse: 'Continue the Phase 4+ per-content port: finish Waves E/F/G and port H/I/J with the Port->Verify->Fix->Re-verify discipline. Orchestrator handles files.cmake + central build + ctest afterward.',
  phases: [
    { title: 'Port', detail: 'write/complete faithful cpp+hpp+test per module from the decomp' },
    { title: 'Verify', detail: 'adversarial fidelity re-derivation per module' },
    { title: 'Finalize', detail: 'apply must-fixes and re-verify (only when verify failed)' },
    { title: 'Synthesis', detail: 'write manifest doc + emit files.cmake registration list' },
  ],
};

// ----------------------------------------------------------------------------
// Shared constants (read-only sources, conventions, fidelity rules)
// ----------------------------------------------------------------------------

const REPO = 'D:/Soul Knight/Soul-Knight-ai';
const DECOMP = 'D:/Soul Knight/_reverse/ghidra_export/game_full.c';
const RECREATION = 'D:/Soul Knight/_reverse/recreation';

const SOURCES = [
  'SOURCES (read-only, source of truth ranked):',
  '- Decompilation (TRUTH, ~50MB, named line-numbered bodies): ' + DECOMP,
  "  Find bodies with the Grep tool: pattern '<Class>__' with path set to that file, output_mode content, -n true. Then Read the exact cited line ranges (offset/limit). Never load the whole file.",
  '- Recreation C# (field names + logic reference where present): ' + RECREATION,
  '- Field-offset maps: ' + RECREATION + '/Enemy/RGEController.cs (RGEController layout), ' + RECREATION + '/Pet/RGPetController.cs, ' + RECREATION + '/Player/RGController.cs.',
  '- The IL2CPP metadata dump is BODY-EMPTY: use it for names/signatures only; porting logic from it is fabrication.',
].join('\n');

const CONVENTIONS = [
  'CONVENTIONS (mandatory, enforced by /W4 + clang-tidy + an ASCII pre-commit hook):',
  '- C++17. MUST compile warning-clean under MSVC /W4 AND -Wall -Wextra -pedantic: initialize EVERY member, omit unused parameter names, no signed/unsigned mismatch, no narrowing conversions.',
  '- ASCII-ONLY source bytes in .hpp/.cpp (no em-dash, smart quotes, or any non-ASCII) -- they trip MSVC C4819.',
  '- Naming: CamelCase types/functions/namespaces; camelBack locals/params; m_ instance members; s_ statics; kFoo file-scope constants; UPPER_CASE enum/global constants. Header guard GAME_<NAME>_HPP. All code in namespace Game.',
  '- Depend ONLY on "data/RGRandom.hpp" (+ <glm/glm.hpp> for vectors; <gtest/gtest.h> in tests). NO engine/Unity/PTSD headers.',
  '- Doxygen-light comment on each public method. Every ported method body cites: // FAITHFUL: <Class>__<Method> @ game_full.c:<line>.',
  '- RGRandom API: void SetRandomSeed(int); int Range(int min, int maxExclusive); float Range(float min, float maxInclusive); bool Seeded(). Brains wrap seeding: void SetSeed(int seed){ m_Rng.SetRandomSeed(seed); }.',
  '- Includes use the include/ root, e.g. #include "combat/Foo.hpp", #include "data/RGRandom.hpp".',
].join('\n');

const FIDELITY = [
  'FAITHFULNESS (non-negotiable):',
  '- RNG draw COUNT and ORDER are sacred. Range(int) max is EXCLUSIVE; Range(float) max is INCLUSIVE. A gated-out / branch-not-taken path takes NO draw. Replay must stay lockstep with a parallel same-seeded RGRandom.',
  '- NEVER write a field/state the decomp does not write. NEVER invent a mechanic, archetype, or threshold to fill a gap.',
  '- Truncated tails, vtable/jumptable dispatch ("Could not recover jumptable"), register-only FUN_/unaff_ helpers, and Instantiate/Invoke/Animator/Transform/Physics2D/get_position calls are OWNER-side: note them at the decomp site as owner concerns; do NOT model them.',
  '- Unrecoverable magic constants (uninitialised DAT_*, jumptable targets): expose as a named constant marked // TODO[verify]; never guess the numeric value.',
  '- A faithful "this is owner-only / unrecoverable" outcome (status=blocked, or a method left to the owner with a cited comment) is CORRECT and expected for several Wave H/J modules. Fabricating a body to look complete is the ONE unforgivable failure.',
].join('\n');

const TEST_GUIDE = [
  'TEST GUIDANCE:',
  '- gtest. File test/<Name>Test.cpp, suite name <Name>Test. Wrap cases in NOLINTBEGIN/END(readability-magic-numbers).',
  '- Derive RNG goldens from a parallel same-seeded Game::RGRandom and assert draw-for-draw equality (count + order). For zero-draw bodies, assert the stream is NON-advancing (a parallel stream still matches after the call).',
  '- Assert gate/branch logic and constants against the DECOMP, not against guessed behavior. A test that asserts ungrounded behavior is a fidelity failure, not a feature.',
  '- Match the structure of test/Gun016Test.cpp and test/WolfControllerTest.cpp.',
].join('\n');

const HARD_RULES = [
  'HARD RULES (orchestration safety):',
  '- DO NOT edit files.cmake. DO NOT run cmake / build / msbuild / ctest / git. The orchestrator registers files and builds centrally (concurrent MSBuild corrupts the shared build/).',
  '- Write ONLY this module\'s own files. Do not touch other modules or shared headers (except a designated deepen target).',
  '- Repo root: ' + REPO + '. Write to absolute paths under it.',
].join('\n');

function dirs(area) {
  return { inc: REPO + '/include/' + area, src: REPO + '/src/' + area };
}

function templatesFor(item) {
  if (item.wave === 'F' || item.wave === 'G') {
    return 'TEMPLATES (Read first, match exactly): include/combat/Gun016.hpp, src/combat/Gun019.cpp, test/Gun016Test.cpp.';
  }
  if (item.wave === 'E') {
    return 'TEMPLATES (Read first, match exactly): src/combat/WolfController.cpp, test/WolfControllerTest.cpp, include/combat/NpcSummon01.hpp (a fully-written sibling header), include/combat/RGBatteryController.hpp; field map ' + RECREATION + '/Enemy/RGEController.cs.';
  }
  if (item.wave === 'H') {
    return 'TEMPLATES (Read first): src/combat/BulletRoundabout.cpp, include/combat/BulletRoundabout.hpp, test/BulletRoundaboutTest.cpp; field map ' + RECREATION + '/Weapon/RGBullet.cs.';
  }
  if (item.wave === 'I') {
    return 'TEMPLATES (Read first): include/combat/CombatStats.hpp (the deepen target), test/CombatStatsTest.cpp, include/combat/Damage.hpp.';
  }
  // J
  return 'TEMPLATES (Read first): src/world/RoomGen.cpp, include/world/RoomGen.hpp, test/RoomGenTest.cpp.';
}

// ----------------------------------------------------------------------------
// Work items (Waves E-J). hard=true -> inherit Opus; hard=false -> sonnet.
// task: full | impl | test | verify | deepen
// ----------------------------------------------------------------------------

const ITEMS = [
  // ---- Wave E: pet / NPC / summon allies ----
  { name: 'NpcSummon01', wave: 'E', area: 'combat', task: 'impl', hard: true, allowBlocked: false, cs: null,
    anchors: 'header include/combat/NpcSummon01.hpp ALREADY cites per-method lines; decomp NpcSummon01__* @ game_full.c:1671325-1671745 (ShootReflection 1671325, RunReflection 1671485, EndCycle 1671568, GetHurt 1671668, OnGameStateChange 1671726).',
    note: 'Header fully written. Implement src/combat/NpcSummon01.cpp against the header\'s cited lines + write test. Closest sibling = WolfController.' },
  { name: 'RGBatteryController', wave: 'E', area: 'combat', task: 'impl', hard: true, allowBlocked: false, cs: null,
    anchors: 'header include/combat/RGBatteryController.hpp ALREADY cites per-method lines; decomp RGBatteryController__* @ game_full.c:467933-468140 (ShootReflection 467947, CreateRocket 467989, StopShooting 468033, EndCycle 468053, GetHurt 468077). ZERO rg_random draws anywhere in this class.',
    note: 'Header fully written. Implement src/combat/RGBatteryController.cpp + test. Verify the ZERO-draw claim against the decomp.' },
  { name: 'NpcMercenaryController', wave: 'E', area: 'combat', task: 'full', hard: true, allowBlocked: true, cs: null,
    anchors: 'NpcMercenaryController__* @ game_full.c: FixedUpdate 1670409, ShootReflection 1670499, MeleeShootReflection 1670503/1670647, RemoteShootReflection 1670506/1670707, MeleeScout 1670512, RemoteScout 1670531, RemoteRunReflection 1670550, EndCycle 1670814, FixedRotation 1670843, GetHurt 1670954, Dead 968089/1670972, TalkGetMercenary 1671048, SetUpWeapon 1671127, ResetIsCharge 1671142, CanPickWeapon 1671212/1671235/1671278, StartPickWeapon 1671230, PeakingWeapon 1671256, StopPickWeapon 1671269.',
    note: 'No recreation .cs -> decomp only. Audit hints (verify against decomp, do not trust blindly): FixedUpdate knockback-decel; three Random.Range(0,10)<8-gated shoot/run reflections with weapon-type branches; Invoke cadence formulas base + itemLevel*0.25 and base*(itemLevel*0.3+1). Port only recoverable decision/cadence/scalar math; pick-weapon/talk/setup-weapon bodies are likely owner-side.' },

  // ---- Wave F: new gun fire-pattern shapes (cpp+hpp exist; need test + fidelity check) ----
  { name: 'Gun019', wave: 'F', area: 'combat', task: 'test', hard: false, allowBlocked: false, cs: null,
    anchors: 'Gun019__CreateBullet @ game_full.c:965364-965389 (burst counter/limit reschedule + main-shot spread + one symmetric scatter draw).', note: 'cleanest self-contained burst.' },
  { name: 'Gun007', wave: 'F', area: 'combat', task: 'test', hard: false, allowBlocked: false, cs: null,
    anchors: 'Grep Gun007__ in the decomp. Charge cannon: bulletCount = Max(1, FloorToInt(maxCount*Min(1,charge))), 0.6/1.0 thresholds, charge-scaled muzzle pos, start..end size lerp.', note: 'richer than ported Gun005.' },
  { name: 'Gun004', wave: 'F', area: 'combat', task: 'test', hard: false, allowBlocked: false, cs: null,
    anchors: 'Grep Gun004__ in the decomp (fan/multi-shot shape).', note: '' },
  { name: 'Gun014', wave: 'F', area: 'combat', task: 'test', hard: false, allowBlocked: false, cs: null,
    anchors: 'Grep Gun014__ in the decomp (fan/multi-shot shape).', note: '' },
  { name: 'Gun016', wave: 'F', area: 'combat', task: 'verify', hard: false, allowBlocked: false, cs: null,
    anchors: 'Gun016__* @ game_full.c:964550-964700 (ctor, Update heat gate, Attack spread + one scatter draw).', note: 'port+test already exist.' },
  { name: 'Gun002', wave: 'F', area: 'combat', task: 'verify', hard: false, allowBlocked: false, cs: null,
    anchors: 'Grep Gun002__ in the decomp.', note: 'port+test already exist.' },
  { name: 'Gun012', wave: 'F', area: 'combat', task: 'verify', hard: false, allowBlocked: false, cs: null,
    anchors: 'Grep Gun012__ in the decomp.', note: 'port+test already exist.' },
  { name: 'Gun018', wave: 'F', area: 'combat', task: 'verify', hard: false, allowBlocked: false, cs: null,
    anchors: 'Grep Gun018__ in the decomp.', note: 'port+test already exist.' },

  // ---- Wave G: single-shot / secondary spread guns + drone parent ----
  { name: 'Gun017', wave: 'G', area: 'combat', task: 'test', hard: false, allowBlocked: false, cs: null,
    anchors: 'Grep Gun017__ in the decomp.', note: 'cpp+hpp exist; needs test + fidelity check.' },
  { name: 'Gun001', wave: 'G', area: 'combat', task: 'verify', hard: false, allowBlocked: false, cs: null, anchors: 'Grep Gun001__ in the decomp.', note: 'port+test exist.' },
  { name: 'Gun009', wave: 'G', area: 'combat', task: 'verify', hard: false, allowBlocked: false, cs: null, anchors: 'Grep Gun009__ in the decomp.', note: 'port+test exist.' },
  { name: 'Gun013', wave: 'G', area: 'combat', task: 'verify', hard: false, allowBlocked: false, cs: null, anchors: 'Grep Gun013__ in the decomp.', note: 'port+test exist.' },
  { name: 'Gun011', wave: 'G', area: 'combat', task: 'verify', hard: false, allowBlocked: false, cs: null, anchors: 'Grep Gun011__ in the decomp.', note: 'port+test exist.' },
  { name: 'Gun006Paw', wave: 'G', area: 'combat', task: 'verify', hard: false, allowBlocked: false, cs: null, anchors: 'Grep Gun006Paw__ in the decomp.', note: 'port+test exist (drone parent).' },

  // ---- Wave H: spawn-pattern bullet-trigger math (full ports) ----
  { name: 'RGBDelayDivision', wave: 'H', area: 'combat', task: 'full', hard: true, allowBlocked: true, cs: null,
    anchors: 'RGBDelayDivision__* @ game_full.c: AdjustAngle 468170, OnTaken 468185, FixedUpdate 468263, Division 468285.',
    note: 'No .cs. Port the recoverable angle/division/timer math; the bullet spawn (Instantiate) and coroutine scheduling are owner-side.' },
  { name: 'RGBulletTrigger', wave: 'H', area: 'combat', task: 'full', hard: true, allowBlocked: true, cs: RECREATION + '/Weapon/RGBulletTrigger.cs',
    anchors: 'RGBulletTrigger__* @ game_full.c: get/set through_count 390729/390730/467542/467550, get_the_bullet 467476/467507/468785, DestroyBullet 468759/469260, OnTriggerEnter2D 468950/468981/469051/469508/469517, AddEffectTrigger 469616, GetDamageFactor 469729, RemoveEffectTrigger 469774.',
    note: 'Has recreation .cs. Port the recoverable scalar/state (through_count accessors, GetDamageFactor, the OnTriggerEnter2D gate head); the exploded-bool guard + owner Instantiate / effect-trigger lists are owner-side -- model only what is recoverable.' },
  { name: 'RGBTDivision', wave: 'H', area: 'combat', task: 'full', hard: true, allowBlocked: true, cs: null,
    anchors: 'RGBTDivision__* @ game_full.c: OnTriggerEnter2D 468941, Division 469069.',
    note: 'No .cs. Subclass of RGBulletTrigger. Port the recoverable division fan math; spawns are owner-side.' },
  { name: 'RGBTRebound', wave: 'H', area: 'combat', task: 'full', hard: true, allowBlocked: true, cs: null,
    anchors: 'RGBTRebound__Start @ game_full.c:469273 (only this body present).',
    note: 'No .cs and only Start() present -- likely thin/owner. If nothing pure is recoverable, return status=blocked with evidence rather than fabricate.' },

  // ---- Wave I: CombatStats deepening (additive to existing files) ----
  { name: 'CombatStats', wave: 'I', area: 'combat', task: 'deepen', hard: true, allowBlocked: false, cs: RECREATION + '/Weapon/RGWeapon.cs',
    anchors: 'Grep RoleAttributePlayer__ (regen tickers), RoleAttribute__ (speed_rate / speed), RGWeapon__/RGEWeapon__ (ctor default-stat tables) in the decomp.',
    note: 'ADDITIVE deepen of include/combat/CombatStats.hpp + test/CombatStatsTest.cpp. Add ONLY new recoverable fields/methods/constants (RoleAttributePlayer HP/armor/energy regen tickers, RoleAttribute speed_rate, RGWeapon/RGEWeapon default stat tables). NEVER remove or change the meaning of existing symbols; keep all existing tests passing. If a regen cadence lives in a stripped InvokeRepeating handler with no decompiled body, flag it // TODO[verify] and port only the recoverable scalar.' },

  // ---- Wave J: world grid-gen + interactables (capped by known external block) ----
  { name: 'RGRoomX', wave: 'J', area: 'world', task: 'full', hard: true, allowBlocked: true, cs: RECREATION + '/Dungeon/RGRoomX.cs',
    anchors: 'RGRoomX__* @ game_full.c: Awake 427616, ChangeDoorsColor 427673/427708, CloseDoor 427679, OpenDoor 427736/427777, ClearRoom 427765, GetRoomReward 427775/427788, LoadRoom 427835, CreateFloor 428211, CreateWall 428425, SetUpRoom 428470, CreateObstacle 428565, IsWallIntersect 428824, GetMinimapSprite 428922.',
    note: 'world/RoomGen.cpp ALREADY ports the grid generation (CreateFloor/CreateWall/CreateObstacle/IsWallIntersect, determinism-locked by RoomGenTest). DO NOT duplicate or edit RoomGen. Port into a NEW world/RGRoomX module ONLY genuinely-new recoverable logic not already in RoomGen (e.g. door open/close state machine, ClearRoom gate, GetRoomReward selection IF pure). Anything that is net-authority/owner (RGDoor, NetController) or already covered -> skip/blocked. The size-roll/ComputeBaseLevel is external-blocked; do not re-derive it.' },
  { name: 'RGAisle', wave: 'J', area: 'world', task: 'full', hard: true, allowBlocked: true, cs: RECREATION + '/Dungeon/RGAisle.cs',
    anchors: 'RGAisle__* @ game_full.c: CreateAisle 390206, CreateFloor 390234/390250, CreateAisleWall 390235/390523, CreateWall 390614.',
    note: 'Has recreation .cs. Port the recoverable aisle floor/wall grid-stamping math (the cell loops + bounds), per the audit "RGAisle.CreateFloor". Tile Instantiate is owner-side.' },
  { name: 'RGBox', wave: 'J', area: 'world', task: 'full', hard: true, allowBlocked: true, cs: null,
    anchors: 'RGBox__* @ game_full.c: Hit 467578, BoxDestroy 468688, CreateItem 468712, SetSourceObject 468730.',
    note: 'No .cs. Port the recoverable Hit/destroy state + any pure loot-index math; the Instantiate/PrefabPool spawn is owner-side. Per audit "RGBox.Hit".' },
  { name: 'ItemWishingWell', wave: 'J', area: 'world', task: 'full', hard: true, allowBlocked: true, cs: null,
    anchors: 'ItemWishingWell__* @ game_full.c: Triggerable 257329, OnItemTriggerSuccess 257354, CreateObject 257374, OnItemTriggerFail 257387, OpenChest 257407/257603.',
    note: 'No .cs. Interactables are mostly owner/net + one-line RGRandom index draws. Port the recoverable gate/roll; if every body is an owner Instantiate/NetController gate, return status=blocked with evidence. Do NOT fabricate magnitudes.' },
  { name: 'ItemRoomEgg', wave: 'J', area: 'world', task: 'full', hard: true, allowBlocked: true, cs: null,
    anchors: 'ItemRoomEgg__* @ game_full.c: OnItemTriggerSuccess 257084, CreateObject 257103, CreateWeapon 257116, CreateGem 257133, CreateBoom 257148, CreatePots 257177.',
    note: 'No .cs. The Create* bodies are likely owner Instantiate dispatch. Port only a pure selection/roll if one is recoverable; otherwise status=blocked with evidence.' },
];

// ----------------------------------------------------------------------------
// Schemas
// ----------------------------------------------------------------------------

const PORT_SCHEMA = {
  type: 'object', additionalProperties: false,
  required: ['name', 'status', 'filesWritten', 'summary'],
  properties: {
    name: { type: 'string' },
    status: { type: 'string', enum: ['ported', 'completed', 'tested', 'deepened', 'partial', 'blocked'] },
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
    name: { type: 'string' },
    faithful: { type: 'boolean' },
    mustFix: { type: 'array', items: { type: 'string' } },
    shouldFix: { type: 'array', items: { type: 'string' } },
    notes: { type: 'string' },
  },
};

const FIX_SCHEMA = {
  type: 'object', additionalProperties: false,
  required: ['name', 'applied', 'notes'],
  properties: {
    name: { type: 'string' },
    applied: { type: 'array', items: { type: 'string' } },
    filesChanged: { type: 'array', items: { type: 'string' } },
    notes: { type: 'string' },
  },
};

const SYNTH_SCHEMA = {
  type: 'object', additionalProperties: false,
  required: ['srcFiles', 'includeFiles', 'testFiles', 'blocked', 'summary'],
  properties: {
    manifestPath: { type: 'string' },
    srcFiles: { type: 'array', items: { type: 'string' } },
    includeFiles: { type: 'array', items: { type: 'string' } },
    testFiles: { type: 'array', items: { type: 'string' } },
    deepenedFiles: { type: 'array', items: { type: 'string' } },
    blocked: { type: 'array', items: { type: 'string' } },
    summary: { type: 'string' },
  },
};

// ----------------------------------------------------------------------------
// Prompt builders
// ----------------------------------------------------------------------------

function taskInstruction(item) {
  const d = dirs(item.area);
  if (item.task === 'impl') {
    return [
      'TASK = COMPLETE IMPLEMENTATION. The header ' + d.inc + '/' + item.name + '.hpp ALREADY EXISTS and declares the full API with per-method decomp citations. Read it first.',
      '1) Implement ' + d.src + '/' + item.name + '.cpp faithfully against the decomp at the header\'s cited lines. Implement exactly the declared methods. If the header\'s declared behavior contradicts the decomp, fix the header minimally and note it in fabricationFlags.',
      '2) Write ' + REPO + '/test/' + item.name + 'Test.cpp.',
    ].join('\n');
  }
  if (item.task === 'full') {
    return [
      'TASK = FULL PORT (no files exist yet). Enumerate ALL ' + item.name + '__ bodies in the decomp, classify each pure-logic vs owner-side, and port ONLY the recoverable decision/cadence/scalar/state math.',
      'Write: ' + d.inc + '/' + item.name + '.hpp, ' + d.src + '/' + item.name + '.cpp, ' + REPO + '/test/' + item.name + 'Test.cpp.',
      'If the whole class is owner-only / unrecoverable, set status=blocked, write NO files, and give blockedReason with cited evidence.',
    ].join('\n');
  }
  if (item.task === 'test') {
    return [
      'TASK = WRITE TEST + FIDELITY-CHECK EXISTING PORT. ' + d.src + '/' + item.name + '.cpp and ' + d.inc + '/' + item.name + '.hpp ALREADY EXIST. Read both first.',
      '1) FIDELITY-CHECK the existing cpp against the decomp: if you find a fabrication, a wrong RNG count/order, an invented field, or a wrong gate, FIX the cpp (and hpp if needed) minimally -- re-read the decomp before editing. List any fix in fabricationFlags.',
      '2) Write ' + REPO + '/test/' + item.name + 'Test.cpp matching the Gun016Test.cpp style (parallel same-seeded RGRandom goldens, draw count+order).',
      'status = tested.',
    ].join('\n');
  }
  if (item.task === 'deepen') {
    return [
      'TASK = ADDITIVE DEEPEN of an EXISTING, REGISTERED, TESTED file. Target: ' + d.inc + '/' + item.name + '.hpp and ' + REPO + '/test/' + item.name + 'Test.cpp. Read both fully first.',
      'Add ONLY new recoverable fields/methods/constants from the decomp. NEVER remove or change the meaning of an existing symbol; keep every existing test passing and the header warning-clean. Add new tests for the new logic to the existing test file.',
      'If a target sub-mechanic is external-blocked (uninitialised DAT_*, stripped InvokeRepeating body), flag it // TODO[verify] and port only the recoverable scalar. status = deepened.',
    ].join('\n');
  }
  // verify task is routed around the Port stage; no port instruction needed.
  return '';
}

function portPrompt(item) {
  return [
    'You are a FAITHFUL-PORT agent for the Soul Knight 1.7.10 -> C++ reimplementation. Work on a SINGLE module: ' + item.name + ' (backlog Wave ' + item.wave + ').',
    '',
    SOURCES,
    '',
    'DECOMP ANCHORS: ' + item.anchors,
    (item.cs ? 'RECREATION C# for this class: ' + item.cs + ' (use as a logic/field cross-check, but the DECOMP is truth).' : 'RECREATION C#: NONE for this class -- decomp only, so fabrication risk is higher; lean toward blocked over guessing.'),
    'MODULE NOTE: ' + item.note,
    '',
    templatesFor(item),
    '',
    taskInstruction(item),
    '',
    CONVENTIONS,
    '',
    FIDELITY,
    '',
    TEST_GUIDE,
    '',
    HARD_RULES,
    '',
    'Return the PORT structured result: status, filesWritten (absolute paths you actually wrote), a one-line rngSummary (draws per method), fabricationFlags (any // TODO[verify] or header fixes), and a concise summary. If blocked, set status=blocked + blockedReason and write no files.',
  ].join('\n');
}

function verifyPrompt(item, port) {
  const d = dirs(item.area);
  const files = item.task === 'deepen'
    ? (d.inc + '/' + item.name + '.hpp and test/' + item.name + 'Test.cpp (the deepened additions)')
    : (d.inc + '/' + item.name + '.hpp, ' + d.src + '/' + item.name + '.cpp, and test/' + item.name + 'Test.cpp');
  return [
    'You are an ADVERSARIAL FIDELITY VERIFIER. MODULE: ' + item.name + ' (Wave ' + item.wave + '). Your job is to try to BREAK the port by re-deriving each method INDEPENDENTLY from the decomp. Assume the port is wrong until proven faithful.',
    '',
    'Read the on-disk files: ' + files + '.',
    SOURCES,
    'DECOMP ANCHORS: ' + item.anchors,
    (item.cs ? 'RECREATION C#: ' + item.cs : 'RECREATION C#: NONE -- decomp only.'),
    (port && port.status === 'blocked' ? 'NOTE: the port agent declared this BLOCKED (reason: ' + (port.blockedReason || '') + '). Independently confirm there is genuinely no recoverable pure-logic body; if there IS recoverable math that was skipped, that is a mustFix.' : ''),
    '',
    'CHECK FOR:',
    '(1) miscounted / reordered RGRandom draws (Range(int) max-EXCLUSIVE, Range(float) max-INCLUSIVE; a gated-out path must take NO draw);',
    '(2) fields/state written that the decomp does NOT write;',
    '(3) wrong gate/branch conditions or thresholds;',
    '(4) fabricated bodies for owner-only / truncated / jumptable / FUN_-helper tails;',
    '(5) magic constants invented instead of flagged // TODO[verify];',
    '(6) tests asserting behavior not grounded in the decomp (false goldens), or tests that do NOT actually pin draw count+order.',
    '',
    'For each real defect, emit a concrete mustFix: file + method + what is wrong + the game_full.c line that proves it. Separate mustFix (fabrication / wrong RNG / wrong gate / false golden) from shouldFix (style/clarity/missing-but-owner). Set faithful=true ONLY if there are ZERO mustFix items. Do NOT edit any files. Cite line numbers.',
  ].filter(Boolean).join('\n');
}

function fixPrompt(item, verify) {
  return [
    'You are a FIX agent. MODULE: ' + item.name + '. Apply ONLY the must-fixes below to the on-disk files. Re-read the decomp (' + DECOMP + ', anchors: ' + item.anchors + ') before each edit. Introduce NO new behavior beyond what the fix requires. Keep conventions (ASCII-only, /W4-clean, naming). Do NOT edit files.cmake, do NOT build.',
    '',
    'MUST-FIXES:',
    ...(verify.mustFix || []).map(function (m, i) { return (i + 1) + '. ' + m; }),
    '',
    CONVENTIONS,
    FIDELITY,
    '',
    'Return what you changed (applied[], filesChanged[]).',
  ].join('\n');
}

// ----------------------------------------------------------------------------
// Run: pipeline each module through Port -> Verify -> (Fix -> Re-verify)
// ----------------------------------------------------------------------------

function mopts(item, label, phase, schema) {
  const o = { label: label, phase: phase, schema: schema };
  if (!item.hard) o.model = 'sonnet';
  return o;
}

log('Waves E-J: ' + ITEMS.length + ' modules -> Port/Verify/Fix/Re-verify pipeline. '
  + ITEMS.filter(function (i) { return i.task === 'verify'; }).length + ' verify-only, '
  + ITEMS.filter(function (i) { return i.task === 'test'; }).length + ' test-only, '
  + ITEMS.filter(function (i) { return i.task === 'full'; }).length + ' full ports, '
  + ITEMS.filter(function (i) { return i.task === 'impl'; }).length + ' impl, '
  + ITEMS.filter(function (i) { return i.task === 'deepen'; }).length + ' deepen.');

const results = await pipeline(
  ITEMS,
  // Stage 1: Port / complete / test / deepen (verify-only items skip)
  function (item) {
    if (item.task === 'verify') {
      return { name: item.name, status: 'pre-existing', skippedPort: true };
    }
    return agent(portPrompt(item), mopts(item, 'port:' + item.name, 'Port', PORT_SCHEMA));
  },
  // Stage 2: adversarial verify (always)
  function (port, item) {
    return agent(verifyPrompt(item, port), mopts(item, 'verify:' + item.name, 'Verify', VERIFY_SCHEMA))
      .then(function (v) { return { port: port, verify: v }; });
  },
  // Stage 3: fix + re-verify, only when verify found must-fixes
  async function (vr, item) {
    const port = vr.port;
    const verify = vr.verify;
    const base = { name: item.name, item: item, port: port, verify: verify };
    if (!verify || verify.faithful || (verify.mustFix || []).length === 0) {
      base.finalFaithful = verify ? verify.faithful : null;
      base.fixed = false;
      return base;
    }
    const fix = await agent(fixPrompt(item, verify), mopts(item, 'fix:' + item.name, 'Finalize', FIX_SCHEMA));
    const re = await agent(verifyPrompt(item, port) + '\n\nNOTE: a fix was just applied to address prior must-fixes. Re-verify INDEPENDENTLY: confirm the prior defects are gone AND no NEW fabrication was introduced.',
      mopts(item, 'reverify:' + item.name, 'Finalize', VERIFY_SCHEMA));
    base.fix = fix;
    base.reverify = re;
    base.finalFaithful = re ? re.faithful : false;
    base.fixed = true;
    return base;
  }
);

const done = results.filter(Boolean);
const faithfulCount = done.filter(function (r) { return r.finalFaithful === true; }).length;
const blockedCount = done.filter(function (r) { return r.port && r.port.status === 'blocked'; }).length;
log('Pipeline done: ' + done.length + ' modules; ' + faithfulCount + ' verified faithful, ' + blockedCount + ' blocked. Synthesizing manifest...');

// ----------------------------------------------------------------------------
// Synthesis: write manifest doc + emit files.cmake registration list
// ----------------------------------------------------------------------------

phase('Synthesis');

const compact = done.map(function (r) {
  return {
    name: r.name,
    wave: r.item ? r.item.wave : '?',
    area: r.item ? r.item.area : '?',
    task: r.item ? r.item.task : '?',
    portStatus: r.port ? r.port.status : 'n/a',
    blocked: r.port ? (r.port.status === 'blocked') : false,
    blockedReason: r.port ? (r.port.blockedReason || '') : '',
    filesWritten: r.port ? (r.port.filesWritten || []) : [],
    rngSummary: r.port ? (r.port.rngSummary || '') : '',
    fabricationFlags: r.port ? (r.port.fabricationFlags || []) : [],
    finalFaithful: r.finalFaithful,
    fixed: r.fixed === true,
    verifyNotes: r.verify ? r.verify.notes : '',
    remainingMustFix: (r.reverify && r.reverify.mustFix) ? r.reverify.mustFix : ((r.verify && !r.verify.faithful && !r.fixed) ? r.verify.mustFix : []),
  };
});

const synthPrompt = [
  'You are the INTEGRATION SYNTHESIZER for the Soul Knight Waves E-J faithful port. Below is the per-module Port+Verify+Fix result JSON.',
  '',
  'JSON:',
  JSON.stringify(compact, null, 1),
  '',
  'DO:',
  '1) For EACH module, verify on disk which files actually now exist: include/<area>/<Name>.hpp, src/<area>/<Name>.cpp, test/<Name>Test.cpp (area is combat or world). Use Read/Grep under ' + REPO + '. (deepen modules edit existing files -- list them under deepenedFiles, not new registration.)',
  '2) Write ' + REPO + '/docs/PORT_WAVE_EJ_MANIFEST.md: a clear report with a table of every Wave E-J module (wave, status: ported/tested/deepened/blocked, final faithful y/n, files, RNG-draw summary, fabrication flags) and a short "BLOCKED / owner-only" section explaining each blocked module with its cited reason, plus any remaining must-fix items that survived re-verify (flag these loudly for the orchestrator).',
  '3) Return the registration lists for files.cmake (NEW files only, EXCLUDING blocked modules and deepen modules): srcFiles as paths like "combat/NpcSummon01.cpp" or "world/RGAisle.cpp"; includeFiles like "combat/NpcSummon01.hpp"; testFiles like "NpcSummon01Test.cpp". Also list deepenedFiles (already-registered files that were edited) and blocked (module names with no files).',
  '',
  'DO NOT edit files.cmake. DO NOT build. Only write the manifest .md and return the lists.',
].join('\n');

const synth = await agent(synthPrompt, { label: 'synthesis', phase: 'Synthesis', schema: SYNTH_SCHEMA });

return { modules: compact, synthesis: synth, faithfulCount: faithfulCount, blockedCount: blockedCount, total: done.length };
