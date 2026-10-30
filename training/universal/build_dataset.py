"""Builds the universal training set: squares cut the way the app cuts them, labelled enemy body, enemy head and own character.

Every source is brought to one form, so the model learns one thing from all of them:
  - The picture is a square round the screen centre, 0.44 to 0.78 of the frame height on a side (what the app reads,
    from its smallest detection zone to its largest on a 720 px tall capture), resized to 320 px.
  - Classes: 0 body (an enemy or any other player), 1 head (of such a body), 2 self (the player's own character in a
    third-person game). The app ignores class 2, so the model can say "that is you" instead of "that is a target".
  - What no source can vouch for (a person only one teacher half believes in, a box cut down to a sliver by the
    square's edge) is painted grey, so it is taught neither as a target nor as background.
  - Broadcast overlays (team lists, player cameras, scoreboards) are painted grey before anything looks at the frame,
    and a PUBG Mobile square that still shows a face on a broadcast panel is left out.
  - Frames that look like the one before are skipped, and only some empty squares are kept, so near copies and
    empty scenery do not outweigh the people.
Videos and PC datasets are split by source: the validation videos are never trained on, and the test videos used
for the judged comparison are not read at all.
"""
import argparse
import json
import pathlib
import random
import sys

import cv2
import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from teachers import Box, FrameLabels, Teachers  # noqa: E402

OUTPUT_SIZE = 320
SIDE_RANGE = (0.44, 0.78)       # square side as a share of the frame height
CENTRE_JITTER = (0.04, 0.03)    # the first square of a frame: this far from the centre at most (share of width, height)
ASIDE_JITTER = (0.18, 0.08)     # the second one, as if the player looked a little aside
BROADCAST_ASIDE = (0.10, 0.05)  # broadcasts keep to the middle, away from their overlays
MIN_VISIBLE_BODY = 0.35         # a body cut by the square keeps its label if this much of it is inside
MIN_VISIBLE_HEAD = 0.60
MIN_VISIBLE_SELF = 0.15
KEEP_EMPTY = 0.35               # share of squares with nobody in them that are kept
FACE_MAX_ASPECT = 1.3           # a labelled person no taller than this many widths...
FACE_MIN_HEAD = 0.35            # ...whose head is over this share of its height is a face on a broadcast panel
SAME_FRAME_DIFF = 3.0           # mean grey difference (0..255) below which a frame repeats the last one kept
GREY = (114, 114, 114)

# Where broadcast overlays sit, as shares of the frame: top, bottom, left, right.
BROADCAST_BANDS = (0.06, 0.15, 0.14, 0.14)

# game, video, split, seconds between frames, broadcast
VIDEOS = [
    ("freefire", "5mJrCitRC_w", "train", 3.0, False),
    ("freefire", "6s5w3MWmqSQ", "train", 3.0, False),
    ("freefire", "Cm2r7uPPeuk", "train", 3.0, False),
    ("freefire", "F9d35_J7I3I", "train", 3.0, False),
    ("freefire", "IfGyARltkSs", "train", 3.0, False),
    ("freefire", "P_itov3W6l4", "train", 3.0, False),
    ("freefire", "Sk0TAusKxHc", "train", 3.0, False),
    ("freefire", "Y5xb6QiUsbs", "train", 3.0, False),
    ("freefire", "f-4M7Ff0hxE", "train", 3.0, False),
    ("freefire", "jmvmum3eurI", "train", 3.0, False),
    ("freefire", "tmIOqcqS9kQ", "train", 3.0, False),
    ("freefire", "wdil2Ny12Aw", "train", 3.0, False),
    ("freefire", "6XhS2eiXu1c", "val", 3.0, False),
    ("freefire", "O5fHr3-GhP0", "val", 3.0, False),
] + [("pubgm", f"chunk_{i:03d}", "train", 0.75, True) for i in range(9)] + [
    ("pubgm", "chunk_009", "val", 0.75, True),
] + [("pubgm", name, "train", 0.4, True) for name in
     ("highlight", "highlight2", "highlight4", "highlight5", "highlight6", "highlight8", "highlight10", "highlight11",
      "highlight13", "highlight17", "hightlight16")] + [
    ("pubgm", "highlight19", "train", 0.4, False),   # a phone recording, no broadcast overlay
    ("pubgm", "highlight9", "val", 0.4, True),
    ("pubgm", "highlight14", "val", 0.4, True),
]

# PC game sets (CC BY 4.0): folder, class name to our class (None: not a person, left as it is), split map.
PC_SETS = [
    ("csgo-videogame", {"CT": 0, "T": 0}),
    ("apex-videogame", {"avatar": 0, "object": None}),
    ("halo-infinite-angel-videogame", {"enemy": 0, "friendly": 0, "enemy-head": 1, "friendly-head": 1}),
    ("farcry6-videogame", {"person": 0, "assassin": 0}),
]
PC_SPLITS = {"train": "train", "valid": "val", "test": None}


def grey_out_broadcast(frame):
    """Paints the bands where broadcasts put their overlays grey."""
    height, width = frame.shape[:2]
    top, bottom, left, right = BROADCAST_BANDS
    frame[: int(top * height)] = GREY
    frame[int((1 - bottom) * height):] = GREY
    frame[:, : int(left * width)] = GREY
    frame[:, int((1 - right) * width):] = GREY


def square_at(width, height, jitter, rng):
    """A square of the app's size near the frame centre: (x0, y0, side) in frame pixels."""
    side = min(int(rng.uniform(*SIDE_RANGE) * height), width, height)
    cx = width / 2 + rng.uniform(-jitter[0], jitter[0]) * width
    cy = height / 2 + rng.uniform(-jitter[1], jitter[1]) * height
    x0 = int(min(max(cx - side / 2, 0), width - side))
    y0 = int(min(max(cy - side / 2, 0), height - side))
    return x0, y0, side


def clip_to(box, x0, y0, side):
    """The part of a box inside the square, in square pixels, and the share of the box it is."""
    cx0, cy0 = max(box.x0, x0), max(box.y0, y0)
    cx1, cy1 = min(box.x1, x0 + side), min(box.y1, y0 + side)
    if cx1 <= cx0 or cy1 <= cy0:
        return None, 0.0
    visible = (cx1 - cx0) * (cy1 - cy0) / max((box.x1 - box.x0) * (box.y1 - box.y0), 1e-6)
    return (cx0 - x0, cy0 - y0, cx1 - x0, cy1 - y0), visible


def write_square(frame, square, labels, out_images, out_labels, name, rng, drop_faces=False):
    """Cuts one square, paints what cannot be vouched for, writes the picture and its labels.

    Returns the number of labels written, or -1 when the square is left out.
    """
    enemies, heads, own, unsure = labels.enemies, labels.heads, labels.own, labels.unsure
    x0, y0, side = square
    crop = frame[y0:y0 + side, x0:x0 + side].copy()
    lines = []
    kept_boxes = []
    for cls, boxes, needed in ((0, enemies, MIN_VISIBLE_BODY), (2, own, MIN_VISIBLE_SELF)):
        for box in boxes:
            part, visible = clip_to(box, x0, y0, side)
            if part is None:
                continue
            if visible >= needed:
                lines.append((cls, part))
                kept_boxes.append(Box(*part, 1.0))
            elif cls == 0:
                paint(crop, part)
    body_parts = [p for c, p in lines if c == 0]
    for head in heads:
        part, visible = clip_to(head, x0, y0, side)
        if part is not None and visible >= MIN_VISIBLE_HEAD and any(inside(part, b) for b in body_parts):
            lines.append((1, part))
    for box in unsure:
        part, _ = clip_to(box, x0, y0, side)
        if part is not None and not any(Box(*part, 1.0).iou(k) > 0.3 for k in kept_boxes):
            paint(crop, part)
    if drop_faces and shows_face(lines):
        return -1
    if not lines and rng.random() > KEEP_EMPTY:
        return -1
    interpolation = cv2.INTER_AREA if side > OUTPUT_SIZE else cv2.INTER_LINEAR
    cv2.imwrite(str(out_images / f"{name}.jpg"), cv2.resize(crop, (OUTPUT_SIZE, OUTPUT_SIZE), interpolation=interpolation),
                [cv2.IMWRITE_JPEG_QUALITY, 93])
    with open(out_labels / f"{name}.txt", "w", encoding="utf-8") as handle:
        for cls, (a, b, c, d) in lines:
            handle.write(f"{cls} {(a + c) / 2 / side:.6f} {(b + d) / 2 / side:.6f} {(c - a) / side:.6f} {(d - b) / side:.6f}\n")
    return len(lines)


def shows_face(lines):
    """True when a labelled body is a face card of a broadcast: about as wide as tall, with a head filling it."""
    heads = [p for c, p in lines if c == 1]
    for cls, (a, b, c, d) in lines:
        if cls != 0 or (d - b) >= FACE_MAX_ASPECT * (c - a):
            continue
        for ha, hb, hc, hd in heads:
            centre_x, centre_y = (ha + hc) / 2, (hb + hd) / 2
            if a < centre_x < c and b < centre_y < d and (hd - hb) > FACE_MIN_HEAD * (d - b):
                return True
    return False


def paint(crop, part):
    """Paints a box of the square grey, so it is taught as neither a target nor background."""
    a, b, c, d = (int(round(v)) for v in part)
    crop[max(b, 0):d, max(a, 0):c] = GREY


def inside(part, body):
    """True when the centre of a box lies inside a body box."""
    cx, cy = (part[0] + part[2]) / 2, (part[1] + part[3]) / 2
    return body[0] <= cx <= body[2] and body[1] <= cy <= body[3]


def build_videos(args, teachers, rng, stats):
    """Cuts squares from every listed video, labelled by the teachers."""
    for game, video, split, every_s, broadcast in VIDEOS:
        path = pathlib.Path(args.videos) / game / f"{video}.mp4"
        if not path.is_file():
            # a missing video would only shrink the set without a word
            raise SystemExit(f"{path} is missing; see docs/Training.md for where the videos come from")
        # Decoding on the graphics card is about ten times faster here, and every frame between two samples is decoded.
        capture = cv2.VideoCapture(str(path), cv2.CAP_FFMPEG, [cv2.CAP_PROP_HW_ACCELERATION, cv2.VIDEO_ACCELERATION_ANY])
        fps = capture.get(cv2.CAP_PROP_FPS) or 30.0
        step = max(1, int(round(every_s * fps)))
        out_images = pathlib.Path(args.out) / "images" / split
        out_labels = pathlib.Path(args.out) / "labels" / split
        out_images.mkdir(parents=True, exist_ok=True)
        out_labels.mkdir(parents=True, exist_ok=True)
        last_small = None
        index = -1
        written = 0
        while True:
            index += 1
            if not capture.grab():
                break
            if index % step:
                continue
            ok, frame = capture.retrieve()
            if not ok:
                break
            small = cv2.resize(cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY), (64, 36), interpolation=cv2.INTER_AREA).astype(np.float32)
            if last_small is not None and float(np.abs(small - last_small).mean()) < SAME_FRAME_DIFF:
                stats["repeats"] += 1
                continue
            last_small = small
            if broadcast:
                grey_out_broadcast(frame)
            labels = teachers.label(frame)
            height, width = frame.shape[:2]
            aside = BROADCAST_ASIDE if broadcast else ASIDE_JITTER
            for k, jitter in enumerate((CENTRE_JITTER, aside)):
                square = square_at(width, height, jitter, rng)
                count = write_square(frame, square, labels, out_images, out_labels, f"{game}_{video}_{index:07d}_{k}", rng,
                                     drop_faces=game == "pubgm")
                if count >= 0:
                    written += 1
                    stats[f"{split}_squares"] += 1
                    stats[f"{split}_empty"] += 1 if count == 0 else 0
        print(f"{game} {video} ({split}): {written} squares", flush=True)


def build_pc(args, teachers, rng, stats):
    """Cuts squares from the PC sets' training and validation pictures, with their own labels."""
    for folder, mapping in PC_SETS:
        root = pathlib.Path(args.pc) / folder
        names = read_names(root / "data.yaml")
        for source_split, split in PC_SPLITS.items():
            if split is None or not (root / source_split / "images").exists():
                continue
            out_images = pathlib.Path(args.out) / "images" / split
            out_labels = pathlib.Path(args.out) / "labels" / split
            out_images.mkdir(parents=True, exist_ok=True)
            out_labels.mkdir(parents=True, exist_ok=True)
            written = 0
            for image_path in sorted((root / source_split / "images").glob("*.jpg")):
                image = cv2.imread(str(image_path))
                # The sets were stretched to 640 x 640 from 16:9 screenshots: put the shape back.
                height = image.shape[0]
                width = int(round(height * 16 / 9))
                image = cv2.resize(image, (width, height), interpolation=cv2.INTER_LINEAR)
                bodies, heads = [], []
                label_path = root / source_split / "labels" / f"{image_path.stem}.txt"
                for line in label_path.read_text().splitlines() if label_path.exists() else []:
                    parts = line.split()
                    if len(parts) != 5:
                        continue
                    cls = mapping.get(names[int(parts[0])])
                    if cls is None:
                        continue
                    cx, cy, w, h = (float(v) for v in parts[1:])
                    box = Box((cx - w / 2) * width, (cy - h / 2) * height, (cx + w / 2) * width, (cy + h / 2) * height, 1.0)
                    (bodies if cls == 0 else heads).append(box)
                if not any(cls == 1 for cls in mapping.values() if cls is not None):
                    # No heads in this set: take sunxds's heads that lie inside a labelled body.
                    heads = [h for h in teachers.heads_in(image) if any(b.contains_centre_of(h) for b in bodies)]
                for k in range(2):
                    side = min(int(rng.uniform(*SIDE_RANGE) * height), width)
                    x0 = rng.randint(0, width - side)
                    y0 = rng.randint(0, height - side) if height > side else 0
                    count = write_square(image, (x0, y0, side), FrameLabels(bodies, heads, [], []), out_images,
                                         out_labels, f"pc_{folder}_{image_path.stem[:40]}_{k}", rng)
                    if count >= 0:
                        written += 1
                        stats[f"{split}_squares"] += 1
                        stats[f"{split}_empty"] += 1 if count == 0 else 0
            print(f"pc {folder} {source_split} -> {split}: {written} squares", flush=True)


def read_names(path):
    """The class names of a Roboflow data.yaml, in class order."""
    names, inside_names = [], False
    for line in path.read_text().splitlines():
        if line.startswith("names:"):
            inside_names = True
            continue
        if inside_names and line.startswith("- "):
            names.append(line[2:].strip())
        elif inside_names:
            break
    return names


def main():
    """Builds the squares from the sources named on the command line."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--videos", required=True)
    parser.add_argument("--pc", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--sunxds", required=True)
    parser.add_argument("--coco", required=True)
    parser.add_argument("--seed", type=int, default=20261030)
    parser.add_argument("--only", choices=("videos", "pc", "all"), default="all")
    args = parser.parse_args()
    rng = random.Random(args.seed)
    teachers = Teachers(args.sunxds, args.coco)
    stats = {"repeats": 0, "train_squares": 0, "train_empty": 0, "val_squares": 0, "val_empty": 0}
    if args.only in ("videos", "all"):
        build_videos(args, teachers, rng, stats)
    if args.only in ("pc", "all"):
        build_pc(args, teachers, rng, stats)
    root = pathlib.Path(args.out).resolve().as_posix()
    names = "names:\n  0: body\n  1: head\n  2: self\n"
    (pathlib.Path(args.out) / "data.yaml").write_text(
        f"path: {root}\ntrain: images/train\nval: images/val\n{names}", encoding="utf-8")
    (pathlib.Path(args.out) / f"stats_{args.only}.json").write_text(json.dumps(stats, indent=1))
    print(stats)


if __name__ == "__main__":
    main()
