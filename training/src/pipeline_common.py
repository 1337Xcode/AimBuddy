#!/usr/bin/env python3

from __future__ import annotations

from datetime import datetime, timezone
from pathlib import Path
import json

from training_config import TrainingConfig


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def ensure_output_structure(cfg: TrainingConfig) -> None:
    (cfg.paths.output_dir / "reports").mkdir(parents=True, exist_ok=True)
    (cfg.paths.output_dir / "runs").mkdir(parents=True, exist_ok=True)
    (cfg.paths.output_dir / "export").mkdir(parents=True, exist_ok=True)


def ensure_workspace_structure(cfg: TrainingConfig) -> None:
    cfg.paths.dataset_dir.mkdir(parents=True, exist_ok=True)
    cfg.paths.videos_dir.mkdir(parents=True, exist_ok=True)
    cfg.paths.raw_frames_dir.mkdir(parents=True, exist_ok=True)
    cfg.paths.app_assets_models_dir.mkdir(parents=True, exist_ok=True)

    for split in ("train", "valid", "test"):
        (cfg.paths.dataset_dir / split / "images").mkdir(parents=True, exist_ok=True)
        (cfg.paths.dataset_dir / split / "labels").mkdir(parents=True, exist_ok=True)

    ensure_output_structure(cfg)


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2), encoding="utf-8")


def append_log_line(path: Path, message: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as file:
        file.write(message + "\n")
