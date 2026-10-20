"""Pack NABO DUEL sprites into duel_sprites.bin for the assets partition.

Input: game-art/duel/sprites/large/<anim>/NN.png (256x256 RGBA, waist anchor at (128, 224)),
cut from the accepted art atlases in game-art/duel/art/.

The file uses the NABO TABLE pack format (see generate_table_sprites.py, read by
nabo_table_sprites.c), with two differences in how it is filled:
  * frames are scaled to SCALE around the anchor (NABO about 128 px tall on screen), and
  * every animation is keyframe + deltas, including the big one. The duel shows a single
    NABO, so one index buffer of the largest animation (nts_buffer_need) is affordable, and
    deltas are what make the pack fit the assets partition.

It also writes game-art/duel/prototype/assets/nabo_duel_sprites.js: the decoded 64-colour
frames as WebP strips, so the browser prototype shows what the badge will show.

Usage:
    python3 tools/generate_duel_sprites.py [--check]
"""

from __future__ import annotations

import argparse
import base64
import io
import json
import logging
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_table_sprites as nts  # noqa: E402  (shared encoder, quantizer and decoders)

logger = logging.getLogger(__name__)

REPO = Path(__file__).resolve().parent.parent
SPRITES = REPO / "game-art" / "duel" / "sprites"
BOARD = REPO / "xiaozhi-esp32" / "main" / "boards" / "folotoy" / "ai-passport-nabo"
OUT_BIN = BOARD / "duel_assets" / "duel_sprites.bin"
OUT_H = BOARD / "nabo_duel_sprite_ids.h"
OUT_JS = REPO / "game-art" / "duel" / "prototype" / "assets" / "nabo_duel_sprites.js"
ANCHOR = (128, 224)
SCALE = 0.85
EDGE_BG = (110, 52, 48)        # dusk tone behind NABO: edges are pre-blended over it
BUDGET = 500 * 1024            # assets partition: 3 MB, v1.6.0 uses 2,537,295 bytes -> ~594 KB free


@dataclass(frozen=True)
class DuelAnim:
    name: str
    step: int        # keep every Nth accepted frame
    fps: int         # playback rate after the step
    loop: bool = False
    max_w: int = 0   # crop the union box to this width around the anchor (0 = keep)


SPECS: List[DuelAnim] = [
    DuelAnim("stance", 1, 12, True),          # 24 frames, 2 s loop: the stand-off
    DuelAnim("enter", 2, 8),                  # 6 frames, 0.75 s: ready
    DuelAnim("draw", 1, 8),                   # 12 frames, 1.5 s: opponent fires (v2 candidate)
    DuelAnim("smug", 1, 12),                  # 18 frames, 1.5 s: you lost
    DuelAnim("splashed", 1, 12, max_w=140),   # 18 frames, 1.5 s: you won (far spray cropped)
    DuelAnim("laugh", 2, 6),                  # 8 frames, 1.33 s: you jumped the gun
    DuelAnim("sleepy", 2, 4),                 # 7 frames, 1.75 s: nobody fired
]


def load(spec: DuelAnim) -> List[np.ndarray]:
    """Accepted RGBA frames, scaled around the anchor."""
    paths = sorted((SPRITES / "large" / spec.name).glob("[0-9][0-9].png"))
    if not paths:
        raise FileNotFoundError(f"no frames for {spec.name}")
    out = []
    n = round(256 * SCALE)
    for p in paths[::spec.step]:
        im = Image.open(p).convert("RGBA")
        # Scale premultiplied, so transparent pixels do not bleed their RGB into the edges.
        a = np.asarray(im).astype(np.float32)
        a[..., :3] *= a[..., 3:4] / 255.0
        pm = Image.fromarray(a.round().clip(0, 255).astype(np.uint8), "RGBA").resize((n, n), Image.LANCZOS)
        s = np.asarray(pm).astype(np.float32)
        alpha = s[..., 3:4]
        s[..., :3] = np.where(alpha > 0, s[..., :3] * 255.0 / np.maximum(alpha, 1), 0)
        canvas = np.zeros((256, 256, 4), np.uint8)
        x0, y0 = ANCHOR[0] - round(ANCHOR[0] * SCALE), ANCHOR[1] - round(ANCHOR[1] * SCALE)
        canvas[y0:y0 + n, x0:x0 + n] = s.round().clip(0, 255).astype(np.uint8)
        if spec.max_w:
            half = spec.max_w // 2
            canvas[:, :ANCHOR[0] - half] = 0
            canvas[:, ANCHOR[0] + spec.max_w - half:] = 0
        out.append(canvas)
    return out


def build():
    nts.EDGE_BG = EDGE_BG
    anims = []
    for spec in SPECS:
        rgbs, masks, (x0, y0, x1, y1) = nts.flatten(load(spec))
        pal, idx = nts.quantize(rgbs, masks)
        enc = []
        for k, f in enumerate(idx):
            rows = [nts.encode_row(r) for r in f] if k == 0 else [nts.encode_delta_row(p, r) for p, r in zip(idx[k - 1], f)]
            offs, acc = [], 0
            for r in rows:
                offs.append(acc)
                acc += len(r)
            offs.append(acc)
            if acc > 0xFFFF:
                raise ValueError(f"{spec.name}: frame {k} row data exceeds 64 KB")
            enc.append((struct.pack(f"<{len(offs)}H", *offs), b"".join(rows)))
        anims.append(dict(spec=spec, w=x1 - x0, h=y1 - y0, ox=x0 - ANCHOR[0], oy=y0 - ANCHOR[1], pal=pal, enc=enc,
                          idx=idx, rgb=rgbs))
    n_frames = sum(len(a["enc"]) for a in anims)
    anims_off = 16
    frames_off = anims_off + 32 * len(anims)
    cursor = frames_off + 8 * n_frames
    body, frame_table, anim_table, first = bytearray(), bytearray(), bytearray(), 0
    for a in anims:
        pal_off = cursor + len(body)
        body += a["pal"].tobytes()
        for rows, rle in a["enc"]:
            rows_off = cursor + len(body)
            body += rows
            rle_off = cursor + len(body)
            body += rle
            frame_table += struct.pack("<II", rows_off, rle_off)
        s = a["spec"]
        anim_table += struct.pack("<12sBBBBHHhhHHI", s.name.encode(), 0, len(a["enc"]), s.fps, 1 if s.loop else 0,
                                  a["w"], a["h"], a["ox"], a["oy"], first, len(a["pal"]), pal_off)
        a["first"] = first
        first += len(a["enc"])
    header = struct.pack("<4sHHII", b"NTS1", len(anims), n_frames, anims_off, frames_off)
    return bytes(header + anim_table + frame_table + body), anims


def check(blob: bytes, anims: List[Dict]) -> List[List[np.ndarray]]:
    """Decode every frame back (keyframe, then deltas) and compare indices and colours."""
    _, count, n_frames, anims_off, frames_off = struct.unpack_from("<4sHHII", blob)
    decoded = []
    for k, a in enumerate(anims):
        f0 = struct.unpack_from("<12sBBBBHHhhHHI", blob, anims_off + 32 * k)[9]
        buf = np.zeros((a["h"], a["w"]), np.uint8)
        out = []
        for i, want in enumerate(a["idx"]):
            rows_off, rle_off = struct.unpack_from("<II", blob, frames_off + 8 * (f0 + i))
            for y in range(a["h"]):
                s, e = struct.unpack_from("<HH", blob, rows_off + 2 * y)
                data = blob[rle_off + s:rle_off + e]
                if i == 0:
                    buf[y] = nts.decode_row(data, a["w"])
                else:
                    nts.apply_delta_row(data, buf[y])
            if not np.array_equal(buf, want):
                raise ValueError(f"{a['spec'].name} frame {i} mismatch")
            m = buf > 0
            err = np.abs(a["pal"][buf[m] - 1].astype(np.int32) - a["rgb"][i][m].astype(np.int32)).mean()
            if err > nts.MAX_COLOR_ERR:
                raise ValueError(f"{a['spec'].name} frame {i}: colour error {err:.1f} > {nts.MAX_COLOR_ERR}")
            out.append(buf.copy())
        decoded.append(out)
    logger.info("round trip ok: %d animations, %d frames", count, n_frames)
    return decoded


def rgb565(c: np.ndarray) -> np.ndarray:
    """What the panel shows for an RGB888 colour."""
    r, g, b = c[..., 0] >> 3, c[..., 1] >> 2, c[..., 2] >> 3
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], -1).astype(np.uint8)


def write_proto(anims: List[Dict], decoded: List[List[np.ndarray]]) -> None:
    """Decoded frames as WebP strips for the prototype, placed like the firmware places them."""
    data = {}
    for a, frames in zip(anims, decoded):
        w, h, n = a["w"], a["h"], len(frames)
        strip = np.zeros((h, w * n, 4), np.uint8)
        pal = rgb565(a["pal"])
        for i, f in enumerate(frames):
            m = f > 0
            cell = strip[:, i * w:(i + 1) * w]
            cell[m, :3] = pal[f[m] - 1]
            cell[m, 3] = 255
        b = io.BytesIO()
        Image.fromarray(strip, "RGBA").save(b, "WEBP", lossless=True)
        s = a["spec"]
        data[s.name] = dict(n=n, w=w, h=h, ox=a["ox"], oy=a["oy"], fps=s.fps, loop=s.loop,
                            img="data:image/webp;base64," + base64.b64encode(b.getvalue()).decode())
    OUT_JS.parent.mkdir(parents=True, exist_ok=True)
    OUT_JS.write_text("// 由 tools/generate_duel_sprites.py 生成，不要手改：工牌上实际显示的 64 色帧\n"
                      "window.NABO_DUEL_SPRITES = " + json.dumps(data, separators=(",", ":")) + ";\n")


def write_header(anims: List[Dict]) -> None:
    lines = ["#pragma once", "/* Generated by tools/generate_duel_sprites.py. Do not edit. */", "",
             "#define NABO_DUEL_SPRITES_FILE \"duel_sprites.bin\"", "", "typedef enum {"]
    for i, a in enumerate(anims):
        s = a["spec"]
        lines.append(f"    NDS_{s.name.upper()} = {i},   /* {len(a['enc'])} frames @ {s.fps} fps, {a['w']}x{a['h']} */")
    lines += [f"    NDS_COUNT = {len(anims)}", "} nabo_duel_sprite_t;", ""]
    OUT_H.write_text("\n".join(lines))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="only build and verify, do not write files")
    args = ap.parse_args()
    blob, anims = build()
    decoded = check(blob, anims)
    buf = 0
    for a in anims:
        s = a["spec"]
        size = sum(len(r) + len(e) for r, e in a["enc"]) + len(a["pal"]) * 3
        buf = max(buf, a["w"] * a["h"])
        logger.info("%-9s %3d frames @ %2d fps %3dx%-3d %3d colors %7.1f KB", s.name, len(a["enc"]), s.fps, a["w"],
                    a["h"], len(a["pal"]), size / 1024)
    logger.info("total %.1f KB (budget %d KB), index buffer %d bytes", len(blob) / 1024, BUDGET // 1024, buf)
    if len(blob) > BUDGET:
        raise SystemExit(f"duel_sprites.bin is {len(blob)} bytes, over the {BUDGET} byte budget")
    if args.check:
        return
    OUT_BIN.parent.mkdir(parents=True, exist_ok=True)
    OUT_BIN.write_bytes(blob)
    write_header(anims)
    write_proto(anims, decoded)
    logger.info("wrote %s, %s and %s", OUT_BIN.relative_to(REPO), OUT_H.relative_to(REPO), OUT_JS.relative_to(REPO))


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    main()
