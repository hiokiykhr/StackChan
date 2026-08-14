#!/usr/bin/env python3
from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Tuple

from PIL import Image

LABELS = [
    ('happy', 0, 0),
    ('crying', 1, 0),
    ('angry', 0, 1),
    ('surprised', 1, 1),
]

THRESHOLD = 200
PADDING = 10
TARGET_HEIGHT = 76


@dataclass
class Cell:
    row: int
    col: int
    bbox: Tuple[int, int, int, int]


def detect_cells(img: Image.Image) -> Dict[str, List[Cell]]:
    gray = img.convert('L')
    width, height = gray.size
    quad_w, quad_h = width // 2, height // 2
    cells: Dict[str, List[Cell]] = {}
    for label, qx_i, qy_i in LABELS:
        qx = qx_i * quad_w
        qy = qy_i * quad_h
        items: List[Cell] = []
        for row in range(2):
            for col in range(5):
                x0 = qx + int(col * quad_w / 5)
                x1 = qx + int((col + 1) * quad_w / 5)
                y0 = qy + (60 if row == 0 else 265)
                y1 = qy + (220 if row == 0 else 450)
                crop = gray.crop((x0, y0, x1, y1))
                px = crop.load()
                xs: List[int] = []
                ys: List[int] = []
                for y in range(crop.size[1]):
                    for x in range(crop.size[0]):
                        if px[x, y] < THRESHOLD:
                            xs.append(x)
                            ys.append(y)
                if not xs:
                    raise RuntimeError(f'No dark pixels found for {label} r{row} c{col}')
                bbox = (x0 + min(xs), y0 + min(ys), x0 + max(xs) + 1, y0 + max(ys) + 1)
                items.append(Cell(row=row, col=col, bbox=bbox))
        cells[label] = items
    return cells


def build_uniform_frames(img: Image.Image, cells: Dict[str, List[Cell]]):
    gray = img.convert('L')
    frames = {}
    manifest = {}
    for label, items in cells.items():
        max_w = max(c.bbox[2] - c.bbox[0] for c in items) + PADDING * 2
        max_h = max(c.bbox[3] - c.bbox[1] for c in items) + PADDING * 2
        label_frames = []
        label_manifest = []
        for idx, cell in enumerate(items, start=1):
            x0, y0, x1, y1 = cell.bbox
            cx = (x0 + x1) / 2.0
            cy = (y0 + y1) / 2.0
            crop_x0 = int(round(cx - max_w / 2))
            crop_y0 = int(round(cy - max_h / 2))
            crop_x1 = crop_x0 + max_w
            crop_y1 = crop_y0 + max_h
            region = gray.crop((crop_x0, crop_y0, crop_x1, crop_y1))
            target_w = round(region.size[0] * (TARGET_HEIGHT / region.size[1]))
            resized = region.resize((target_w, TARGET_HEIGHT), Image.Resampling.LANCZOS)
            bits = []
            pix = resized.load()
            for y in range(resized.size[1]):
                row = []
                for x in range(resized.size[0]):
                    row.append(1 if pix[x, y] < THRESHOLD else 0)
                bits.append(row)
            label_frames.append(bits)
            label_manifest.append({
                'index': idx,
                'source_bbox': [x0, y0, x1, y1],
                'uniform_crop': [crop_x0, crop_y0, crop_x1, crop_y1],
                'output_size': [resized.size[0], resized.size[1]],
            })
        frames[label] = label_frames
        manifest[label] = {
            'uniform_size': [max_w, max_h],
            'frames': label_manifest,
        }
    return frames, manifest


def pack_bits(bits: List[List[int]]) -> List[int]:
    out: List[int] = []
    for row in bits:
        byte = 0
        count = 0
        for bit in row:
            byte = (byte << 1) | bit
            count += 1
            if count == 8:
                out.append(byte)
                byte = 0
                count = 0
        if count:
            byte <<= (8 - count)
            out.append(byte)
    return out


def c_array(name: str, data: List[int]) -> str:
    lines = []
    for i in range(0, len(data), 12):
        chunk = ', '.join(f'0x{b:02X}' for b in data[i:i+12])
        lines.append(f'    {chunk}')
    inner = ',\n'.join(lines)
    return f'static const uint8_t {name}[] = {{\n{inner}\n}};\n'


def write_header(frames: Dict[str, List[List[List[int]]]], out_path: Path):
    enums = ['happy', 'crying', 'angry', 'surprised']
    header = []
    header.append('#pragma once\n')
    header.append('#include <cstddef>\n#include <cstdint>\n\n')
    header.append('namespace hermes_buddy::frames {\n\n')
    header.append('struct Frame {\n    uint16_t width;\n    uint16_t height;\n    const uint8_t *data;\n};\n\n')
    header.append('enum class Expression : uint8_t {\n    Happy,\n    Crying,\n    Angry,\n    Surprised,\n};\n\n')
    frame_table_lines = []
    for label in enums:
        label_frames = frames[label]
        names = []
        for i, bits in enumerate(label_frames, start=1):
            packed = pack_bits(bits)
            data_name = f'{label}_frame_{i}'
            header.append(c_array(data_name, packed))
            names.append((data_name, len(bits[0]), len(bits)))
        header.append(f'static const Frame {label}_frames[] = {{\n')
        for data_name, w, h in names:
            header.append(f'    Frame{{{w}, {h}, {data_name}}},\n')
        header.append('};\n\n')
        frame_table_lines.append(f'        case Expression::{label.capitalize()}: return {label}_frames;')
    header.append('inline const Frame *get_frames(Expression expression)\n{\n    switch (expression) {\n')
    header.append('\n'.join(frame_table_lines))
    header.append('\n    }\n    return happy_frames;\n}\n\n')
    header.append('inline constexpr size_t kFrameCount = 10;\n\n')
    header.append('}  // namespace hermes_buddy::frames\n')
    out_path.write_text(''.join(header))


def main():
    src = Path('/Users/p00939/Desktop/image.png')
    out_header = Path('/Users/p00939/dev/stackchan/remote/m5sticks3-secretary-remote/main/hermes_chan_frames.generated.h')
    out_manifest = Path('/Users/p00939/dev/stackchan/remote/m5sticks3-secretary-remote/main/hermes_chan_frames_manifest.json')
    img = Image.open(src)
    cells = detect_cells(img)
    frames, manifest = build_uniform_frames(img, cells)
    write_header(frames, out_header)
    out_manifest.write_text(json.dumps(manifest, ensure_ascii=False, indent=2))
    print(f'wrote {out_header}')
    print(f'wrote {out_manifest}')
    for label, meta in manifest.items():
        size = meta['frames'][0]['output_size']
        print(label, 'output_size=', size, 'frames=', len(meta['frames']))


if __name__ == '__main__':
    main()
