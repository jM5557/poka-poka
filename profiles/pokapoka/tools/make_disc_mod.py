#!/usr/bin/env python3
"""Turns an xdelta patch for the Felyne Village G disc image into a disc mod.

Usage:
  make_disc_mod.py <clean.iso> <patch.xdelta> <name> [--out DIR] [--expect-sha256 HEX]

Applies the patch to a temporary copy of the clean image with `xdelta3`
(pacman -S xdelta3), keeps only the 2048-byte sectors the patch changes and
writes them to DIR/<name>.pkdm (default DIR: profiles/pokapoka/game/disc_mods),
together with a fingerprint of the original sectors, so the port refuses the
mod on any other image. The clean image is never modified and the temporary
copy is deleted. Enable the mod with scripts/mods.sh enable <name>.

The format is described in host/kernel/disc_mods.hpp.
"""

import argparse
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile

SECTOR = 2048
CHUNK = 4 * 1024 * 1024
MAGIC = b"PKDMOD1\0"
HEADER_SIZE = 128


def fail(message):
    print("error: " + message, file=sys.stderr)
    sys.exit(1)


def changed_sectors(clean_path, patched_path):
    """Yields (sector, original bytes, new bytes) and returns the clean image's SHA-256."""
    digest = hashlib.sha256()
    found = []
    with open(clean_path, "rb") as clean, open(patched_path, "rb") as patched:
        offset = 0
        while True:
            a = clean.read(CHUNK)
            b = patched.read(CHUNK)
            if not a and not b:
                break
            digest.update(a)
            if a != b:
                if len(a) != len(b):
                    fail("the patched image is a different size; this tool only handles in-place patches")
                for start in range(0, len(a), SECTOR):
                    old = a[start : start + SECTOR]
                    new = b[start : start + SECTOR]
                    if old != new:
                        found.append(((offset + start) // SECTOR, old, new))
            offset += len(a)
    return found, digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("clean_iso")
    parser.add_argument("patch")
    parser.add_argument("name", help="mod name: letters, digits, '-', '_' or '.'")
    here = os.path.dirname(os.path.abspath(__file__))
    parser.add_argument("--out", default=os.path.join(here, "..", "game", "disc_mods"))
    parser.add_argument("--expect-sha256", help="refuse unless the clean image has this SHA-256")
    args = parser.parse_args()

    name = args.name
    if not name or len(name) > 64 or ".." in name or not all(c.isalnum() or c in "-_." for c in name):
        fail("the name may only use letters, digits, '-', '_' and '.'")
    xdelta = shutil.which("xdelta3")
    if xdelta is None:
        fail("xdelta3 not found (Arch: sudo pacman -S xdelta3)")
    for path in (args.clean_iso, args.patch):
        if not os.path.isfile(path):
            fail(path + " not found")

    os.makedirs(args.out, exist_ok=True)
    size = os.path.getsize(args.clean_iso)
    # The temporary image goes next to the output, not to a small /tmp.
    with tempfile.TemporaryDirectory(dir=args.out, prefix=".make-") as work:
        patched = os.path.join(work, "patched.iso")
        print("applying the patch to a temporary copy (about %d MB)..." % (size // (1 << 20)))
        run = subprocess.run([xdelta, "-d", "-f", "-s", args.clean_iso, args.patch, patched], capture_output=True, text=True)
        if run.returncode != 0:
            fail("xdelta3 could not apply the patch (is the image the clean one it was made for?):\n" + run.stderr.strip())
        print("comparing sectors...")
        sectors, clean_sha = changed_sectors(args.clean_iso, patched)

    print("clean image SHA-256: " + clean_sha)
    if args.expect_sha256 and clean_sha.lower() != args.expect_sha256.lower():
        fail("the clean image's SHA-256 is not the expected " + args.expect_sha256)
    if not sectors:
        fail("the patch changes nothing")

    original = hashlib.sha256(b"".join(old for _, old, _ in sectors)).hexdigest()
    header = MAGIC + struct.pack("<IIQ", HEADER_SIZE, len(sectors), size) + original.encode("ascii")
    header += b"\0" * (HEADER_SIZE - len(header))
    target = os.path.join(args.out, name + ".pkdm")
    with open(target + ".part", "wb") as out:
        out.write(header)
        for number, _, new in sectors:
            out.write(struct.pack("<I", number))
            out.write(new)
    os.replace(target + ".part", target)
    first, last = sectors[0][0], sectors[-1][0]
    print(
        "wrote %s: %d sectors (%d KB), sectors %d to %d"
        % (target, len(sectors), os.path.getsize(target) // 1024, first, last)
    )
    print("enable it with: profiles/pokapoka/scripts/mods.sh enable " + name)


if __name__ == "__main__":
    main()
