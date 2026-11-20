"""Turn the team's bounding-box dataset (YOLO .txt or Pascal-VOC .xml) into a 4-class detection set.

    python ml/prepare_detect.py --src <folder with images+labels> [--empty-dir <photos of the empty tray>]

Output: ml/data/trashnet_det/{images,labels}/{train,val,test,val_blur}/ + data.yaml + report.json
Class names are mapped to the 4 bins with ml/class_map.json (exact name) and then ml/labeling.py prefix rules.
"""
import argparse
import json
import random
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict
from pathlib import Path

import cv2
import numpy as np

from build_dataset import DSU, Item
from common import ROOT, hamming, iter_images, phash, read_image, short_hash, write_jpeg
from degrade import degrade
from labeling import LABELS, label_for

MAX_SIDE = 640


def load_class_map(path: Path) -> dict[str, str]:
    try:
        return {k.lower(): v for k, v in json.loads(path.read_text("utf-8")).items() if not k.startswith("_")}
    except (OSError, ValueError):
        return {}


def to_bin(name: str, cmap: dict[str, str]):
    n = name.strip().lower()
    if n in cmap:
        return cmap[n] if cmap[n] in LABELS else None
    label, _, _ = label_for(n, "")
    return label


def read_names(src: Path) -> list[str]:
    """Class names from data.yaml / classes.txt / obj.names (index = YOLO class id)."""
    for f in list(src.rglob("data.yaml")) + list(src.rglob("classes.txt")) + list(src.rglob("*.names")):
        if f.suffix == ".yaml":
            txt = f.read_text("utf-8")
            if "names:" in txt:
                body = txt.split("names:", 1)[1].strip()
                if body.startswith("["):
                    return [x.strip(" '\"") for x in body.strip("[]").split(",") if x.strip()]
                out = {}
                for line in body.splitlines():
                    if ":" in line and line.strip()[0].isdigit():
                        k, v = line.split(":", 1)
                        out[int(k.strip())] = v.strip(" '\"")
                if out:
                    return [out[i] for i in sorted(out)]
        else:
            return [ln.strip() for ln in f.read_text("utf-8").splitlines() if ln.strip()]
    return []


def find_label(img: Path):
    for cand in (img.with_suffix(".txt"), img.with_suffix(".xml")):
        if cand.exists():
            return cand
    parts = list(img.parts)
    for i in range(len(parts) - 1, -1, -1):
        if parts[i].lower() in ("images", "image", "imgs"):
            for sub in ("labels", "label", "annotations"):
                base = Path(*parts[:i], sub, *parts[i + 1:])
                for ext in (".txt", ".xml"):
                    if base.with_suffix(ext).exists():
                        return base.with_suffix(ext)
    return None


def parse_boxes(lab: Path, names: list[str], size: tuple[int, int]):
    """Return [(class_name, cx, cy, w, h)] normalised."""
    out = []
    if lab.suffix == ".txt":
        for ln in lab.read_text("utf-8").splitlines():
            p = ln.split()
            if len(p) >= 5:
                k = int(float(p[0]))
                out.append((names[k] if k < len(names) else str(k), *map(float, p[1:5])))
    else:
        root = ET.parse(lab).getroot()
        w, h = size
        for o in root.iter("object"):
            bb = o.find("bndbox")
            if bb is None:
                continue
            x1, y1, x2, y2 = (float(bb.findtext(t)) for t in ("xmin", "ymin", "xmax", "ymax"))
            out.append((o.findtext("name", ""), (x1 + x2) / 2 / w, (y1 + y2) / 2 / h, (x2 - x1) / w, (y2 - y1) / h))
    return out


def low_contrast(img: np.ndarray, box_px, k: float) -> np.ndarray:
    """Pull the object toward the colour of the surrounding tray: simulates white paper on a white tray."""
    h, w = img.shape[:2]
    x1, y1, x2, y2 = box_px
    pad = max(6, int(0.15 * max(x2 - x1, y2 - y1)))
    X1, Y1, X2, Y2 = max(0, x1 - pad), max(0, y1 - pad), min(w, x2 + pad), min(h, y2 + pad)
    outer = np.ones((Y2 - Y1, X2 - X1), bool)
    outer[(y1 - Y1):(y2 - Y1), (x1 - X1):(x2 - X1)] = False   # ring = padded window minus the box
    ring = img[Y1:Y2, X1:X2][outer]
    bg = np.median(ring, axis=0) if len(ring) else np.full(3, 235.0)
    out = img.astype(np.float32)
    out[y1:y2, x1:x2] = bg + k * (out[y1:y2, x1:x2] - bg)
    return np.clip(out, 0, 255).astype(np.uint8)


def _write(out: Path, split: str, stem: str, img: np.ndarray, boxes: list[tuple]) -> None:
    write_jpeg(out / "images" / split / f"{stem}.jpg", img)
    lab = out / "labels" / split / f"{stem}.txt"
    lab.parent.mkdir(parents=True, exist_ok=True)
    lab.write_text("\n".join(f"{c} {cx:.6f} {cy:.6f} {w:.6f} {h:.6f}" for c, cx, cy, w, h in boxes), "utf-8")


def _px(box, shape):
    h, w = shape[:2]
    _, cx, cy, bw, bh = box
    return (max(0, int((cx - bw / 2) * w)), max(0, int((cy - bh / 2) * h)),
            min(w, int((cx + bw / 2) * w)), min(h, int((cy + bh / 2) * h)))


def build(src: Path, out: Path, class_map: dict, empty_dir: Path | None = None, seed=42, dist=6,
          hard_ratio=0.25, deg_copies=1) -> dict:
    rng, nrng = random.Random(seed), np.random.default_rng(seed)
    names = read_names(src)
    stats, skipped_names = Counter(), Counter()
    samples = []  # (path, [(bin_id, cx, cy, w, h)], hash)
    for p in iter_images(src):
        lab = find_label(p)
        img = read_image(p)
        if lab is None or img is None:
            stats["no_label_or_unreadable"] += 1
            continue
        boxes = []
        for name, cx, cy, w, h in parse_boxes(lab, names, (img.shape[1], img.shape[0])):
            b = to_bin(name, class_map)
            if b is None:
                skipped_names[name] += 1
                continue
            if w * h < 1e-4:
                stats["tiny_box"] += 1
                continue
            boxes.append((LABELS.index(b), cx, cy, w, h))
        if not boxes:
            stats["no_usable_box"] += 1
            continue
        samples.append((p, boxes, phash(img)))
    items = [Item(p, LABELS[Counter(b[0] for b in bx).most_common(1)[0][0]], "", p.stem, False, h, 0, 0) for p, bx, h in samples]
    dsu = DSU(len(items))
    for byte in range(8):
        buckets = defaultdict(list)
        for i, it in enumerate(items):
            buckets[(it.hash >> (8 * byte)) & 0xFF].append(i)
        for idxs in buckets.values():
            for a in range(len(idxs)):
                for b in range(a + 1, len(idxs)):
                    if hamming(items[idxs[a]].hash, items[idxs[b]].hash) <= dist:
                        dsu.union(idxs[a], idxs[b])
    groups = defaultdict(list)
    for i in range(len(items)):
        groups[dsu.find(i)].append(i)
    by_label = defaultdict(list)
    for g in groups.values():
        by_label[items[g[0]].label].append(g)
    split_of = {}
    for label, gs in by_label.items():
        rng.shuffle(gs)
        n = len(gs)
        n_test, n_val = (max(1, round(n * 0.1)) if n >= 5 else 0), (max(1, round(n * 0.1)) if n >= 5 else 0)
        for j, g in enumerate(gs):
            for i in g:
                split_of[i] = "test" if j < n_test else "val" if j < n_test + n_val else "train"
    counts = Counter()
    for i, (p, boxes, _) in enumerate(samples):
        img = read_image(p)
        h0, w0 = img.shape[:2]
        s = min(1.0, MAX_SIDE / max(h0, w0))
        if s < 1:
            img = cv2.resize(img, (round(w0 * s), round(h0 * s)), interpolation=cv2.INTER_AREA)
        split, stem = split_of[i], f"{short_hash(str(p))}_{p.stem[:30]}".replace(" ", "_")
        _write(out, split, stem, img, boxes)
        counts[f"{split}/{LABELS[boxes[0][0]]}"] += 1
        if split == "train":
            for k in range(deg_copies):  # blur / low-res / JPEG copies, boxes unchanged
                _write(out, "train", f"{stem}_d{k}", degrade(img, int(nrng.integers(1, 4)), nrng), boxes)
            if rng.random() < hard_ratio:  # white-on-white: pull the object toward the tray colour
                b = boxes[0]
                _write(out, "train", f"{stem}_lc", low_contrast(img, _px(b, img.shape), rng.uniform(0.3, 0.6)), boxes)
                counts["train/low_contrast"] += 1
        if split == "val":
            _write(out, "val_blur", stem, degrade(img, 2, nrng), boxes)
            _write(out, "val_hard", stem, low_contrast(img, _px(boxes[0], img.shape), 0.4), boxes)
    if empty_dir and empty_dir.exists():  # empty-tray photos teach the model to stay quiet
        empties = list(iter_images(empty_dir))
        rng.shuffle(empties)
        for j, p in enumerate(empties):
            img = read_image(p)
            if img is None:
                continue
            s = min(1.0, MAX_SIDE / max(img.shape[:2]))
            img = cv2.resize(img, None, fx=s, fy=s) if s < 1 else img
            split = "val" if j % 10 == 0 else "train"
            _write(out, split, f"empty_{j}", img, [])
            counts[f"{split}/empty"] += 1
    (out / "data.yaml").write_text(
        f"path: {out.resolve().as_posix()}\ntrain: images/train\nval: images/val\ntest: images/test\n"
        f"names:\n" + "".join(f"  {i}: {n}\n" for i, n in enumerate(LABELS)), "utf-8")
    report = {"images_used": len(samples), "groups": len(groups), "counts": dict(counts), "scan_stats": dict(stats),
              "unmapped_class_names": dict(skipped_names), "source_class_names": names}
    (out / "report.json").write_text(json.dumps(report, indent=2), "utf-8")
    return report


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", type=Path, required=True)
    ap.add_argument("--out", type=Path, default=ROOT / "ml" / "data" / "trashnet_det")
    ap.add_argument("--class-map", type=Path, default=ROOT / "ml" / "class_map.json")
    ap.add_argument("--empty-dir", type=Path, default=None, help="photos of the empty tray (negatives)")
    a = ap.parse_args()
    rep = build(a.src, a.out, load_class_map(a.class_map), a.empty_dir)
    print(json.dumps(rep, indent=1, ensure_ascii=False))
    if rep["unmapped_class_names"]:
        print("\nSome class names were not mapped. Add them to ml/class_map.json as {\"name\": \"paper|glass|other|plastic\"}.")
