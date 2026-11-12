#!/usr/bin/env python3
"""Generate the main-menu illustrations as LVGL 9 ARGB8888 image descriptors.

Original flat illustrations drawn with Pillow (no external artwork):
cocktail with layers, recipe card, line priming/washing, service tools.

Usage (from the repository root):
    python3 shared/SmartMixerUi/scripts/make_menu_images.py
Writes shared/SmartMixerUi/src/menu_images.c. Requires Pillow.
"""
import math
import os
from PIL import Image, ImageDraw

SS = 4  # supersampling for smooth edges


def canvas(size):
    img = Image.new("RGBA", (size * SS, size * SS), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img), size * SS


def finish(img, size):
    return img.resize((size, size), Image.LANCZOS)


def mask_paste(dst, src, mask_poly):
    mask = Image.new("L", dst.size, 0)
    ImageDraw.Draw(mask).polygon(mask_poly, fill=255)
    dst.alpha_composite(Image.composite(src, Image.new("RGBA", dst.size, (0, 0, 0, 0)), mask))


def cocktail(size=184):
    img, d, n = canvas(size)
    top, bottom = 0.26 * n, 0.90 * n
    tl, tr = 0.22 * n, 0.70 * n
    bl, br = 0.29 * n, 0.63 * n
    inner = [(tl + 0.02 * n, top + 0.02 * n), (tr - 0.02 * n, top + 0.02 * n),
             (br - 0.015 * n, bottom - 0.06 * n), (bl + 0.015 * n, bottom - 0.06 * n)]
    # Straw behind the liquid surface.
    d.line([(0.52 * n, 0.52 * n), (0.80 * n, 0.06 * n)], fill=(255, 92, 122, 255), width=int(0.045 * n))
    for t in (0.25, 0.5, 0.75):
        x = 0.52 * n + (0.28 * n) * t
        y = 0.52 * n - (0.46 * n) * t
        d.line([(x - 0.012 * n, y - 0.02 * n), (x + 0.012 * n, y + 0.02 * n)],
               fill=(255, 255, 255, 200), width=int(0.018 * n))
    # Liquid layers, clipped to the glass interior.
    layers = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ld = ImageDraw.Draw(layers)
    bands = [(0.40, (243, 230, 200, 255)), (0.47, (246, 201, 69, 255)),
             (0.58, (242, 140, 40, 255)), (0.72, (224, 56, 75, 255)), (0.90, (224, 56, 75, 255))]
    prev = 0.33
    for y, color in bands:
        ld.rectangle([0, prev * n, n, y * n], fill=color)
        prev = y
    for cx, cy, r in ((0.40, 0.52, 0.012), (0.55, 0.64, 0.010), (0.46, 0.76, 0.014), (0.36, 0.68, 0.008),
                      (0.58, 0.48, 0.008)):
        ld.ellipse([(cx - r) * n, (cy - r) * n, (cx + r) * n, (cy + r) * n], fill=(255, 255, 255, 150))
    mask_paste(img, layers, inner)
    # Glass outline, thick base and highlight.
    d.line([(tl, top), (bl, bottom), (br, bottom), (tr, top)], fill=(236, 244, 250, 235),
           width=int(0.022 * n), joint="curve")
    d.rounded_rectangle([bl - 0.01 * n, bottom - 0.06 * n, br + 0.01 * n, bottom], radius=int(0.02 * n),
                        fill=(220, 236, 246, 170))
    shine = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(shine).polygon([(0.62 * n, 0.30 * n), (0.655 * n, 0.30 * n), (0.60 * n, 0.82 * n),
                                   (0.575 * n, 0.82 * n)], fill=(255, 255, 255, 90))
    img.alpha_composite(shine)
    # Lime slice on the rim.
    cx, cy, r = 0.24 * n, 0.25 * n, 0.12 * n
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(124, 179, 66, 255))
    d.ellipse([cx - r * 0.82, cy - r * 0.82, cx + r * 0.82, cy + r * 0.82], fill=(220, 237, 156, 255))
    for k in range(6):
        a = math.pi * 2 * k / 6
        d.line([(cx, cy), (cx + math.cos(a) * r * 0.8, cy + math.sin(a) * r * 0.8)],
               fill=(160, 205, 90, 255), width=int(0.012 * n))
    d.ellipse([cx - r * 0.12, cy - r * 0.12, cx + r * 0.12, cy + r * 0.12], fill=(240, 248, 200, 255))
    return finish(img, size)


def recipe(size=88):
    img, d, n = canvas(size)
    d.rounded_rectangle([0.14 * n, 0.08 * n, 0.74 * n, 0.88 * n], radius=int(0.08 * n), fill=(244, 238, 255, 255))
    d.rounded_rectangle([0.14 * n, 0.08 * n, 0.74 * n, 0.26 * n], radius=int(0.08 * n), fill=(190, 160, 230, 255))
    d.rectangle([0.14 * n, 0.18 * n, 0.74 * n, 0.26 * n], fill=(190, 160, 230, 255))
    for i, w in enumerate((0.44, 0.36, 0.40, 0.26)):
        y = (0.36 + i * 0.12) * n
        d.rounded_rectangle([0.22 * n, y, (0.22 + w) * n, y + 0.045 * n], radius=int(0.02 * n),
                            fill=(150, 130, 185, 255))
    cx, cy, r = 0.72 * n, 0.74 * n, 0.19 * n
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(255, 122, 168, 255))
    w = 0.05 * n
    d.rounded_rectangle([cx - r * 0.55, cy - w / 2, cx + r * 0.55, cy + w / 2], radius=int(w / 2), fill=(255, 255, 255, 255))
    d.rounded_rectangle([cx - w / 2, cy - r * 0.55, cx + w / 2, cy + r * 0.55], radius=int(w / 2), fill=(255, 255, 255, 255))
    return finish(img, size)


def arrow_arc(d, n, cx, cy, r, start, end, color, width):
    box = [cx - r, cy - r, cx + r, cy + r]
    d.arc(box, start, end, fill=color, width=width)
    a = math.radians(end)
    tip = (cx + r * math.cos(a), cy + r * math.sin(a))
    tangent = (-math.sin(a), math.cos(a))
    normal = (math.cos(a), math.sin(a))
    s = width * 1.4
    d.polygon([(tip[0] + tangent[0] * s, tip[1] + tangent[1] * s),
               (tip[0] + normal[0] * s, tip[1] + normal[1] * s),
               (tip[0] - normal[0] * s, tip[1] - normal[1] * s)], fill=color)


def care(size=88):
    img, d, n = canvas(size)
    color = (165, 214, 167, 255)
    arrow_arc(d, n, 0.5 * n, 0.5 * n, 0.40 * n, 200, 330, color, int(0.07 * n))
    arrow_arc(d, n, 0.5 * n, 0.5 * n, 0.40 * n, 20, 150, color, int(0.07 * n))
    # Droplet: circle bottom + pointed top.
    cx, cy, r = 0.5 * n, 0.58 * n, 0.17 * n
    d.polygon([(cx, 0.22 * n), (cx - r * 0.95, cy - r * 0.3), (cx + r * 0.95, cy - r * 0.3)], fill=(79, 195, 247, 255))
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(79, 195, 247, 255))
    d.ellipse([cx - r * 0.55, cy - r * 0.35, cx - r * 0.15, cy + r * 0.25], fill=(200, 236, 252, 255))
    return finish(img, size)


def service(size=88):
    img, d, n = canvas(size)
    cx, cy = 0.42 * n, 0.44 * n
    teeth, r_out, r_in = 8, 0.33 * n, 0.25 * n
    pts = []
    for k in range(teeth * 4):
        a = 2 * math.pi * k / (teeth * 4)
        r = r_out if k % 4 in (1, 2) else r_in
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    d.polygon(pts, fill=(176, 196, 222, 255))
    d.ellipse([cx - 0.11 * n, cy - 0.11 * n, cx + 0.11 * n, cy + 0.11 * n], fill=(0, 0, 0, 0))
    # Wrench across the lower right.
    wrench = Image.new("RGBA", img.size, (0, 0, 0, 0))
    wd = ImageDraw.Draw(wrench)
    wc = 0.5 * n
    wd.rounded_rectangle([wc - 0.06 * n, wc - 0.05 * n, wc + 0.06 * n, wc + 0.42 * n], radius=int(0.05 * n),
                         fill=(255, 209, 102, 255))
    wd.ellipse([wc - 0.15 * n, wc - 0.30 * n, wc + 0.15 * n, wc], fill=(255, 209, 102, 255))
    wd.rectangle([wc - 0.055 * n, wc - 0.32 * n, wc + 0.055 * n, wc - 0.17 * n], fill=(0, 0, 0, 0))
    wrench = wrench.rotate(-40, center=(wc, wc), resample=Image.BICUBIC)
    wrench = wrench.transform(wrench.size, Image.AFFINE, (1, 0, -0.10 * n, 0, 1, -0.08 * n), resample=Image.BICUBIC)
    img.alpha_composite(wrench)
    return finish(img, size)


def c_array(name, image):
    w, h = image.size
    data = bytearray()
    for r, g, b, a in image.get_flattened_data() if hasattr(image, "get_flattened_data") else image.getdata():
        data += bytes((b, g, r, a))  # lv_color32_t: blue, green, red, alpha
    lines = []
    for i in range(0, len(data), 24):
        lines.append("  " + ",".join("0x%02x" % x for x in data[i:i + 24]) + ",")
    body = "\n".join(lines)
    return (f"static const uint8_t {name}_map[] = {{\n{body}\n}};\n"
            f"const lv_image_dsc_t {name} = {{\n"
            f"  .header = {{ .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_ARGB8888, .flags = 0,\n"
            f"              .w = {w}, .h = {h}, .stride = {w * 4} }},\n"
            f"  .data_size = sizeof({name}_map),\n"
            f"  .data = {name}_map,\n}};\n")


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "..", "src", "menu_images.c")
    images = [("menu_cocktail", cocktail()), ("menu_recipe", recipe()),
              ("menu_care", care()), ("menu_service", service())]
    with open(out, "w") as f:
        f.write("/* Generated by scripts/make_menu_images.py; do not edit by hand. */\n")
        f.write("#ifdef LV_LVGL_H_INCLUDE_SIMPLE\n#include \"lvgl.h\"\n#else\n#include \"lvgl/lvgl.h\"\n#endif\n\n")
        for name, image in images:
            f.write(c_array(name, image) + "\n")
    preview = os.environ.get("MENU_PREVIEW")
    if preview:
        sheet = Image.new("RGBA", (184 + 3 * 96, 184), (16, 24, 39, 255))
        x = 0
        for _, image in images:
            sheet.alpha_composite(image, (x, 0)); x += image.size[0] + 8
        sheet.save(preview)


if __name__ == "__main__":
    main()
