#!/usr/bin/env python3
"""Compare a compiled t602x Linux DTB's PMP node (/soc/pmp@28e700000) against
Apple ADT /arm-io/pmp/iop-pmp-nub data.

For every ADT given, reports per property whether the DTB's
apple,tunable-<name> is identical, differs (with the first differing byte),
is missing from the DTB, or is DTB-only. It also mirrors what m1n1 >= 1.6
dt_set_pmp() would inject (every iop-pmp-nub property except its exclusion
list) so you can see what a newer m1n1 overwrites at boot.

Usage:
    adt_pmp_dt_check.py DTB ADT_BLOB [ADT_BLOB ...]
    e.g. adt_pmp_dt_check.py build/.../t6021-j416c.dtb \
         blobs/13.5/Firmware/all_flash/DeviceTree.j416cap \
         blobs/27.0.1/Firmware/all_flash/DeviceTree.j416cap

Needs pylibfdt (pip install pylibfdt) and adt_raw.py next to this file.
"""
import os
import sys

import libfdt

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from adt_raw import load, get, find  # noqa: E402

# m1n1 src/kboot.c excluded_pmp_props[]
M1N1_EXCLUDED = {
    "compatible", "AAPL,phandle", "region-base", "region-size",
    "segment-names", "segment-ranges", "pre-loaded", "firmware-name",
    "dram-capacity", "coredump-enable", "name",
}
PREFIX = "apple,tunable-"


def dtb_pmp_props(path):
    fdt = libfdt.Fdt(open(path, "rb").read())
    off = fdt.path_offset("/soc/pmp@28e700000")
    props = {}
    poff = fdt.first_property_offset(off, libfdt.QUIET_NOTFOUND)
    while poff >= 0:
        p = fdt.get_property_by_offset(poff)
        props[p.name] = bytes(p)
        poff = fdt.next_property_offset(poff, libfdt.QUIET_NOTFOUND)
    return props


def first_diff(a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return i
    return min(len(a), len(b))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        sys.exit(1)
    dt = dtb_pmp_props(argv[0])
    dt_tun = {k[len(PREFIX):]: v for k, v in dt.items() if k.startswith(PREFIX)}
    print("DTB %s: %d apple,tunable-* props; status=%r" %
          (argv[0], len(dt_tun), dt.get("status", b"").rstrip(b"\0")))
    for k in ("apple,board-id", "apple,dram-vendor-id", "apple,dram-capacity"):
        if k in dt:
            print("  %s = %s" % (k, dt[k].hex()))
    for adt_path in argv[1:]:
        root = load(adt_path)
        nub = find(root, "/arm-io/pmp/iop-pmp-nub")
        adt = {k: v for k, v in nub.props.items() if k not in M1N1_EXCLUDED}
        print("\n== vs %s (m1n1>=1.6 would inject %d tunables)" % (adt_path, len(adt)))
        print("   ADT dram-capacity=%s chosen/board-id=%s chosen/dram-vendor-id=%s" % (
            (nub.props.get("dram-capacity") or b"").hex(),
            (get(root, "/chosen", "board-id") or b"").hex(),
            (get(root, "/chosen", "dram-vendor-id") or b"").hex()))
        same = []
        for k in sorted(set(adt) | set(dt_tun)):
            a, d = adt.get(k), dt_tun.get(k)
            if a == d:
                same.append(k)
            elif d is None:
                print("   MISSING in DTB : %s (%d bytes)" % (k, len(a)))
            elif a is None:
                print("   DTB-only       : %s (%d bytes)" % (k, len(d)))
            else:
                i = first_diff(a, d)
                print("   DIFFERS        : %s ADT %d B vs DTB %d B, first diff at byte %d"
                      " (ADT %s / DTB %s)" % (k, len(a), len(d), i,
                                             a[i:i + 4].hex(), d[i:i + 4].hex()))
        print("   identical: %d" % len(same))


if __name__ == "__main__":
    main(sys.argv[1:])
