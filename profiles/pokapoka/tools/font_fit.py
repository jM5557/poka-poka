#!/usr/bin/env python3
"""Gives the game's text font natural English spacing, as a disc mod.

Usage:
  font_fit.py <image.iso> [--name NAME] [--out DIR] [--report-only]
              [--keep-fullwidth] [--space N]

The game draws text with FromSoftware CCM fonts. In MainFont.ccm every
Latin character has a pre-space (where its picture starts) and an advance
(how far the pen moves on). An English patch that leaves the space, the
punctuation and the digits at the Japanese full advance of 15 pixels makes
the gaps between words twice as wide as they should be, so English lines
come out a quarter to a third too long and overflow their boxes.

This measures each Latin character's picture in the font texture and sets
its pre-space and advance from it, in the style the letters already have:
  - letters keep their spacing when they already look proportional, and get
    it when they do not;
  - punctuation gets the same spacing as the letters;
  - the space becomes about half a lowercase letter (--space to choose);
  - the digits all get the advance of the widest one, so numbers in columns
    still line up;
  - full-width Latin letters and digits (Ａ, ｂ, １...), which the English
    text also uses, are tightened the same way unless --keep-fullwidth.
Japanese characters are not touched.

Point it at the image you play (the translated one). It writes
DIR/NAME.pkdm (default ./font_fit_out/english-font.pkdm), a disc mod for
that image: put it in the disc_mods folder and switch it on in the menu
(Mods), or with scripts/mods.sh. The image is only read. --report-only
prints what would change and writes nothing else.
"""

import argparse
import hashlib
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from font_probe import SECTOR, Image, iso_find  # noqa: E402

PKDM_MAGIC = b"PKDMOD1\0"
INK = 0x40  # alpha above this counts as part of a character's picture


# ---------------------------------------------------------------- archive


class Archive:
    """POKAPOKA.BHD (offsets, sizes), .FAT (names) and .BND (data)."""

    def __init__(self, image):
        self.image = image
        self.where = {e: iso_find(image, "PSP_GAME/USRDIR/POKAPOKA." + e) for e in ("BHD", "FAT", "BND")}
        missing = [e for e, w in self.where.items() if not w]
        if missing:
            sys.exit("error: POKAPOKA.%s not found; is this the Felyne Village G image?" % missing[0])
        self.bhd = bytearray(image.read(*self.where["BHD"]))
        if self.bhd[:4] != b"LDMU":
            sys.exit("error: unexpected POKAPOKA.BHD")
        fat = image.read(*self.where["FAT"])
        self.records = {}  # id -> index of its 40-byte record in the BHD
        for k in range(struct.unpack_from("<I", self.bhd, 8)[0]):
            at = 0x44 + 40 * k
            if at + 40 > len(self.bhd):
                break
            self.records.setdefault(struct.unpack_from("<I", self.bhd, at)[0], k)
        self.names = {}
        for k in range(struct.unpack_from("<I", fat, 0)[0]):
            name_offset, ident = struct.unpack_from("<II", fat, 0x24 + 8 * k)
            end = fat.find(b"\0", name_offset)
            self.names.setdefault(fat[name_offset:end].decode("latin1"), ident + 99)
        offsets = sorted({struct.unpack_from("<I", self.bhd, 0x44 + 40 * k + 4)[0] for k in self.records.values()})
        self.next_offset = {o: n for o, n in zip(offsets, offsets[1:])}

    def record(self, ident):
        at = 0x44 + 40 * self.records[ident]
        return at, struct.unpack_from("<IIII", self.bhd, at)

    def read(self, ident):
        _, (_, offset, size, packed) = self.record(ident)
        blob = self.image.read(self.where["BND"][0] + offset * 1024, packed or size)
        return zlib.decompress(blob) if packed else blob

    def slot(self, ident):
        """Where the packed file may go: (offset in the image, bytes available)."""
        _, (_, offset, _, _) = self.record(ident)
        end = self.next_offset.get(offset, offset + 2 * 1024)
        return self.where["BND"][0] + offset * 1024, (end - offset) * 1024


def bnd_files(data):
    """{name: (offset, size)} of an old-style FromSoftware BND ("BND\\0")."""
    if data[:4] != b"BND\0":
        return {}
    files = {}
    for k in range(struct.unpack_from("<I", data, 0x10)[0]):
        _, offset, size, name_offset = struct.unpack_from("<IIII", data, 0x20 + 16 * k)
        end = data.find(b"\0", name_offset)
        files[data[name_offset:end].decode("ascii", "replace")] = (offset, size)
    return files


# ---------------------------------------------------------------- textures


def gim_blocks(g):
    found = {}

    def walk(off, end):
        while off + 16 <= end:
            ident, _, size, _, data = struct.unpack_from("<HHIII", g, off)
            if ident in (2, 3):
                walk(off + data, off + size)
            elif ident in (4, 5):
                found.setdefault(ident, off)
            if size == 0:
                break
            off += size

    walk(16, len(g))
    return found


def gim_image(g, off):
    head = off + struct.unpack_from("<I", g, off + 12)[0]
    _, _, fmt, order, width, height = struct.unpack_from("<HHHHHH", g, head)
    return fmt, order, width, height, head + struct.unpack_from("<I", g, head + 0x1C)[0]


def gim_alpha(g):
    """(width, height, alpha bytes row by row) of an indexed GIM texture."""
    blocks = gim_blocks(g)
    pfmt, _, count, _, pdata = gim_image(g, blocks[5])
    alpha = []
    for i in range(count):
        if pfmt == 3:
            alpha.append(g[pdata + 4 * i + 3])
        else:
            v = struct.unpack_from("<H", g, pdata + 2 * i)[0]
            alpha.append({0: 255, 1: 255 if v >> 15 else 0, 2: ((v >> 12) & 15) * 17}[pfmt])
    fmt, order, width, height, data = gim_image(g, blocks[4])
    bits = {4: 4, 5: 8}[fmt]
    row = width * bits // 8
    pixels = g[data : data + row * height]
    if order == 1:  # PSP swizzle: 16-byte by 8-row tiles
        linear = bytearray(len(pixels))
        i = 0
        for by in range(height // 8):
            for bx in range(row // 16):
                for r in range(8):
                    at = (by * 8 + r) * row + bx * 16
                    linear[at : at + 16] = pixels[i : i + 16]
                    i += 16
        pixels = bytes(linear)
    out = bytearray(width * height)
    for y in range(height):
        for x in range(width):
            if bits == 4:
                v = pixels[y * row + x // 2]
                v = (v >> 4) if x & 1 else (v & 15)
            else:
                v = pixels[y * row + x]
            out[y * width + x] = alpha[v]
    return width, height, bytes(out)


# ---------------------------------------------------------------- fonts


class Ccm:
    """A CCM font: Shift-JIS code ranges and 24-byte glyphs (two UV corners,
    pre-space, width, advance, texture)."""

    def __init__(self, data):
        self.data = bytearray(data)
        self.ranges_count, self.glyph_count, self.ranges_at, self.glyphs_at = struct.unpack_from("<HHII", data, 0x10)
        self.ranges = [struct.unpack_from("<III", data, self.ranges_at + 12 * k) for k in range(self.ranges_count)]

    def index(self, code):
        for first, last, start in self.ranges:
            if first <= code <= last:
                return start + code - first
        return None

    def glyph(self, g):
        return struct.unpack_from("<ffffhhhh", self.data, self.glyphs_at + 24 * g)

    def set_metrics(self, g, pre, advance):
        # Pre-space and advance only: the width between them stays (a pad
        # byte in pack_into would write zeros over it, and a glyph of width
        # 0 is not drawn).
        at = self.glyphs_at + 24 * g
        struct.pack_into("<h", self.data, at + 16, pre)
        struct.pack_into("<h", self.data, at + 20, advance)


def texture_order(names):
    """The texture numbers the font's glyphs use, from the file names: the
    number in the name (MainFont_0003.gim is 3), and texture 0 for the one
    without a number (keyset.gim, the button symbols) when the numbers start
    at 1."""
    import re

    numbered, other = {}, []
    for name in names:
        found = re.findall(r"(\d+)", name)
        if found:
            numbered[int(found[-1])] = name
        else:
            other.append(name)
    if not numbered:
        return sorted(names)
    first = min(numbered)
    order = [None] * (max(numbered) + 1)
    for n, name in numbered.items():
        order[n] = name
    if first == 1 and len(other) == 1:
        order[0] = other[0]
    return order


def ink(texture, u0, v0, u1, v1):
    """Columns of the cell that hold the picture: (left, right) or None."""
    width, height, alpha = texture
    x0, y0, x1, y1 = round(u0 * width), round(v0 * height), round(u1 * width), round(v1 * height)
    cols = [x for x in range(x0, x1) if any(alpha[y * width + x] > INK for y in range(y0, min(y1, height)))]
    return (cols[0] - x0, cols[-1] - x0 + 1) if cols else None


LETTERS = [ord(c) for c in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"]
DIGITS = [ord(c) for c in "0123456789"]
PUNCT = [c for c in range(0x21, 0x7F) if c not in LETTERS and c not in DIGITS]
# Shift-JIS full-width Latin: digits 0x824F-58, capitals 0x8260-79, small 0x8281-9A.
FW_DIGITS = list(range(0x824F, 0x8259))
FW_LETTERS = list(range(0x8260, 0x827A)) + list(range(0x8281, 0x829B))


def fit(ccm, textures, keep_fullwidth, space):
    """Sets the new metrics; returns report lines."""
    out = []
    measured = {}
    for code in [0x20] + LETTERS + DIGITS + PUNCT + FW_DIGITS + FW_LETTERS:
        g = ccm.index(code)
        if g is None:
            continue
        u0, v0, u1, v1, pre, width, advance, tex = ccm.glyph(g)
        if not 0 <= tex < len(textures) or textures[tex] is None:
            continue
        measured[code] = (g, pre, width, advance, ink(textures[tex], u0, v0, u1, v1))

    # The letters' spacing: where the picture starts after the pen and the gap
    # after it. Taken from the lowercase letters when they already look
    # proportional, else one pixel either side.
    lower = [measured[c] for c in LETTERS[:26] if c in measured and measured[c][4]]
    def ink_width(ch):
        m = measured.get(ord(ch))
        return m[4][1] - m[4][0] if m and m[4] else None

    sane = (len(lower) >= 20 and 0x20 in measured and measured[0x20][4] is None
            and None not in (ink_width("i"), ink_width("m"), ink_width("."))
            and ink_width("i") < ink_width("m") and ink_width(".") <= 5)
    if not sane:
        sys.exit("error: the characters' pictures are not where this tool expects them in the textures "
                 "(i %s, m %s, . %s px wide); nothing was written" % (ink_width("i"), ink_width("m"), ink_width(".")))
    tuned = [m for m in lower if m[3] < 15]
    if len(tuned) >= 13:
        lead = sorted(m[1] + m[4][0] for m in tuned)[len(tuned) // 2]
        gap = sorted(m[3] - (m[1] + m[4][1]) for m in tuned)[len(tuned) // 2]
    else:
        lead, gap = 1, 1
    lead, gap = max(0, min(lead, 2)), max(1, min(gap, 3))
    out.append("letter spacing: picture starts %d px after the pen, %d px gap after it%s" % (
        lead, gap, " (from the letters)" if len(tuned) >= 13 else " (default)"))

    def proportional(code):
        g, pre, width, advance, box = measured[code]
        return (lead - box[0], lead + (box[1] - box[0]) + gap) if box else (pre, advance)

    changes = {}
    # Letters the translation already spaced are left as they are.
    letters = [c for c in LETTERS if c in measured and measured[c][3] >= 15]
    groups = [letters, PUNCT, DIGITS] + ([] if keep_fullwidth else [FW_LETTERS, FW_DIGITS])
    for group in groups:
        codes = [c for c in group if c in measured and measured[c][4]]
        if group in (DIGITS, FW_DIGITS) and codes:
            widest = max(measured[c][4][1] - measured[c][4][0] for c in codes)
            for c in codes:
                box = measured[c][4]
                left = lead + (widest - (box[1] - box[0])) // 2
                changes[c] = (left - box[0], lead + widest + gap)
        else:
            for c in codes:
                changes[c] = proportional(c)
    # Never wider than before: a picture wider than its old advance keeps it.
    for c, (pre, advance) in list(changes.items()):
        if advance > measured[c][3]:
            changes[c] = (measured[c][1], measured[c][3])
    if 0x20 in measured:
        lower_adv = [changes.get(c, (0, measured[c][3]))[1] for c in LETTERS[:26] if c in measured]
        size = space if space else max(3, round(sorted(lower_adv)[len(lower_adv) // 2] * 0.5))
        changes[0x20] = (0, min(size, measured[0x20][3]))

    rows = []
    for c in sorted(changes):
        g, pre, width, advance, box = measured[c]
        new_pre, new_adv = changes[c]
        if (new_pre, new_adv) != (pre, advance):
            ccm.set_metrics(g, new_pre, new_adv)
            label = chr(c) if c < 0x80 else "SJIS %04X" % c
            rows.append("  %-10s advance %2d -> %2d   pre-space %3d -> %3d" % (label, advance, new_adv, pre, new_pre))
    out.append("%d characters changed:" % len(rows))
    out.extend(rows)
    return out


# ---------------------------------------------------------------- disc mod


def write_mod(image, changes, target):
    """A disc mod with `changes` ({image offset: new bytes}) for this image."""
    sectors = {}
    for offset, data in changes.items():
        first, last = offset // SECTOR, (offset + len(data) - 1) // SECTOR
        for number in range(first, last + 1):
            if number not in sectors:
                sectors[number] = bytearray(image.read(number * SECTOR, SECTOR))
            start = number * SECTOR
            a, b = max(start, offset), min(start + SECTOR, offset + len(data))
            sectors[number][a - start : b - start] = data[a - offset : b - offset]
    changed = [n for n in sorted(sectors) if bytes(sectors[n]) != image.read(n * SECTOR, SECTOR)]
    original = hashlib.sha256(b"".join(image.read(n * SECTOR, SECTOR) for n in changed)).hexdigest()
    header = PKDM_MAGIC + struct.pack("<IIQ", 128, len(changed), image.size) + original.encode("ascii")
    header += b"\0" * (128 - len(header))
    with open(target + ".part", "wb") as out:
        out.write(header)
        for n in changed:
            out.write(struct.pack("<I", n))
            out.write(bytes(sectors[n]))
    os.replace(target + ".part", target)
    return len(changed)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("iso")
    parser.add_argument("--name", default="english-font")
    parser.add_argument("--out", default="font_fit_out")
    parser.add_argument("--report-only", action="store_true")
    parser.add_argument("--keep-fullwidth", action="store_true")
    parser.add_argument("--space", type=int, help="advance of the space in pixels (default: half a letter)")
    args = parser.parse_args()
    if not all(c.isalnum() or c in "-_." for c in args.name) or ".." in args.name:
        sys.exit("error: the name may only use letters, digits, '-', '_' and '.'")

    image = Image(args.iso)
    archive = Archive(image)
    report = ["font_fit: image %d bytes" % image.size]

    # The textures of the main font, in name order, are its texture numbers.
    if "MainFont.bnd" not in archive.names or archive.names["MainFont.bnd"] not in archive.records:
        sys.exit("error: font/MainFont.bnd is not in the archive index")
    main_bnd = archive.read(archive.names["MainFont.bnd"])
    gims = texture_order([n for n in bnd_files(main_bnd) if n.lower().endswith(".gim")])
    report.append("MainFont.bnd textures: %s" % ", ".join("%d %s" % (i, n) for i, n in enumerate(gims)))
    textures = []
    for name in gims:
        if name is None:
            textures.append(None)
            continue
        offset, size = bnd_files(main_bnd)[name]
        try:
            textures.append(gim_alpha(main_bnd[offset : offset + size]))
        except (KeyError, struct.error, IndexError) as error:
            report.append("  %s: cannot read (%s)" % (name, error))
            textures.append(None)

    # Every archive of the font folder that holds MainFont.ccm gets the same change.
    edits = {}
    reference = None
    for name in ("MainFont.bnd", "AllCCM_Master.bnd", "AllCCM.bnd", "00sjis12_menu_f.bnd", "TitleFont.bnd"):
        ident = archive.names.get(name)
        if ident is None or ident not in archive.records:
            continue
        data = bytearray(archive.read(ident))
        files = bnd_files(data)
        if "MainFont.ccm" not in files:
            continue
        offset, size = files["MainFont.ccm"]
        ccm = Ccm(data[offset : offset + size])
        lines = fit(ccm, textures, args.keep_fullwidth, args.space)
        if reference is None:
            reference = lines
            report.append("")
            report.extend(lines)
        data[offset : offset + size] = ccm.data
        edits[ident] = (name, bytes(data))
    if not edits:
        sys.exit("error: no MainFont.ccm found in the font archives")

    changes = {}
    bhd_at = archive.where["BHD"][0]
    for ident, (name, data) in edits.items():
        packed = zlib.compress(data, 9)
        where, room = archive.slot(ident)
        record_at, (rid, roffset, rsize, rpacked) = archive.record(ident)
        if len(packed) > room:
            sys.exit("error: %s no longer fits its place in the archive (%d > %d bytes)" % (name, len(packed), room))
        changes[where] = packed + b"\0" * (min(room, (len(packed) + SECTOR - 1) // SECTOR * SECTOR) - len(packed))
        struct.pack_into("<II", archive.bhd, record_at + 8, len(data), len(packed))
        report.append("%s: packed %d -> %d bytes (room %d)" % (name, rpacked, len(packed), room))
    changes[bhd_at] = bytes(archive.bhd)

    os.makedirs(args.out, exist_ok=True)
    text = "\n".join(report) + "\n"
    with open(os.path.join(args.out, "report.txt"), "w") as f:
        f.write(text)
    print(text)
    if args.report_only:
        return 0
    target = os.path.join(args.out, args.name + ".pkdm")
    count = write_mod(image, changes, target)
    print("wrote %s (%d sectors). Put it in the disc_mods folder and switch it on under Mods." % (target, count))
    return 0


if __name__ == "__main__":
    sys.exit(main())
