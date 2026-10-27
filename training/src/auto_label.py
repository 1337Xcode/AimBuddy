#!/usr/bin/env python3
"""
Auto-label gameplay frames with a teacher detector.

Two kinds of teacher can label the frames:

- Ultralytics weights with a "person" class (default: yolov8x.pt, COCO id 0). The teacher runs
  once per frame at a much higher input resolution than the runtime model, so small / distant
  targets get labeled.
- A game model exported to ONNX, such as the community models of the Aimmy V2 repository
  (--teacher-onnx). Those are trained on one game and know what its enemies look like, and
  what its own weapon and HUD are not. They are applied to a square of the screen at its own
  pixel size, so a frame is covered with overlapping tiles.

Detections are filtered, NMS'd, and written as YOLO-format labels with our single-class id
(0 = enemy).

Why use a teacher instead of labeling by hand:
- yolov8x at imgsz=1280 recovers distant / partially-occluded humans
  the runtime model would miss outright.
- 95% of frames are labeled without human review; you only need to
  spot-check and prune false positives (vehicles, posters, billboards).
- After the first training pass, run this again with `--teacher` set
  to your trained best.pt at imgsz=640 to harvest more in-game data.
  The student bootstraps the next-generation teacher (self-distillation).

Usage:
    python src/auto_label.py --in raw_frames --out dataset/train \
        --teacher yolov8x.pt --imgsz 1280 --conf 0.30

    python src/auto_label.py --in raw_frames --out dataset/train \
        --teacher-onnx "path/to/Your Game.onnx" --conf 0.40 --write-negatives

The output mirrors YOLO dataset layout:
    out/images/<name>.jpg
    out/labels/<name>.txt
"""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path
from typing import Iterable, Protocol

import cv2
import numpy as np

IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}

# COCO id for "person". Most off-the-shelf YOLO weights ship with this
# class at index 0; we remap to our single-class enemy id (also 0).
COCO_PERSON_ID = 0

# Input size assumed for an ONNX model that does not state one.
DEFAULT_TILE = 640

# What the padding of a frame smaller than one tile is filled with (the grey Ultralytics pads with).
PAD_VALUE = 114

# A box this close to a tile's border, where another tile carries on, is cut by the border and is not a whole person.
CUT_MARGIN_PX = 3

# A cut box is dropped when this share of it lies inside another box.
CUT_CONTAINED_SHARE = 0.7


class Teacher(Protocol):
    """Finds the enemies in one frame."""

    def detect(self, image_path: Path) -> tuple[np.ndarray, int, int]:
        """Returns the boxes as an (N, 4) array of x1, y1, x2, y2 in pixels, and the frame's width and height."""


class UltralyticsTeacher:
    """Ultralytics weights run on the whole frame at one input size."""

    def __init__(self, weights: Path, imgsz: int, conf: float, iou: float, keep_class_ids: list[int], device: str) -> None:
        try:
            from ultralytics import YOLO
        except ImportError:
            print("ERROR: ultralytics not installed. Run scripts/01_setup_environment.bat first.")
            raise SystemExit(2)
        if not weights.exists():
            # Let Ultralytics auto-download if it's a known model name.
            if weights.name in {
                "yolov8x.pt", "yolov8l.pt", "yolov8m.pt",
                "yolo11x.pt", "yolo11l.pt", "yolo11m.pt",
                "yolov10x.pt", "yolov10l.pt",
            }:
                print(f"Teacher not found locally; ultralytics will download {weights.name}")
            else:
                print(f"ERROR: teacher weights not found: {weights}")
                raise SystemExit(2)
        self._model = YOLO(str(weights))
        self._imgsz = imgsz
        self._conf = conf
        self._iou = iou
        self._keep = keep_class_ids
        self._device = device

    def detect(self, image_path: Path) -> tuple[np.ndarray, int, int]:
        results = self._model.predict(
            source=str(image_path), imgsz=self._imgsz, conf=self._conf, iou=self._iou, device=self._device, verbose=False,
        )
        if not results:
            return np.zeros((0, 4), np.float32), 0, 0
        result = results[0]
        height, width = result.orig_shape[:2]
        boxes = result.boxes
        if boxes is None or len(boxes) == 0:
            return np.zeros((0, 4), np.float32), width, height
        xyxy = boxes.xyxy.cpu().numpy()
        classes = boxes.cls.cpu().numpy().astype(int)
        keep = np.isin(classes, self._keep)
        return xyxy[keep].astype(np.float32), width, height


class OnnxTileTeacher:
    """
    A detector exported to ONNX (input [1, 3, S, S], output [1, 4 + classes, anchors] as an
    Ultralytics export gives), run over a frame in overlapping S by S tiles at the pixel scale
    the model was trained on. A box seen in two tiles is kept once.
    """

    def __init__(self, path: Path, conf: float, iou: float, keep_class_ids: list[int], scale: float, tile: int | None, device: str) -> None:
        if not path.is_file():
            print(f"ERROR: teacher model not found: {path}")
            raise SystemExit(2)
        import onnxruntime

        available = onnxruntime.get_available_providers()
        providers = [name for name in ("CUDAExecutionProvider", "CPUExecutionProvider") if name in available]
        if device == "cpu":
            providers = ["CPUExecutionProvider"]
        self._session = onnxruntime.InferenceSession(str(path), providers=providers)
        shape = self._session.get_inputs()[0].shape
        stated = shape[2] if len(shape) == 4 and isinstance(shape[2], int) else None
        self._tile = tile or stated or DEFAULT_TILE
        if stated is not None and self._tile != stated:
            print(f"ERROR: this model takes {stated} by {stated} pixels, not {self._tile}")
            raise SystemExit(2)
        self._input = self._session.get_inputs()[0].name
        self._conf = conf
        self._iou = iou
        self._keep = keep_class_ids
        self._scale = scale

    @staticmethod
    def _origins(length: int, tile: int) -> list[int]:
        """Where tiles start along one axis: half a tile apart, the last one flush with the end."""
        if length <= tile:
            return [0]
        origins = list(range(0, length - tile + 1, tile // 2))
        if origins[-1] != length - tile:
            origins.append(length - tile)
        return origins

    def _decode(self, output: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
        """Boxes as x1, y1, x2, y2 in tile pixels and their scores, for the anchors above the confidence."""
        data = output[0]
        if data.shape[0] >= data.shape[1]:
            print(f"ERROR: the model's output is {tuple(data.shape)}; only [1, 4 + classes, anchors] exports are supported")
            raise SystemExit(2)
        classes = data[4:]
        if classes.shape[0] < 1 or max(self._keep) >= classes.shape[0]:
            print(f"ERROR: the model has {classes.shape[0]} classes and class {max(self._keep)} was asked for")
            raise SystemExit(2)
        scores = classes[self._keep].max(axis=0)
        keep = scores >= self._conf
        centre_x, centre_y, width, height = data[0][keep], data[1][keep], data[2][keep], data[3][keep]
        boxes = np.stack([centre_x - width / 2, centre_y - height / 2, centre_x + width / 2, centre_y + height / 2], axis=1)
        return boxes.astype(np.float32), scores[keep].astype(np.float32)

    def detect(self, image_path: Path) -> tuple[np.ndarray, int, int]:
        image = cv2.imread(str(image_path))
        if image is None:
            print(f"WARNING: could not read {image_path}; skipped")
            return np.zeros((0, 4), np.float32), 0, 0
        original_height, original_width = image.shape[:2]
        if self._scale != 1.0:
            image = cv2.resize(image, None, fx=self._scale, fy=self._scale, interpolation=cv2.INTER_AREA if self._scale < 1 else cv2.INTER_CUBIC)
        height, width = image.shape[:2]
        rgb = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
        if height < self._tile or width < self._tile:
            rgb = cv2.copyMakeBorder(rgb, 0, max(self._tile - height, 0), 0, max(self._tile - width, 0), cv2.BORDER_CONSTANT, value=(PAD_VALUE,) * 3)
        padded_height, padded_width = rgb.shape[:2]

        all_boxes: list[np.ndarray] = []
        all_scores: list[np.ndarray] = []
        all_cut: list[np.ndarray] = []
        for top in self._origins(padded_height, self._tile):
            for left in self._origins(padded_width, self._tile):
                tile = rgb[top:top + self._tile, left:left + self._tile]
                tensor = np.ascontiguousarray(tile.transpose(2, 0, 1)[None].astype(np.float32) / 255.0)
                output = self._session.run(None, {self._input: tensor})[0]
                boxes, scores = self._decode(output)
                if len(boxes):
                    edge = self._tile - CUT_MARGIN_PX
                    all_cut.append(
                        ((boxes[:, 0] <= CUT_MARGIN_PX) & (left > 0))
                        | ((boxes[:, 1] <= CUT_MARGIN_PX) & (top > 0))
                        | ((boxes[:, 2] >= edge) & (left + self._tile < padded_width))
                        | ((boxes[:, 3] >= edge) & (top + self._tile < padded_height))
                    )
                    boxes[:, [0, 2]] += left
                    boxes[:, [1, 3]] += top
                    all_boxes.append(boxes)
                    all_scores.append(scores)
        if not all_boxes:
            return np.zeros((0, 4), np.float32), original_width, original_height
        boxes = np.concatenate(all_boxes)
        scores = np.concatenate(all_scores)
        cut = np.concatenate(all_cut)
        as_xywh = [[float(b[0]), float(b[1]), float(b[2] - b[0]), float(b[3] - b[1])] for b in boxes]
        kept = np.array(cv2.dnn.NMSBoxes(as_xywh, scores.tolist(), self._conf, self._iou), dtype=int).reshape(-1)
        boxes, cut = boxes[kept], cut[kept]
        # A person on the border between two tiles is whole in one of them and cut in the other. The cut
        # part overlaps the whole box too little for NMS to join them, so it is dropped when it lies inside another box.
        areas = (boxes[:, 2] - boxes[:, 0]) * (boxes[:, 3] - boxes[:, 1])
        keep_box = np.ones(len(boxes), dtype=bool)
        for i in np.nonzero(cut)[0]:
            inside = np.minimum(boxes[:, 2], boxes[i, 2]) - np.maximum(boxes[:, 0], boxes[i, 0])
            across = np.minimum(boxes[:, 3], boxes[i, 3]) - np.maximum(boxes[:, 1], boxes[i, 1])
            shared = np.clip(inside, 0, None) * np.clip(across, 0, None)
            shared[i] = 0.0
            if areas[i] > 0 and shared.max() / areas[i] >= CUT_CONTAINED_SHARE:
                keep_box[i] = False
        boxes = boxes[keep_box] / self._scale
        boxes[:, [0, 2]] = boxes[:, [0, 2]].clip(0, original_width)
        boxes[:, [1, 3]] = boxes[:, [1, 3]].clip(0, original_height)
        return boxes.astype(np.float32), original_width, original_height


def _iter_images(root: Path) -> Iterable[Path]:
    for path in sorted(root.rglob("*")):
        if path.is_file() and path.suffix.lower() in IMAGE_EXTS:
            yield path


def _normalize_box(xyxy, img_w: int, img_h: int) -> tuple[float, float, float, float] | None:
    """xyxy in pixel coords -> YOLO (cx, cy, w, h) normalized."""
    x1, y1, x2, y2 = xyxy
    x1 = max(0.0, float(x1))
    y1 = max(0.0, float(y1))
    x2 = min(float(img_w), float(x2))
    y2 = min(float(img_h), float(y2))
    if x2 <= x1 or y2 <= y1:
        return None
    cx = ((x1 + x2) / 2.0) / img_w
    cy = ((y1 + y2) / 2.0) / img_h
    w = (x2 - x1) / img_w
    h = (y2 - y1) / img_h
    if w <= 0 or h <= 0:
        return None
    return cx, cy, w, h


def label_frames(
    frames_dir: Path,
    out_dir: Path,
    teacher: Teacher,
    write_negatives: bool,
    min_box_area_frac: float,
    max_box_area_frac: float,
) -> int:
    """Labels every frame in `frames_dir` with `teacher` and writes the dataset split to `out_dir`."""
    images_out = out_dir / "images"
    labels_out = out_dir / "labels"
    images_out.mkdir(parents=True, exist_ok=True)
    labels_out.mkdir(parents=True, exist_ok=True)

    total_frames = 0
    total_labels = 0
    skipped_no_detection = 0

    for img_path in _iter_images(frames_dir):
        total_frames += 1

        xyxy, img_w, img_h = teacher.detect(img_path)
        if img_w == 0 or img_h == 0:
            continue

        keep_lines: list[str] = []
        for box in xyxy:
            norm = _normalize_box(box, img_w, img_h)
            if norm is None:
                continue
            cx, cy, w, h = norm
            area = w * h
            if area < min_box_area_frac:
                # tiny detections at this scale are almost always noise on
                # in-game UI (kill feed avatars, minimap icons, etc.)
                continue
            if area > max_box_area_frac:
                # huge boxes are almost always background players or HUD elements
                continue
            # remap to our single-class enemy id (0)
            keep_lines.append(f"0 {cx:.6f} {cy:.6f} {w:.6f} {h:.6f}")

        if not keep_lines and not write_negatives:
            skipped_no_detection += 1
            continue

        out_image_path = images_out / img_path.name
        if not out_image_path.exists():
            shutil.copy2(img_path, out_image_path)

        label_path = labels_out / (img_path.stem + ".txt")
        label_path.write_text("\n".join(keep_lines), encoding="utf-8")
        total_labels += len(keep_lines)

    print("=" * 60)
    print("Auto-label summary")
    print(f"  Frames scanned          : {total_frames}")
    print(f"  Frames with detections  : {total_frames - skipped_no_detection}")
    print(f"  Frames skipped (no det) : {skipped_no_detection}")
    print(f"  Total enemy labels      : {total_labels}")
    print(f"  Output images           : {images_out}")
    print(f"  Output labels           : {labels_out}")
    print()
    print("Next steps:")
    print("  1. Spot-check labels in Roboflow / labelImg  -  the teacher will")
    print("     occasionally box vehicles, posters, NPCs. Delete those.")
    print("  2. Run scripts/03_validate_dataset.bat to verify format.")
    print("  3. Train.")
    return 0


def auto_label(
    frames_dir: Path,
    out_dir: Path,
    teacher_weights: Path,
    imgsz: int,
    conf_threshold: float,
    iou_threshold: float,
    keep_class_ids: list[int],
    device: str,
    write_negatives: bool,
    min_box_area_frac: float,
    max_box_area_frac: float,
) -> int:
    """Labels the frames with Ultralytics weights as the teacher."""
    if not frames_dir.exists():
        print(f"ERROR: input frames directory missing: {frames_dir}")
        return 2
    teacher = UltralyticsTeacher(teacher_weights, imgsz, conf_threshold, iou_threshold, keep_class_ids, device)
    return label_frames(frames_dir, out_dir, teacher, write_negatives, min_box_area_frac, max_box_area_frac)


def main() -> int:
    parser = argparse.ArgumentParser(description="Auto-label gameplay frames with a teacher detector")
    parser.add_argument("--in", dest="frames_dir", type=Path, required=True, help="Input directory of frames")
    parser.add_argument("--out", dest="out_dir", type=Path, required=True, help="Output dataset split directory (e.g. dataset/train)")
    teacher_group = parser.add_mutually_exclusive_group()
    teacher_group.add_argument("--teacher", type=Path, default=None, help="Teacher model weights (default: yolov8x.pt  -  auto-downloads)")
    teacher_group.add_argument("--teacher-onnx", type=Path, default=None, help="A game model exported to ONNX, such as a community model from the Aimmy V2 repository")
    parser.add_argument("--imgsz", type=int, default=1280, help="Inference image size for the teacher (default: 1280 for small targets); not used with --teacher-onnx")
    parser.add_argument("--conf", type=float, default=0.30, help="Confidence threshold (default: 0.30; lower = more recall)")
    parser.add_argument("--iou", type=float, default=0.55, help="NMS IoU threshold (default: 0.55)")
    parser.add_argument("--classes", type=int, nargs="+", default=[COCO_PERSON_ID], help="Class ids in the teacher to keep (default: [0] = person)")
    parser.add_argument("--device", type=str, default="auto", help="cuda device id, 'cpu', or 'auto'")
    parser.add_argument("--onnx-scale", type=float, default=1.0, help="With --teacher-onnx: resize each frame by this factor before cutting tiles, for recordings of another size than the game the model was trained on (default: 1.0)")
    parser.add_argument("--onnx-tile", type=int, default=None, help="With --teacher-onnx: tile size in pixels (default: the model's own input size)")
    parser.add_argument("--write-negatives", action="store_true", help="Also copy frames that produced zero detections (with empty labels)")
    parser.add_argument("--min-box-area-frac", type=float, default=0.0005, help="Drop boxes smaller than this fraction of image area")
    parser.add_argument("--max-box-area-frac", type=float, default=0.85, help="Drop boxes larger than this fraction of image area")
    args = parser.parse_args()

    device = args.device
    if device == "auto":
        try:
            import torch  # type: ignore
            device = 0 if torch.cuda.is_available() else "cpu"
        except Exception:
            device = "cpu"

    if args.teacher_onnx is None:
        return auto_label(
            frames_dir=args.frames_dir,
            out_dir=args.out_dir,
            teacher_weights=args.teacher or Path("yolov8x.pt"),
            imgsz=args.imgsz,
            conf_threshold=args.conf,
            iou_threshold=args.iou,
            keep_class_ids=args.classes,
            device=device,
            write_negatives=args.write_negatives,
            min_box_area_frac=args.min_box_area_frac,
            max_box_area_frac=args.max_box_area_frac,
        )
    if not args.frames_dir.exists():
        print(f"ERROR: input frames directory missing: {args.frames_dir}")
        return 2
    teacher = OnnxTileTeacher(args.teacher_onnx, args.conf, args.iou, args.classes, args.onnx_scale, args.onnx_tile, str(device))
    return label_frames(args.frames_dir, args.out_dir, teacher, args.write_negatives, args.min_box_area_frac, args.max_box_area_frac)


if __name__ == "__main__":
    raise SystemExit(main())
