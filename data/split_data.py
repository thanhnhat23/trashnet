import os
import re
import json
import random
from pathlib import Path

# Mapping according to constants.py (0-indexed)
ZERO_INDEXED_CLASSES = {
    'glass': 0,
    'paper': 1,
    'cardboard': 2,
    'plastic': 3,
    'metal': 4,
    'trash': 5,
    'battery': 6,
    'biological': 7,
}

# 1-indexed mapping (used for Torch / DataLoader.lua)
ONE_INDEXED_CLASSES = {k: v + 1 for k, v in ZERO_INDEXED_CLASSES.items()}

# Standard ImageFolder mapping (alphabetical order) to update classes.json
IMAGEFOLDER_CLASSES = {k: idx for idx, k in enumerate(sorted(ZERO_INDEXED_CLASSES.keys()))}

VALID_EXTENSIONS = {'.jpg', '.jpeg', '.png', '.bmp', '.webp'}

def natural_sort_key(s):
    """Sort strings with embedded numbers naturally (e.g., glass2 before glass10)."""
    return [int(text) if text.isdigit() else text.lower() for text in re.split(r'(\d+)', str(s))]

def get_category_files(dataset_dir):
    data = {}
    for cat in ZERO_INDEXED_CLASSES.keys():
        cat_dir = dataset_dir / cat
        if not cat_dir.exists():
            print(f"[!] Warning: Category folder not found: {cat_dir}")
            data[cat] = []
            continue
        files = [
            f.name for f in cat_dir.iterdir()
            if f.is_file() and f.suffix.lower() in VALID_EXTENSIONS and not f.name.startswith('.')
        ]
        files.sort(key=natural_sort_key)
        data[cat] = files
        print(f"  - {cat:12s}: {len(files)} images")
    return data

def main():
    base_dir = Path(__file__).resolve().parent
    dataset_dir = base_dir / 'dataset-resized'
    
    print(f"Scanning images from: {dataset_dir}")
    category_files = get_category_files(dataset_dir)
    total_images = sum(len(f) for f in category_files.values())
    print(f"Total valid images across all 8 classes: {total_images}\n")

    # 1. Write zero-indexed-files.txt
    zero_indexed_path = base_dir / 'zero-indexed-files.txt'
    with open(zero_indexed_path, 'w', encoding='utf-8') as f:
        for cat, files in category_files.items():
            label = ZERO_INDEXED_CLASSES[cat]
            for fname in files:
                f.write(f"{fname} {label}\n")
    print(f"[+] Written {total_images} lines to {zero_indexed_path.name}")

    # 2. Write one-indexed-files.txt
    one_indexed_path = base_dir / 'one-indexed-files.txt'
    with open(one_indexed_path, 'w', encoding='utf-8') as f:
        for cat, files in category_files.items():
            label = ONE_INDEXED_CLASSES[cat]
            for fname in files:
                f.write(f"{fname} {label}\n")
    print(f"[+] Written {total_images} lines to {one_indexed_path.name}")

    # 3. Stratified dataset split (70% Train, 13% Val, 17% Test)
    rng = random.Random(42)
    train_items = []
    val_items = []
    test_items = []

    for cat, files in category_files.items():
        label = ONE_INDEXED_CLASSES[cat]
        cat_items = [(fname, label) for fname in files]
        rng.shuffle(cat_items)

        n = len(cat_items)
        n_train = int(round(0.70 * n))
        n_val = int(round(0.13 * n))
        n_test = n - n_train - n_val

        cat_train = cat_items[:n_train]
        cat_val = cat_items[n_train:n_train + n_val]
        cat_test = cat_items[n_train + n_val:]

        train_items.extend(cat_train)
        val_items.extend(cat_val)
        test_items.extend(cat_test)
        print(f"  {cat:12s} ({n:4d}) -> Train: {len(cat_train):4d}, Val: {len(cat_val):3d}, Test: {len(cat_test):3d}")

    # Shuffle randomly within each split
    rng.shuffle(train_items)
    rng.shuffle(val_items)
    rng.shuffle(test_items)

    splits = {
        'one-indexed-files-notrash_train.txt': train_items,
        'one-indexed-files-notrash_val.txt': val_items,
        'one-indexed-files-notrash_test.txt': test_items,
    }

    print("\nWriting split files:")
    for split_filename, items in splits.items():
        split_path = base_dir / split_filename
        with open(split_path, 'w', encoding='utf-8') as f:
            for fname, label in items:
                f.write(f"{fname} {label}\n")
        print(f"[+] Written {len(items):4d} items ({len(items)/total_images*100:.1f}%) to {split_filename}")

    # 4. Update classes.json
    classes_json_path = base_dir / 'classes.json'
    with open(classes_json_path, 'w', encoding='utf-8') as f:
        json.dump(IMAGEFOLDER_CLASSES, f, indent=4)
    print(f"\n[+] Updated {classes_json_path.name}:")
    print(json.dumps(IMAGEFOLDER_CLASSES, indent=2))

if __name__ == '__main__':
    main()
