#!/usr/bin/env python3
"""Minimal raw Apple Device Tree (ADT) reader and cross-version/board differ.

Parses an unpacked DeviceTree.<board> blob (not the .im4p) without any type
interpretation, so every property comes back as raw bytes. Useful for
comparing tunables byte for byte against a Linux DT, or across macOS versions.

Library use:
    from adt_raw import load, get
    root = load("DeviceTree.j416cap")
    raw = get(root, "/arm-io/pmp/iop-pmp-nub", "soc-device")   # bytes

CLI:
    adt_raw.py dump  BLOB NODEPATH [PROP]        hex dump of node props
    adt_raw.py diff  BLOB_A BLOB_B NODEPATH      props that differ / are missing
"""
import struct
import sys


class Node:
    __slots__ = ("name", "props", "children", "path")

    def __init__(self):
        self.name = ""
        self.props = {}
        self.children = []
        self.path = ""


def _parse(buf, off, parent_path):
    nprops, nchildren = struct.unpack_from("<II", buf, off)
    off += 8
    n = Node()
    for _ in range(nprops):
        pname = buf[off:off + 32].split(b"\0", 1)[0].decode("ascii", "replace")
        size, = struct.unpack_from("<I", buf, off + 32)
        size &= 0x7FFFFFFF  # bit 31 = template/placeholder flag
        off += 36
        n.props[pname] = bytes(buf[off:off + size])
        off += (size + 3) & ~3
    n.name = n.props.get("name", b"").split(b"\0", 1)[0].decode()
    n.path = "/" if parent_path is None else (parent_path.rstrip("/") + "/" + n.name)
    for _ in range(nchildren):
        c, off = _parse(buf, off, n.path)
        n.children.append(c)
    return n, off


def load(path):
    with open(path, "rb") as f:
        buf = f.read()
    root, _ = _parse(buf, 0, None)
    return root


def find(root, path):
    node = root
    for comp in [c for c in path.split("/") if c]:
        for c in node.children:
            if c.name == comp:
                node = c
                break
        else:
            return None
    return node


def get(root, path, prop):
    n = find(root, path)
    return None if n is None else n.props.get(prop)


def walk(node):
    yield node
    for c in node.children:
        yield from walk(c)


def _hex(b):
    return " ".join("%02x" % x for x in b)


def main(argv):
    if len(argv) >= 3 and argv[0] == "dump":
        n = find(load(argv[1]), argv[2])
        if n is None:
            sys.exit("node not found")
        for k, v in n.props.items():
            if len(argv) > 3 and k != argv[3]:
                continue
            print("%s (%d): %s" % (k, len(v), _hex(v)))
    elif len(argv) == 4 and argv[0] == "diff":
        a = find(load(argv[1]), argv[3])
        b = find(load(argv[2]), argv[3])
        if a is None or b is None:
            sys.exit("node missing in %s" % ("A" if a is None else "B"))
        for k in sorted(set(a.props) | set(b.props)):
            va, vb = a.props.get(k), b.props.get(k)
            if va == vb:
                continue
            if va is None or vb is None:
                print("%-40s only in %s" % (k, "B" if va is None else "A"))
            else:
                print("%-40s differs (%d vs %d bytes)" % (k, len(va), len(vb)))
    else:
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    main(sys.argv[1:])
