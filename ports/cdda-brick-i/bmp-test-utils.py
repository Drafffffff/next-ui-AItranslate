"""Read SDL BMP pixels for validation without an external ImageMagick dependency."""
import re
import struct
from pathlib import Path

def crop_rgb_file(path, area):
    data = Path(path).read_bytes()
    assert data[:2] == b'BM'
    offset = struct.unpack_from('<I', data, 10)[0]
    dib, width, height, planes, bits, compression = struct.unpack_from('<IiiHHI', data, 14)
    assert planes == 1 and bits in (24, 32) and width > 0 and height != 0
    stride = ((width * bits + 31) // 32) * 4
    assert len(data) >= offset + stride * abs(height)
    if bits == 32 and compression == 3:
        assert dib >= 40
        masks = struct.unpack_from('<III', data, 54)
        channels = tuple(next(i for i in range(4) if mask == 255 << (i * 8)) for mask in masks)
    else:
        assert compression == 0
        channels = (2, 1, 0)
    match = re.fullmatch(r'(\d+)x(\d+)\+(\d+)\+(\d+)', area)
    assert match is not None
    w, h, x, y = map(int, match.groups())
    assert x + w <= width and y + h <= abs(height)
    result = bytearray(w * h * 3)
    pixel_size = bits // 8
    for row in range(h):
        physical = abs(height) - 1 - (y + row) if height > 0 else y + row
        start = offset + physical * stride + x * pixel_size
        pixels = data[start:start + w * pixel_size]
        dest = row * w * 3
        for channel, byte in enumerate(channels):
            result[dest + channel:dest + w * 3:3] = pixels[byte::pixel_size]
    return bytes(result)
