#!/usr/bin/env python3
"""Finds the game's fonts in a Felyne Village G disc image and describes them.

Usage:
  font_probe.py <image.iso> [--mod english.pkdm] [--out DIR]

Works on a clean image, an image already patched with the translation, or a
clean image plus a disc mod (--mod, applied on the fly; the image is only
read). The game keeps its files in PSP_GAME/USRDIR/POKAPOKA.BND, indexed by
POKAPOKA.BHD (offsets and sizes) and POKAPOKA.FAT (names). The fonts are
font/MainFont.bnd and font/TitleFont.bnd: FromSoftware BND3 archives holding
CCM fonts and their textures.

This finds those archives through the index, unpacks them when it knows the
packing, and:

  - writes what it extracts to DIR (default ./font_probe_out), which stays
    on your computer;
  - prints a report with offsets, sizes, the first bytes of each packed
    entry and the CCM headers and glyph-table bytes. It holds no game text
    and no texture pixels, so it is fine to share.
"""

import argparse
import hashlib
import os
import struct
import sys
import zlib

SECTOR = 2048
PKDM_MAGIC = b"PKDMOD1\0"


class Image:
    """The disc image, with the sectors of a disc mod in place of the originals."""

    def __init__(self, path, mod=None):
        self.file = open(path, "rb")
        self.size = os.path.getsize(path)
        self.overlay = {}
        if mod:
            with open(mod, "rb") as m:
                head = m.read(128)
                if head[:8] != PKDM_MAGIC:
                    sys.exit("error: " + mod + " is not a disc mod")
                header_size, count, image_size = struct.unpack_from("<IIQ", head, 8)
                if image_size != self.size:
                    sys.exit("error: the mod was made for an image of another size")
                m.seek(header_size)
                for _ in range(count):
                    (number,) = struct.unpack("<I", m.read(4))
                    self.overlay[number] = m.read(SECTOR)

    def read(self, offset, length):
        self.file.seek(offset)
        data = bytearray(self.file.read(length))
        if self.overlay and data:
            first = offset // SECTOR
            last = (offset + len(data) - 1) // SECTOR
            for number in range(first, last + 1):
                sector = self.overlay.get(number)
                if sector is None:
                    continue
                start = number * SECTOR
                a = max(start, offset)
                b = min(start + SECTOR, offset + len(data))
                data[a - offset : b - offset] = sector[a - start : b - start]
        return bytes(data)


def hexdump(data, base=0, rows=None):
    lines = []
    for i in range(0, len(data), 16):
        if rows is not None and i // 16 >= rows:
            break
        row = data[i : i + 16]
        text = "".join(chr(c) if 32 <= c < 127 else "." for c in row)
        lines.append("  %06x  %-47s  %s" % (base + i, " ".join("%02x" % c for c in row), text))
    return "\n".join(lines)


def describe_ccm(name, data, out):
    out.append("  %s: %d bytes" % (name, len(data)))
    out.append(hexdump(data[:0x40]))
    if len(data) >= 0x20:
        h = struct.unpack_from("<HHIHHHHH", data, 0)
        out.append("  header (u16 u16 u32 u16 x5): " + " ".join(str(v) for v in h))
    # Where the Latin letters' entries are: every place 'A'..'Z', 'a', 'i', 'm', 'W',
    # space and apostrophe appear as 16- or 32-bit little-endian codes.
    for ch in "A i m W ' .".split() + [" "]:
        code = ord(ch)
        for width, fmt in ((4, "<I"), (2, "<H")):
            needle = struct.pack(fmt, code)
            hits = []
            at = data.find(needle, 0x10)
            while at != -1 and len(hits) < 3:
                if at % width == 0:
                    hits.append(at)
                at = data.find(needle, at + 1)
            if hits:
                out.append("  code %r as u%d at %s" % (ch, width * 8, ", ".join("0x%x" % h for h in hits)))
                out.append(hexdump(data[hits[0] : hits[0] + 32], hits[0], 2))
                break


def iso_find(image, path):
    """(offset, size) of a file in the ISO 9660 image, or None."""
    pvd = image.read(16 * SECTOR, SECTOR)
    if pvd[1:6] != b"CD001":
        return None
    root = pvd[156:190]
    extent, size = struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0]
    for part in path.upper().split("/"):
        data = image.read(extent * SECTOR, size)
        at, found = 0, None
        while at < len(data):
            length = data[at]
            if length == 0:
                at = (at // SECTOR + 1) * SECTOR
                continue
            name = data[at + 33 : at + 33 + data[at + 32]].decode("ascii", "replace").split(";")[0]
            if name.upper() == part:
                found = (struct.unpack_from("<I", data, at + 2)[0], struct.unpack_from("<I", data, at + 10)[0])
                break
            at += length
        if found is None:
            return None
        extent, size = found
    return extent * SECTOR, size


def unpack(blob, size):
    """Tries the packings this tool knows; (data, how) or (None, "")."""
    for how, wbits in (("zlib", 15), ("deflate", -15), ("gzip", 31)):
        try:
            data = zlib.decompressobj(wbits).decompress(blob)
            if len(data) == size:
                return data, how
        except zlib.error:
            pass
    at = blob.find(b"\x78\xda")
    if at == -1:
        at = blob.find(b"\x78\x9c")
    if 0 <= at < 64:
        try:
            data = zlib.decompressobj().decompress(blob[at:])
            if len(data) == size:
                return data, "zlib after a %d-byte header" % at
        except zlib.error:
            pass
    return None, ""


def bnd3_files(data):
    """(name, bytes) for each file of a BND3 archive held in memory."""
    if data[:4] != b"BND3":
        return []
    count = struct.unpack_from("<I", data, 0x10)[0]
    for stride, name_field in ((0x14, 16), (0x18, 16), (0x1C, 16), (0x10, 12)):
        files = []
        for i in range(count):
            at = 0x20 + i * stride
            if at + stride > len(data):
                break
            size, offset = struct.unpack_from("<II", data, at + 4)
            name_offset = struct.unpack_from("<I", data, at + name_field)[0]
            end = data.find(b"\0", name_offset) if 0x20 <= name_offset < len(data) else -1
            name = data[name_offset:end]
            if end == -1 or not name or any(c < 32 or c > 126 for c in name) or offset + size > len(data):
                break
            files.append((name.decode("ascii"), data[offset : offset + size]))
        if len(files) == count:
            return files
    return []


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("iso")
    parser.add_argument("--mod")
    parser.add_argument("--out", default="font_probe_out")
    args = parser.parse_args()
    image = Image(args.iso, args.mod)
    os.makedirs(args.out, exist_ok=True)
    out = ["font_probe 2: image %d bytes, mod %s" % (image.size, os.path.basename(args.mod) if args.mod else "none")]

    where = {}
    for ext in ("BHD", "FAT", "BND"):
        where[ext] = iso_find(image, "PSP_GAME/USRDIR/POKAPOKA." + ext)
        out.append("POKAPOKA.%s: %s" % (ext, "offset 0x%x, %d bytes" % where[ext] if where[ext] else "not found"))
    if not all(where.values()):
        out.append("the archive was not found; is this the Felyne Village G image?")
        print("\n".join(out))
        return 1
    bhd = image.read(*where["BHD"])
    fat = image.read(*where["FAT"])
    bnd_at, bnd_size = where["BND"]
    out.append("BHD header: " + bhd[:0x44].hex(" "))
    if bhd[:4] != b"LDMU":
        out.append("unexpected BHD magic")
    records = []
    for k in range(struct.unpack_from("<I", bhd, 8)[0]):
        at = 0x44 + 40 * k
        if at + 40 > len(bhd):
            break
        records.append(struct.unpack_from("<IIII16sI", bhd, at))
    by_id = {}
    for r in records:
        by_id.setdefault(r[0], r)
    count = struct.unpack_from("<I", fat, 0)[0]
    names = {}
    for k in range(count):
        name_offset, ident = struct.unpack_from("<II", fat, 0x24 + 8 * k)
        end = fat.find(b"\0", name_offset)
        names[fat[name_offset:end].decode("latin1")] = ident
    out.append("BHD %d records, FAT %d names" % (len(records), len(names)))

    written = 0
    for name in ("MainFont.bnd", "TitleFont.bnd", "00sjis12_menu_f.bnd", "AllCCM_Master.bnd", "AllCCM.bnd"):
        out.append("")
        if name not in names or names[name] + 99 not in by_id:
            out.append("%s: not in the index" % name)
            continue
        ident, offset, size, packed, digest, extra = by_id[names[name] + 99]
        out.append("%s: id %d, offset %d, size %d, packed %d, hash %s, %08x" % (
            name, ident, offset, size, packed, digest.hex(), extra))
        stored = packed or size
        data, how = None, ""
        for unit in (1024, 2048, 1):
            if offset * unit + stored > bnd_size:
                continue
            blob = image.read(bnd_at + offset * unit, stored)
            out.append("  at offset x %d: %s" % (unit, blob[:48].hex(" ")))
            out.append("    md5 of these bytes %s" % hashlib.md5(blob).hexdigest())
            if packed == 0 and blob[:4] == b"BND3":
                data, how = blob, "stored"
            elif packed:
                data, how = unpack(blob, size)
            if data is not None:
                out.append("    unpacked (%s, unit %d), md5 %s" % (how, unit, hashlib.md5(data).hexdigest()))
                with open(os.path.join(args.out, name + ".unpacked"), "wb") as f:
                    f.write(data)
                written += 1
                break
        if data is None:
            with open(os.path.join(args.out, name + ".packed"), "wb") as f:
                f.write(image.read(bnd_at + offset * 1024, stored))
            out.append("  not unpacked: the packing is not one this tool knows yet")
            continue
        files = bnd3_files(data)
        out.append("  " + ("BND3, %d files" % len(files) if files else "not a BND3: " + data[:32].hex(" ")))
        for fname, fdata in files:
            base = os.path.basename(fname.replace("\\", "/"))
            out.append("    %-28s %d bytes, starts %s" % (base, len(fdata), fdata[:8].hex(" ")))
            with open(os.path.join(args.out, base), "wb") as f:
                f.write(fdata)
            written += 1
            if base.lower().endswith(".ccm"):
                describe_ccm(base, fdata, out)

    report = "\n".join(out) + "\n"
    with open(os.path.join(args.out, "report.txt"), "w") as f:
        f.write(report)
    print(report)
    print("wrote %d files and report.txt to %s" % (written, args.out), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
