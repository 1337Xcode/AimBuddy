#!/usr/bin/env python3
"""
Leak-free split of a labelled directory into train/valid/test.

Frames cut from one video look almost the same, so they all go to the same split:
the split is chosen by hashing the clip a frame came from (the name without its
`_frame_00042` part), and a frame that does not follow that pattern is its own
clip. A split by frame would put near copies of a training image into the
validation set, and the validation score, and the choice of best.pt, would only
measure how well the model remembers.

The choice is deterministic, so re-running the script (with new images added)
keeps already-assigned images in their original split and only assigns
new ones. No image ever moves between splits.

A handful of videos can leave the validation split empty (each video lands in
one split, and valid gets 15% of them). The script then says so and stops
with an error, because training needs a validation set. More videos fix it.
`--by-frame` splits frame by frame instead, which fills every split with
as few as one video, and which lets near copies of a training image into
validation: the score is then optimistic.

Output layout (in-place merge under `dataset_dir`):
    dataset_dir/train/images,  dataset_dir/train/labels
    dataset_dir/valid/images,  dataset_dir/valid/labels
    dataset_dir/test/images,   dataset_dir/test/labels
    dataset_dir/data.yaml   (created if missing)

Source can be either:
- A flat folder of <name>.jpg + <name>.txt pairs.
- An existing dataset/train/ that was bulk-populated by auto_label.py
  (in which case we *re-distribute* a portion into valid/ and test/).

Usage:
    python src/split_dataset.py --source dataset/train --dataset dataset \
        --train 0.80 --valid 0.15 --test 0.05
"""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
from pathlib import Path

IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}

# extract_frames.py names a frame <video>_frame_<number>.
_FRAME_SUFFIX = re.compile(r"_frame_\d+$")


def clip_of(stem: str) -> str:
    """The clip a frame was cut from: the name of its video, or the whole name when it is not a numbered frame."""
    return _FRAME_SUFFIX.sub("", stem)


def _bucket_for(name: str, train: float, valid: float) -> str:
    """Deterministic bucket: stable across runs, no shuffle, no seed dependency."""
    h = int(hashlib.md5(name.encode("utf-8")).hexdigest(), 16)
    r = (h % 1_000_000) / 1_000_000.0  # 0..1
    if r < train:
        return "train"
    if r < train + valid:
        return "valid"
    return "test"


def _iter_pairs(src_images: Path, src_labels: Path):
    for img in sorted(src_images.iterdir()):
        if not (img.is_file() and img.suffix.lower() in IMAGE_EXTS):
            continue
        label = src_labels / (img.stem + ".txt")
        yield img, label if label.exists() else None


def split(
    source_dir: Path,
    dataset_dir: Path,
    train_ratio: float,
    valid_ratio: float,
    test_ratio: float,
    move: bool,
    by_frame: bool = False,
) -> int:
    total = train_ratio + valid_ratio + test_ratio
    if abs(total - 1.0) > 0.001:
        print(f"ERROR: split ratios must sum to 1.0 (got {total:.3f})")
        return 2

    src_images = source_dir / "images" if (source_dir / "images").exists() else source_dir
    src_labels = source_dir / "labels" if (source_dir / "labels").exists() else source_dir

    if not src_images.exists():
        print(f"ERROR: source images dir missing: {src_images}")
        return 2

    for split_name in ("train", "valid", "test"):
        (dataset_dir / split_name / "images").mkdir(parents=True, exist_ok=True)
        (dataset_dir / split_name / "labels").mkdir(parents=True, exist_ok=True)

    counts = {"train": 0, "valid": 0, "test": 0, "missing_label": 0, "skipped_existing": 0}

    for img_path, label_path in _iter_pairs(src_images, src_labels):
        bucket = _bucket_for(img_path.stem if by_frame else clip_of(img_path.stem), train_ratio, valid_ratio)

        dst_img = dataset_dir / bucket / "images" / img_path.name
        dst_label = dataset_dir / bucket / "labels" / (img_path.stem + ".txt")

        # Idempotent: skip if the same file already exists in the target bucket.
        # If the same image exists in a DIFFERENT bucket, we leave it alone  -  never
        # rebucket, that would silently create cross-split leakage.
        already_present = False
        for split_name in ("train", "valid", "test"):
            if (dataset_dir / split_name / "images" / img_path.name).exists():
                already_present = True
                break
        if already_present:
            counts["skipped_existing"] += 1
            continue

        op = shutil.move if move else shutil.copy2
        op(str(img_path), str(dst_img))
        if label_path is not None and label_path.exists():
            op(str(label_path), str(dst_label))
        else:
            # Empty label file = legitimate negative sample (the model learns
            # there are zero enemies in this image).
            dst_label.write_text("", encoding="utf-8")
            counts["missing_label"] += 1
        counts[bucket] += 1

    data_yaml = dataset_dir / "data.yaml"
    if not data_yaml.exists():
        data_yaml.write_text(
            "train: train/images\n"
            "val: valid/images\n"
            "test: test/images\n"
            "nc: 1\n"
            "names: ['enemy']\n",
            encoding="utf-8",
        )

    print("=" * 60)
    print(f"Dataset split complete -> {dataset_dir}")
    print(f"  train  : {counts['train']:>6}")
    print(f"  valid  : {counts['valid']:>6}")
    print(f"  test   : {counts['test']:>6}")
    print(f"  skipped (already in dataset): {counts['skipped_existing']}")
    print(f"  missing label files         : {counts['missing_label']}")

    # What the dataset holds now, not only what this run added: an earlier run may have filled a split.
    held = {name: sum(1 for item in (dataset_dir / name / "images").iterdir() if item.suffix.lower() in IMAGE_EXTS) for name in ("train", "valid", "test")}
    if held["valid"] == 0:
        print("ERROR: the validation split is empty, and training needs one. Each video goes to one split, so a few videos can leave a split out.")
        print("       Add more videos, or run again with --by-frame to split frame by frame (the validation score is then optimistic).")
        return 3
    if held["test"] == 0:
        print("WARNING: the test split is empty. More videos fill it; --by-frame does too, at the cost of an optimistic score.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Stable hash-based train/valid/test split")
    parser.add_argument("--source", type=Path, required=True, help="Folder of image+label pairs (or a dataset/train/ directory)")
    parser.add_argument("--dataset", type=Path, required=True, help="Target dataset root (will gain train/, valid/, test/)")
    parser.add_argument("--train", type=float, default=0.80)
    parser.add_argument("--valid", type=float, default=0.15)
    parser.add_argument("--test", type=float, default=0.05)
    parser.add_argument("--move", action="store_true", help="Move files instead of copying (saves disk space)")
    parser.add_argument("--by-frame", action="store_true", help="Split frame by frame instead of video by video (near copies then land on both sides)")
    args = parser.parse_args()
    return split(args.source, args.dataset, args.train, args.valid, args.test, args.move, args.by_frame)


if __name__ == "__main__":
    raise SystemExit(main())
