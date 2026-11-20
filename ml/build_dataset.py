"""Rebuild a clean 4-class dataset from local data only.

Steps: scan -> relabel by filename prefix -> clean -> pHash/source-key grouping ->
group-aware stratified split -> class balancing -> letterbox export + report.json.

    python ml/build_dataset.py            # defaults: data/dataset-resized + data/pbl4_split
"""
import argparse
import json
import random
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from common import ROOT, hamming, iter_images, letterbox, phash, read_image, short_hash, write_jpeg
from labeling import LABELS, label_for, source_key

MIN_SIDE = 64
MIN_STD = 3.0


@dataclass
class Item:
    path: Path
    label: str
    subtype: str
    key: str
    variant: bool
    hash: int
    pixels: int
    nbytes: int


class DSU:
    def __init__(self, n: int):
        self.p = list(range(n))

    def find(self, x: int) -> int:
        while self.p[x] != x:
            self.p[x] = self.p[self.p[x]]
            x = self.p[x]
        return x

    def union(self, a: int, b: int) -> None:
        self.p[self.find(a)] = self.find(b)


def _load(path: Path):
    """Return (Item fields, image) or (reason, None)."""
    img = read_image(path)
    if img is None:
        return "unreadable", None
    h, w = img.shape[:2]
    if min(h, w) < MIN_SIDE:
        return "too_small", None
    if float(img.std()) < MIN_STD:
        return "flat", None
    return (phash(img), h * w), img


def scan(sources: list[Path], limit: int = 0, workers: int = 8):
    stats = Counter()
    files = []
    for src in sources:
        for p in iter_images(src):
            label, subtype, how = label_for(p.name, p.parent.name)
            stats[f"how_{how}"] += 1
            if label is None:
                stats["unresolved"] += 1
                continue
            files.append((p, label, subtype))
    if limit:
        files = files[:limit]
    with ThreadPoolExecutor(workers) as pool:
        loaded = list(pool.map(lambda t: _load(t[0]), files))
    items: list[Item] = []
    for (p, label, subtype), (info, _) in zip(files, loaded):
        if isinstance(info, str):
            stats[info] += 1
            continue
        key, variant = source_key(p.name)
        items.append(Item(p, label, subtype, key, variant, info[0], info[1], p.stat().st_size))
    return items, stats


def group_items(items: list[Item], dist: int):
    """Union items that share a source key or are perceptual near-duplicates."""
    dsu = DSU(len(items))
    by_key = defaultdict(list)
    for i, it in enumerate(items):
        by_key[it.key].append(i)
    for idxs in by_key.values():
        for j in idxs[1:]:
            dsu.union(idxs[0], j)
    # pigeonhole: hamming <= 7 over 8 bytes implies one identical byte
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
        groups[dsu.find(i)].append(items[i])
    return list(groups.values())


def dedupe_group(members: list[Item], dist: int) -> list[Item]:
    """Keep members that are not perceptual copies of one already kept (best resolution first)."""
    kept: list[Item] = []
    for it in sorted(members, key=lambda m: (m.variant, -m.pixels, -m.nbytes, str(m.path))):
        if all(hamming(it.hash, k.hash) > dist for k in kept):
            kept.append(it)
    return kept


def split_groups(groups: list[list[Item]], val: float, test: float, rng: random.Random):
    """Stratified by (label, subtype); whole groups stay in one split."""
    buckets = defaultdict(list)
    for g in groups:
        buckets[(g[0].label, g[0].subtype)].append(g)
    out = {"train": [], "val": [], "test": []}
    for key in sorted(buckets):
        gs = buckets[key]
        rng.shuffle(gs)
        n = len(gs)
        n_test = max(1, round(n * test)) if n >= 3 else 0
        n_val = max(1, round(n * val)) if n >= 3 else 0
        out["test"] += gs[:n_test]
        out["val"] += gs[n_test:n_test + n_val]
        out["train"] += gs[n_test + n_val:]
    return out


def balance(train: list[Item], max_ratio: float, rng: random.Random) -> list[Item]:
    """Undersample big classes to at most max_ratio x the smallest (subtype round-robin)."""
    per = defaultdict(list)
    for it in train:
        per[it.label].append(it)
    if len(per) < 2:
        return train
    cap = int(max_ratio * min(len(v) for v in per.values()))
    out: list[Item] = []
    for label, items in per.items():
        if len(items) <= cap:
            out += items
            continue
        subs = defaultdict(list)
        for it in items:
            subs[it.subtype].append(it)
        for v in subs.values():
            rng.shuffle(v)
        picked: list[Item] = []
        while len(picked) < cap and any(subs.values()):
            for v in subs.values():
                if v and len(picked) < cap:
                    picked.append(v.pop())
        out += picked
    return out


def export(split: dict[str, list[Item]], out: Path, size: int) -> None:
    for name, items in split.items():
        for it in items:
            img = read_image(it.path)
            if img is None:
                continue
            dst = out / name / it.label / f"{short_hash(str(it.path))}_{it.path.stem[:40]}.jpg".replace(" ", "_")
            write_jpeg(dst, letterbox(img, size))


def build(sources: list[Path], out: Path, size=320, dist=6, seed=42, val=0.1, test=0.1,
          max_ratio=1.5, limit=0) -> dict:
    rng = random.Random(seed)
    items, stats = scan(sources, limit)
    groups = group_items(items, dist)
    clean_groups, conflicts = [], 0
    for g in groups:
        if len({m.label for m in g}) > 1:
            conflicts += 1
            continue
        clean_groups.append(g)
    split_g = split_groups(clean_groups, val, test, rng)
    split: dict[str, list[Item]] = {}
    dup_removed = 0
    for name, gs in split_g.items():
        items_out: list[Item] = []
        for g in gs:
            kept = dedupe_group(g, dist)
            if name != "train":
                kept = kept[:1]  # one clean representative per group in eval sets
            dup_removed += len(g) - len(kept)
            items_out += kept
        split[name] = items_out
    split["train"] = balance(split["train"], max_ratio, rng)
    export(split, out, size)
    report = {
        "scanned": len(items), "scan_stats": dict(stats), "groups": len(groups),
        "label_conflict_groups_dropped": conflicts, "duplicates_removed": dup_removed,
        "splits": {n: {"total": len(v), "by_label": dict(Counter(i.label for i in v)),
                       "by_subtype": dict(Counter(f"{i.label}/{i.subtype}" for i in v))}
                   for n, v in split.items()},
        "size": size, "seed": seed, "labels": LABELS,
    }
    out.mkdir(parents=True, exist_ok=True)
    (out / "report.json").write_text(json.dumps(report, indent=2), "utf-8")
    return report


def main(argv=None) -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", nargs="+", type=Path,
                    default=[ROOT / "data" / "dataset-resized", ROOT / "data" / "pbl4_split"])
    ap.add_argument("--out", type=Path, default=ROOT / "ml" / "data" / "trashnet4")
    ap.add_argument("--size", type=int, default=320)
    ap.add_argument("--hash-dist", type=int, default=6)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--max-ratio", type=float, default=1.5)
    ap.add_argument("--limit", type=int, default=0)
    args = ap.parse_args(argv)
    rep = build(args.src, args.out, args.size, args.hash_dist, args.seed, max_ratio=args.max_ratio, limit=args.limit)
    print(json.dumps({k: rep[k] for k in ("scanned", "groups", "duplicates_removed", "label_conflict_groups_dropped")}, indent=2))
    for name, s in rep["splits"].items():
        print(name, s["total"], s["by_label"])


if __name__ == "__main__":
    main()
