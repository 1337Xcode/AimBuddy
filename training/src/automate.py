#!/usr/bin/env python3
"""
End-to-end automation: videos in, NCNN model out.

Pipeline:
  1. Setup virtualenv check
  2. Extract frames from training/videos/*  -> raw_frames/<stem>/
  3. Auto-label raw_frames/ with teacher    -> dataset/train/{images,labels}
  4. Mine negatives from raw_frames/negatives/ (if present)
  5. Hash-split dataset/train/ into train/valid/test
  6. Validate dataset
  7. Train at imgsz=640 with strong augmentations
  8. Export NCNN at runtime_imgsz=256 (FP16)  -> outputs/export/{model.param,model.bin,model.json}
  9. Active-learning sweep -> outputs/review/uncertain/

Each step writes a checkpoint to outputs/reports/automate_state.json so
re-running the script resumes from the last failure point.

Manual touch points (the 1% you can't automate):
  - Drop gameplay videos in training/videos/
  - Drop no-enemy frames in raw_frames/negatives/
  - After step 3 (auto-label), spot-check dataset/train/labels/ and
    delete obviously-wrong boxes (Roboflow / labelImg take 5-10 minutes
    for a few hundred frames vs the 10+ hours of from-scratch labelling).
  - After step 10, label the surfaced frames in outputs/review/uncertain/
    then re-run this script to fold them in.

Flags:
  --skip-extract         Skip video frame extraction
  --skip-autolabel       Skip teacher auto-labelling
  --skip-train           Skip training
  --skip-export          Skip NCNN export
  --skip-active          Skip active-learning sweep
  --teacher PATH         Override teacher weights (default: yolov8x.pt)
  --teacher-imgsz N      Default: 1280
  --teacher-conf F       Default: 0.30
  --fps F                Frame-extraction fps (default: 1.0)
  --device D             cuda id, 'cpu', or 'auto'
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
import time
from pathlib import Path

# Make sibling modules importable when invoked from training/src/.
SRC_DIR = Path(__file__).resolve().parent
if str(SRC_DIR) not in sys.path:
    sys.path.insert(0, str(SRC_DIR))

from training_config import load_config  # noqa: E402
from pipeline_common import ensure_workspace_structure, write_json  # noqa: E402


def _stamp() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%S")


def _save_state(reports_dir: Path, state: dict) -> None:
    state["updated_at"] = _stamp()
    write_json(reports_dir / "automate_state.json", state)


def _resolve_device(device: str) -> str | int:
    if device != "auto":
        return device
    try:
        import torch  # type: ignore
        return 0 if torch.cuda.is_available() else "cpu"
    except Exception:
        return "cpu"


def _step_extract(cfg, fps: float) -> int:
    from extract_frames import extract
    if not any(cfg.paths.videos_dir.iterdir()) if cfg.paths.videos_dir.exists() else True:
        print("[extract] no videos found; skipping")
        return 0
    return extract(
        config_path=None,
        fps_extract=fps,
        source_crop=320,        # matches runtime CROP_SIZE
        capture_height=720,     # matches runtime CAPTURE_HEIGHT
        target_imgsz=cfg.training.imgsz,
    )


def _step_autolabel(cfg, teacher: Path, imgsz: int, conf: float, device) -> int:
    from auto_label import auto_label
    return auto_label(
        frames_dir=cfg.paths.raw_frames_dir,
        out_dir=cfg.paths.dataset_dir / "_unsplit",
        teacher_weights=teacher,
        imgsz=imgsz,
        conf_threshold=conf,
        iou_threshold=0.55,
        keep_class_ids=[0],
        device=device,
        write_negatives=False,
        min_box_area_frac=0.0005,
        max_box_area_frac=0.85,
    )


def _step_mine_negatives(cfg) -> int:
    negatives_dir = cfg.paths.raw_frames_dir / "negatives"
    if not negatives_dir.exists():
        print("[negatives] raw_frames/negatives/ not found; skipping")
        return 0
    from mine_negatives import mine_negatives
    return mine_negatives(negatives_dir, cfg.paths.dataset_dir / "_unsplit")


def _step_split(cfg) -> int:
    from split_dataset import split
    source = cfg.paths.dataset_dir / "_unsplit"
    if not source.exists():
        # Treat existing train/ as the source (re-balance into valid/test).
        source = cfg.paths.dataset_dir / "train"
        if not source.exists():
            print("[split] no labelled data found; skipping")
            return 1
    return split(
        source_dir=source,
        dataset_dir=cfg.paths.dataset_dir,
        train_ratio=0.80,
        valid_ratio=0.15,
        test_ratio=0.05,
        move=True,
    )


def _step_validate(cfg) -> int:
    from validate_dataset import main as validate_main  # type: ignore
    return validate_main()


def _step_train(cfg) -> int:
    from train import run_training
    return run_training(config_path=None, adaptive_override=True)


def _step_export(cfg) -> int:
    from export_to_ncnn import run_export
    return run_export(config_path=None, weights=None)


def _step_active(cfg, teacher: Path, teacher_imgsz: int, device) -> int:
    from active_learning import active_learning
    student = cfg.paths.output_dir / "runs" / "detect" / "train" / "weights" / "best.pt"
    if not student.exists():
        print(f"[active] no student weights at {student}; skipping")
        return 0
    return active_learning(
        student_weights=student,
        teacher_weights=teacher,
        frames_dir=cfg.paths.raw_frames_dir,
        out_dir=cfg.paths.output_dir / "review",
        student_imgsz=cfg.training.runtime_imgsz,
        teacher_imgsz=teacher_imgsz,
        student_conf=0.50,
        teacher_conf=0.30,
        uncertain_lo=0.25,
        uncertain_hi=0.55,
        disagreement_threshold=1,
        max_review=300,
        device=device,
    )


def automate(args: argparse.Namespace) -> int:
    cfg = load_config(None)
    ensure_workspace_structure(cfg)
    reports_dir = cfg.paths.output_dir / "reports"
    reports_dir.mkdir(parents=True, exist_ok=True)
    state: dict = {"started_at": _stamp(), "steps": {}}

    device = _resolve_device(args.device)
    teacher = Path(args.teacher)

    steps = [
        ("extract", not args.skip_extract, lambda: _step_extract(cfg, args.fps)),
        ("autolabel", not args.skip_autolabel, lambda: _step_autolabel(cfg, teacher, args.teacher_imgsz, args.teacher_conf, device)),
        ("negatives", True, lambda: _step_mine_negatives(cfg)),
        ("split", True, lambda: _step_split(cfg)),
        ("validate", True, lambda: _step_validate(cfg)),
        ("train", not args.skip_train, lambda: _step_train(cfg)),
        ("export", not args.skip_export, lambda: _step_export(cfg)),
        ("active", not args.skip_active, lambda: _step_active(cfg, teacher, args.teacher_imgsz, device)),
    ]

    overall_rc = 0
    for name, enabled, fn in steps:
        if not enabled:
            state["steps"][name] = {"status": "skipped"}
            _save_state(reports_dir, state)
            continue
        state["steps"][name] = {"status": "running", "started_at": _stamp()}
        _save_state(reports_dir, state)
        print()
        print("=" * 70)
        print(f"[automate] STEP: {name}")
        print("=" * 70)
        try:
            rc = fn()
        except Exception as exc:  # any per-step failure is recorded, not raised
            state["steps"][name] = {"status": "error", "error": str(exc), "finished_at": _stamp()}
            _save_state(reports_dir, state)
            overall_rc = 1
            print(f"[automate] {name} raised: {exc}")
            if name in {"split", "validate", "train"}:
                # These are blocking: later steps depend on them.
                break
            continue

        state["steps"][name] = {"status": "ok" if rc == 0 else "warning", "rc": rc, "finished_at": _stamp()}
        _save_state(reports_dir, state)
        if rc != 0 and name in {"train", "export"}:
            overall_rc = rc
            break

    # Clean up _unsplit staging area (already moved out by split).
    staging = cfg.paths.dataset_dir / "_unsplit"
    if staging.exists() and not any(staging.rglob("*")):
        shutil.rmtree(staging, ignore_errors=True)

    state["finished_at"] = _stamp()
    state["overall_rc"] = overall_rc
    _save_state(reports_dir, state)

    print()
    print("=" * 70)
    print("Automation summary")
    for name, info in state["steps"].items():
        print(f"  {name:<10} {info}")
    print(f"  state -> {reports_dir / 'automate_state.json'}")
    return overall_rc


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the full training pipeline end-to-end")
    parser.add_argument("--skip-extract", action="store_true")
    parser.add_argument("--skip-autolabel", action="store_true")
    parser.add_argument("--skip-train", action="store_true")
    parser.add_argument("--skip-export", action="store_true")
    parser.add_argument("--skip-active", action="store_true")
    parser.add_argument("--teacher", type=str, default="yolov8x.pt")
    parser.add_argument("--teacher-imgsz", type=int, default=1280)
    parser.add_argument("--teacher-conf", type=float, default=0.30)
    parser.add_argument("--fps", type=float, default=1.0)
    parser.add_argument("--device", type=str, default="auto")
    return automate(parser.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
