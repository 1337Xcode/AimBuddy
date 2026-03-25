#!/usr/bin/env python3

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import configparser


@dataclass
class TrainingPaths:
    root_dir: Path
    dataset_dir: Path
    videos_dir: Path
    raw_frames_dir: Path
    output_dir: Path
    app_assets_models_dir: Path
    base_model: Path


@dataclass
class TrainingParams:
    imgsz: int
    epochs: int
    batch: int
    patience: int
    workers: int
    device: str
    project: str
    run_name: str
    seed: int
    optimizer: str
    lr0: float
    lrf: float
    close_mosaic: int
    single_cls: bool


@dataclass
class AdaptiveParams:
    enabled: bool
    min_epochs: int
    max_epochs: int
    small_dataset_threshold: int
    medium_dataset_threshold: int


@dataclass
class ExportParams:
    half: bool
    simplify: bool
    dynamic: bool
    batch: int


@dataclass
class TrainingConfig:
    paths: TrainingPaths
    training: TrainingParams
    adaptive: AdaptiveParams
    export: ExportParams


def _get_bool(cfg: configparser.ConfigParser, section: str, key: str, fallback: bool) -> bool:
    if not cfg.has_option(section, key):
        return fallback
    return cfg.getboolean(section, key)


def load_config(config_path: Path | None = None) -> TrainingConfig:
    src_dir = Path(__file__).resolve().parent
    root_dir = src_dir.parent
    cfg_path = config_path or (root_dir / "config" / "config.ini")

    if not cfg_path.exists():
        raise FileNotFoundError(f"Missing config file: {cfg_path}")

    parser = configparser.ConfigParser()
    parser.read(cfg_path, encoding="utf-8")

    paths = TrainingPaths(
        root_dir=root_dir,
        dataset_dir=(root_dir / parser.get("paths", "dataset_dir", fallback="dataset")).resolve(),
        videos_dir=(root_dir / parser.get("paths", "videos_dir", fallback="videos")).resolve(),
        raw_frames_dir=(root_dir / parser.get("paths", "raw_frames_dir", fallback="raw_frames")).resolve(),
        output_dir=(root_dir / parser.get("paths", "output_dir", fallback="outputs")).resolve(),
        app_assets_models_dir=(root_dir / parser.get("paths", "app_assets_models_dir", fallback="../app/src/main/assets/models")).resolve(),
        base_model=(root_dir / parser.get("paths", "base_model", fallback="yolo26n.pt")).resolve(),
    )

    training = TrainingParams(
        imgsz=parser.getint("training", "imgsz", fallback=256),
        epochs=parser.getint("training", "epochs", fallback=180),
        batch=parser.getint("training", "batch", fallback=16),
        patience=parser.getint("training", "patience", fallback=25),
        workers=parser.getint("training", "workers", fallback=4),
        device=parser.get("training", "device", fallback="auto"),
        project=parser.get("training", "project", fallback="outputs/runs/detect"),
        run_name=parser.get("training", "run_name", fallback="train"),
        seed=parser.getint("training", "seed", fallback=42),
        optimizer=parser.get("training", "optimizer", fallback="AdamW"),
        lr0=parser.getfloat("training", "lr0", fallback=0.01),
        lrf=parser.getfloat("training", "lrf", fallback=0.01),
        close_mosaic=parser.getint("training", "close_mosaic", fallback=10),
        single_cls=_get_bool(parser, "training", "single_cls", True),
    )

    adaptive = AdaptiveParams(
        enabled=_get_bool(parser, "adaptive", "enabled", True),
        min_epochs=parser.getint("adaptive", "min_epochs", fallback=80),
        max_epochs=parser.getint("adaptive", "max_epochs", fallback=320),
        small_dataset_threshold=parser.getint("adaptive", "small_dataset_threshold", fallback=300),
        medium_dataset_threshold=parser.getint("adaptive", "medium_dataset_threshold", fallback=1200),
    )

    export = ExportParams(
        half=_get_bool(parser, "export", "half", True),
        simplify=_get_bool(parser, "export", "simplify", True),
        dynamic=_get_bool(parser, "export", "dynamic", False),
        batch=parser.getint("export", "batch", fallback=1),
    )

    return TrainingConfig(paths=paths, training=training, adaptive=adaptive, export=export)


def count_training_images(dataset_dir: Path) -> int:
    image_dir = dataset_dir / "train" / "images"
    if not image_dir.exists():
        return 0
    return sum(1 for p in image_dir.iterdir() if p.suffix.lower() in {".jpg", ".jpeg", ".png", ".bmp", ".webp"})


def resolve_training_params(config: TrainingConfig, adaptive_override: bool | None = None) -> TrainingParams:
    use_adaptive = config.adaptive.enabled if adaptive_override is None else adaptive_override
    resolved = TrainingParams(**vars(config.training))

    if not use_adaptive:
        return resolved

    image_count = count_training_images(config.paths.dataset_dir)
    if image_count <= 0:
        return resolved

    if image_count < config.adaptive.small_dataset_threshold:
        resolved.epochs = min(config.adaptive.max_epochs, max(config.adaptive.min_epochs, 260))
        resolved.batch = max(8, min(12, resolved.batch))
        resolved.patience = max(35, resolved.patience)
        resolved.close_mosaic = max(15, resolved.close_mosaic)
    elif image_count < config.adaptive.medium_dataset_threshold:
        resolved.epochs = min(config.adaptive.max_epochs, max(config.adaptive.min_epochs, 180))
        resolved.batch = max(12, min(20, resolved.batch))
        resolved.patience = max(25, resolved.patience)
        resolved.close_mosaic = max(10, resolved.close_mosaic)
    else:
        resolved.epochs = min(config.adaptive.max_epochs, max(config.adaptive.min_epochs, 120))
        resolved.batch = max(16, min(32, resolved.batch))
        resolved.patience = min(resolved.patience, 20)
        resolved.close_mosaic = min(max(resolved.close_mosaic, 8), 12)

    return resolved
