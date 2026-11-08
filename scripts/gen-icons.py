#!/usr/bin/env python3
"""Generate the toolbar and menu icons of UefiShellGuiRW.

The icons are drawn here from simple shapes (they are original artwork in the
style of a classic Windows toolbar, not copies of any other program's
bitmaps). Each icon is drawn at 5x and downsampled for anti-aliasing, then
written as an LVGL ARGB8888 image descriptor.

    python3 scripts/gen-icons.py            # writes Icons.c next to the app
    python3 scripts/gen-icons.py --preview icons.png

Requires Pillow. The generated file is committed, so building the
application does not need Python or Pillow.
"""
import argparse
import os
import sys

from PIL import Image, ImageDraw, ImageFont

SIZE = 20          # final icon size in pixels
SS = 5             # supersampling factor
S = SIZE * SS

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "UefiShellGuiRWPkg", "Application",
                   "UefiShellGuiRW", "Shell", "Icons.c")
FONT_CANDIDATES = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "C:/Windows/Fonts/arialbd.ttf",
]


def font(px):
    for f in FONT_CANDIDATES:
        if os.path.exists(f):
            return ImageFont.truetype(f, px * SS)
    return ImageFont.load_default()


def P(*v):
    """Scale coordinates from the 20 px grid to the supersampled canvas."""
    return [x * SS for x in v]


def text(d, cx, cy, s, px, fill):
    f = font(px)
    b = d.textbbox((0, 0), s, font=f)
    w, h = b[2] - b[0], b[3] - b[1]
    d.text((cx * SS - w / 2 - b[0], cy * SS - h / 2 - b[1]), s, font=f, fill=fill)


def chip(d, x0, y0, x1, y1, body=(40, 40, 40), pins=True, label=None, px=6,
         label_fill=(255, 255, 255)):
    if pins:
        n = max(2, int((x1 - x0) // 2))
        for i in range(n):
            px0 = x0 + 1 + i * (x1 - x0 - 2) / max(1, n - 1) - 0.5
            d.rectangle(P(px0, y0 - 1.5, px0 + 1, y1 + 1.5), fill=(170, 170, 170))
    d.rounded_rectangle(P(x0, y0, x1, y1), radius=SS, fill=body)
    if label:
        text(d, (x0 + x1) / 2, (y0 + y1) / 2, label, px, label_fill)


def page(d, fill=(255, 255, 255)):
    d.polygon([tuple(P(4, 1)), tuple(P(13, 1)), tuple(P(17, 5)), tuple(P(17, 19)), tuple(P(4, 19))],
              fill=fill, outline=(90, 90, 90), width=SS)
    d.polygon([tuple(P(13, 1)), tuple(P(13, 5)), tuple(P(17, 5))], fill=(210, 210, 210),
              outline=(90, 90, 90))


# --- icon drawings ---------------------------------------------------------

def ic_pci(d):
    d.rectangle(P(1, 3, 19, 14), fill=(0, 120, 60), outline=(0, 70, 30), width=SS)
    for i in range(8):
        d.rectangle(P(3 + i * 2, 14, 3.9 + i * 2, 17), fill=(230, 180, 40))
    chip(d, 4, 5, 10, 11, pins=False)
    chip(d, 12, 6, 16, 9, body=(60, 60, 60), pins=False)


def ic_mem(d):
    d.rectangle(P(0, 6, 19.8, 14), fill=(0, 120, 60), outline=(0, 70, 30), width=SS)
    for i in range(4):
        d.rectangle(P(1.5 + i * 4.6, 7.5, 4.8 + i * 4.6, 11.5), fill=(30, 30, 30))
    for i in range(9):
        d.rectangle(P(1 + i * 2.1, 14, 1.9 + i * 2.1, 16), fill=(230, 180, 40))


def ic_io(d):
    d.rounded_rectangle(P(1, 2, 19, 18), radius=2 * SS, fill=(30, 90, 200), outline=(10, 40, 120), width=SS)
    text(d, 10, 10, "I/O", 7, (255, 255, 255))


def ic_idx(d):
    d.rectangle(P(1, 2, 12, 8), fill=(30, 90, 200), outline=(10, 40, 120), width=SS)
    d.rectangle(P(8, 12, 19, 18), fill=(230, 120, 20), outline=(140, 60, 0), width=SS)
    d.line(P(6, 8, 6, 15), fill=(40, 40, 40), width=int(1.5 * SS))
    d.line(P(6, 15, 8, 15), fill=(40, 40, 40), width=int(1.5 * SS))
    d.polygon([tuple(P(8, 13)), tuple(P(8, 17)), tuple(P(10, 15))], fill=(40, 40, 40))


def ic_cmos(d):
    d.ellipse(P(2, 2, 18, 18), fill=(200, 200, 210), outline=(100, 100, 110), width=SS)
    d.ellipse(P(4.5, 4.5, 15.5, 15.5), outline=(150, 150, 160), width=SS)
    text(d, 10, 10, "+", 10, (60, 60, 70))


def ic_msr(d):
    for i in range(5):
        o = 3.5 + i * 3
        d.rectangle(P(o, 0.5, o + 1, 19.5), fill=(190, 160, 60))
        d.rectangle(P(0.5, o, 19.5, o + 1), fill=(190, 160, 60))
    d.rectangle(P(2.5, 2.5, 17.5, 17.5), fill=(90, 90, 100), outline=(40, 40, 50), width=SS)
    d.rectangle(P(5.5, 5.5, 14.5, 14.5), fill=(170, 170, 180))
    text(d, 10, 10, "CPU", 4, (30, 30, 40))


def ic_ec(d):
    chip(d, 2, 4, 18, 16, label="EC", px=8)


def ic_sio(d):
    chip(d, 2, 4, 18, 16, body=(60, 40, 110), label="SIO", px=6)


def ic_smbus(d):
    d.line(P(1, 6, 19, 6), fill=(200, 40, 40), width=int(1.6 * SS))
    d.line(P(1, 11, 19, 11), fill=(30, 90, 200), width=int(1.6 * SS))
    for x in (5, 15):
        d.line(P(x, 6, x, 16), fill=(80, 80, 80), width=SS)
        d.line(P(x + 1.5, 11, x + 1.5, 16), fill=(80, 80, 80), width=SS)
        d.rectangle(P(x - 2, 15, x + 3.5, 19), fill=(40, 40, 40))


def ic_ata(d):
    d.rounded_rectangle(P(1, 3, 19, 17), radius=SS, fill=(170, 175, 185), outline=(80, 80, 90), width=SS)
    d.ellipse(P(3, 4.5, 14, 15.5), fill=(215, 220, 230), outline=(120, 120, 130), width=SS)
    d.ellipse(P(7.5, 9, 9.5, 11), fill=(90, 90, 100))
    d.line(P(9, 10, 16, 14), fill=(60, 60, 70), width=int(1.3 * SS))
    d.ellipse(P(16, 14, 18, 16), fill=(40, 200, 60))


def ic_usb(d):
    c = (30, 30, 30)
    w = int(1.6 * SS)
    d.line(P(10, 2, 10, 17), fill=c, width=w)
    d.polygon([tuple(P(10, 0.5)), tuple(P(7.5, 4)), tuple(P(12.5, 4))], fill=c)
    d.line(P(10, 12, 5, 8, 5, 6), fill=c, width=w)
    d.ellipse(P(3.5, 4, 6.5, 7), fill=c)
    d.line(P(10, 14, 15, 10, 15, 8), fill=c, width=w)
    d.rectangle(P(13.5, 5.5, 16.5, 8.5), fill=c)
    d.ellipse(P(7.5, 16, 12.5, 20), fill=c)


def ic_acpi(d):
    page(d)
    text(d, 10.5, 9, "A", 7, (180, 30, 30))
    for y in (13.5, 16):
        d.line(P(6, y, 15, y), fill=(120, 120, 120), width=SS)


def ic_smbios(d):
    page(d)
    text(d, 10.5, 8, "SM", 5, (30, 90, 200))
    for y in (12.5, 15, 17):
        d.line(P(6, y, 15, y), fill=(120, 120, 120), width=SS)


def ic_edid(d):
    d.rounded_rectangle(P(1, 2, 19, 14), radius=SS, fill=(50, 50, 55))
    d.rectangle(P(2.5, 3.5, 17.5, 12.5), fill=(90, 160, 230))
    d.rectangle(P(8.5, 14, 11.5, 17), fill=(70, 70, 75))
    d.rectangle(P(5, 17, 15, 18.5), fill=(70, 70, 75))


def ic_com(d):
    d.polygon([tuple(P(1, 5)), tuple(P(19, 5)), tuple(P(16.5, 15)), tuple(P(3.5, 15))],
              fill=(160, 165, 175), outline=(70, 70, 80))
    for i in range(5):
        d.ellipse(P(3.6 + i * 3, 7, 5.4 + i * 3, 8.8), fill=(30, 30, 30))
    for i in range(4):
        d.ellipse(P(5.1 + i * 3, 11, 6.9 + i * 3, 12.8), fill=(30, 30, 30))


def ic_tree(d):
    c = (60, 60, 60)
    d.line(P(4, 5, 4, 16), fill=c, width=SS)
    d.line(P(4, 10, 9, 10), fill=c, width=SS)
    d.line(P(4, 16, 9, 16), fill=c, width=SS)
    d.rectangle(P(1, 1, 7, 6), fill=(230, 180, 40), outline=(120, 90, 0), width=SS)
    d.rectangle(P(9, 7.5, 18, 12.5), fill=(0, 120, 60), outline=(0, 70, 30), width=SS)
    d.rectangle(P(9, 13.5, 18, 18.5), fill=(0, 120, 60), outline=(0, 70, 30), width=SS)


def ic_refresh(d):
    c = (20, 140, 40)
    d.arc(P(3, 3, 17, 17), start=-60, end=230, fill=c, width=int(2.4 * SS))
    d.polygon([tuple(P(13, 1)), tuple(P(19, 4.5)), tuple(P(13.5, 8.5))], fill=c)


def ic_shot(d):
    d.rounded_rectangle(P(1, 5, 19, 17), radius=2 * SS, fill=(70, 70, 80))
    d.rectangle(P(6, 3, 12, 5), fill=(70, 70, 80))
    d.ellipse(P(6, 7, 14, 15), fill=(200, 210, 230), outline=(30, 30, 40), width=SS)
    d.ellipse(P(8.5, 9.5, 11.5, 12.5), fill=(40, 60, 120))
    d.rectangle(P(15, 7, 17, 8.5), fill=(240, 200, 60))


def _win(d, x0, y0, x1, y1):
    d.rectangle(P(x0, y0, x1, y1), fill=(255, 255, 255), outline=(60, 60, 60), width=SS)
    d.rectangle(P(x0, y0, x1, y0 + 2.5), fill=(10, 36, 106))


def ic_cascade(d):
    _win(d, 1, 1, 12, 10)
    _win(d, 4, 5, 15, 14)
    _win(d, 7, 9, 18, 18)


def ic_tileh(d):
    _win(d, 1, 1, 19, 9)
    _win(d, 1, 11, 19, 19)


def ic_tilev(d):
    _win(d, 1, 1, 9, 19)
    _win(d, 11, 1, 19, 19)


def ic_about(d):
    d.ellipse(P(1, 1, 19, 19), fill=(30, 90, 200), outline=(10, 40, 120), width=SS)
    text(d, 10, 10.5, "i", 12, (255, 255, 255))


def ic_dump(d):
    page(d)
    d.polygon([tuple(P(7, 9)), tuple(P(14, 9)), tuple(P(10.5, 15))], fill=(20, 140, 40))
    d.rectangle(P(9, 5, 12, 9), fill=(20, 140, 40))


def ic_wlog(d):
    page(d)
    for i, y in enumerate((7, 10, 13, 16)):
        d.rectangle(P(6, y, 7.5, y + 1.2), fill=(200, 30, 30))
        d.rectangle(P(9, y, 15 - (i % 2) * 2, y + 1.2), fill=(60, 60, 60))
    d.line(P(12, 2, 19, 9), fill=(30, 60, 160), width=int(1.8 * SS))
    d.polygon([tuple(P(10.5, 0.8)), tuple(P(13.2, 1.5)), tuple(P(11.2, 3.5))], fill=(30, 60, 160))


def ic_files(d):
    d.polygon([tuple(P(1, 4)), tuple(P(7, 4)), tuple(P(9, 6)), tuple(P(19, 6)), tuple(P(19, 17)), tuple(P(1, 17))],
              fill=(230, 180, 60), outline=(140, 100, 20))
    d.polygon([tuple(P(1, 8)), tuple(P(19, 8)), tuple(P(19, 17)), tuple(P(1, 17))], fill=(250, 210, 90),
              outline=(140, 100, 20))
    d.rectangle(P(5, 10, 15, 11.2), fill=(140, 100, 20))


def ic_exit(d):
    d.rectangle(P(3, 1, 13, 19), fill=(150, 90, 40), outline=(80, 40, 10), width=SS)
    d.ellipse(P(10, 9, 12, 11), fill=(240, 200, 60))
    d.polygon([tuple(P(12, 7)), tuple(P(19, 10)), tuple(P(12, 13))], fill=(200, 30, 30))


ICONS = [
    ("Pci", ic_pci), ("Memory", ic_mem), ("Io", ic_io), ("IndexData", ic_idx),
    ("Cmos", ic_cmos), ("Msr", ic_msr), ("Ec", ic_ec), ("SuperIo", ic_sio),
    ("Smbus", ic_smbus), ("Storage", ic_ata), ("Usb", ic_usb), ("Acpi", ic_acpi),
    ("Smbios", ic_smbios), ("Edid", ic_edid), ("ComPort", ic_com), ("PcieTree", ic_tree),
    ("Refresh", ic_refresh), ("Screenshot", ic_shot), ("DumpAll", ic_dump),
    ("WriteLog", ic_wlog), ("Files", ic_files),
    ("Cascade", ic_cascade), ("TileH", ic_tileh), ("TileV", ic_tilev),
    ("About", ic_about), ("Exit", ic_exit),
]


def render(fn):
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    fn(ImageDraw.Draw(img))
    return img.resize((SIZE, SIZE), Image.LANCZOS)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview")
    ap.add_argument("--out", default=OUT)
    a = ap.parse_args()
    imgs = [(n, render(f)) for n, f in ICONS]
    if a.preview:
        scale = 4
        sheet = Image.new("RGB", (len(imgs) * (SIZE + 4) * scale, (SIZE + 4) * scale), (240, 240, 240))
        for i, (_, im) in enumerate(imgs):
            big = im.resize((SIZE * scale, SIZE * scale), Image.NEAREST)
            sheet.paste(big, (i * (SIZE + 4) * scale + 2 * scale, 2 * scale), big)
        sheet.save(a.preview)
    out = []
    out.append("/** @file\n  Toolbar and menu icons of UefiShellGuiRW (20 x 20, ARGB8888).\n\n"
               "  GENERATED by scripts/gen-icons.py - do not edit by hand.\n"
               "  The icons are original drawings made by that script.\n\n"
               "  Copyright (c) 2026, Niall Tseng. All rights reserved.<BR>\n"
               "  SPDX-License-Identifier: BSD-2-Clause-Patent\n**/\n\n"
               "#include \"../UefiShellGuiRW.h\"\n\n")
    for name, im in imgs:
        px = im.load()
        data = []
        for y in range(SIZE):
            for x in range(SIZE):
                r, g, b, al = px[x, y]
                data += [b, g, r, al]   # LVGL ARGB8888 is B, G, R, A in memory
        out.append("STATIC CONST UINT8  mIcon%sData[] = {\n" % name)
        for i in range(0, len(data), 16):
            out.append("  " + ", ".join("0x%02x" % v for v in data[i:i + 16]) + ",\n")
        out.append("};\n\n")
        out.append("CONST lv_image_dsc_t  gIcon%s = {\n" % name)
        out.append("  .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_ARGB8888,\n"
                   "              .w = %d, .h = %d, .stride = %d },\n" % (SIZE, SIZE, SIZE * 4))
        out.append("  .data_size = sizeof (mIcon%sData),\n  .data      = mIcon%sData,\n};\n\n" % (name, name))
    with open(a.out, "w", newline="\n") as f:
        f.write("".join(out))
    hdr = "".join("extern CONST lv_image_dsc_t  gIcon%s;\n" % n for n, _ in imgs)
    sys.stdout.write(hdr)


if __name__ == "__main__":
    main()
