/*
 * sk_frida_dump.js — recover the il2cpp RUNTIME values that static analysis (game_full.c)
 * leaves as DAT_/jumptable. We don't read the DAT_ addresses (they're in .text/runtime-init);
 * instead we HOOK the functions and observe what they actually compute. No addressing ambiguity.
 *
 * Target: Soul Knight 1.7.10  (com.ChillyRoom.DungeonShooter)  ARM v7 / 32-bit.
 *
 * SETUP (once): device rooted + frida-server running on it; PC has frida tools (`pip install frida-tools`).
 * RUN:
 *     frida -U -f com.ChillyRoom.DungeonShooter -l sk_frida_dump.js
 *   (or, if the game is already open:  frida -U -n "Soul Knight" -l sk_frida_dump.js )
 *
 * THEN PLAY for a few minutes to trigger each section:
 *   B) walk through several rooms / go down a few floors  -> ROOM sizes + wall/obstacle levels
 *   C) pick up + FIRE a "throw" weapon (knife/axe/shuriken) -> GunThrow config fields
 *   D) reach and FIGHT the FIRST boss                       -> BossAI01 attack-bucket distribution
 * The script prints a SUMMARY every 20s. Copy the WHOLE console log back.
 *
 * If NO hooks fire: the methods may be Thumb -- set THUMB=true below (adds |1 to addresses).
 */
'use strict';

const MOD = 'libil2cpp.so';
const THUMB = false; // flip to true only if hooks never fire

// base-0 RVAs (from _reverse/ghidra_export/game_typed_index.txt)
const RVA = {
  RGRoomX_SetRGRandomSeed:  0x0050f350,
  RGRoomX_CreateFloor:      0x0050d3e4, // fallback if SetRGRandomSeed has no dims
  GunThrow_Attack:          0x00b2482c,
  BossAI01_ShootReflection: 0x00537c34,
  BossAI01_InAtk01:         0x00538d7c,
  BossAI01_InAtk02:         0x00539710,
  BossAI01_InAtk03:         0x005399d0,
  BossAI01_InAtk04:         0x00539cd4,
};

// Candidate DAT_ floats (game_named.c) -- a DIAGNOSTIC cross-check. These addresses are in .text,
// so they will likely print garbage; that just CONFIRMS they are not static data (informative).
const DAT = {
  'RoomGen weight >=22 (DAT_0050f930)':         0x0050f930,
  'RoomGen weight [16,22) (DAT_0050f934)':      0x0050f934,
  'GunThrow spread <8  (DAT_00b24804)':         0x00b24804,
  'GunThrow spread >=8 (DAT_00b24808)':         0x00b24808,
  'RGBulletTrigger non-ice (DAT_005b9c88, exp 1.0)': 0x005b9c88,
};

function A(base, rva){ const p = base.add(rva); return THUMB ? p.or(1) : p; }

// Frida 17 removed Module.findBaseAddress -> use Process.findModuleByName; keep a 16.x fallback.
function baseOf(name){
  try { const m = Process.findModuleByName(name); if (m) return m.base; } catch (e) {}
  try { if (typeof Module.findBaseAddress === 'function') return Module.findBaseAddress(name); } catch (e) {}
  return null;
}

function waitForBase(cb){
  let n = 0;
  const t = setInterval(() => {
    const b = baseOf(MOD);
    if (b) { clearInterval(t); cb(b); return; }
    if (n === 8 || n === 40 || n === 120) { // 4s / 20s / 60s: dump loaded modules to find the real name
      try {
        const names = Process.enumerateModules().map(m => m.name);
        console.log('[i] total modules: ' + names.length);
        console.log('[i] candidates (il2cpp/unity/mono/game/houdini/ndk/translat/main):');
        const hit = names.filter(x => /il2cpp|unity|mono|assembly|game|houdini|ndk|translat|libmain|chilly|dungeon/i.test(x));
        console.log('    ' + (hit.length ? hit.join('\n    ') : '(none matched -- pasting ALL .so names below)'));
        if (!hit.length) console.log('    ' + names.filter(x => x.endsWith('.so')).join('\n    '));
      } catch (e) { console.log('[i] enumerate err ' + e); }
    }
    if (++n > 360) { clearInterval(t); console.log('[!] ' + MOD + ' not found after 180s -- see module list above'); }
  }, 500);
}

waitForBase(function (base) {
  console.log('\n[+] ' + MOD + ' base = ' + base + (THUMB ? '  (THUMB mode)' : ''));

  // ---- A: diagnostic DAT_ reads (after il2cpp init) ----
  setTimeout(function () {
    console.log('\n===== A: DAT_ runtime reads (diagnostic; garbage here = confirms .text, not data) =====');
    for (const k in DAT) {
      const p = base.add(DAT[k]);
      let f = '?', i = '?';
      try { f = p.readFloat(); i = (p.readU32() >>> 0).toString(16); } catch (e) {}
      console.log('  ' + k + '  float=' + f + '  hex=0x' + i);
    }
  }, 8000);

  // ---- B: RGRoomX.SetRGRandomSeed -> room width/height + wall/obstacle level (THE size table) ----
  const rooms = {};
  Interceptor.attach(A(base, RVA.RGRoomX_SetRGRandomSeed), {
    onEnter(a) { this.self = a[0]; },
    onLeave() {
      const s = this.self;
      try {
        const w  = s.add(0x14).readS32(); // param_1[5] width
        const h  = s.add(0x18).readS32(); // param_1[6] height
        const wl = s.add(0x20).readS32(); // param_1[8] wallLevel
        const ol = s.add(0x24).readS32(); // param_1[9] obstacleLevel
        const key = w + 'x' + h + ' wl=' + wl + ' ol=' + ol;
        if (!rooms[key]) {
          rooms[key] = 1;
          console.log('[ROOM] ' + key + '   (distinct so far: ' + Object.keys(rooms).length + ')');
          if (Object.keys(rooms).length <= 2) {        // first 2: raw dump to verify offsets
            console.log(hexdump(s, { offset: 0, length: 0x40, ansi: false }));
          }
        }
      } catch (e) { console.log('[ROOM] read err ' + e); }
    }
  });

  // ---- C: GunThrow.Attack -> dump the instance fields (max_angle / counts / the spread config) ----
  let gt = 0;
  Interceptor.attach(A(base, RVA.GunThrow_Attack), {
    onEnter(a) {
      if (gt++ > 2) return;
      console.log('\n[GunThrow.Attack] this fields (look for max_angle / count band magnitudes):');
      console.log(hexdump(a[0], { offset: 0, length: 0xA0, ansi: false }));
    }
  });

  // ---- D: BossAI01 attack bucketing -- roll->InAtk distribution (the truncated jumptable) ----
  const atk = { 1: 0, 2: 0, 3: 0, 4: 0 };
  let lastShoot = false;
  Interceptor.attach(A(base, RVA.BossAI01_ShootReflection), {
    onEnter() { lastShoot = true; }
  });
  [['BossAI01_InAtk01', 1], ['BossAI01_InAtk02', 2], ['BossAI01_InAtk03', 3], ['BossAI01_InAtk04', 4]]
    .forEach(([k, n]) => {
      Interceptor.attach(A(base, RVA[k]), {
        onEnter() {
          atk[n]++;
          console.log('[BOSS] InAtk0' + n + (lastShoot ? ' (after ShootReflection roll)' : '') +
                      '   counts=' + JSON.stringify(atk));
          lastShoot = false;
        }
      });
    });

  // ---- periodic summary ----
  setInterval(function () {
    const tot = atk[1] + atk[2] + atk[3] + atk[4];
    console.log('\n----- SUMMARY -----');
    console.log('  distinct rooms (w x h / levels): ' + JSON.stringify(Object.keys(rooms)));
    if (tot > 0) {
      console.log('  boss attack distribution (reveals roll->attack bucket widths over 0..100):');
      console.log('    InAtk01..04 = ' + JSON.stringify(atk) + '  (n=' + tot + ', pct=' +
        [1,2,3,4].map(n => Math.round(100 * atk[n] / tot)).join('/') + ')');
    }
    console.log('-------------------');
  }, 20000);

  console.log('[+] hooks installed. Play: rooms/floors (B), fire a throw weapon (C), fight boss 1 (D).');
});
