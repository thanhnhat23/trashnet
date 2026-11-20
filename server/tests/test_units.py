import cv2
import numpy as np

from domain import labels
from services import inference, vision
from services.history_repo import HistoryRepo


def test_labels_single_source():
    assert labels.LABEL_IDS == ["paper", "glass", "other", "plastic"]
    assert labels.bin_of("plastic") == 4
    assert labels.get("unknown")["id"] == "other"


def _tray():
    rng = np.random.default_rng(1)
    return np.clip(np.full((240, 320, 3), 150) + rng.normal(0, 2, (240, 320, 3)), 0, 255).astype(np.uint8)


def test_presence_detects_object_and_ignores_brightness_shift():
    ref = vision.TrayReference()
    ref.set(_tray())
    assert vision.find_object(_tray(), ref) is None
    brighter = np.clip(_tray().astype(int) + 40, 0, 255).astype(np.uint8)
    assert vision.find_object(brighter, ref) is None
    with_obj = _tray()
    with_obj[80:160, 110:210] = (30, 60, 200)
    box = vision.find_object(with_obj, ref)
    assert box is not None and 90 < box.x < 125 and 60 < box.y < 95
    assert 0.08 < box.area_frac < 0.2


def test_sharpness_drops_when_blurred():
    img = np.random.default_rng(2).integers(0, 255, (120, 120, 3), dtype=np.uint8)
    assert vision.sharpness(cv2.GaussianBlur(img, (15, 15), 5)) < vision.sharpness(img) / 20


def test_letterbox_keeps_aspect():
    out = vision.letterbox(np.full((100, 200, 3), 255, np.uint8), 224)
    assert out.shape == (224, 224, 3)
    assert out[0, 0, 0] == 114 and out[112, 112, 0] == 255


def test_temperature_and_reject():
    p = np.array([0.7, 0.1, 0.1, 0.1])
    soft = inference.apply_temperature(p, 2.0)
    assert soft[0] < p[0] and abs(soft.sum() - 1) < 1e-9
    assert inference.decide(p, 0.6, 0.15).label == "paper"
    unsure = inference.decide(np.array([0.4, 0.35, 0.15, 0.1]), 0.6, 0.15)
    assert unsure.uncertain and unsure.label == "other"


def test_vote_weights():
    a, b = np.array([1.0, 0, 0, 0]), np.array([0, 1.0, 0, 0])
    p = inference.vote([a, b], [3.0, 1.0])
    assert p[0] == 0.75 and p[1] == 0.25


def test_history_roundtrip(tmp_path):
    h = HistoryRepo(tmp_path / "h.db", tmp_path / "t")
    h.add("glass", 0.9, False, "manual", np.zeros((50, 80, 3), np.uint8))
    h.add("plastic", 0.5, True, "auto")
    st = h.stats()
    assert st["total"] == 2 and st["by_label"]["glass"] == 1 and st["uncertain"] == 1
    rows = h.list()
    assert rows[1]["thumb"].endswith("/thumb") and rows[0]["thumb"] == ""
    h.delete(rows[0]["id"])
    assert len(h.list()) == 1
    h.clear()
    assert h.stats()["total"] == 0
