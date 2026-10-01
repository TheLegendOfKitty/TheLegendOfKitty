#!/usr/bin/env python3
"""Cross-check the PMP / PMGR facts the J416c power patches rely on against
Apple's own code, for each macOS version given.

From the t602x kernelcache (kernelcache.release.mac14j, decompressed):
  - AppleT6021PMGR: FAB/DCS perf-state setters (register offset, field,
    busy bit, timeout) and the quiesce / restore sequences   [patch 0003]
  - ApplePMGR: the SOC-DEV-DVFS demand word encoder
    (_setDeviceDVFSState) and the PTD write/read views     [patches 0006/0007]
From the PMP firmware (Firmware/pmp/t6020pmp, decompressed):
  - the demand word decoder (state mask, sideband nibbles, DCS nibble)

Needs: llvm-objdump, llvm-nm, and blacktop's `ipsw` (for kext extraction).

Usage: pmgr_kext_xref.py IPSW_BIN BLOBS_DIR VERSION [VERSION ...]
  BLOBS_DIR/<ver>/kernelcache.release.mac14j.macho
  BLOBS_DIR/<ver>/Firmware/pmp/t6020pmp
"""
import os
import re
import subprocess
import sys
import tempfile


def sh(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def extract_kext(ipsw, kc, bundle, outdir):
    path = os.path.join(outdir, bundle)
    if not os.path.exists(path):
        subprocess.run([ipsw, "kernel", "extract", kc, bundle, "--output", outdir],
                       check=True, capture_output=True)
    return path


def functions(macho):
    """Map mangled symbol -> list of instruction strings."""
    out = {}
    cur = None
    for line in sh("llvm-objdump", "--macho", "-d", "--no-show-raw-insn", macho).splitlines():
        if re.match(r"^[_A-Za-z]\S*:$", line):
            cur = out.setdefault(line[:-1], [])
        elif cur is not None and "\t" in line:
            cur.append(line.split("\t", 1)[1].replace("\t", " "))
    return out


def setter_calls(body):
    """(FAB|DCS, value) for each call to the perf-state setters."""
    seq, last_w1 = [], None
    for ins in body:
        m = re.match(r"mov w1, #(0x[0-9a-f]+|\d+)$", ins)
        if m:
            last_w1 = int(m.group(1), 0)
        m = re.search(r"\bbl? __ZN14AppleT6020PMGR16_set(FAB|DCS)PerfStateEj", ins)
        if m:
            seq.append("%s%s" % (m.group(1), last_w1))
    return seq


def check_pmgr(fns):
    def find(prefix):
        return next((v for k, v in fns.items() if k.startswith(prefix)), None)

    for name in ("FAB", "DCS"):
        body = find("__ZN14AppleT6020PMGR16_set%sPerfStateEj" % name) or []
        regs = sorted({int(m.group(1), 0) for i in body
                       for m in [re.match(r"mov w1, #(0x[0-9a-f]+|\d+)$", i)] if m})
        field = [i for i in body if i.startswith("bfxil")]
        busy = any("#-0x80000000" in i for i in body)
        tmo = [i for i in body if re.match(r"mov(k)? w4", i)]
        print("  _set%sPerfState: reg offset %s, field %s, waits bit31=%s, timeout %s"
              % (name, [hex(r) for r in regs], field, busy, tmo))
    for fn in ("__ZN14AppleT6020PMGR16quiescePerfStateEv", "__ZN14AppleT6020PMGR9restoreHW"):
        body = find(fn)
        print("  %s: %s" % (fn.split("PMGR", 1)[1], setter_calls(body) if body else "absent"))


def check_dvfs_encoder(fns):
    enc = next((v for k, v in fns.items()
                if k.startswith("__ZN9ApplePMGR19_setDeviceDVFSState")), None)
    if enc is None:
        print("  _setDeviceDVFSState: absent")
        return
    keep = [i for i in enc if re.match(r"(and|bfi|orr) x", i) and "sp" not in i]
    print("  _setDeviceDVFSState word ops:", "; ".join(keep))
    for k, v in fns.items():
        if k.startswith("__ZNK8ApplePTD9_writePTD"):
            print("  %s: %s" % (k, "; ".join(i for i in v if re.match(r"(lsl|add) w2|lsl w8", i))))
        if k.startswith("__ZNK8ApplePTD8_readPTD"):
            print("  %s: %s" % (k, "; ".join(i for i in v if re.match(r"(lsl w\d+, w\d+, #4|bfi w\d+, w\d+, #4)", i))))


def check_pmp_fw(path):
    dis = sh("llvm-objdump", "--macho", "-d", "--no-show-raw-insn", path)
    nib = re.findall(r"ubfx\s+x\d+, x\d+, #(32|36|40|44), #4", dis)
    dcs = re.findall(r"bfxil\s+x\d+, x\d+, #48, #4", dis)
    st = re.findall(r"and\s+w\d+, w\d+, #0x1f", dis)
    print("  PMP fw decoder: sideband nibbles at bits %s, DCS nibble at 48: %d, "
          "state masks (#0x1f): %d" % (sorted(set(nib), key=int), len(dcs), len(st)))


def main():
    ipsw, blobs, versions = sys.argv[1], sys.argv[2], sys.argv[3:]
    for v in versions:
        print("=== macOS", v)
        kc = os.path.join(blobs, v, "kernelcache.release.mac14j.macho")
        with tempfile.TemporaryDirectory() as tmp:
            pmgr = functions(extract_kext(ipsw, kc, "com.apple.driver.ApplePMGR", tmp))
            t6021 = functions(extract_kext(ipsw, kc, "com.apple.driver.AppleT6021PMGR", tmp))
        check_pmgr(t6021)
        check_dvfs_encoder(pmgr)
        check_pmp_fw(os.path.join(blobs, v, "Firmware/pmp/t6020pmp"))


if __name__ == "__main__":
    main()
