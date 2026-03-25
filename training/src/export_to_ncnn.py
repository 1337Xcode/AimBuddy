#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path
import shutil

from training_config import load_config
from pipeline_common import ensure_workspace_structure

try:
    from ultralytics import YOLO
except ImportError:
    print("ERROR: ultralytics not installed. Run scripts/01_setup_environment.bat first.")
    raise SystemExit(2)


def run_export(config_path: Path | None = None, weights: Path | None = None) -> int:
    cfg = load_config(config_path)
    ensure_workspace_structure(cfg)

    runs_dir = (cfg.paths.root_dir / cfg.training.project).resolve()
    best_default = runs_dir / cfg.training.run_name / "weights" / "best.pt"
    weights_path = (weights or best_default).resolve()

    if not weights_path.exists():
        print(f"ERROR: Missing weights file: {weights_path}")
        return 2

    model = YOLO(str(weights_path))

    export_dir = cfg.paths.output_dir / "export"
    export_dir.mkdir(parents=True, exist_ok=True)
    assets_dir = cfg.paths.app_assets_models_dir
    assets_dir.mkdir(parents=True, exist_ok=True)

    exported = model.export(
        format="ncnn",
        imgsz=cfg.training.imgsz,
        half=cfg.export.half,
        simplify=cfg.export.simplify,
        dynamic=cfg.export.dynamic,
        batch=cfg.export.batch,
    )

    exported_path = Path(str(exported)).resolve()
    if exported_path.is_file():
        exported_path = exported_path.parent

    param_files = list(exported_path.glob("*.param"))
    bin_files = list(exported_path.glob("*.bin"))

    if not param_files or not bin_files:
        print(f"ERROR: NCNN export did not produce .param/.bin in {exported_path}")
        return 3

    param_src = param_files[0]
    bin_src = bin_files[0]

    param_dst = export_dir / "yolo26n-opt.param"
    bin_dst = export_dir / "yolo26n-opt.bin"

    shutil.copy2(param_src, param_dst)
    shutil.copy2(bin_src, bin_dst)

    shutil.copy2(param_dst, assets_dir / param_dst.name)
    shutil.copy2(bin_dst, assets_dir / bin_dst.name)

    print(f"Export complete: {param_dst} and {bin_dst}")
    print(f"Copied to app assets: {assets_dir}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Export trained weights to NCNN")
    parser.add_argument("--config", type=Path, default=None, help="Path to config/config.ini")
    parser.add_argument("--weights", type=Path, default=None, help="Path to weights file (best.pt)")
    args = parser.parse_args()
    return run_export(args.config, args.weights)


if __name__ == "__main__":
    raise SystemExit(main())
