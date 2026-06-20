/*
 * sk_il2cpp_agent.ts — recover blocked il2cpp RUNTIME values BY NAME (architecture-agnostic).
 * Use this on LDPlayer / any x86 emulator (the game is ARM-translated, so raw-address hooks are
 * unreliable; resolving il2cpp classes/methods/fields by NAME works regardless of translation).
 *
 * Target: Soul Knight 1.7.10 (com.ChillyRoom.DungeonShooter), il2cpp metadata v24 (Unity 2017.x).
 *
 * BUILD + RUN (PC):
 *   npm init -y
 *   npm install frida-il2cpp-bridge frida-compile @types/frida-gum
 *   npx frida-compile tools/sk_il2cpp_agent.ts -o agent.js
 *   frida -U -f com.ChillyRoom.DungeonShooter -l agent.js
 *
 * THEN PLAY a few minutes:
 *   B) walk through rooms / go down floors     -> [ROOM] sizes + wall/obstacle levels
 *   C) fire a THROW weapon (knife/axe/shuriken) -> [GunThrow] config fields
 *   D) fight the FIRST boss                      -> [BOSS] InAtk01..04 distribution (the bucketing)
 * Copy the WHOLE console log back.
 */
import "frida-il2cpp-bridge";

Il2Cpp.perform(() => {
  console.log("[+] il2cpp bridge ready. Unity " + Il2Cpp.unityVersion);
  const img = Il2Cpp.domain.assembly("Assembly-CSharp").image;

  // ---------- B: RGRoomX room sizes + levels (read fields BY NAME after generation) ----------
  const rooms: Record<string, number> = {};
  try {
    const RGRoomX = img.class("RGRoomX");
    const hook = (m: string) => {
      const method = RGRoomX.tryMethod(m);
      if (!method) { console.log("[!] RGRoomX." + m + " not found"); return; }
      method.implementation = function (...a: Il2Cpp.Parameter.Type[]) {
        const r = (this as Il2Cpp.Object).method(m).invoke(...a);
        try {
          const self = this as Il2Cpp.Object;
          const w  = self.field("room_width").value;
          const h  = self.field("room_height").value;
          const wl = self.field("wall_level").value;
          const ol = self.field("obstacle_level").value;
          const ty = self.field("room_type").value;
          const key = `${w}x${h} type=${ty} wall_level=${wl} obstacle_level=${ol}`;
          if (!rooms[key]) {
            rooms[key] = 1;
            console.log("[ROOM] " + key + "   (distinct=" + Object.keys(rooms).length + ")");
          }
        } catch (e) { console.log("[ROOM] field read err: " + e); }
        return r;
      };
    };
    hook("SetUpRoom");
    hook("SetRGRandomSeed");
  } catch (e) { console.log("[!] RGRoomX: " + e); }

  // ---------- C: GunThrow config (the spread depends on these) ----------
  try {
    const GunThrow = img.class("GunThrow");
    let gt = 0;
    const am = GunThrow.tryMethod("Attack");
    if (am) am.implementation = function (...a: Il2Cpp.Parameter.Type[]) {
      const r = (this as Il2Cpp.Object).method("Attack").invoke(...a);
      if (gt++ < 4) {
        const self = this as Il2Cpp.Object;
        const f = (n: string) => { try { return self.field(n).value; } catch { return "?"; } };
        console.log(`[GunThrow] max_count=${f("max_count")} max_consume=${f("max_consume")} ` +
                    `prepare_time=${f("prepare_time")} max_angle=${f("max_angle")} angle=${f("angle")}`);
      }
      return r;
    };
  } catch (e) { console.log("[!] GunThrow: " + e); }

  // ---------- D: BossAI01 attack bucketing (roll -> which InAtk fires) ----------
  try {
    const Boss = img.class("BossAI01");
    const atk: Record<number, number> = { 1: 0, 2: 0, 3: 0, 4: 0 };
    [1, 2, 3, 4].forEach((n) => {
      const m = Boss.tryMethod("InAtk0" + n);
      if (!m) { console.log("[!] BossAI01.InAtk0" + n + " not found"); return; }
      m.implementation = function (...a: Il2Cpp.Parameter.Type[]) {
        atk[n]++;
        const tot = atk[1] + atk[2] + atk[3] + atk[4];
        console.log(`[BOSS] InAtk0${n}  counts=${JSON.stringify(atk)}  pct=` +
          [1, 2, 3, 4].map((k) => Math.round((100 * atk[k]) / tot)).join("/"));
        return (this as Il2Cpp.Object).method("InAtk0" + n).invoke(...a);
      };
    });
  } catch (e) { console.log("[!] BossAI01: " + e); }

  setInterval(() => {
    console.log("\n----- SUMMARY -----\n  distinct rooms: " +
      JSON.stringify(Object.keys(rooms)) + "\n-------------------");
  }, 20000);

  console.log("[+] hooks installed. Play rooms/floors (B), fire a throw weapon (C), fight boss 1 (D).");
});
