"""Filename-prefix labeling that fixes the legacy `burnable` class (it mixed organic waste and paper)."""
import re
from typing import Optional

LABELS = ["paper", "glass", "other", "plastic"]
FOLDER_LABEL = {"paper": "paper", "glass": "glass", "plastic": "plastic", "other": "other", "metal": "other"}

# Ordered (label, subtype, prefix) rules; the first match wins.
PREFIX_RULES = [
    ("other", "biological", "biological"),
    ("other", "battery", "battery"),
    ("other", "trash", "trash"),
    ("other", "metal", "beverage cans"),
    ("other", "metal", "spray cans"),
    ("other", "metal", "construction scrap"),
    ("other", "metal", "metal containers"),
    ("other", "metal", "other metal"),
    ("other", "metal", "metal"),
    ("paper", "paper", "clean_paper"),
    ("paper", "cardboard", "clean_cardboard"),
    ("paper", "paper", "paper_cups"),
    ("paper", "paper", "paper"),
    ("paper", "cardboard", "cardboard"),
    ("glass", "glass", "brown-glass"),
    ("glass", "glass", "green-glass"),
    ("glass", "glass", "white-glass"),
    ("glass", "glass", "glass"),
    ("plastic", "plastic", "plastic"),
]

_VARIANT = re.compile(r"_(bright|dark|fliph|flipv|rot_neg|rot|zoom)$")
_COPY = re.compile(r"\s*-\s*copy(\s*\(\d+\))?$")


def label_for(filename: str, parent_folder: str) -> tuple[Optional[str], str, str]:
    """Return (label, subtype, how). label is None when the file cannot be placed safely."""
    name = filename.lower()
    for label, subtype, prefix in PREFIX_RULES:
        if name.startswith(prefix):
            return label, subtype, "prefix"
    if re.search(r"(^|[_\d])trash", name):
        return "other", "trash", "token"
    folder = FOLDER_LABEL.get(parent_folder.lower())
    if folder:
        return folder, folder if folder != "other" else "misc", "folder"
    return None, "", "unresolved"


def source_key(filename: str) -> tuple[str, bool]:
    """Key shared by augmented variants / copies of one source photo, and a variant flag."""
    stem = filename.rsplit(".", 1)[0].lower().strip()
    is_variant = False
    changed = True
    while changed:
        changed = False
        m = _VARIANT.search(stem)
        if m:
            stem, is_variant, changed = stem[:m.start()], True, True
        m = _COPY.search(stem)
        if m:
            stem, is_variant, changed = stem[:m.start()], True, True
    return stem, is_variant
