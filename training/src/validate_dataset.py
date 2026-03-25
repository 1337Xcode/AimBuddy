#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
from pathlib import Path

from training_config import load_config
from pipeline_common import ensure_workspace_structure


VALID_IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}


def _iter_images(path: Path):
    if not path.exists():
        return []
    return [p for p in path.iterdir() if p.suffix.lower() in VALID_IMAGE_EXTS]


def _validate_label_line(line: str, max_class_id: int) -> tuple[bool, str]:
    parts = line.strip().split()
    if len(parts) != 5:
        return False, "Label row must have exactly 5 values: class cx cy w h"

    try:
        class_id = int(parts[0])
        coords = [float(v) for v in parts[1:]]
    except ValueError:
        return False, "Label row contains non-numeric values"

    if class_id < 0 or class_id > max_class_id:
        return False, f"Class id {class_id} is outside expected range 0..{max_class_id}"

    for value in coords:
        if value < 0.0 or value > 1.0:
            return False, "Normalized bbox values must be in [0, 1]"

    if coords[2] <= 0 or coords[3] <= 0:
        return False, "Width and height must be > 0"

    return True, ""


def run_validation(config_path: Path | None = None) -> int:
    cfg = load_config(config_path)
    ensure_workspace_structure(cfg)
    dataset_dir = cfg.paths.dataset_dir
    report_dir = cfg.paths.output_dir / "reports"
    report_dir.mkdir(parents=True, exist_ok=True)

    if not dataset_dir.exists():
        print(f"ERROR: Dataset directory not found: {dataset_dir}")
        return 2

    data_yaml = dataset_dir / "data.yaml"
    if not data_yaml.exists():
        print(f"ERROR: Missing data.yaml in {dataset_dir}")
        return 2

    split_stats: dict[str, dict[str, int]] = {}
    errors: list[str] = []
    warnings: list[str] = []

    total_images = 0
    total_labels = 0
    negative_images = 0

    for split in ("train", "valid", "test"):
        image_dir = dataset_dir / split / "images"
        label_dir = dataset_dir / split / "labels"

        images = _iter_images(image_dir)
        split_images = len(images)
        split_labels = 0

        if split == "train" and split_images == 0:
            errors.append("train/images is empty")

        for image_path in images:
            label_path = label_dir / f"{image_path.stem}.txt"
            if not label_path.exists() or label_path.read_text(encoding="utf-8", errors="ignore").strip() == "":
                negative_images += 1
                continue

            lines = [ln for ln in label_path.read_text(encoding="utf-8", errors="ignore").splitlines() if ln.strip()]
            if not lines:
                negative_images += 1
                continue

            for line_idx, row in enumerate(lines, start=1):
                ok, msg = _validate_label_line(row, max_class_id=0)
                if not ok:
                    errors.append(f"{split}/{label_path.name}:L{line_idx} {msg}")
            split_labels += len(lines)

        split_stats[split] = {
            "images": split_images,
            "labels": split_labels,
        }
        total_images += split_images
        total_labels += split_labels

    if total_images > 0:
        neg_ratio = negative_images / total_images
        if neg_ratio < 0.10:
            warnings.append("Very few background images (<10%). This can increase false positives.")
        if neg_ratio > 0.70:
            warnings.append("Very high background ratio (>70%). This can hurt recall.")
    else:
        errors.append("No images found in dataset")

    report = {
        "dataset_dir": str(dataset_dir),
        "splits": split_stats,
        "total_images": total_images,
        "total_labels": total_labels,
        "background_images": negative_images,
        "errors": errors,
        "warnings": warnings,
    }

    report_path = report_dir / "dataset_report.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")

    print("Dataset validation summary")
    print(f"  images: {total_images}")
    print(f"  labels: {total_labels}")
    print(f"  backgrounds: {negative_images}")
    print(f"  report: {report_path}")

    for warning in warnings:
        print(f"WARNING: {warning}")

    if errors:
        print("ERRORS:")
        for err in errors[:20]:
            print(f"  - {err}")
        if len(errors) > 20:
            print(f"  ... and {len(errors) - 20} more")
        return 1

    print("Validation passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate YOLO dataset integrity and label correctness")
    parser.add_argument("--config", type=Path, default=None, help="Path to config/config.ini")
    args = parser.parse_args()
    return run_validation(args.config)


if __name__ == "__main__":
    raise SystemExit(main())
