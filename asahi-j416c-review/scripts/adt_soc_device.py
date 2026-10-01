#!/usr/bin/env python3
"""Decode the PMP "soc-device" array of an ADT and check DTB pmp_report bits.

The PMP firmware (t602x, 22G74) builds its device masks with 1 << <position
in this array>, so a Linux pmp_report entry's "reg" must be the array
position, not id - 1. This prints, per ADT: position, id, type tag, word 2
(flags; bit 0 and bit 3 feed the firmware's DVFS aggregator masks), name,
and whether the position is set in soc-device-ps-group. With a DTB it maps
every report entry (enabled or not) to the ADT device at that position.

Usage:
    adt_soc_device.py ADT_BLOB [DTB]

Record layout (t600x and t602x, 124 bytes): u32 id, char[4] type (LE),
u32 flags ("word 2"), 26 more u32, char[8] name.
Needs adt_raw.py next to this file; pylibfdt only when a DTB is given.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from adt_raw import load, get  # noqa: E402

REC = 124
NUB = "/arm-io/pmp/iop-pmp-nub"


def records(adt_path):
    root = load(adt_path)
    sd = get(root, NUB, "soc-device")
    grp_raw = get(root, NUB, "soc-device-ps-group") or b"\0" * 8
    grp = struct.unpack("<Q", grp_raw[:8])[0]
    out = []
    for pos in range(len(sd) // REC):
        r = sd[pos * REC:(pos + 1) * REC]
        w = struct.unpack("<31I", r)
        out.append(dict(pos=pos, id=w[0], tag=r[4:8][::-1].decode("latin1"),
                        flags=w[2], name=r[116:124].rstrip(b"\0").decode("latin1"),
                        ps_group=bool(grp >> pos & 1) if pos < 64 else None))
    return out, grp


def _prop(fdt, node, name):
    import libfdt
    try:
        return bytes(fdt.getprop(node, name))
    except libfdt.FdtException:
        return None


def dtb_report_entries(dtb_path):
    import libfdt
    fdt = libfdt.Fdt(open(dtb_path, "rb").read())
    out = []
    node = 0
    depth = 0
    while True:
        node, depth = fdt.next_node(node, depth, (libfdt.NOTFOUND,))
        if node < 0 or depth < 0:
            break
        comp = _prop(fdt, node, "compatible")
        if comp is None or b"pmp-v2-report-entry" not in comp:
            continue
        reg = struct.unpack(">I", _prop(fdt, node, "reg"))[0]
        label = _prop(fdt, node, "label").rstrip(b"\0").decode()
        st = _prop(fdt, node, "status")
        status = st.rstrip(b"\0").decode() if st is not None else "okay"
        always = _prop(fdt, node, "apple,always-on") is not None
        out.append((reg, label, status, always, fdt.get_name(node)))
    return sorted(out)


def main(argv):
    if not argv:
        print(__doc__)
        sys.exit(1)
    recs, grp = records(argv[0])
    print("%s: %d records, soc-device-ps-group=%#018x" % (argv[0], len(recs), grp))
    for r in recs:
        print("  pos %2d (0x%02x) id %3d %-4s flags %#x  %-9s ps-group=%s" % (
            r["pos"], r["pos"], r["id"], r["tag"], r["flags"], r["name"], r["ps_group"]))
    if len(argv) > 1:
        print("\nDTB report entries (%s):" % argv[1])
        for reg, label, status, always, nname in dtb_report_entries(argv[1]):
            tgt = recs[reg]["name"] if reg < len(recs) else "<beyond array>"
            byid = [r["name"] for r in recs if r["id"] == reg + 1]
            print("  reg 0x%02x %-22s %-8s%s -> position: %-9s | id-1 rule: %s" % (
                reg, label, status, " AO" if always else "   ", tgt,
                byid[0] if byid else "-"))


if __name__ == "__main__":
    main(sys.argv[1:])
