"""Pipeline test on a synthetic dataset that reuses the real filename patterns."""
import sys
from pathlib import Path

import cv2
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_dataset import build  # noqa: E402
from calibrate import calibrate, fit_reject, fit_temperature  # noqa: E402
from common import letterbox  # noqa: E402
from degrade import degrade, run as degrade_run  # noqa: E402
from evaluate import evaluate, macro_f1  # noqa: E402
from labeling import label_for, source_key  # noqa: E402

COLORS = {"paper": (200, 200, 220), "glass": (60, 160, 60), "other": (60, 60, 200), "plastic": (200, 120, 40)}


def test_labeling_rules():
    assert label_for("biological123.jpg", "burnable")[0] == "other"
    assert label_for("cardboard12.jpg", "burnable")[0] == "paper"
    assert label_for("beverage cans_beverage_cans 5.jpg", "metal")[0] == "other"
    assert label_for("brown-glass7.jpg", "glass")[0] == "glass"
    assert label_for("kaggle_1.jpg", "plastic")[0] == "plastic"
    assert label_for("IMG_E12.jpg", "plastic")[0] == "plastic"
    assert label_for("2_trash.jpg", "burnable")[0] == "other"
    assert label_for("weird.jpg", "burnable")[0] is None
    assert source_key("abc_bright.jpg") == ("abc", True)
    assert source_key("Image_1 - Copy.png") == ("image_1", True)


def _make(root: Path):
    rng = np.random.default_rng(0)
    spec = {("resized", "paper"): "clean_paper_paper", ("resized", "other"): "battery",
            ("pbl4", "glass"): "green-glass", ("pbl4", "plastic"): "plastic_bottle ",
            ("pbl4", "burnable"): "biological"}
    for (grp, folder), prefix in spec.items():
        label = {"burnable": "other"}.get(folder, folder)
        for i in range(30):
            img = np.full((120, 160, 3), COLORS[label], np.uint8)
            img = np.clip(img + rng.integers(-40, 40, img.shape), 0, 255).astype(np.uint8)
            cv2.circle(img, (int(rng.integers(30, 130)), int(rng.integers(30, 90))), int(rng.integers(10, 30)),
                       tuple(int(c) for c in rng.integers(0, 255, 3)), -1)
            p = root / grp / folder / f"{prefix}{i}.jpg"
            p.parent.mkdir(parents=True, exist_ok=True)
            cv2.imwrite(str(p), img)
    dup = cv2.imread(str(root / "resized/paper/clean_paper_paper0.jpg"))
    cv2.imwrite(str(root / "resized/paper/clean_paper_paper0_bright.jpg"), dup)


def test_pipeline(tmp_path):
    _make(tmp_path / "src")
    out = tmp_path / "ds"
    rep = build([tmp_path / "src"], out, size=64, limit=0)
    assert rep["scanned"] == 151
    assert rep["duplicates_removed"] >= 1
    sp = rep["splits"]
    assert sp["train"]["total"] > sp["val"]["total"] > 0 and sp["test"]["total"] > 0
    assert set(sp["train"]["by_label"]) == {"paper", "glass", "other", "plastic"}
    tr = {p.name for p in (out / "train").rglob("*.jpg")}
    te = {p.name for p in (out / "test").rglob("*.jpg")}
    assert not tr & te
    counts = degrade_run(out)
    assert counts["val_blur_3"] == sp["val"]["total"]

    def predict(imgs):  # colour nearest-centroid stand-in for YOLO
        labels = list(COLORS)
        order = ["paper", "glass", "other", "plastic"]
        res = []
        for im in imgs:
            m = im.reshape(-1, 3).mean(0)
            d = np.array([np.linalg.norm(m - np.array(COLORS[l])) for l in order])
            p = np.exp(-d / 40); res.append(p / p.sum())
        return np.array(res)

    sc = evaluate(out, predict, 64)
    assert sc["test"]["macro_f1"] > 0.8 and "test_blur_3" in sc
    cal = calibrate(out, predict, 64)
    assert 0.5 <= cal["temperature"] <= 3.0 and 0 <= cal["tau"] <= 0.8


def test_degrade_reduces_sharpness():
    img = cv2.imread(cv2.samples.findFile("starry_night.jpg")) if False else (np.random.default_rng(1).integers(0, 255, (200, 200, 3), dtype=np.uint8))
    lap = lambda x: cv2.Laplacian(cv2.cvtColor(x, cv2.COLOR_BGR2GRAY), cv2.CV_64F).var()
    rng = np.random.default_rng(2)
    l1, l3 = lap(degrade(img, 1, rng)), lap(degrade(img, 3, rng))
    assert l3 < l1 < lap(img)


def test_calibration_math():
    y = np.array([0, 1, 2, 3] * 10)
    probs = np.eye(4)[y] * 0.5 + 0.125
    assert 0.5 <= fit_temperature(y, probs) <= 3
    assert macro_f1([0, 1, 2, 3], [0, 1, 2, 3]) == 1.0
    assert len(fit_reject(y, probs)) == 2
    assert letterbox(np.zeros((10, 20, 3), np.uint8), 32).shape == (32, 32, 3)


def test_prepare_detect_and_thresholds(tmp_path):
    from prepare_detect import build, low_contrast
    from export_detect import score_thresholds, choose
    src = tmp_path / "team"
    (src / "images").mkdir(parents=True)
    (src / "labels").mkdir()
    (src / "data.yaml").write_text("names:\n  0: paper_sheet\n  1: plastic_bottle\n  2: glass_jar\n  3: battery\n", "utf-8")
    rng = np.random.default_rng(0)
    for i in range(40):
        c = i % 4
        img = np.full((200, 300, 3), 235, np.uint8)
        x, y = int(rng.integers(20, 150)), int(rng.integers(20, 80))
        img[y:y + 70, x:x + 90] = rng.integers(0, 255, 3)
        img = np.clip(img + rng.integers(-20, 20, img.shape), 0, 255).astype(np.uint8)
        cv2.imwrite(str(src / "images" / f"im{i}.jpg"), img)
        (src / "labels" / f"im{i}.txt").write_text(f"{c} {(x + 45) / 300} {(y + 35) / 200} {90 / 300} {70 / 200}")
    rep = build(src, tmp_path / "det", {}, None, hard_ratio=1.0)
    assert rep["images_used"] == 40 and not rep["unmapped_class_names"]
    for sp in ("train", "val", "test", "val_blur", "val_hard"):
        assert list((tmp_path / "det" / "images" / sp).glob("*.jpg")), sp
    lab = next((tmp_path / "det" / "labels" / "train").glob("*.txt")).read_text().split()
    assert int(lab[0]) in range(4) and all(0 <= float(v) <= 1 for v in lab[1:5])
    assert "names:" in (tmp_path / "det" / "data.yaml").read_text()
    flat = np.full((100, 100, 3), 255, np.uint8); flat[20:80, 20:80] = 0
    lc = low_contrast(flat, (20, 20, 80, 80), 0.3)
    assert lc[50, 50, 0] > 150  # dark object pulled toward the white background
    box = (10, 10, 50, 50)
    recs = [([(0, box)], [(0.9, 0, box)]), ([(0, box)], [(0.3, 0, box), (0.4, 1, (60, 60, 90, 90))]), ([], [(0.2, 2, box)])]
    conf, tau = choose(score_thresholds(recs, np.arange(0.05, 0.9, 0.05)))
    assert 0.05 <= conf <= 0.9 and tau >= conf
