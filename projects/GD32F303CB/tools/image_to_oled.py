"""Convert one image into a native 128x32 SSD1306 page-ordered C bitmap."""

import argparse
from pathlib import Path

from PIL import Image


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("header", type=Path)
    parser.add_argument("preview", type=Path)
    parser.add_argument("--content-width", type=int, default=60)
    parser.add_argument("--threshold", type=int, default=130)
    args = parser.parse_args()

    source = Image.open(args.image).convert("L")
    ink = source.point(lambda value: 255 if value < 230 else 0)
    bbox = ink.getbbox()
    if bbox is None:
        raise ValueError("No dark pixels found in the input image")
    left, top, right, bottom = bbox
    source = source.crop((max(0, left - 3), max(0, top - 3),
                          min(source.width, right + 3),
                          min(source.height, bottom + 3)))
    source = source.resize((args.content_width, 30), Image.Resampling.LANCZOS)
    source = source.point(lambda value: 255 if value < args.threshold else 0)

    canvas = Image.new("L", (128, 32), 0)
    canvas.paste(source, ((128 - args.content_width) // 2, 1))

    pixels = canvas.load()
    data = []
    for page in range(4):
        for x in range(128):
            value = 0
            for bit in range(8):
                if pixels[x, page * 8 + bit]:
                    value |= 1 << bit
            data.append(value)

    lines = [
        "#ifndef OLED_IMAGE_H",
        "#define OLED_IMAGE_H",
        "#include <stdint.h>",
        "/* 128x32, four 8-pixel pages, one byte per column. */",
        "static const uint8_t oled_image[512] = {",
    ]
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join(f"0x{value:02X}U" for value in data[i:i + 16]) + ",")
    lines += ["};", "#endif", ""]
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.header.write_text("\n".join(lines), encoding="ascii")

    preview = Image.new("RGB", (128, 32), (5, 15, 27))
    preview_pixels = preview.load()
    for y in range(32):
        for x in range(128):
            if pixels[x, y]:
                preview_pixels[x, y] = (220, 245, 255)
    preview.resize((1024, 256), Image.Resampling.NEAREST).save(args.preview)
    print(f"Wrote {len(data)} bytes; {sum(v.bit_count() for v in data)} lit pixels")


if __name__ == "__main__":
    main()
