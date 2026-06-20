export const meta = {
  name: 'close-logic-gaps-wave-l',
  description: 'Close the 8 recoverable logic gaps (G1-G8) the completeness sweep found -- RGPetController family, 4 named-gun state machines, RGRoomXEndless rect-overlap -- then re-confirm the faithful logic layer is complete',
  whenToUse: 'Final logic wave before the engine port: port G1-G8, then a re-confirm critic verifies no recoverable decision/cadence/scalar/state body remains un-ported.',
  phases: [
    { title: 'Port', detail: 'port each gap module from the decomp' },
    { title: 'Verify', detail: 'adversarial fidelity re-derivation per module' },
    { title: 'Finalize', detail: 'fix + re-verify only where verify failed' },
    { title: 'Reconfirm', detail: 'completeness critic verifies G1-G8 closed, no new gaps' },
    { title: 'Synthesis', detail: 'write report addendum + registration list' },
  ],
};

const REPO = 'D:/Soul Knight/Soul-Knight-ai';
const DECOMP = 'D:/Soul Knight/_reverse/ghidra_export/game_full.c';
const RECREATION = 'D:/Soul Knight/_reverse/recreation';

const SOURCES = [
  'SOURCES (read-only, ranked truth):',
  '- Decompilation (TRUTH, ~50MB): ' + DECOMP + '. Find bodies via Grep (pattern "<Class>__", path=that file, output_mode content, -n true); Read cited line ranges with offset/limit. Never load the whole file.',
  '- Recreation C# (field/logic cross-check where present): ' + RECREATION + '.',
  '- Field-offset maps: ' + RECREATION + '/Pet/RGPetController.cs, ' + RECREATION + '/Enemy/RGEController.cs, ' + RECREATION + '/Weapon/RGWeapon.cs, ' + RECREATION + '/Dungeon/RGRoomX.cs.',
  '- IL2CPP dump is body-empty: names/signatures only; porting logic from it is fabrication.',
].join('\n');

const CONVENTIONS = [
  'CONVENTIONS (mandatory): C++17 warning-clean under MSVC /W4 + -Wall -Wextra -pedantic (init every member, omit unused param names, no signed/unsigned mismatch, no narrowing). ASCII-only .hpp/.cpp.',
  'Naming: CamelCase types/functions/namespaces; camelBack locals/params; m_ members; kFoo file-scope constants; UPPER_CASE enum constants. Header guard GAME_<NAME>_HPP. namespace Game.',
  'Depend ONLY on "data/RGRandom.hpp" (+ <glm/glm.hpp>; <gtest/gtest.h> in tests). NO engine/Unity/PTSD headers. Includes use the include/ root.',
  'Doxygen-light per public method; every ported body cites // FAITHFUL: <Class>__<Method> @ game_full.c:<line>.',
  'RGRandom: void SetRandomSeed(int); int Range(int min,int maxExclusive); float Range(float min,float maxInclusive); bool Seeded(). Brains wrap SetSeed(int).',
].join('\n');

const FIDELITY = [
  'FAITHFULNESS (non-negotiable): RNG draw COUNT+ORDER sacred (Range(int) max-EXCLUSIVE, Range(float) max-INCLUSIVE; gated-out path takes NO draw -- e.g. GunWaken skips the scatter draw entirely in awakened mode).',
  'NEVER write a field/state the decomp does not; NEVER invent a mechanic/threshold/constant. Rigidbody2D/Transform/Instantiate/Invoke/Animator/GetComponent/Physics2D are OWNER-side: model the recoverable scalar/state (the branch, the counter write-back, the charge ratio, the reflect, the heal formula), not the owner write.',
  'deltaTime accumulation -> model as Tick(dt). Unrecoverable constants -> named // TODO[verify], never guessed.',
  'A global non-seeded UnityEngine.Random draw is a DIFFERENT stream -- do NOT model it through the seeded RGRandom (that is fabrication); leave it owner-side or flag it. A faithful blocked/partial outcome is correct; fabrication is the only failure.',
].join('\n');

const TEST_GUIDE = 'TEST GUIDANCE: gtest, test/<Name>Test.cpp, suite <Name>Test, NOLINTBEGIN/END(readability-magic-numbers). Pin any RNG draw count+order via a parallel same-seeded Game::RGRandom; zero-draw paths assert a non-advancing stream; deltaTime accumulators driven by fixed dt sequences asserting the exact decomp formula. Match test/Gun016Test.cpp + test/WolfControllerTest.cpp.';

const HARD_RULES = 'HARD RULES: DO NOT edit files.cmake. DO NOT run cmake/build/ctest/git (orchestrator builds centrally; parallel MSBuild corrupts build/). Write ONLY this module\'s files. Repo root: ' + REPO + '.';

function templatesFor(item) {
  if (item.area === 'world') return 'TEMPLATES (Read first): src/world/RoomGen.cpp, include/world/RoomGen.hpp, test/RoomGenTest.cpp; ' + (item.cs ? 'recreation ' + item.cs : '');
  if (item.name === 'RGPetController') return 'TEMPLATES (Read first): src/combat/WolfController.cpp (its subclass), include/combat/WolfController.hpp, test/WolfControllerTest.cpp, include/combat/NpcSummon01.hpp; recreation ' + item.cs + '.';
  return 'TEMPLATES (Read first): include/combat/Gun016.hpp, src/combat/Gun019.cpp, test/Gun016Test.cpp (+ the charge pattern in src/combat/Gun007.cpp for charge guns).';
}

const ITEMS = [
  { name: 'RGPetController', area: 'combat', hard: true, allowBlocked: false, cs: RECREATION + '/Pet/RGPetController.cs',
    anchors: 'RGPetController__FixedUpdate @427251-427312 (two-branch velocity SM: decel[0x30]<=1.0 -> follow-master velocity = dir[0x50]*master.vel[(0x40)+0x10]*(master.vel[(0x40)+0x14]+1.0); else coast velocity = dir[0x34]*decel[0x30] then decel[0x30] *= damping[0x24] write-back). RGPetController__ReplyingHP @427317-427364 (gated hp[0x1c]<maxHp[0x18]; timer[0x64]+=dt; when timer>=interval[0x60]+[0x5c] heal hp += maxHp/5 INTEGER, reset timer=[0x5c], clamp hp=min(hp,maxHp)). RGPetController__TurnTo @427457-427477 (dir[0x50,0x54] = Vector2.Reflect(dir, normal)).',
    note: 'BASE class of the shipped WolfController/SnowmanController (G1-G3, audit #1 highest-value, dropped by Wave E). Port the 3 recoverable base bodies. Vector2.Reflect = v - 2*dot(v,n)*n. Only Rigidbody2D.set_velocity writes are owner; the branch/decel-decay/heal/reflect are pure logic. Has recreation .cs -- cross-check fields.' },
  { name: 'GunStaffWizard', area: 'combat', hard: true, allowBlocked: true, cs: null,
    anchors: 'GunStaffWizard__Attack @968615-968675: 4-state cyclic counter [0x1e] written back (0->1->2->3->0 barrel/phase rotation), a sign-gated early-out ([0x1c] < -[0x1c], the burst-exhausted idiom == [0x1c]<0), and scatter RGRandom.Range(-fVar4,+fVar4) with fVar4 = baseDev + baseDev*deviation[(owner)+0x20].',
    note: 'G4. Richer than bare-scatter guns: a real written-back 4-state machine + sign-gate + one scatter draw. The bullet spawn is owner.' },
  { name: 'GunWaken', area: 'combat', hard: true, allowBlocked: true, cs: null,
    anchors: 'GunWaken__Attack @970426-970465: spread draw RGRandom.Range(-fVar4,+fVar4) taken ONLY when wakenFlag[0x84]==0; in awakened mode (flag!=0) the RNG draw is SKIPPED ENTIRELY (perfect accuracy, no draw advances the stream).',
    note: 'G5. Conditional-RNG decision: the gated-out awakened path takes NO draw -- preserve that exactly (lockstep).' },
  { name: 'GunMagicBow', area: 'combat', hard: true, allowBlocked: true, cs: null,
    anchors: 'GunMagicBow__Update @967246-967272 (charge gate: accumulate while Animator-bool set AND charge[0x8c] < maxCharge[0x90]; the increment is owner-side -- model the predicate). GunMagicBow__Attack @967301-967327 (chargeRatio = charge[0x8c]/maxCharge[0x90]; scale bullet base-velocity components x[0x80],y[0x84],z[0x88] by chargeRatio). Zero RNG.',
    note: 'G6. Same charge family as the ported Gun007. Deterministic charge-ratio scalar; the GetComponent<RGBullet> apply is owner.' },
  { name: 'GunMultiBullet', area: 'combat', hard: true, allowBlocked: true, cs: null,
    anchors: 'GunMultiBullet__GetAttack/GetSpeed/GetCanThrough/GetCritics @967541-967675: each checks whether a per-bullet override array (atk@0x70 / spd@0x74 / canThrough@0x80 / crit@0x78) has the same element count[0xc] as the bullet-count array[0x6c]; if equal AND index<count returns the indexed element (array + index*4 + 0x10), else returns a scalar fallback (atk@0x20 / spd@0x28 / canThrough@0x38 / crit@0x2c).',
    note: 'G7 (lower value but recoverable). Four pure array-vs-scalar selectors + index formula. Model the arrays as inputs (std::vector or pointer+count) since their contents are owner-populated.' },
  { name: 'RGRoomXEndless', area: 'world', hard: true, allowBlocked: true, cs: RECREATION + '/Dungeon/RGRoomX.cs',
    anchors: 'RGRoomXEndless__IsOccupied @429286-429422 (thunk 429427-429431; caller 429150): builds a query Rect (c-1, r-1, w+2, h+2) (1-cell padding), loops the placed-obstacle List<Rect> at 0x7c doing a per-axis AABB overlap test (x via Rect.x/Rect.width, y via Rect.y/Rect.height; reconstruct xMax/yMax), returns 1 on first overlap else 0.',
    note: 'G8. Pure-geometry AABB Rect-overlap validity test. DISTINCT from the ported RoomGen::IsWallIntersect (grid-CELL map==0 scan) -- this is geometric Rect overlap against a runtime List<Rect> (owner-populated -> model as input). Port just IsOccupied (the self-contained recoverable unit); CreateObstacle truncates into owner Instantiate tails.' },
];

const PORT_SCHEMA = {
  type: 'object', additionalProperties: false, required: ['name', 'status', 'filesWritten', 'summary'],
  properties: {
    name: { type: 'string' }, status: { type: 'string', enum: ['ported', 'partial', 'blocked'] },
    blocked: { type: 'boolean' }, blockedReason: { type: 'string' },
    filesWritten: { type: 'array', items: { type: 'string' } }, rngSummary: { type: 'string' },
    fabricationFlags: { type: 'array', items: { type: 'string' } }, summary: { type: 'string' },
  },
};
const VERIFY_SCHEMA = {
  type: 'object', additionalProperties: false, required: ['name', 'faithful', 'mustFix', 'notes'],
  properties: { name: { type: 'string' }, faithful: { type: 'boolean' }, mustFix: { type: 'array', items: { type: 'string' } }, shouldFix: { type: 'array', items: { type: 'string' } }, notes: { type: 'string' } },
};
const FIX_SCHEMA = {
  type: 'object', additionalProperties: false, required: ['name', 'applied', 'notes'],
  properties: { name: { type: 'string' }, applied: { type: 'array', items: { type: 'string' } }, filesChanged: { type: 'array', items: { type: 'string' } }, notes: { type: 'string' } },
};
const RECONFIRM_SCHEMA = {
  type: 'object', additionalProperties: false, required: ['gapsClosed', 'newGaps', 'logicComplete', 'notes'],
  properties: {
    gapsClosed: { type: 'array', items: { type: 'string' } },
    stillOpen: { type: 'array', items: { type: 'string' } },
    newGaps: { type: 'array', items: { type: 'object', additionalProperties: false, required: ['className', 'method', 'decompLine', 'why'], properties: { className: { type: 'string' }, method: { type: 'string' }, decompLine: { type: 'string' }, why: { type: 'string' } } } },
    logicComplete: { type: 'boolean' }, notes: { type: 'string' },
  },
};
const SYNTH_SCHEMA = {
  type: 'object', additionalProperties: false, required: ['srcFiles', 'includeFiles', 'testFiles', 'blocked', 'logicComplete', 'summary'],
  properties: {
    reportPath: { type: 'string' }, srcFiles: { type: 'array', items: { type: 'string' } },
    includeFiles: { type: 'array', items: { type: 'string' } }, testFiles: { type: 'array', items: { type: 'string' } },
    blocked: { type: 'array', items: { type: 'string' } }, logicComplete: { type: 'boolean' },
    remainingGaps: { type: 'array', items: { type: 'string' } }, summary: { type: 'string' },
  },
};

function portPrompt(item) {
  return [
    'You are a FAITHFUL-PORT agent for the Soul Knight 1.7.10 -> C++ reimplementation. SINGLE module: ' + item.name + ' (a recoverable LOGIC GAP the completeness sweep found).',
    '', SOURCES, '', 'DECOMP ANCHORS: ' + item.anchors, 'MODULE NOTE: ' + item.note,
    (item.cs ? 'RECREATION C#: ' + item.cs + ' (cross-check fields; decomp is truth).' : 'RECREATION C#: NONE -- decomp only; lean toward partial/blocked over guessing.'),
    '', templatesFor(item), '',
    'TASK = FULL PORT. Write include/' + item.area + '/' + item.name + '.hpp, src/' + item.area + '/' + item.name + '.cpp, test/' + item.name + 'Test.cpp. Enumerate the ' + item.name + '__ bodies at the anchors, classify pure-logic vs owner-side, port ONLY the recoverable decision/cadence/scalar/state. If a body is wholly owner-only, set status=partial (port the recoverable ones) or blocked (none recoverable) with cited evidence -- never fabricate.',
    '', CONVENTIONS, '', FIDELITY, '', TEST_GUIDE, '', HARD_RULES,
    '', 'Return the PORT result (status, filesWritten absolute paths, rngSummary, fabricationFlags, summary).',
  ].join('\n');
}
function verifyPrompt(item, port) {
  return [
    'You are an ADVERSARIAL FIDELITY VERIFIER. MODULE: ' + item.name + '. Re-derive each method INDEPENDENTLY from the decomp; assume wrong until proven faithful.',
    'Read on-disk include/' + item.area + '/' + item.name + '.hpp, src/' + item.area + '/' + item.name + '.cpp, test/' + item.name + 'Test.cpp.',
    SOURCES, 'DECOMP ANCHORS: ' + item.anchors,
    (item.cs ? 'RECREATION C#: ' + item.cs : 'RECREATION C#: NONE.'),
    (port && port.status === 'blocked' ? 'NOTE: port declared BLOCKED (' + (port.blockedReason || '') + '). Independently confirm NO recoverable logic exists; if it does, mustFix.' : ''),
    'CHECK: (1) miscounted/reordered RNG draws (esp. GunWaken gated-out awakened path = NO draw; GunStaffWizard one scatter draw); (2) fields/state written the decomp does not; (3) wrong gate/counter-rollover/threshold; (4) owner-only writes (velocity/Instantiate/get_transform) modelled as if recoverable; (5) invented constants; (6) the maxHp/5 INTEGER division and clamp in ReplyingHP; (7) Vector2.Reflect = v-2*dot(v,n)*n exactly; (8) tests asserting ungrounded behavior or not pinning draw/formula. faithful=true ONLY if zero mustFix. Do NOT edit files. Cite line numbers.',
  ].filter(Boolean).join('\n');
}
function fixPrompt(item, verify) {
  return ['FIX agent. MODULE: ' + item.name + '. Apply ONLY these must-fixes on-disk; re-read the decomp (' + DECOMP + ', anchors ' + item.anchors + ') before each edit. No new behavior. Do NOT edit files.cmake or build.',
    'MUST-FIXES:', ...(verify.mustFix || []).map((m, i) => (i + 1) + '. ' + m), '', CONVENTIONS, FIDELITY, 'Return applied[], filesChanged[].'].join('\n');
}
function mopts(label, phase, schema) { return { label, phase, schema }; }

log('Wave L: closing ' + ITEMS.length + ' recoverable gaps (G1-G8) + re-confirm.');

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
log('Wave L ported. Re-confirming completeness...');

phase('Reconfirm');
const reconfirm = await agent([
  'You are the FINAL COMPLETENESS CRITIC. The prior sweep found 8 recoverable gaps G1-G8; a port wave just attempted to close them. Verify.',
  SOURCES,
  'Read the just-written modules on disk (include/combat + include/world; the new ones: RGPetController, GunStaffWizard, GunWaken, GunMagicBow, GunMultiBullet, RGRoomXEndless) and ' + REPO + '/docs/LOGIC_COMPLETENESS_REPORT.md (the G1-G8 list + DEAD list).',
  'For EACH of G1-G8: confirm the recoverable body is now faithfully ported (cite the cpp). Then do a FINAL skeptical pass for any OTHER recoverable decision/cadence/scalar/state body still un-ported anywhere in the game-content decomp (you may spot-check the DEAD list). Report gapsClosed, stillOpen, any newGaps (className/method/decompLine/why), and logicComplete = true ONLY if G1-G8 are all closed AND you find no new recoverable gaps.',
].join('\n'), { label: 'reconfirm', phase: 'Reconfirm', schema: RECONFIRM_SCHEMA });

phase('Synthesis');
const compact = done.map((r) => ({
  name: r.name, status: r.port ? r.port.status : 'n/a', blocked: r.port ? r.port.status === 'blocked' : false,
  files: r.port ? (r.port.filesWritten || []) : [], rng: r.port ? (r.port.rngSummary || '') : '',
  flags: r.port ? (r.port.fabricationFlags || []) : [], finalFaithful: r.finalFaithful, fixed: r.fixed === true,
}));
const synth = await agent([
  'You are the INTEGRATION SYNTHESIZER for the final logic wave (G1-G8).',
  'Wave L port results JSON:', JSON.stringify(compact, null, 1),
  'Re-confirm critic result JSON:', JSON.stringify(reconfirm, null, 1),
  '',
  'DO: (1) confirm on disk which files now exist per module. (2) APPEND a "Wave L (G1-G8 closure)" section to ' + REPO + '/docs/LOGIC_COMPLETENESS_REPORT.md: a table (module/status/files/rng/flags/faithful), the re-confirm verdict, and a clear final GATE line stating whether the faithful LOGIC layer is now COMPLETE. (3) Return registration lists for files.cmake (NEW non-blocked files only): srcFiles like "combat/RGPetController.cpp" / "world/RGRoomXEndless.cpp"; includeFiles; testFiles like "RGPetControllerTest.cpp"; blocked = names with no files; logicComplete = the re-confirm verdict; remainingGaps = any still-open or new gaps.',
  'DO NOT edit files.cmake. DO NOT build.',
].join('\n'), { label: 'synthesis', phase: 'Synthesis', schema: SYNTH_SCHEMA });

return { waveL: compact, reconfirm, synthesis: synth };
