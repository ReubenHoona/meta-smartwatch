#!/usr/bin/env python3
"""Split a blob of concatenated flattened device trees into one file each.

vkb-base.dtb carries one DTB per SoC variant (MonacoP msm-id 0x205 and
Monaco msm-id 0x1e6); fdtput only ever reads and rewrites the first tree of
a file, so each one has to be patched on its own.

Usage: aurora-split-dtbs.py <in.dtb> <outdir>
Writes <outdir>/dtb-N.dtb and prints the number of DTBs found."""
import os
import struct
import sys

FDT_MAGIC = 0xD00DFEED

def main():
    blob = open(sys.argv[1], "rb").read()
    outdir = sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    off = n = 0
    while off < len(blob):
        if off + 8 > len(blob):
            break
        magic, total = struct.unpack_from(">II", blob, off)
        if magic != FDT_MAGIC or total < 40 or off + total > len(blob):
            sys.exit("aurora-split-dtbs: bad FDT header at offset %d" % off)
        with open(os.path.join(outdir, "dtb-%d.dtb" % n), "wb") as f:
            f.write(blob[off:off + total])
        n += 1
        off += total
        # tolerate zero padding between/after trees
        while off < len(blob) and blob[off] == 0:
            off += 1
    if n == 0:
        sys.exit("aurora-split-dtbs: no DTB found")
    print(n)

if __name__ == "__main__":
    main()
