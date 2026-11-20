"""Simulate ESP32-CAM (OV2640) artefacts on the local dataset.

Creates, next to the dataset: train_deg/{1,2,3}, val_blur_{1,2,3}, test_blur_{1,2,3}.
    python ml/degrade.py --root ml/data/trashnet4
"""
import argparse
import zlib
from pathlib import Path

import cv2
import numpy as np

from common import ROOT, iter_images, read_image, write_jpeg

# level -> parameter ranges (min, max)
LEVELS = {
    1: dict(blur=(0.5, 1.2), motion=(3, 5), scale=(0.65, 0.85), jpeg=(35, 55), noise=(2, 5), gamma=(0.85, 1.2), temp=0.06),
    2: dict(blur=(1.0, 2.0), motion=(5, 9), scale=(0.4, 0.65), jpeg=(20, 40), noise=(4, 9), gamma=(0.75, 1.35), temp=0.12),
    3: dict(blur=(1.8, 3.2), motion=(7, 13), scale=(0.25, 0.45), jpeg=(10, 28), noise=(7, 14), gamma=(0.65, 1.5), temp=0.18),
}


def _motion_kernel(k: int, angle: float) -> np.ndarray:
    ker = np.zeros((k, k), np.float32)
    ker[k // 2, :] = 1.0
    rot = cv2.getRotationMatrix2D((k / 2 - 0.5, k / 2 - 0.5), angle, 1.0)
    ker = cv2.warpAffine(ker, rot, (k, k))
    return ker / max(ker.sum(), 1e-6)


def degrade(img: np.ndarray, level: int, rng: np.random.Generator) -> np.ndarray:
    """Apply photometric, blur, resolution, noise, lens and JPEG artefacts."""
    p = LEVELS[level]
    out = img.astype(np.float32)
    # photometric: exposure, gamma, colour temperature
    out = out * rng.uniform(0.75, 1.25) + rng.uniform(-18, 18)
    out = 255.0 * np.power(np.clip(out, 0, 255) / 255.0, rng.uniform(*p["gamma"]))
    shift = rng.uniform(-p["temp"], p["temp"])
    out[:, :, 0] *= 1 + shift
    out[:, :, 2] *= 1 - shift
    out = np.clip(out, 0, 255).astype(np.uint8)
    # focus / shake blur
    kind = rng.choice(["defocus", "motion", "both"])
    if kind in ("defocus", "both"):
        out = cv2.GaussianBlur(out, (0, 0), float(rng.uniform(*p["blur"])))
    if kind in ("motion", "both"):
        k = int(rng.integers(p["motion"][0], p["motion"][1] + 1)) | 1
        out = cv2.filter2D(out, -1, _motion_kernel(k, float(rng.uniform(0, 180))))
    # low resolution
    h, w = out.shape[:2]
    s = rng.uniform(*p["scale"])
    small = cv2.resize(out, (max(8, int(w * s)), max(8, int(h * s))), interpolation=cv2.INTER_AREA)
    out = cv2.resize(small, (w, h), interpolation=cv2.INTER_LINEAR)
    # sensor noise
    noise = rng.normal(0, rng.uniform(*p["noise"]), out.shape)
    out = np.clip(out.astype(np.float32) + noise, 0, 255).astype(np.uint8)
    # cheap-lens vignetting and flash glare
    if rng.random() < 0.25:
        yy, xx = np.mgrid[0:h, 0:w]
        d = np.sqrt(((xx - w / 2) / (w / 2)) ** 2 + ((yy - h / 2) / (h / 2)) ** 2)
        out = np.clip(out * (1 - 0.35 * np.clip(d - 0.4, 0, 1))[..., None], 0, 255).astype(np.uint8)
    if level >= 2 and rng.random() < 0.12:
        glare = np.zeros((h, w), np.float32)
        cv2.ellipse(glare, (int(rng.uniform(0.2, 0.8) * w), int(rng.uniform(0.2, 0.8) * h)),
                    (int(w * rng.uniform(0.08, 0.2)), int(h * rng.uniform(0.05, 0.15))), 0, 0, 360, 1.0, -1)
        glare = cv2.GaussianBlur(glare, (0, 0), w * 0.05)
        out = np.clip(out.astype(np.float32) + 160 * glare[..., None], 0, 255).astype(np.uint8)
    # JPEG last, like the camera
    q = int(rng.integers(p["jpeg"][0], p["jpeg"][1] + 1))
    ok, buf = cv2.imencode(".jpg", out, [cv2.IMWRITE_JPEG_QUALITY, q])
    return cv2.imdecode(buf, cv2.IMREAD_COLOR) if ok else out


def _rng_for(path: Path, level: int, seed: int) -> np.random.Generator:
    return np.random.default_rng(zlib.crc32(f"{path.name}:{level}:{seed}".encode()))


def run(root: Path, seed: int = 0) -> dict:
    counts = {}
    jobs = [("train", f"train_deg/{{l}}", range(1, 4)),
            ("val", "val_blur_{l}", range(1, 4)),
            ("test", "test_blur_{l}", range(1, 4))]
    for split, pattern, levels in jobs:
        for src in iter_images(root / split):
            img = read_image(src)
            if img is None:
                continue
            label = src.parent.name
            for lvl in levels:
                dst = root / pattern.format(l=lvl) / label / f"{src.stem}_d{lvl}.jpg"
                write_jpeg(dst, degrade(img, lvl, _rng_for(src, lvl, seed)))
                counts[pattern.format(l=lvl)] = counts.get(pattern.format(l=lvl), 0) + 1
    return counts


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=ROOT / "ml" / "data" / "trashnet4")
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()
    print(run(args.root, args.seed))


if __name__ == "__main__":
    main()
