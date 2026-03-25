#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
from pathlib import Path

from training_config import load_config, resolve_training_params, count_training_images
from pipeline_common import ensure_workspace_structure

try:
    from ultralytics import YOLO
except ImportError:
    print("ERROR: ultralytics not installed. Run scripts/01_setup_environment.bat first.")
    raise SystemExit(2)


def run_training(config_path: Path | None = None, adaptive_override: bool | None = None) -> int:
    cfg = load_config(config_path)
    ensure_workspace_structure(cfg)
    params = resolve_training_params(cfg, adaptive_override)

    if cfg.paths.base_model.name.lower() != "yolo26n.pt":
        print("ERROR: base_model must be yolo26n.pt for this repository contract")
        return 2

    if not cfg.paths.base_model.exists():
        print(f"ERROR: Missing base model: {cfg.paths.base_model}")
        return 2

    data_yaml = cfg.paths.dataset_dir / "data.yaml"
    if not data_yaml.exists():
        print(f"ERROR: Missing dataset yaml: {data_yaml}")
        return 2

    output_runs_dir = (cfg.paths.root_dir / params.project).resolve()
    reports_dir = cfg.paths.output_dir / "reports"
    reports_dir.mkdir(parents=True, exist_ok=True)

    selected = {
        "adaptive_enabled": cfg.adaptive.enabled if adaptive_override is None else adaptive_override,
        "train_images": count_training_images(cfg.paths.dataset_dir),
        "imgsz": params.imgsz,
        "epochs": params.epochs,
        "batch": params.batch,
        "patience": params.patience,
        "workers": params.workers,
        "device": params.device,
        "optimizer": params.optimizer,
        "lr0": params.lr0,
        "lrf": params.lrf,
        "close_mosaic": params.close_mosaic,
    }
    (reports_dir / "selected_training_config.json").write_text(json.dumps(selected, indent=2), encoding="utf-8")

    print("Training configuration")
    for key, value in selected.items():
        print(f"  {key}: {value}")

    model = YOLO(str(cfg.paths.base_model))

    device_value = params.device
    if params.device.lower() == "auto":
        try:
            import torch  # type: ignore

            device_value = 0 if torch.cuda.is_available() else "cpu"
        except Exception:
            device_value = "cpu"

    model.train(
        data=str(data_yaml),
        imgsz=params.imgsz,
        epochs=params.epochs,
        batch=params.batch,
        patience=params.patience,
        workers=params.workers,
        project=str(output_runs_dir),
        name=params.run_name,
        exist_ok=True,
        optimizer=params.optimizer,
        lr0=params.lr0,
        lrf=params.lrf,
        close_mosaic=params.close_mosaic,
        seed=params.seed,
        deterministic=True,
        single_cls=params.single_cls,
        cos_lr=True,
        device=device_value,
        amp=True,
        verbose=True,
    )

    best = output_runs_dir / params.run_name / "weights" / "best.pt"
    if not best.exists():
        print(f"ERROR: Training completed but best.pt not found at {best}")
        return 3

    print(f"Training completed: {best}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Train YOLOv26n using config/config.ini")
    parser.add_argument("--config", type=Path, default=None, help="Path to config/config.ini")
    parser.add_argument("--adaptive", action="store_true", help="Force adaptive settings")
    parser.add_argument("--manual", action="store_true", help="Force manual settings from config")
    args = parser.parse_args()

    if args.adaptive and args.manual:
        print("ERROR: choose either --adaptive or --manual")
        return 2

    adaptive_override = None
    if args.adaptive:
        adaptive_override = True
    if args.manual:
        adaptive_override = False

    return run_training(args.config, adaptive_override)


if __name__ == "__main__":
    raise SystemExit(main())
