#!/usr/bin/env python3
"""arm_disasm.py -- reproduce the DISASM evidence (T-39) behind the P3.3.c skill ports.

game_typed.c (the Ghidra export) truncates many il2cpp method bodies at an
"unrecoverable" tail call ("Subroutine does not return"), so the hero-skill tails
(C03 CreateTunder, C05 bottle volley, C07 BatBack / bat spawn loop, C11 holy light,
C12 summon/cut, BulletThunder / BulletBat / BulletGreenLight ...) are recovered by
disassembling the ARM code directly from libil2cpp.so. Every `DISASM 0x...` address
quoted in src/combat/CharSkillC03/05/07/11/12.cpp and include/combat/CharSkillC*.hpp is
a Ghidra address; file offset = Ghidra address - 0x10000 (image base 0x10000).

Usage (needs `pip install capstone`):
    python tools/arm_disasm.py 0x264230 0x26451c            # C03Controller$$RoleSkill
    python tools/arm_disasm.py 0x26451c 0x264c14            # C03Controller$$CreateTunder
    python tools/arm_disasm.py 0x267098 0x2672e4            # C07Controller$$BatBack
    python tools/arm_disasm.py 0xaf2a8c 0xaf37a0            # BulletBat$$FixedUpdate
    python tools/arm_disasm.py 0x2672d4 0x2672dc --floats   # literal-pool floats (6.0 / 10.0)

Annotations: bl/blx/b targets are named from Il2CppDumper's script.json (and PLT stubs are
resolved through .rel.plt: cosf / sinf / __aeabi_idiv ...); `ldr rX,[pc,#imm]` loads are
annotated with the pool word (and, for the il2cpp metadata-usage idiom, the string
literal / type / method the slot names, e.g. "Body_E", "Method$...GetComponent<BulletBat>()").
"""
from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SO_DEFAULT = ROOT.parent / "1.7.10" / "lib" / "armeabi-v7a" / "libil2cpp.so"
SCRIPT_JSON_DEFAULT = ROOT.parent / "_reverse" / "tools" / "il2cpp_out" / "script.json"
IMAGE_BASE = 0x10000  # Ghidra address = ELF vaddr + 0x10000 for this binary.


class Binary:
    def __init__(self, so: Path, script_json: Path | None):
        self.d = so.read_bytes()
        self.names: dict[int, str] = {}
        self.strings: dict[int, str] = {}
        self.meta: dict[int, str] = {}
        if script_json is not None and script_json.is_file():
            sj = json.loads(script_json.read_text(encoding="utf-8"))
            for m in sj.get("ScriptMethod", []):
                self.names[m["Address"] + IMAGE_BASE] = m["Name"]
            for m in sj.get("ScriptString", []):
                self.strings[m["Address"]] = m["Value"]
            for k in ("ScriptMetadata", "ScriptMetadataMethod"):
                for m in sj.get(k, []):
                    self.meta[m["Address"]] = m["Name"]
        self._plt = None

    # -- helpers -----------------------------------------------------------
    def word(self, ghidra_addr: int) -> int:
        o = ghidra_addr - IMAGE_BASE
        return struct.unpack_from("<I", self.d, o)[0]

    def plt_symbols(self) -> dict[int, str]:
        """GOT slot (vaddr) -> symbol name, from the PT_DYNAMIC relocations."""
        if self._plt is not None:
            return self._plt
        e_phoff = struct.unpack_from("<I", self.d, 0x1C)[0]
        n = struct.unpack_from("<H", self.d, 0x2C)[0]
        dyn = None
        for i in range(n):
            p_type, p_off, _v, _p, p_filesz = struct.unpack_from("<5I", self.d, e_phoff + i * 32)
            if p_type == 2:
                dyn = (p_off, p_filesz)
        tags: dict[int, int] = {}
        if dyn:
            for i in range(dyn[1] // 8):
                t, v = struct.unpack_from("<II", self.d, dyn[0] + i * 8)
                tags.setdefault(t, v)
        out: dict[int, str] = {}
        if dyn and 5 in tags and 6 in tags:
            symtab, strtab = tags[6], tags[5]
            for tag_off, tag_sz in ((23, 2), (17, 18)):
                if tag_off in tags and tag_sz in tags:
                    for i in range(tags[tag_sz] // 8):
                        r_off, r_info = struct.unpack_from("<II", self.d, tags[tag_off] + i * 8)
                        sym = r_info >> 8
                        if sym:
                            st_name = struct.unpack_from("<I", self.d, symtab + sym * 16)[0]
                            s = strtab + st_name
                            out[r_off] = self.d[s:self.d.index(b"\0", s)].decode()
        self._plt = out
        return out

    def plt_name(self, ghidra_addr: int) -> str | None:
        """Name a PLT stub (add ip,pc,#a ; add ip,ip,#b ; ldr pc,[ip,#c]!) via .rel.plt."""
        o = ghidra_addr - IMAGE_BASE
        if o < 0 or o + 12 > len(self.d):
            return None
        w0, w1, w2 = struct.unpack_from("<III", self.d, o)
        if (w0 >> 20) != 0xE28 or (w1 >> 20) != 0xE28 or (w2 & 0xFFFFF000) != 0xE5BCF000:
            return None

        def rot(w: int) -> int:
            imm, r = w & 0xFF, ((w >> 8) & 0xF) * 2
            return ((imm >> r) | (imm << (32 - r))) & 0xFFFFFFFF if r else imm

        slot = o + 8 + rot(w0) + rot(w1) + (w2 & 0xFFF)
        return self.plt_symbols().get(slot)


def disassemble(b: Binary, start: int, end: int, floats: bool) -> None:
    import capstone

    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
    s, e = start - IMAGE_BASE, end - IMAGE_BASE
    if floats:
        for a in range(start, end, 4):
            w = b.word(a)
            print("%x: 0x%08x  float=%g  int=%d" % (a, w, struct.unpack("<f", struct.pack("<I", w))[0], w))
        return
    insns = []
    pos = s
    while pos < e:  # capstone stops at literal-pool words: step over them
        got = False
        for ins in md.disasm(b.d[pos:e], pos + IMAGE_BASE):
            insns.append(ins)
            pos = ins.address - IMAGE_BASE + ins.size
            got = True
        if pos < e and not got or pos < e:
            w = struct.unpack_from("<I", b.d, pos)[0]
            insns.append(("word", pos + IMAGE_BASE, w))
            pos += 4
    for idx, ins in enumerate(insns):
        if isinstance(ins, tuple):
            _, a, w = ins
            print("%x: .word    0x%08x f=%g" % (a, w, struct.unpack("<f", struct.pack("<I", w))[0]))
            continue
        note = ""
        m = re.match(r"#0x([0-9a-f]+)$", ins.op_str)
        if ins.mnemonic in ("bl", "blx", "b") and m:
            t = int(m.group(1), 16)
            nm = b.names.get(t) or b.plt_name(t)
            if nm:
                note = "   ; " + nm
        m2 = re.match(r"(r\d+), \[pc, #(0x[0-9a-f]+)\]", ins.op_str)
        if m2 and ins.mnemonic in ("ldr", "vldr"):
            pool = ins.address + 8 + int(m2.group(2), 16)
            w = b.word(pool)
            note = "   ; pool[%x]=0x%08x f=%g" % (
                pool, w, struct.unpack("<f", struct.pack("<I", w))[0])
            # il2cpp metadata-usage idiom: ldr rX,[pc,#imm] ; ... ; ldr rX,[pc,rX] ; ldr r1,[rX]
            for k in range(1, 9):
                if idx + k < len(insns) and not isinstance(insns[idx + k], tuple):
                    c = insns[idx + k]
                    if c.mnemonic == "ldr" and re.match(r"r\d+, \[pc, r\d+\]", c.op_str):
                        try:
                            slot = c.address + 8 + w - IMAGE_BASE
                            ref = struct.unpack_from("<I", b.d, slot)[0]
                            lab = b.strings.get(ref) or b.meta.get(ref)
                            if lab:
                                note += "   ; -> " + lab
                        except (struct.error, IndexError):
                            pass
                        break
        print("%x: %-8s %s%s" % (ins.address, ins.mnemonic, ins.op_str, note))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("start", help="Ghidra start address (hex)")
    ap.add_argument("end", help="Ghidra end address (hex, exclusive)")
    ap.add_argument("--so", type=Path, default=SO_DEFAULT)
    ap.add_argument("--script-json", type=Path, default=SCRIPT_JSON_DEFAULT)
    ap.add_argument("--floats", action="store_true", help="dump words as float/int (literal pools)")
    a = ap.parse_args()
    try:
        import capstone  # noqa: F401
    except ImportError:
        print("capstone is required: pip install capstone", file=sys.stderr)
        return 2
    if not a.so.is_file():
        print(f"missing {a.so}", file=sys.stderr)
        return 2
    b = Binary(a.so, a.script_json)
    disassemble(b, int(a.start, 16), int(a.end, 16), a.floats)
    return 0


if __name__ == "__main__":
    sys.exit(main())
