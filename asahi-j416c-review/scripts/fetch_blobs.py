#!/usr/bin/env python3
"""Pull only the firmware members needed for the J416c power/VRR review out
of Apple's UniversalMac IPSWs, using HTTP range requests (no full download),
and unwrap each IM4P payload.

Usage: fetch_blobs.py OUTDIR [VERSION ...]
"""
import os
import sys

from pyimg4 import IM4P
from remotezip import RemoteZip

IPSWS = {
    "12.3": "https://updates.cdn-apple.com/2022SpringFCS/fullrestores/071-08757/74A4F2A1-C747-43F9-A22A-C0AD5FB4ECB6/UniversalMac_12.3_21E230_Restore.ipsw",
    "13.5": "https://updates.cdn-apple.com/2023SummerFCS/fullrestores/032-69606/D3E05CDF-E105-434C-A4A1-4E3DC7668DD0/UniversalMac_13.5_22G74_Restore.ipsw",
    "27.0.1": "https://updates.cdn-apple.com/2026FallFCS/59241290-5d51-4ca8-9df4-31624b9a4eac/UniversalMac_27.0.1_26A434_Restore.ipsw",
}

# t602x boards (power patches) and the ProMotion t600x MacBook Pros (VRR).
ADTS = ["j414cap", "j414sap", "j416cap", "j416sap", "j474sap", "j475cap",
        "j475dap", "j314cap", "j314sap", "j316cap", "j316sap"]

MEMBERS = (
    ["Firmware/all_flash/DeviceTree.%s.im4p" % b for b in ADTS] +
    ["Firmware/pmp/t6000pmp.im4p", "Firmware/pmp/t6020pmp.im4p",
     "Firmware/dcp/t600xdcp.im4p", "Firmware/dcp/t602xdcp.im4p",
     "Firmware/agx/armfw_g14c.im4p",
     "kernelcache.release.mac13j", "kernelcache.release.mac14j",
     "BuildManifest.plist"]
)


def unwrap(path):
    data = open(path, "rb").read()
    if b"IM4P" not in data[:16]:
        return None
    im4p = IM4P(data)
    if im4p.payload.compression:
        im4p.payload.decompress()
    # Firmware/x/y.im4p -> Firmware/x/y; kernelcache.* -> kernelcache.*.macho
    out = path[:-5] if path.endswith(".im4p") else path + ".macho"
    open(out, "wb").write(bytes(im4p.payload.output().data))
    return out


def main():
    outdir = sys.argv[1]
    versions = sys.argv[2:] or list(IPSWS)
    for v in versions:
        vdir = os.path.join(outdir, v)
        os.makedirs(vdir, exist_ok=True)
        with RemoteZip(IPSWS[v]) as z:
            names = set(z.namelist())
            for m in MEMBERS:
                if m not in names:
                    print(f"{v}: absent {m}")
                    continue
                dst = os.path.join(vdir, m)
                if not os.path.exists(dst):
                    z.extract(m, vdir)
                if m.endswith(".plist"):
                    continue
                out = unwrap(dst)
                print(f"{v}: {m} -> {out}")


if __name__ == "__main__":
    main()
