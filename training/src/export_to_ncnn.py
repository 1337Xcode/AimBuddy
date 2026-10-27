#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil

from training_config import load_config
from pipeline_common import ensure_workspace_structure

try:
    from ultralytics import YOLO
except ImportError:
    print("ERROR: ultralytics not installed. Run scripts/01_setup_environment.bat first.")
    raise SystemExit(2)


def write_model_description(path: Path, input_size: int) -> None:
    """Writes the model.json the app and the model store read, or brings an existing one up to date.

    The pipeline trains one class (an enemy), so the description says so. The title, description and
    author are for the person who publishes the model: the store refuses a model without an author.
    An existing file keeps everything its author wrote, except the input size, which is a fact about
    the export that was just made.
    """
    if path.exists():
        try:
            description = json.loads(path.read_text(encoding="utf-8"))
        except ValueError as error:
            raise SystemExit(f"ERROR: {path} is not valid JSON ({error}). Fix it or delete it, then export again.")
        if description.get("inputSize") != input_size:
            description["inputSize"] = input_size
            path.write_text(json.dumps(description, indent=2) + "\n", encoding="utf-8")
            print(f"Set inputSize in {path.name} to {input_size}; raise its version, the model's files changed")
        else:
            print(f"Kept the existing {path.name}; raise its version if the model's files changed")
        return
    description = {
        "title": "Custom model",
        "description": "Trained with the AimBuddy training pipeline.",
        "author": "",
        "version": 1,
        "roles": ["body"],
        "inputSize": input_size,
    }
    path.write_text(json.dumps(description, indent=2) + "\n", encoding="utf-8")


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

    # Export at the runtime imgsz, NOT the training imgsz. We train at 640
    # (to learn small/distant targets) but deploy at 256 for mobile latency.
    export_imgsz = cfg.training.runtime_imgsz or cfg.training.imgsz
    print(f"Exporting NCNN model at imgsz={export_imgsz} (training imgsz={cfg.training.imgsz})")
    exported = model.export(
        format="ncnn",
        imgsz=export_imgsz,
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

    # The names the app's Models screen and the model store use for a model's files.
    param_dst = export_dir / "model.param"
    bin_dst = export_dir / "model.bin"

    shutil.copy2(param_src, param_dst)
    shutil.copy2(bin_src, bin_dst)
    write_model_description(export_dir / "model.json", export_imgsz)

    print(f"Export complete: {param_dst}, {bin_dst} and {export_dir / 'model.json'}")
    print("To try it, import the .param and .bin on the phone (Models, Import). To publish it, see model/README.md.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Export trained weights to NCNN")
    parser.add_argument("--config", type=Path, default=None, help="Path to config/config.ini")
    parser.add_argument("--weights", type=Path, default=None, help="Path to weights file (best.pt)")
    args = parser.parse_args()
    return run_export(args.config, args.weights)


if __name__ == "__main__":
    raise SystemExit(main())
