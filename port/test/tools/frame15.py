#!/usr/bin/env python3
"""Frame tools for renderer checkpoints (port-plan/render-design-bg.md 8.1).

Standard library only (zlib, struct): no Pillow needed.

  frame15.py png FRAME.555 OUT.png
      Raw PC frame (240x160 little-endian BGR555 u16, as gate0_hook.c writes
      with GATE0_SHOTDIR) to an 8-bit PNG through the D3 expansion
      (v << 3) | (v >> 2), the same as port/render/out_rgba.c and mGBA.

  frame15.py compare GBA.png PC.555 [--diff DIFF.png] [--pc-png PC.png]
      Compare in 15-bit space: the top 5 bits of each GBA PNG channel against
      the PC frame. Prints one JSON line: differing pixel count, first and
      last differing line, distinct differences, and whether either frame is a
      single colour. --diff writes an image: matching pixels as a dimmed grey
      copy of the GBA frame, differing pixels in magenta.

  frame15.py batch GBA_DIR PC_DIR [DIFF_DIR]
      compare for every GBA_DIR/tickNNNNNN.png that has PC_DIR/tickNNNNNN.555,
      one JSON line per tick; diff images go to DIFF_DIR when given.

  frame15.py hash FILE
      FNV-1a 32 of the frame's 15-bit values (PNG or .555), as gate0_compare.py.
"""

import json
import struct
import sys
import zlib

W, H = 240, 160


def read_png_rgb(path):
    """List of H rows, each a list of W (r, g, b) 8-bit tuples."""
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos, idat, width, height, ctype = 8, b"", 0, 0, 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or interlace:
                raise ValueError(f"{path}: unsupported PNG (depth {depth}, type {ctype})")
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    if (width, height) != (W, H):
        raise ValueError(f"{path}: {width}x{height}, expected {W}x{H}")
    bpp = 3 if ctype == 2 else 4
    raw = zlib.decompress(idat)
    stride = width * bpp
    prev = bytearray(stride)
    rows, p = [], 0
    for _ in range(height):
        ftype = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if ftype == 1:
                line[i] = (line[i] + a) & 0xFF
            elif ftype == 2:
                line[i] = (line[i] + b) & 0xFF
            elif ftype == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 0xFF
            elif ftype == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        rows.append([(line[x * bpp], line[x * bpp + 1], line[x * bpp + 2]) for x in range(width)])
        prev = line
    return rows


def png_to_555(path):
    """Flat list of W*H 15-bit values r5 | g5 << 5 | b5 << 10 (top 5 bits of each channel)."""
    out = []
    for row in read_png_rgb(path):
        for r, g, b in row:
            out.append((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10))
    return out


def read_555(path):
    with open(path, "rb") as fh:
        data = fh.read()
    if len(data) != W * H * 2:
        raise ValueError(f"{path}: {len(data)} bytes, expected {W * H * 2}")
    return [v & 0x7FFF for v in struct.unpack(f"<{W * H}H", data)]


def ppm_to_555(path):
    """Binary PPM (P6, 240x160, maxval 255), as RenderDumpPpm555 writes it."""
    with open(path, "rb") as fh:
        data = fh.read()
    header = data.split(b"\n", 3)
    if header[0] != b"P6" or header[1].split() != [b"240", b"160"] or header[2] != b"255":
        raise ValueError(f"{path}: not a 240x160 P6 PPM")
    px = header[3]
    return [(px[i] >> 3) | ((px[i + 1] >> 3) << 5) | ((px[i + 2] >> 3) << 10)
            for i in range(0, W * H * 3, 3)]


def load_555(path):
    low = path.lower()
    if low.endswith(".png"):
        return png_to_555(path)
    if low.endswith(".ppm"):
        return ppm_to_555(path)
    return read_555(path)


def expand5(v):
    v &= 31
    return (v << 3) | (v >> 2)


def write_png(path, rgb_rows):
    """8-bit RGB PNG, filter 0 on every row."""
    raw = bytearray()
    for row in rgb_rows:
        raw.append(0)
        for r, g, b in row:
            raw += bytes((r, g, b))

    def chunk(kind, body):
        return (struct.pack(">I", len(body)) + kind + body
                + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))

    with open(path, "wb") as fh:
        fh.write(b"\x89PNG\r\n\x1a\n")
        fh.write(chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)))
        fh.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        fh.write(chunk(b"IEND", b""))


def frame_to_rows(f555):
    return [[(expand5(v), expand5(v >> 5), expand5(v >> 10)) for v in f555[y * W:(y + 1) * W]]
            for y in range(H)]


def fnv1a_555(f555):
    h = 0x811C9DC5
    for v in f555:
        for byte in (v & 0xFF, v >> 8):
            h = ((h ^ byte) * 0x01000193) & 0xFFFFFFFF
    return f"{h:08x}"


def compare(gba, pc):
    diff = [i for i in range(W * H) if gba[i] != pc[i]]
    lines = sorted({i // W for i in diff})
    pairs = {}
    for i in diff:
        key = (gba[i], pc[i])
        pairs[key] = pairs.get(key, 0) + 1
    top = sorted(pairs.items(), key=lambda kv: -kv[1])[:6]
    return {
        "differing_pixels": len(diff),
        "differing_lines": len(lines),
        "first_line": lines[0] if lines else None,
        "last_line": lines[-1] if lines else None,
        "top_pairs_gba_pc": [[f"{g:04x}", f"{p:04x}", n] for (g, p), n in top],
        "gba_hash": fnv1a_555(gba),
        "pc_hash": fnv1a_555(pc),
        "gba_uniform": len(set(gba)) == 1,
        "pc_uniform": len(set(pc)) == 1,
    }, diff


def write_diff(path, gba, diffset):
    rows = []
    for y in range(H):
        row = []
        for x in range(W):
            i = y * W + x
            if i in diffset:
                row.append((255, 0, 255))
            else:
                v = gba[i]
                grey = (expand5(v) * 3 + expand5(v >> 5) * 6 + expand5(v >> 10)) // 10
                row.append((grey // 2, grey // 2, grey // 2))
        rows.append(row)
    write_png(path, rows)


def main(argv):
    if len(argv) >= 3 and argv[0] == "png":
        write_png(argv[2], frame_to_rows(load_555(argv[1])))
        return 0
    if len(argv) >= 2 and argv[0] == "hash":
        print(fnv1a_555(load_555(argv[1])))
        return 0
    if len(argv) >= 3 and argv[0] == "batch":
        # batch GBA_DIR PC_DIR [DIFF_DIR]: every tickNNNNNN.png with a matching .555
        import glob
        import os
        diff_dir = argv[3] if len(argv) > 3 else None
        for g in sorted(glob.glob(os.path.join(argv[1], "tick*.png"))):
            name = os.path.basename(g)[:-4]
            p = os.path.join(argv[2], name + ".555")
            if not os.path.exists(p):
                continue
            gba = load_555(g)
            result, diff = compare(gba, load_555(p))
            result["tick"] = int(name[4:])
            if diff_dir and diff:
                os.makedirs(diff_dir, exist_ok=True)
                result["diff_png"] = os.path.join(diff_dir, name + "_diff.png")
                write_diff(result["diff_png"], gba, set(diff))
            print(json.dumps(result), flush=True)
        return 0
    if len(argv) >= 3 and argv[0] == "compare":
        gba, pc = load_555(argv[1]), load_555(argv[2])
        opts = dict(zip(argv[3::2], argv[4::2]))
        result, diff = compare(gba, pc)
        if "--pc-png" in opts:
            write_png(opts["--pc-png"], frame_to_rows(pc))
        if "--diff" in opts and diff:
            write_diff(opts["--diff"], gba, set(diff))
            result["diff_png"] = opts["--diff"]
        print(json.dumps(result))
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
