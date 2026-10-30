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
import pathlib
import sys

import cv2
import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).parent))   # python -I leaves the script's folder off the path
from app_detector import AppDetector, iou  # noqa: E402

SETS = {
    "csgo": ("csgo-videogame", {0, 1}),                   # CT, T
    "apex": ("apex-videogame", {0}),                      # avatar
    "halo": ("halo-infinite-angel-videogame", {0, 2}),    # enemy, friendly
    "farcry6": ("farcry6-videogame", {0, 9}),             # assassin, person
}
SQUARE = 0.6
SHIFTS = (0.0, -0.25, 0.25)   # square centre, as a share of the picture width from its middle


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
