"""Convert an animated transparent GIF to 128x32 SSD1306 frames."""

import argparse
from pathlib import Path

from PIL import Image, ImageSequence


CONTENT_WIDTH = 48
CONTENT_LEFT = (128 - CONTENT_WIDTH) // 2


def encode_frame(frame, crop_box):
    # One common crop keeps the position of the character consistent across frames.
    rgba = frame.convert("RGBA").crop(crop_box)
    rgba = rgba.resize((CONTENT_WIDTH, 32), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (128, 32), (0, 0, 0, 0))
    canvas.alpha_composite(rgba, (CONTENT_LEFT, 0))
    # Light the dark outline and face details; leave the white body unlit.
    alpha = canvas.getchannel("A")
    luminance = canvas.convert("RGB").convert("L")
    mask = Image.new("L", (128, 32), 0)
    mask.putdata([255 if a >= 96 and light < 180 else 0
                  for a, light in zip(alpha.getdata(), luminance.getdata())])
    pixels = mask.load()
    return [
        sum((1 << bit) if pixels[x, page * 8 + bit] else 0 for bit in range(8))
        for page in range(4)
        for x in range(CONTENT_LEFT, CONTENT_LEFT + CONTENT_WIDTH)
    ], mask


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("gif", type=Path)
    parser.add_argument("header", type=Path)
    parser.add_argument("preview", type=Path)
    args = parser.parse_args()

    source = Image.open(args.gif)
    source_frames = [frame.convert("RGBA") for frame in ImageSequence.Iterator(source)]
    boxes = [frame.getchannel("A").getbbox() for frame in source_frames]
    crop_box = (max(0, min(box[0] for box in boxes) - 5),
                max(0, min(box[1] for box in boxes) - 5),
                min(source.width, max(box[2] for box in boxes) + 5),
                min(source.height, max(box[3] for box in boxes) + 5))
    frames = []
    durations = []
    previews = []
    for frame in ImageSequence.Iterator(source):
        data, mask = encode_frame(frame, crop_box)
        frames.append(data)
        durations.append(max(40, frame.info.get("duration", 40)))
        previews.append(mask)

    lines = [
        "#ifndef OLED_ANIMATION_H",
        "#define OLED_ANIMATION_H",
        "#include <stdint.h>",
        f"#define OLED_ANIMATION_FRAME_COUNT {len(frames)}U",
        f"#define OLED_ANIMATION_COL_START {CONTENT_LEFT}U",
        f"#define OLED_ANIMATION_COL_END {CONTENT_LEFT + CONTENT_WIDTH - 1}U",
        f"#define OLED_ANIMATION_FRAME_BYTES {CONTENT_WIDTH * 4}U",
        "/* SSD1306 page order: four pages, only the occupied columns. */",
        f"static const uint8_t oled_animation[{len(frames)}][{CONTENT_WIDTH * 4}] = {{",
    ]
    for data in frames:
        lines.append("    {")
        for offset in range(0, len(data), 16):
            lines.append("        " + ", ".join(f"0x{x:02X}U" for x in data[offset:offset + 16]) + ",")
        lines.append("    },")
    lines += ["};", f"static const uint16_t oled_animation_duration_ms[{len(frames)}] = {{"]
    lines.append("    " + ", ".join(f"{ms}U" for ms in durations))
    lines += ["};", "#endif", ""]
    args.header.write_text("\n".join(lines), encoding="ascii")

    preview_frames = [im.resize((1024, 256), Image.Resampling.NEAREST) for im in previews]
    preview_frames[0].save(args.preview, save_all=True, append_images=preview_frames[1:],
                           duration=durations, loop=source.info.get("loop", 0), disposal=2)
    print(f"Encoded {len(frames)} frames, {len(frames) * CONTENT_WIDTH * 4} bytes")


if __name__ == "__main__":
    main()
