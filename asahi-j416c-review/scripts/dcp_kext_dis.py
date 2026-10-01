#!/usr/bin/env python3
"""dcp_kext_dis.py - disassemble a symbol from a kext extracted out of a macOS
kernelcache (e.g. with `ipsw kernel extract ... com.apple.iokit.IOMobileGraphicsFamily-DCP`).

The AP-side IOMobileFramebuffer/AppleDCP kexts keep their C++ symbol tables, so
this is the quickest way to see how macOS drives the DCP RPC interface
(IOMobileFramebufferAP::set_parameter_dcp, ::abort_swap_ap_gated, swap record
setup, ...). BL/B targets are annotated with symbol names and ADRP+ADD pairs
with the C string they point at.

Usage:
  dcp_kext_dis.py KEXT sym REGEX          # list symbols matching REGEX
  dcp_kext_dis.py KEXT dis SYMBOL|0xVA    # disassemble one function
Requires llvm-nm and capstone.
"""
import re
import struct
import subprocess
import sys

import capstone


def load(path):
    data = open(path, "rb").read()
    ncmds = struct.unpack_from("<I", data, 16)[0]
    off, segs = 32, []
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", data, off)
        if cmd == 0x19:
            vm, vs, fo, fs = struct.unpack_from("<QQQQ", data, off + 24)
            segs.append((vm, vs, fo, fs))
        off += size
    syms = []
    out = subprocess.run(["llvm-nm", "-n", path], capture_output=True, text=True).stdout
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3 and p[1].lower() == "t":
            syms.append((int(p[0], 16), p[2]))
    return data, segs, syms


def va2off(segs, va):
    for vm, vs, fo, fs in segs:
        if vm <= va < vm + fs:
            return va - vm + fo
    return None


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)
    path, cmd, arg = sys.argv[1:4]
    data, segs, syms = load(path)
    byaddr = dict(syms)
    if cmd == "sym":
        rx = re.compile(arg)
        for a, n in syms:
            if rx.search(n):
                print("%x %s" % (a, n))
        return
    if arg.startswith("0x"):
        start = int(arg, 16)
    else:
        start = [a for a, n in syms if n == arg][0]
    nxt = min([a for a, _ in syms if a > start] or [start + 0x400])
    o = va2off(segs, start)
    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    pages = {}
    print("%s:" % byaddr.get(start, hex(start)))
    for ins in md.disasm(data[o:o + (nxt - start)], start):
        note = ""
        ops = ins.op_str
        if ins.mnemonic in ("bl", "b") and ops.startswith("#"):
            t = int(ops[1:], 16)
            if t in byaddr:
                note = "  ; " + byaddr[t]
        elif ins.mnemonic == "adrp":
            pages[ops.split(",")[0]] = int(ops.split("#")[1], 16)
        elif ins.mnemonic == "add" and "#" in ops:
            p = [x.strip() for x in ops.split(",")]
            if p[1] in pages:
                t = pages[p[1]] + int(p[2].lstrip("#"), 16)
                so = va2off(segs, t)
                if so:
                    end = data.find(b"\0", so, so + 160)
                    s = data[so:end]
                    if len(s) > 3 and all(32 <= c < 127 for c in s):
                        note = "  ; %r" % s.decode()
                    else:
                        note = "  ; 0x%x" % t
        print("%x: %-8s %s%s" % (ins.address, ins.mnemonic, ops, note))


if __name__ == "__main__":
    main()
