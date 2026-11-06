"""Run with Pillow and the path to Inter-Medium.ttf from Inter 3.19."""

import struct
import sys
from pathlib import Path

from PIL import ImageFont


def make_font(source: Path, size: int) -> bytes:
    font = ImageFont.truetype(str(source), size)
    # LovyanGFX supplies the ordinary space. Keep other glyphs in Unicode order.
    codes = [*range(33, 127), *range(161, 256), 0x2013, 0x2014, 0x2026]
    metrics = bytearray(struct.pack(">6I", len(codes), 11, size, 0, *font.getmetrics()))
    pixels = bytearray()
    for code in codes:
        char = chr(code)
        mask, (left, top) = font.getmask2(char, mode="L", anchor="ls")
        width, height = mask.size
        advance = round(font.getlength(char))
        metrics.extend(struct.pack(">7i", code, height, width, advance, -top, left, 0))
        pixels.extend(bytes(mask))
    if len(set(pixels)) < 3:
        raise ValueError("Expected antialiased grayscale glyphs")
    return bytes(metrics + pixels)


if __name__ == "__main__":
    for pixels_per_em in (16, 25, 40):
        path = Path(__file__).with_name(f"panel{pixels_per_em}.vlw")
        path.write_bytes(make_font(Path(sys.argv[1]), pixels_per_em))
