#!/usr/bin/env python3
"""check_model_contract.py

Validates that a trained model matches the runtime contract:
- single class (enemy only)
- expected input size
- successful forward pass with sane output tensors
"""

from pathlib import Path
import argparse
import sys
import numpy as np

from training_config import load_config

try:
    from ultralytics import YOLO
except ImportError:
    print("ERROR: ultralytics not installed. Run scripts/01_setup_environment.bat first.")
    sys.exit(1)


def validate_model(weights_path: Path, input_size: int, num_classes: int) -> int:
    if not weights_path.exists():
        print(f"ERROR: weights file not found: {weights_path}")
        return 2

    model = YOLO(str(weights_path))

    names = getattr(model.model, "names", None) or {}
    class_count = len(names) if isinstance(names, dict) else 0

    print("=== Model Contract Check ===")
    print(f"Weights: {weights_path}")
    print(f"Expected classes: {num_classes}")
    print(f"Detected classes: {class_count}")
    print(f"Class names: {names}")

    if class_count != num_classes:
        print("FAIL: class count mismatch with runtime contract")
        return 3

    dummy = np.zeros((input_size, input_size, 3), dtype=np.uint8)
    results = model.predict(source=dummy, imgsz=input_size, conf=0.01, verbose=False)
    if not results:
        print("FAIL: model returned no result object on forward pass")
        return 4

    result = results[0]
    boxes = result.boxes
    if boxes is None:
        print("FAIL: missing boxes tensor in prediction output")
        return 5

    print(f"Forward pass OK. Output boxes tensor shape: {tuple(boxes.data.shape)}")
    print("PASS: model contract matches runtime expectations")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate trained model contract")
    parser.add_argument("--config", type=Path, default=None, help="Path to config/config.ini")
    parser.add_argument(
        "--weights",
        type=str,
        default=None,
        help="Path to model weights (.pt)",
    )
    args = parser.parse_args()

    cfg = load_config(args.config)
    default_weights = (cfg.paths.root_dir / cfg.training.project / cfg.training.run_name / "weights" / "best.pt").resolve()
    weights = Path(args.weights).resolve() if args.weights else default_weights
    expected_classes = 1 if cfg.training.single_cls else 3

    return validate_model(weights, cfg.training.imgsz, expected_classes)


if __name__ == "__main__":
    raise SystemExit(main())
