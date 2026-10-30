"""Scores exported models on the human-labelled test splits of the four PC sets, in the app's view.

Each test picture is put back to 16:9 (the sets were stretched to 640 x 640), and the app's square is cut round its
centre and at two places beside it, 0.6 of the picture height on a side. The model sees the square as the app feeds
it (bilinear resize to its input, RGB, 0..1) and its output is decoded as the app decodes it: for each anchor its best
class, a minimum score, then per-class merging of boxes that overlap by more than 0.5; classes whose role is ignore
are dropped. Labelled people whose box lies at least half inside the square are the ones to find.

Printed for each model and set: people found at IoU 0.5 or more at the app's default score (0.5) and at 0.3, other
boxes per square at those scores, and the average precision at IoU 0.5 over all scores (AP50).

Usage: score_pc_tests.py <folder with the PC sets> <model folder> [<model folder> ...]
"""
import json
import pathlib
import sys

import cv2
import ncnn
import numpy as np

SETS = {
    "csgo": ("csgo-videogame", {0, 1}),                   # CT, T
    "apex": ("apex-videogame", {0}),                      # avatar
    "halo": ("halo-infinite-angel-videogame", {0, 2}),    # enemy, friendly
    "farcry6": ("farcry6-videogame", {0, 9}),             # assassin, person
}
SQUARE = 0.6
SHIFTS = (0.0, -0.25, 0.25)   # square centre, as a share of the picture width from its middle


class AppDetector:
    """An exported model run and decoded the way the app runs and decodes it."""

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

    def detect(self, bgr_square, min_score=0.05, merge_iou=0.5):
        """Body boxes as (x0, y0, x1, y1, score) in fractions of the square, strongest first."""
        rgb = cv2.cvtColor(cv2.resize(bgr_square, (self.size, self.size), interpolation=cv2.INTER_LINEAR), cv2.COLOR_BGR2RGB)
        mat = ncnn.Mat.from_pixels(np.ascontiguousarray(rgb), ncnn.Mat.PixelType.PIXEL_RGB, self.size, self.size)
        mat.substract_mean_normalize([0, 0, 0], [1 / 255.0] * 3)
        extractor = self.net.create_extractor()
        extractor.input("in0", mat)
        _, out = extractor.extract("out0")
        out = np.array(out)
        if out.ndim == 3:
            out = out[0]
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


def squares_of(image_path, person_classes):
    """The three app squares of one test picture, each with the labelled people at least half inside it."""
    image = cv2.imread(str(image_path))
    height = image.shape[0]
    width = int(round(height * 16 / 9))
    image = cv2.resize(image, (width, height), interpolation=cv2.INTER_LINEAR)
    people = []
    label = image_path.parent.parent / "labels" / f"{image_path.stem}.txt"
    for line in label.read_text().splitlines() if label.exists() else []:
        parts = line.split()
        if len(parts) == 5 and int(parts[0]) in person_classes:
            cx, cy, w, h = (float(v) for v in parts[1:])
            people.append(((cx - w / 2) * width, (cy - h / 2) * height, (cx + w / 2) * width, (cy + h / 2) * height))
    side = int(SQUARE * height)
    for shift in SHIFTS:
        x0 = int(min(max(width / 2 + shift * width - side / 2, 0), width - side))
        y0 = (height - side) // 2
        inside = []
        for (a, b, c, d) in people:
            ca, cb, cc, cd = max(a, x0), max(b, y0), min(c, x0 + side), min(d, y0 + side)
            if cc > ca and cd > cb and (cc - ca) * (cd - cb) >= 0.5 * (c - a) * (d - b):
                inside.append(((ca - x0) / side, (cb - y0) / side, (cc - x0) / side, (cd - y0) / side))
        yield image[y0:y0 + side, x0:x0 + side], inside


def score(detector, root, person_classes):
    """People, squares, (found share, other boxes per square) at 0.5 and at 0.3, and AP50 on one set."""
    records = []   # (score, matched a person) for every body box
    truth_total = 0
    squares = 0
    for image_path in sorted((root / "test" / "images").glob("*.jpg")):
        for square, truth in squares_of(image_path, person_classes):
            squares += 1
            truth_total += len(truth)
            taken = [False] * len(truth)
            for box in detector.detect(square):   # strongest first
                best, best_iou = -1, 0.5
                for k, person in enumerate(truth):
                    overlap = iou(box, person)
                    if not taken[k] and overlap >= best_iou:
                        best, best_iou = k, overlap
                if best >= 0:
                    taken[best] = True
                records.append((box[4], best >= 0))
    records.sort(key=lambda r: -r[0])
    hits = np.cumsum([r[1] for r in records]) if records else np.array([])
    precision = hits / np.arange(1, len(records) + 1) if records else np.array([])
    recall = hits / max(truth_total, 1) if records else np.array([])
    ap = 0.0
    for level in np.linspace(0, 1, 101):
        reached = precision[recall >= level] if records else []
        ap += (reached.max() if len(reached) else 0.0) / 101

    def at(threshold):
        kept = [r for r in records if r[0] >= threshold]
        found = sum(r[1] for r in kept)
        return found / max(truth_total, 1), (len(kept) - found) / max(squares, 1)

    return truth_total, squares, at(0.5), at(0.3), ap


def main():
    """Scores every model folder given on every set."""
    data_root = pathlib.Path(sys.argv[1])
    for name, (folder, classes) in SETS.items():
        for model_folder in sys.argv[2:]:
            people, squares, (r50, f50), (r30, f30), ap = score(AppDetector(model_folder), data_root / folder, classes)
            print(f"{name:8s} {pathlib.Path(model_folder).name:18s} people {people:5d} squares {squares:5d} | "
                  f">=0.5 found {100 * r50:5.1f}% other/sq {f50:.3f} | >=0.3 found {100 * r30:5.1f}% other/sq {f30:.3f} | "
                  f"AP50 {100 * ap:5.1f}", flush=True)


if __name__ == "__main__":
    main()
