#!/usr/bin/env python3
"""Decode the PMP firmware configuration that Apple ships in the ADT
(/arm-io/pmp/iop-pmp-nub): the PTD mailbox map (ptd-range), the SOC-DEV
activity-bit order (soc-device) and the DVFS domains (dvfs-domain).

The patch series hard-codes several of these (SOC-DEV-PS-REQ/ACK entries,
SOC-DEV-DVFS base 0x210, client bits 7-10, PMPTOOL 12-15, SOC-DEV-PKT
0x50-0x1ff); this prints them so they can be checked per board/version.

Usage: pmp_adt_decode.py M1N1_PROXYCLIENT_DIR ADT [ADT ...]
"""
import struct
import sys


def cstr(b):
    return b.split(b"\0", 1)[0].decode("ascii", "replace")


def ptd_ranges(raw):
    out = []
    for off in range(0, len(raw), 32):
        rid, start, count, flags = struct.unpack_from("<4I", raw, off)
        out.append((rid, start, count, flags, cstr(raw[off + 16:off + 32])))
    return out


def soc_devices(raw):
    # 124-byte records: u32 id, fourcc, 27 x u32 fields, char name[8]
    size = 124
    out = []
    for pos, off in enumerate(range(0, len(raw), size)):
        rec = raw[off:off + size]
        did = struct.unpack_from("<I", rec, 0)[0]
        fcc = rec[4:8][::-1].decode("ascii", "replace")
        words = struct.unpack_from("<27I", rec, 8)
        out.append((pos, did, fcc, words, cstr(rec[116:124])))
    return out


def dvfs_domains(raw):
    size = 28
    out = []
    for off in range(0, len(raw) - size + 1, size):
        did, nstates, flags = struct.unpack_from("<3I", raw, off)
        out.append((did, nstates, flags, cstr(raw[off + 12:off + 28])))
    return out


def main():
    sys.path.insert(0, sys.argv[1])
    from m1n1.adt import load_adt

    for path in sys.argv[2:]:
        adt = load_adt(open(path, "rb").read())
        nub = adt["/arm-io/pmp/iop-pmp-nub"]
        print("=== %s (%s)" % (path, adt.compatible[0]))
        print("-- ptd-range: id start..end count flags name")
        for rid, start, count, flags, name in ptd_ranges(nub.ptd_range):
            print("  %2d 0x%03x..0x%03x %3d 0x%x %s" %
                  (rid, start, start + count - 1, count, flags, name))
        print("-- soc-device: bit id fourcc kind/fields name")
        for pos, did, fcc, words, name in soc_devices(nub.soc_device):
            nz = [(i, w) for i, w in enumerate(words) if w]
            print("  bit %2d id 0x%02x %s %-9s %s" % (pos, did, fcc, name, nz))
        print("-- dvfs-domain: id nstates flags name")
        for did, n, flags, name in dvfs_domains(nub.dvfs_domain):
            print("  %d %d 0x%x %s" % (did, n, flags, name))
        print("-- soc-device-die-offset", nub.soc_device_die_offset,
              " soc-device-ps-group 0x%016x" % nub.soc_device_ps_group)
        print("-- pm-ptd-ranges", list(struct.unpack("<%dI" % (len(nub.pm_ptd_ranges) // 4), nub.pm_ptd_ranges)))
        mcc = adt["/arm-io/mcc"]
        props = mcc._properties
        reg = mcc.reg[props["pmp_ptd_reg_idx"]]
        print("-- mcc PTD block 0x%x size 0x%x, update view at +0x%x" %
              (reg.addr, reg.size, props["pmp_ptd_update_space_offset"]))


if __name__ == "__main__":
    main()
