#!/usr/bin/env python3

from __future__ import annotations

import argparse
import shutil
from pathlib import Path

from training_config import load_config

try:
    from ultralytics import YOLO
except ImportError:
    print("ERROR: ultralytics not installed. Run setup first.")
    raise SystemExit(2)


def ensure_base_model(config_path: Path | None = None) -> int:
    cfg = load_config(config_path)
    base_model = cfg.paths.base_model

    if base_model.name.lower() != "yolo26n.pt":
        print("ERROR: base_model must be yolo26n.pt for this repository contract")
        return 2

    base_model.parent.mkdir(parents=True, exist_ok=True)

    if base_model.exists():
        print(f"Base model already present: {base_model}")
        return 0

    print("Downloading base model yolo26n.pt...")
    model = YOLO(base_model.name)

    ckpt_path = Path(str(getattr(model, "ckpt_path", ""))).resolve()
    if ckpt_path.exists() and ckpt_path != base_model:
        shutil.copy2(ckpt_path, base_model)

    if not base_model.exists():
        print(f"ERROR: Download completed but model is missing at {base_model}")
        return 3

    print(f"Base model ready: {base_model}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Ensure yolo26n.pt exists for training")
    parser.add_argument("--config", type=Path, default=None, help="Path to config/config.ini")
    args = parser.parse_args()
    return ensure_base_model(args.config)


if __name__ == "__main__":
    raise SystemExit(main())
