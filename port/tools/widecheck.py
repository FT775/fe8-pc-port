#!/usr/bin/env python3
# Widescreen invariance check, the frame half (port/tools/widecheck.sh runs it; python3 -I).
#
#   widecheck.py OFF_DUMPS WIDE_DUMPS [--sheet OUT.png --title TEXT] [--expect WHAT[,WHAT...]]
#
# OFF_DUMPS: the 240x160 frames of the run with the row on (FE8PC_DUMP_FRAMES; widecheck.sh has already
# compared them with the row-off run's). WIDE_DUMPS: the same frames' 284x160 pictures (FE8PC_DUMP_WIDE)
# and wide.log ("frame off map reason" per frame; v1.28.0: then "objs mus fogs hps flags"). Checks every
# wide picture: its columns [off, off+240) equal the 240 frame; with map 0 the sides are black; v1.28.0:
# the strip moves at most 3 pixels a frame between frames whose flags say the strip slides, and a
# suspended map's frames (battle animation) have the strip in the middle once their slide is over. Prints:
#   WIDE: N frames, M with the map at the sides, P black sides, off min..max, E errors
#   COVER: frames with side sprites, moving units, moving enemies the fog hid, HP bars, range squares,
#          local BG2 effects left out, the phase banner's squares, the strip sliding, a suspended map
# --expect: each named item (sprites mus fogs hps range local squares slide held) must be in at least one
# frame, else an error (a route meant to cover it no longer does); allmap: every frame shows the map at the sides. With --sheet writes a PNG contact sheet
# of up to 24 frames with the map at the sides (PIL when present, else a PNG written by hand with zlib).
import os
import struct
import sys
import zlib

W, H, CW = 284, 160, 240
M = (W - CW) // 2


def read_ppm(path):
    with open(path, 'rb') as f:
        data = f.read()
    # "P6\n<w> <h>\n255\n"
    parts = data.split(b'\n', 3)
    w, h = (int(v) for v in parts[1].split())
    return w, h, parts[3]


def write_png(path, w, h, rgb):
    raw = b''.join(b'\x00' + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(tag, body):
        c = struct.pack('>I', len(body)) + tag + body
        return c + struct.pack('>I', zlib.crc32(tag + body) & 0xFFFFFFFF)

    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)


def sheet(path, frames, wide_dir, title):
    cols = 3
    scale = 2
    pad = 4
    rows = (len(frames) + cols - 1) // cols
    sw, sh = W * scale, H * scale
    tw, th = cols * (sw + pad) + pad, rows * (sh + pad) + pad
    canvas = bytearray(b'\x40\x40\x40' * tw * th)
    for k, fr in enumerate(frames):
        w, h, pix = read_ppm(os.path.join(wide_dir, 'frame%06u.ppm' % fr))
        x0 = pad + (k % cols) * (sw + pad)
        y0 = pad + (k // cols) * (sh + pad)
        for y in range(h):
            row = pix[y * w * 3:(y + 1) * w * 3]
            srow = bytearray(len(row) * scale)
            for s in range(scale):
                for c in range(3):
                    srow[s * 3 + c::3 * scale] = row[c::3]
            for dy in range(scale):
                o = ((y0 + y * scale + dy) * tw + x0) * 3
                canvas[o:o + len(srow)] = srow
    try:  # labels when PIL is there (only frombytes, text and save: this system's PIL cannot resize)
        from PIL import Image, ImageDraw
        img = Image.frombytes('RGB', (tw, th), bytes(canvas))
        d = ImageDraw.Draw(img)
        for k, fr in enumerate(frames):
            d.text((pad + (k % cols) * (sw + pad) + 3, pad + (k // cols) * (sh + pad) + 2), 'frame %u' % fr,
                   fill=(255, 255, 0))
        img.save(path)
    except Exception:
        write_png(path, tw, th, bytes(canvas))
    print('sheet %s: %s (%d frames)' % (title, path, len(frames)))


def main():
    args = sys.argv[1:]
    sheet_out = None
    title = ''
    if '--sheet' in args:
        i = args.index('--sheet')
        sheet_out = args[i + 1]
        del args[i:i + 2]
    if '--title' in args:
        i = args.index('--title')
        title = args[i + 1]
        del args[i:i + 2]
    expect = []
    if '--expect' in args:
        i = args.index('--expect')
        expect = [e for e in args[i + 1].split(',') if e]
        del args[i:i + 2]
    off_dir, wide_dir = args
    log = {}
    extra = {}
    with open(os.path.join(wide_dir, 'wide.log')) as f:
        for line in f:
            p = line.split()
            if len(p) >= 4:
                log[int(p[0])] = (int(p[1]), int(p[2]), int(p[3]))
            if len(p) >= 9:  # v1.28.0: objs mus fogs hps flags
                extra[int(p[0])] = tuple(int(v) for v in p[4:9])
    names = sorted(n for n in os.listdir(off_dir) if n.endswith('.ppm'))
    errors = 0
    nmap = nblack = 0
    offs = []
    reasons = {}
    map_frames = []
    cover = dict((k, 0) for k in ('sprites', 'mus', 'fogs', 'hps', 'range', 'local', 'squares', 'slide', 'held'))
    prev = None  # (frame, off, held, mapish) of the frame before
    for n in names:
        fr = int(n[5:11])
        wp = os.path.join(wide_dir, n)
        if fr not in log or not os.path.exists(wp):
            errors += 1
            if errors <= 5:
                print('  frame %u: no wide picture or log line' % fr)
            continue
        off, mp, reason = log[fr]
        objs, mus, fogs, hps, flags = extra.get(fr, (0, 0, 0, 0, 0))
        for k, v in (('sprites', objs), ('mus', mus), ('fogs', fogs), ('hps', hps), ('range', flags & 2),
                     ('local', flags & 4), ('squares', flags & 8), ('slide', flags & 1), ('held', flags & 16)):
            cover[k] += 1 if v else 0
        # v1.28.0: the strip's x between the map and a suspended map (the battle animation) only slides, and a
        # suspended map has the strip in the middle once the slide is over
        held = bool(flags & 16) and (reason == 2 or mp)
        mapish = (mp or reason == 12) and not held
        slide_bad = False
        if fr in extra:
            if prev is not None and prev[0] == fr - 1 and ((prev[2] and mapish) or (prev[3] and held)) and \
                    abs(off - prev[1]) > 3:
                slide_bad = True
            if held and not (flags & 1) and off != M:
                slide_bad = True
        prev = (fr, off, held, mapish)
        if slide_bad:
            errors += 1
            if errors <= 5:
                print('  frame %u: the strip jumped or is off the middle on a suspended map (off %d)' % (fr, off))
        _, _, c = read_ppm(os.path.join(off_dir, n))
        ww, wh, wpix = read_ppm(wp)
        bad = ww != W or wh != H or not (0 <= off <= 2 * M)
        black = True
        for y in range(H):
            row = wpix[y * W * 3:(y + 1) * W * 3]
            if row[off * 3:(off + CW) * 3] != c[y * CW * 3:(y + 1) * CW * 3]:
                bad = True
                break
            if row[:off * 3].count(0) != off * 3 or row[(off + CW) * 3:].count(0) != (W - off - CW) * 3:
                black = False
        if not mp and not black:
            bad = True
        if bad:
            errors += 1
            if errors <= 5:
                print('  frame %u: wide picture wrong (off %d, map %d)' % (fr, off, mp))
        if mp:
            nmap += 1
            offs.append(off)
            map_frames.append(fr)
        else:
            nblack += 1
            reasons[reason] = reasons.get(reason, 0) + 1
    for k in expect:
        if k == 'allmap':  # every frame shows the map at the sides (no black ones)
            if nblack:
                errors += 1
                print('  expected the map at the sides on every frame: %d black' % nblack)
            continue
        if cover.get(k, 0) == 0:
            errors += 1
            print('  expected frames with %s: none' % k)
    print('WIDE: %d frames, %d with the map at the sides, %d black sides (reasons %s), off %s, %d errors' % (
        len(names), nmap, nblack, ','.join('%d:%d' % kv for kv in sorted(reasons.items())) or '-',
        '%d..%d' % (min(offs), max(offs)) if offs else '-', errors))
    if extra:
        print('COVER: ' + ', '.join('%s %d' % (k, v) for k, v in cover.items()))
    if sheet_out and map_frames:
        step = max(1, len(map_frames) // 24)
        sheet(sheet_out, map_frames[::step][:24], wide_dir, title)
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
