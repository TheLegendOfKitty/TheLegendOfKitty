#!/usr/bin/env python3
"""dcp_xref.py - find code that references strings/addresses in an Apple RTKit
firmware Mach-O (e.g. Firmware/dcp/t602xdcp) and disassemble around it.

The DCP images are stripped arm64 Mach-O "preload" executables with no
symbols. This tool resolves ADRP+ADD/LDR pairs over the whole __TEXT,__text
section to locate references, so log-format strings can be used to find the
functions behind RPC methods (e.g. "IOMFBParameter_adaptive_sync ...").

Usage:
  dcp_xref.py FW strings REGEX             # list strings (VA, text) matching REGEX
  dcp_xref.py FW xref REGEX|0xVA [...]     # code locations referencing them
  dcp_xref.py FW func 0xVA                 # guess the enclosing function start
  dcp_xref.py FW dis 0xSTART [0xEND|+N]    # disassemble (resolves ADRP targets)
  dcp_xref.py FW callers 0xVA              # BL instructions that call VA

VAs are firmware virtual addresses (file offset - segment fileoff + vmaddr).
"""
import re
import struct
import sys

import capstone

LC_SEGMENT_64 = 0x19


class MachO:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        magic, _, _, _, ncmds, _, _, _ = struct.unpack_from("<IiiIIIII", self.data, 0)
        assert magic == 0xFEEDFACF, "not a 64-bit Mach-O"
        self.segs = []   # (name, vmaddr, vmsize, fileoff, filesize)
        self.sects = []  # (seg, sect, addr, size, offset)
        off = 32
        for _ in range(ncmds):
            cmd, size = struct.unpack_from("<II", self.data, off)
            if cmd == LC_SEGMENT_64:
                name = self.data[off + 8:off + 24].rstrip(b"\0").decode()
                vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", self.data, off + 24)
                nsects = struct.unpack_from("<I", self.data, off + 64)[0]
                self.segs.append((name, vmaddr, vmsize, fileoff, filesize))
                so = off + 72
                for _ in range(nsects):
                    sn = self.data[so:so + 16].rstrip(b"\0").decode()
                    addr, ssize, soff = struct.unpack_from("<QQI", self.data, so + 32)
                    self.sects.append((name, sn, addr, ssize, soff))
                    so += 80
            off += size
        text = [s for s in self.sects if s[0] == "__TEXT" and s[1] == "__text"][0]
        self.text_va, self.text_size, self.text_off = text[2], text[3], text[4]

    def va2off(self, va):
        for _, vm, vs, fo, fs in self.segs:
            if vm <= va < vm + min(vs, fs):
                return va - vm + fo
        return None

    def off2va(self, off):
        for _, vm, vs, fo, fs in self.segs:
            if fo <= off < fo + fs:
                return off - fo + vm
        return None

    def u32(self, va):
        return struct.unpack_from("<I", self.data, self.va2off(va))[0]

    def cstr(self, va, maxlen=256):
        o = self.va2off(va)
        if o is None:
            return None
        end = self.data.find(b"\0", o, o + maxlen)
        return self.data[o:end].decode("latin1") if end > 0 else None

    def strings(self, regex, minlen=4):
        pat = re.compile(rb"[\x20-\x7e]{%d,}" % minlen)
        rx = re.compile(regex.encode())
        for m in pat.finditer(self.data):
            if rx.search(m.group()):
                va = self.off2va(m.start())
                if va is not None:
                    yield va, m.group().decode()

    def adrp_refs(self):
        """Yield (insn_va, target_va) for ADRP+ADD/LDR/STR imm pairs in __text."""
        words = struct.unpack_from("<%dI" % (self.text_size // 4), self.data, self.text_off)
        base = self.text_va
        for i, w in enumerate(words):
            if (w & 0x9F000000) != 0x90000000:
                continue
            rd = w & 0x1F
            immlo = (w >> 29) & 3
            immhi = (w >> 5) & 0x7FFFF
            imm = ((immhi << 2) | immlo) << 12
            if imm & (1 << 32):
                imm -= 1 << 33
            pc = base + 4 * i
            page = (pc & ~0xFFF) + imm
            for j in range(i + 1, min(i + 8, len(words))):
                w2 = words[j]
                if (w2 & 0xFF800000) == 0x91000000 and ((w2 >> 5) & 0x1F) == rd:  # ADD imm
                    sh = (w2 >> 22) & 1
                    yield pc, page + (((w2 >> 10) & 0xFFF) << (12 if sh else 0))
                    break
                if (w2 & 0x3B400000) == 0x39400000 and ((w2 >> 5) & 0x1F) == rd:  # LDR/STR uimm
                    size = w2 >> 30
                    if (w2 & 0x04000000) and ((w2 >> 23) & 1):
                        size = 4
                    yield pc, page + (((w2 >> 10) & 0xFFF) << size)
                    break

    def func_start(self, va, limit=0x4000):
        """Walk back to the nearest PACIBSP or 'stp x29,x30' / 'sub sp' prologue
        that follows a RET/B (best effort)."""
        a = va
        while a > max(self.text_va, va - limit):
            w = self.u32(a)
            if w == 0xD503237F:  # pacibsp
                return a
            prev = self.u32(a - 4)
            if prev in (0xD65F03C0, 0xD65F0FFF) or (prev & 0xFC000000) == 0x14000000:
                if (w & 0xFFC003E0) == 0xA98003E0 or (w & 0xFF0003FF) == 0xD10003FF:
                    return a
            a -= 4
        return None

    def disasm(self, start, end):
        md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
        o = self.va2off(start)
        code = self.data[o:o + (end - start)]
        pages = {}
        for ins in md.disasm(code, start):
            note = ""
            if ins.mnemonic == "adrp":
                rd = ins.op_str.split(",")[0]
                pages[rd] = int(ins.op_str.split("#")[1], 16)
            elif ins.mnemonic == "add" and "#" in ins.op_str:
                parts = [p.strip() for p in ins.op_str.split(",")]
                if parts[1] in pages:
                    tgt = pages[parts[1]] + int(parts[2].lstrip("#"), 16)
                    s = self.cstr(tgt, 120)
                    note = "  ; 0x%x %r" % (tgt, s) if s and len(s) > 3 else "  ; 0x%x" % tgt
            print("%08x: %08x  %-8s %s%s" % (ins.address, struct.unpack("<I", ins.bytes)[0],
                                            ins.mnemonic, ins.op_str, note))

    def callers(self, target):
        words = struct.unpack_from("<%dI" % (self.text_size // 4), self.data, self.text_off)
        for i, w in enumerate(words):
            if (w & 0xFC000000) in (0x94000000, 0x14000000):
                imm = w & 0x3FFFFFF
                if imm & (1 << 25):
                    imm -= 1 << 26
                pc = self.text_va + 4 * i
                if pc + imm * 4 == target:
                    yield pc, "bl" if w & 0x80000000 else "b"


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)
    m = MachO(sys.argv[1])
    cmd, args = sys.argv[2], sys.argv[3:]
    if cmd == "strings":
        for va, s in m.strings(args[0]):
            print("0x%08x %s" % (va, s))
    elif cmd == "xref":
        targets = {}
        for a in args:
            if a.startswith("0x"):
                targets[int(a, 16)] = a
            else:
                for va, s in m.strings(a):
                    targets[va] = s
        for pc, tgt in m.adrp_refs():
            if tgt in targets:
                fs = m.func_start(pc)
                print("ref at 0x%08x (func ~0x%s) -> 0x%08x %r" %
                      (pc, "%08x" % fs if fs else "?", tgt, targets[tgt]))
    elif cmd == "func":
        fs = m.func_start(int(args[0], 16))
        print("0x%08x" % fs if fs else "?")
    elif cmd == "dis":
        start = int(args[0], 16)
        if len(args) > 1:
            end = start + int(args[1][1:], 0) if args[1].startswith("+") else int(args[1], 16)
        else:
            end = start + 0x200
        m.disasm(start, end)
    elif cmd == "callers":
        for pc, kind in m.callers(int(args[0], 16)):
            fs = m.func_start(pc)
            print("%s at 0x%08x (func ~0x%s)" % (kind, pc, "%08x" % fs if fs else "?"))
    else:
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    main()
