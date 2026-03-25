#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path

import cv2
from tqdm import tqdm

from training_config import load_config
from pipeline_common import ensure_workspace_structure

VIDEO_EXTENSIONS = {".mp4", ".avi", ".mov", ".mkv", ".webm"}


def _list_videos(videos_dir: Path) -> list[Path]:
    if not videos_dir.exists():
        return []
    return [p for p in videos_dir.iterdir() if p.suffix.lower() in VIDEO_EXTENSIONS]


def _preprocess_frame(frame, target_imgsz: int, crop_size: int):
    h, w = frame.shape[:2]
    scale = 720.0 / max(1, h)
    scaled_w = int(w * scale)
    scaled_h = 720
    resized = cv2.resize(frame, (scaled_w, scaled_h))

    actual_crop = min(crop_size, scaled_w, scaled_h)
    cx = scaled_w // 2
    cy = scaled_h // 2
    half = actual_crop // 2
    x1, x2 = cx - half, cx + half
    y1, y2 = cy - half, cy + half
    cropped = resized[y1:y2, x1:x2]
    if cropped.size == 0:
        return None

    return cv2.resize(cropped, (target_imgsz, target_imgsz))


def _process_video(video_path: Path, output_root: Path, target_imgsz: int, fps_extract: float, crop_size: int) -> tuple[int, bool]:
    cap = cv2.VideoCapture(str(video_path))
    if not cap.isOpened():
        print(f"Skipping unreadable video: {video_path.name}")
        return 0, True

    video_fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    interval = max(1, int(video_fps / max(0.1, fps_extract)))
    total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT) or 0)

    out_dir = output_root / video_path.stem
    out_dir.mkdir(parents=True, exist_ok=True)

    frame_idx = 0
    saved = 0

    with tqdm(total=total if total > 0 else None, desc=video_path.name, unit="frame") as bar:
        while True:
            ok, frame = cap.read()
            if not ok:
                break

            if frame_idx % interval == 0:
                final = _preprocess_frame(frame, target_imgsz, crop_size)
                if final is not None:
                    out_file = out_dir / f"{video_path.stem}_frame_{saved:05d}.jpg"
                    cv2.imwrite(str(out_file), final)
                    saved += 1

            frame_idx += 1
            bar.update(1)

    cap.release()
    print(f"Extracted {saved} frames -> {out_dir}")
    return saved, False


def extract(config_path: Path | None = None, fps_extract: float = 1.0, crop_size: int = 480) -> int:
    cfg = load_config(config_path)
    ensure_workspace_structure(cfg)
    videos_dir = cfg.paths.videos_dir
    output_root = cfg.paths.raw_frames_dir
    output_root.mkdir(parents=True, exist_ok=True)

    videos = _list_videos(videos_dir)

    if not videos:
        print(f"No videos found in {videos_dir}")
        return 1

    had_error = False
    total_saved = 0

    for video_path in videos:
        saved, error = _process_video(video_path, output_root, cfg.training.imgsz, fps_extract, crop_size)
        total_saved += saved
        had_error = had_error or error

    print(f"Total extracted frames: {total_saved}")

    return 2 if had_error else 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Extract training frames using runtime-aligned preprocessing")
    parser.add_argument("--config", type=Path, default=None, help="Path to config/config.ini")
    parser.add_argument("--fps", type=float, default=1.0, help="Frames to extract per second")
    parser.add_argument("--crop", type=int, default=480, help="Center crop size before resize")
    args = parser.parse_args()
    return extract(args.config, args.fps, args.crop)


if __name__ == "__main__":
    raise SystemExit(main())
