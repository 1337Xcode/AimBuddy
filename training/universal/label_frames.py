"""Runs the teacher models over the sampled frames of every video and keeps what each one saw, so the labelling rules in
cut_squares.py can be changed and run again without running the models again.

For each video a JSON file in the cache folder lists the frames taken (one every few seconds, minus frames that look like
the one before) and, for each frame, every box each model gave above a low bar, in frame pixels:
  sunxds  the PC shooter model by SunOner (player, head), whole frame at 1.25 times its width, flips and three scales
  coco    YOLO11x trained on COCO, people only
  gen2    AimBuddy General 2 (body, head, own character), whole frame at its width, flips and three scales
  sunft   sunxds fine-tuned on the first universal set (body, head, own character), as gen2
The two fine-tuned models find mobile players the PC models score low; what they call the player's own character is
kept as a person and decided again by cut_squares.py.

The PC sets are labelled by people, but three of them have no heads: for those, sunxds's heads on every training and
validation picture are kept too (pc_<set>.json).

Usage: label_frames.py --videos <folder> --pc <folder> --cache <folder> --sunxds <pt> --coco <pt> --gen2 <pt> --sunft <pt>
A video or set whose cache file exists is skipped, so a stopped run carries on where it was.
"""
import argparse
import json
import pathlib
import sys
import time

import cv2
import numpy as np
from ultralytics import YOLO

sys.path.insert(0, str(pathlib.Path(__file__).parent))   # python -I leaves the script's folder off the path
from sources import (PC_SETS, VIDEOS, grey_out_broadcast, has_heads, pc_pictures, read_pc_picture,  # noqa: E402
                     video_frames)

SAME_FRAME_DIFF = 3.0   # mean grey difference (0..255) below which a frame repeats the last one kept
MIN_SCORE = 0.10
PC_HEAD_SCORE = 0.30    # sunxds heads on PC pictures, where the bodies are labelled by people


def boxes_of(result, keep_classes=None):
    """A model's boxes as [x0, y0, x1, y1, score, class], rounded."""
    out = []
    for (x0, y0, x1, y1), score, cls in zip(result.boxes.xyxy.tolist(), result.boxes.conf.tolist(),
                                             result.boxes.cls.tolist(), strict=True):
        if keep_classes is not None and int(cls) not in keep_classes:
            continue
        out.append([round(x0, 1), round(y0, 1), round(x1, 1), round(y1, 1), round(score, 3), int(cls)])
    return out


def label_video(path, every_s, broadcast, models, device):
    """Samples the video and runs every model on each frame kept. Returns the frame size and the per-frame boxes."""
    capture = cv2.VideoCapture(str(path))
    fps = capture.get(cv2.CAP_PROP_FPS) or 30.0
    capture.release()
    step = max(1, int(round(every_s * fps)))
    frames, size, last_small = [], None, None
    for index, decode in video_frames(path):
        if index % step:
            continue
        frame = decode()
        height, width = frame.shape[:2]
        size = [width, height]
        small = cv2.resize(cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY), (64, 36), interpolation=cv2.INTER_AREA).astype(np.float32)
        if last_small is not None and float(np.abs(small - last_small).mean()) < SAME_FRAME_DIFF:
            continue
        last_small = small
        if broadcast:
            grey_out_broadcast(frame)
        frames.append({"index": index, "boxes": teacher_boxes(frame, models, device)})
    return size, frames


def teacher_boxes(frame, models, device):
    """Every box each teacher gives one frame above its floor, by teacher name, in frame pixels."""
    width = frame.shape[1]
    found = {}
    sunxds_size = min(2048, int(round(1.25 * width / 32)) * 32)
    found["sunxds"] = boxes_of(models["sunxds"].predict(frame, imgsz=sunxds_size, augment=True, conf=MIN_SCORE, iou=0.6,
                                                        device=device, verbose=False)[0])
    found["coco"] = boxes_of(models["coco"].predict(frame, imgsz=min(960, int(round(width / 32)) * 32), conf=0.15,
                                                    classes=[0], device=device, verbose=False)[0])
    student_size = min(1920, int(round(width / 32)) * 32)
    for name in ("gen2", "sunft"):
        found[name] = boxes_of(models[name].predict(frame, imgsz=student_size, augment=True, conf=MIN_SCORE,
                                                    device=device, verbose=False)[0])
    return found


def label_pc_set(root, folder, sunxds, device):
    """sunxds's heads on every training and validation picture of one PC set, by "<split>/<picture name>"."""
    heads = {}
    for source_split, _, path in pc_pictures(root, folder):
        result = sunxds.predict(read_pc_picture(path), imgsz=1280, augment=True, conf=PC_HEAD_SCORE, device=device,
                                verbose=False)[0]
        heads[f"{source_split}/{path.stem}"] = boxes_of(result, keep_classes={1})
    return heads


def main():
    """Labels every listed video and PC set that has no cache file yet."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    for name in ("videos", "pc", "cache", "sunxds", "coco", "gen2", "sunft"):
        parser.add_argument(f"--{name}", required=True)
    parser.add_argument("--device", default=0)
    args = parser.parse_args()
    cache = pathlib.Path(args.cache)
    cache.mkdir(parents=True, exist_ok=True)
    models = {name: YOLO(getattr(args, name)) for name in ("sunxds", "coco", "gen2", "sunft")}
    for game, video, split, every_s, broadcast in VIDEOS:
        out = cache / f"{game}_{video}.json"
        if out.exists():
            continue
        path = pathlib.Path(args.videos) / game / f"{video}.mp4"
        if not path.is_file():
            raise SystemExit(f"{path} is missing; see docs/Training.md for where the videos come from")
        start = time.time()
        size, frames = label_video(path, every_s, broadcast, models, args.device)
        record = {"game": game, "video": video, "split": split, "broadcast": broadcast, "every_s": every_s, "size": size,
                  "frames": frames}
        out.write_text(json.dumps(record))
        print(f"{game} {video}: {len(frames)} frames in {time.time() - start:.0f} s", flush=True)
    for folder, mapping in PC_SETS:
        out = cache / f"pc_{folder}.json"
        if has_heads(mapping) or out.exists():
            continue
        start = time.time()
        heads = label_pc_set(pathlib.Path(args.pc), folder, models["sunxds"], args.device)
        out.write_text(json.dumps(heads))
        print(f"pc {folder}: {len(heads)} pictures in {time.time() - start:.0f} s", flush=True)


if __name__ == "__main__":
    main()
