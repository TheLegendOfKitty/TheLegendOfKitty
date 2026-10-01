#!/usr/bin/env python3
"""dcp_setparam.py - decode the IOMobileFramebuffer::set_parameter_dcp
dispatcher (RPC A439 on 12.3, A441 on 13.x, A440 on 14.7+) in a DCP firmware
image and report, per IOMFBParameterName, which handler it reaches and which
value counts it accepts.

The dispatcher is a `switch (param)` compiled to a byte jump table
(adrp/add table; adr base; ldrb; add base, idx, lsl #2; br). Each case checks
`count` (w23) and passes value[0..2] (read as u64) to a gated action through
the command gate's runAction (vtable +0x68).

Usage:
  dcp_setparam.py FW HANDLER_VA       # HANDLER_VA: a gated action reached by
                                      # one case (e.g. the adaptive-sync one)
Prints the jump table and, for the case(s) that ADR the handler, the count
check and value loads.
"""
import struct
import sys

import capstone

sys.path.insert(0, __import__("os").path.dirname(__file__))
from dcp_xref import MachO  # noqa: E402


def adr_refs(m, target):
    words = struct.unpack_from("<%dI" % (m.text_size // 4), m.data, m.text_off)
    for i, w in enumerate(words):
        if (w & 0x9F000000) == 0x10000000:
            imm = (((w >> 5) & 0x7FFFF) << 2) | ((w >> 29) & 3)
            if imm & (1 << 20):
                imm -= 1 << 21
            pc = m.text_va + 4 * i
            if pc + imm == target:
                yield pc


def find_jump_table(m, site, back=0x800):
    """Search backwards from `site` for 'ldrb w, [xT, xI]; add xB, xB, w, lsl #2; br xB'."""
    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    start = site - back
    o = m.va2off(start)
    insns = list(md.disasm(m.data[o:o + back], start))
    for k in range(len(insns) - 1, 3, -1):
        if insns[k].mnemonic == "br" and insns[k - 1].mnemonic == "add" and "lsl #2" in insns[k - 1].op_str \
                and insns[k - 2].mnemonic == "ldrb":
            # walk back for adr (base), add (table), cmp (bound)
            base = table = bound = None
            for j in range(k - 1, max(k - 16, 0), -1):
                ins = insns[j]
                if ins.mnemonic == "adr":
                    if base is None:
                        base = int(ins.op_str.split("#")[1], 16)
                    elif table is None:  # 12.3 style: table via a plain adr
                        table = int(ins.op_str.split("#")[1], 16)
                if ins.mnemonic == "add" and "#" in ins.op_str and table is None and insns[j - 1].mnemonic == "adrp":
                    table = int(insns[j - 1].op_str.split("#")[1], 16) + int(ins.op_str.split("#")[1], 16)
                if ins.mnemonic == "cmp" and bound is None and "#" in ins.op_str:
                    bound = int(ins.op_str.split("#")[1], 16)
            if base and table and bound is not None:
                return insns[k].address, base, table, bound
    return None


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    m = MachO(sys.argv[1])
    handler = int(sys.argv[2], 16)
    sites = list(adr_refs(m, handler))
    if not sites:
        print("no ADR reference to 0x%x" % handler)
        sys.exit(1)
    for site in sites:
        jt = find_jump_table(m, site)
        if not jt:
            print("ADR at 0x%x: no jump table found before it" % site)
            continue
        br, base, table, bound = jt
        print("handler 0x%x referenced at 0x%x; switch br at 0x%x, table 0x%x, base 0x%x, max param %d"
              % (handler, site, br, table, base, bound))
        o = m.va2off(table)
        cases = [base + 4 * m.data[o + p] for p in range(bound + 1)]
        hit = max([c for c in cases if c <= site], default=None)
        for p, case in enumerate(cases):
            mark = ""
            if case == hit:
                mark = "   <== reaches handler"
            print("  param %2d -> case 0x%x%s" % (p, case, mark))
            if mark:
                md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
                co = m.va2off(case)
                for ins in md.disasm(m.data[co:co + (site - case) + 0x10], case):
                    print("      %x: %s %s" % (ins.address, ins.mnemonic, ins.op_str))


if __name__ == "__main__":
    main()
