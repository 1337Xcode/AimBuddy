"""An exported model run the way the app runs it: the square resized to the model's input with bilinear filtering,
RGB, scaled to 0..1, through NCNN on the processor. raw() gives the output tensor, for the app's own decoder
(clip_replay.cpp reads it). detect() decodes it as the app does: for each anchor its best class, a minimum score,
then per-class merging of boxes that overlap by more than 0.5; classes whose role is ignore are dropped.
"""
import json
import pathlib

import cv2
import ncnn
import numpy as np


class AppDetector:
    """An exported model (model.param, model.bin, model.json) and how the app feeds it."""

    def __init__(self, folder, threads=4):
        folder = pathlib.Path(folder)
        self.net = ncnn.Net()
        self.net.opt.use_vulkan_compute = False
        self.net.opt.num_threads = threads
        self.net.load_param(str(folder / "model.param"))
        self.net.load_model(str(folder / "model.bin"))
        profile = json.loads((folder / "model.json").read_text(encoding="utf-8"))
        self.size = int(profile["inputSize"])
        self.roles = profile.get("roles", ["body"])
        self.end_to_end = bool(profile.get("endToEnd", False))

    def raw(self, bgr_square):
        """The network's output on a square of any size, as a 2-D float32 array (rows by columns, as NCNN gives it)."""
        rgb = cv2.cvtColor(cv2.resize(bgr_square, (self.size, self.size), interpolation=cv2.INTER_LINEAR), cv2.COLOR_BGR2RGB)
        mat = ncnn.Mat.from_pixels(np.ascontiguousarray(rgb), ncnn.Mat.PixelType.PIXEL_RGB, self.size, self.size)
        mat.substract_mean_normalize([0, 0, 0], [1 / 255.0] * 3)
        extractor = self.net.create_extractor()
        extractor.input("in0", mat)
        _, out = extractor.extract("out0")
        out = np.array(out, dtype=np.float32)
        return out[0] if out.ndim == 3 else out

    def detect(self, bgr_square, min_score=0.05, merge_iou=0.5):
        """Body boxes as (x0, y0, x1, y1, score) in fractions of the square, strongest first."""
        out = self.raw(bgr_square)
        if out.shape[0] < out.shape[1]:   # channels first: (4 + classes) x anchors
            boxes, scores = out[:4].T, out[4:].T
        else:
            boxes, scores = out[:, :4], out[:, 4:]
        best = scores.argmax(axis=1)
        best_score = scores[np.arange(len(best)), best]
        scale = self.size if np.abs(boxes).max() <= 2.0 else 1.0   # some exports give fractions of the input
        found = []
        for i in np.argsort(-best_score):
            score = float(best_score[i])
            if score < min_score:
                break
            role = self.roles[best[i]] if best[i] < len(self.roles) else "ignore"
            if role != "body":
                continue
            cx, cy, w, h = boxes[i] * scale
            x0, y0 = max(0.0, cx - w / 2), max(0.0, cy - h / 2)
            x1, y1 = min(self.size, cx + w / 2), min(self.size, cy + h / 2)
            if x1 - x0 < 2 or y1 - y0 < 2:
                continue
            box = (x0 / self.size, y0 / self.size, x1 / self.size, y1 / self.size, score)
            if all(iou(kept, box) <= merge_iou for kept in found):
                found.append(box)
        return found


def iou(a, b):
    """Intersection over union of two (x0, y0, x1, y1, ...) boxes."""
    w = min(a[2], b[2]) - max(a[0], b[0])
    h = min(a[3], b[3]) - max(a[1], b[1])
    if w <= 0 or h <= 0:
        return 0.0
    inter = w * h
    return inter / ((a[2] - a[0]) * (a[3] - a[1]) + (b[2] - b[0]) * (b[3] - b[1]) - inter)
