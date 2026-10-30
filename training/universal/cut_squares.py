"""Builds the universal training set from the teacher cache (label_frames.py) and the PC sets: squares cut the way the
app cuts them, labelled enemy body (0), enemy head (1) and the player's own character (2).

Every source is brought to one form:
  - A square round the screen centre, 0.44 to 0.78 of the frame height on a side, resized to 320 px, and a second one a
    little aside. For every player in the frame, one more square with the player in it at a size drawn from 15 to 60%
    of the square, so near and far players are both common, wherever they stand in the frame.
  - People the teachers cannot vouch for, figures a broadcast paints in one flat colour, person-like things fixed on the
    screen (buttons, icons, player cards: hud.py) and boxes the square cuts to a sliver are painted grey (114): taught as
    neither a target nor background.
  - The own character is decided per video (own_character.py); a PUBG Mobile square that shows a broadcast face card is
    left out; only some squares with nobody in them are kept.
  - Some training squares get a little motion blur, and in some a player is partly covered by a piece of the scenery
    from elsewhere in the frame (its label shrinks to what is left in view), so the model sees players that move and
    players behind cover.
Videos are split by video (sources.py); PC pictures keep their own train and validation splits.

Usage: cut_squares.py --cache <folder> --videos <folder> --pc <folder> --out <folder> [--only videos|pc|all]
"""
import argparse
import collections
import json
import pathlib
import random
import sys

import cv2
import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).parent))   # python -I leaves the script's folder off the path
from flat_marks import is_flat_mark  # noqa: E402
from fusion import heads_in, people_in  # noqa: E402
from hud import fixed_on_screen  # noqa: E402
from own_character import (in_own_place, is_mostly_over, is_part_of, look_of, own_candidates,  # noqa: E402
                           usual_place)
from sources import (GREY, PC_SETS, VIDEOS, grey_out_broadcast, has_heads, pc_pictures,  # noqa: E402
                     read_names, read_pc_picture)

OUTPUT_SIZE = 320
SIDE_RANGE = (0.44, 0.78)       # square side as a share of the frame height
CENTRE_JITTER = (0.04, 0.03)    # the first square of a frame: this far from the centre at most (share of width, height)
ASIDE_JITTER = (0.18, 0.08)     # the second one, as if the player looked a little aside
BROADCAST_ASIDE = (0.10, 0.05)  # broadcasts keep to the middle, away from their overlays
PLAYER_SHARE = (0.15, 0.60)     # a player square shows its player this tall, as a share of the square
PLAYER_MIN_HEIGHT = 0.05        # players shorter than this share of the frame get no square of their own
MIN_VISIBLE_BODY = 0.35         # a body cut by the square keeps its label if this much of it is inside
MIN_VISIBLE_HEAD = 0.60
MIN_VISIBLE_SELF = 0.15
KEEP_EMPTY = 0.35               # share of squares with nobody in them that are kept
BLUR_SHARE = 0.15               # share of training squares with motion blur
COVER_SHARE = 0.15              # share of training squares with a player partly covered
FACE_MAX_ASPECT = 1.3           # a labelled person no taller than this many widths...
FACE_MIN_HEAD = 0.35            # ...whose head is over this share of its height is a face on a broadcast panel


class Labels:
    """What one frame holds, in frame pixels: players, their heads, the own character, and what to paint grey."""

    def __init__(self):
        self.bodies, self.heads, self.own, self.grey = [], [], [], []


def clip_to(box, x0, y0, side):
    """The part of a box inside the square, in square pixels, and the share of the box it is."""
    cx0, cy0 = max(box[0], x0), max(box[1], y0)
    cx1, cy1 = min(box[2], x0 + side), min(box[3], y0 + side)
    if cx1 <= cx0 or cy1 <= cy0:
        return None, 0.0
    visible = (cx1 - cx0) * (cy1 - cy0) / max((box[2] - box[0]) * (box[3] - box[1]), 1e-6)
    return [cx0 - x0, cy0 - y0, cx1 - x0, cy1 - y0], visible


def paint(crop, part):
    """Paints a box of the square grey, so it is taught as neither a target nor background."""
    a, b, c, d = (int(round(v)) for v in part)
    crop[max(b, 0):max(d, 0), max(a, 0):max(c, 0)] = GREY


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


def cover_one(crop, lines, rng):
    """Covers part of one labelled body with a piece of the square from elsewhere, from one side or from below, as a
    wall or a crate would, and shrinks its label to what stays in view. Returns the new label lines."""
    bodies = [k for k, (cls, part) in enumerate(lines) if cls == 0 and part[3] - part[1] >= 24]
    if not bodies:
        return lines
    k = rng.choice(bodies)
    body = lines[k][1]
    a, b, c, d = body
    share = rng.uniform(0.3, 0.6)
    side = rng.choice(("left", "right", "below"))
    if side == "below":
        cover, kept = [a, d - share * (d - b), c, d], [a, b, c, d - share * (d - b)]
    elif side == "left":
        cover, kept = [a, b, a + share * (c - a), d], [a + share * (c - a), b, c, d]
    else:
        cover, kept = [c - share * (c - a), b, c, d], [a, b, c - share * (c - a), d]
    own_head = [j for j, (cls, part) in enumerate(lines) if cls == 1 and centre_inside(part, body)]
    if any(overlaps(cover, part) for j, (_, part) in enumerate(lines) if j != k and j not in own_head):
        return lines   # it would hide part of someone else too
    x0, y0, x1, y1 = (int(round(v)) for v in cover)
    width, height = x1 - x0, y1 - y0
    size = crop.shape[0]
    if width < 4 or height < 4 or width >= size or height >= size:
        return lines
    # a piece of the same square with nobody in it stands in for the wall
    for _ in range(10):
        sx, sy = rng.randint(0, size - width), rng.randint(0, size - height)
        if any(overlaps([sx, sy, sx + width, sy + height], part) for _, part in lines):
            continue
        crop[y0:y1, x0:x1] = crop[sy:sy + height, sx:sx + width].copy()
        out = []
        for j, (cls, part) in enumerate(lines):
            if j == k:
                out.append((0, kept))
            elif j not in own_head:
                out.append((cls, part))
            elif centre_inside(part, kept):
                out.append((1, [max(part[0], kept[0]), max(part[1], kept[1]), min(part[2], kept[2]), min(part[3], kept[3])]))
        return out
    return lines


def overlaps(first, second):
    """True when two boxes share some area."""
    return min(first[2], second[2]) > max(first[0], second[0]) and min(first[3], second[3]) > max(first[1], second[1])


def motion_blur(crop, rng):
    """Smears the square along a random direction, as a fast turn or a running player does on a phone's screen."""
    length = rng.choice((5, 7, 9, 11))
    kernel = np.zeros((length, length), np.float32)
    kernel[length // 2, :] = 1.0 / length
    angle = rng.uniform(0, 180)
    matrix = cv2.getRotationMatrix2D((length / 2 - 0.5, length / 2 - 0.5), angle, 1.0)
    kernel = cv2.warpAffine(kernel, matrix, (length, length))
    return cv2.filter2D(crop, -1, kernel / max(float(kernel.sum()), 1e-6))


def write_square(frame, square, labels, out, name, rng, augment, drop_faces=False):
    """Cuts one square, paints what cannot be vouched for, writes the picture and its labels.

    Returns the number of labels written, or -1 when the square is left out.
    """
    x0, y0, side = square
    source = frame[y0:y0 + side, x0:x0 + side]
    crop = source.copy()
    lines, kept = [], []
    for cls, boxes, needed in ((0, labels.bodies, MIN_VISIBLE_BODY), (2, labels.own, MIN_VISIBLE_SELF)):
        for box in boxes:
            part, visible = clip_to(box, x0, y0, side)
            if part is None:
                continue
            if visible >= needed:
                lines.append((cls, part))
                kept.append(part)
            elif cls == 0:
                paint(crop, part)
    body_parts = [p for c, p in lines if c == 0]
    for head in labels.heads:
        part, visible = clip_to(head, x0, y0, side)
        if part is None or visible < MIN_VISIBLE_HEAD:
            continue
        cx, cy = (part[0] + part[2]) / 2, (part[1] + part[3]) / 2
        if any(b[0] <= cx <= b[2] and b[1] <= cy <= b[3] for b in body_parts):
            lines.append((1, part))
    for box in labels.grey:
        part, _ = clip_to(box, x0, y0, side)
        if part is not None:
            paint(crop, part)
    # grey never covers a labelled person: an unsure box on someone's arm or gun leaves them whole
    for a, b, c, d in (tuple(int(round(v)) for v in part) for part in kept):
        crop[b:d, a:c] = source[b:d, a:c]
    if drop_faces and shows_face(lines):
        return -1
    if not lines and rng.random() > KEEP_EMPTY:
        return -1
    scale = OUTPUT_SIZE / side
    interpolation = cv2.INTER_AREA if side > OUTPUT_SIZE else cv2.INTER_LINEAR
    crop = cv2.resize(crop, (OUTPUT_SIZE, OUTPUT_SIZE), interpolation=interpolation)
    lines = [(cls, [v * scale for v in p]) for cls, p in lines]
    if augment and rng.random() < COVER_SHARE:
        lines = cover_one(crop, lines, rng)
    if augment and rng.random() < BLUR_SHARE:
        crop = motion_blur(crop, rng)
    cv2.imwrite(str(out[0] / f"{name}.jpg"), crop, [cv2.IMWRITE_JPEG_QUALITY, 93])
    with open(out[1] / f"{name}.txt", "w", encoding="utf-8") as handle:
        for cls, (a, b, c, d) in lines:
            if c - a < 2 or d - b < 2:
                continue
            handle.write(f"{cls} {(a + c) / 2 / OUTPUT_SIZE:.6f} {(b + d) / 2 / OUTPUT_SIZE:.6f} "
                         f"{(c - a) / OUTPUT_SIZE:.6f} {(d - b) / OUTPUT_SIZE:.6f}\n")
    return len(lines)


def square_at(width, height, jitter, rng):
    """A square of the app's size near the frame centre: (x0, y0, side) in frame pixels."""
    side = min(int(rng.uniform(*SIDE_RANGE) * height), width, height)
    cx = width / 2 + rng.uniform(-jitter[0], jitter[0]) * width
    cy = height / 2 + rng.uniform(-jitter[1], jitter[1]) * height
    return int(min(max(cx - side / 2, 0), width - side)), int(min(max(cy - side / 2, 0), height - side)), side


def square_round(box, width, height, rng):
    """A square that shows the player at a size drawn from PLAYER_SHARE, the player somewhere inside it."""
    h = box[3] - box[1]
    side = int(min(max(h / rng.uniform(*PLAYER_SHARE), 0.25 * height), height, width))
    cx = (box[0] + box[2]) / 2 + rng.uniform(-0.3, 0.3) * side
    cy = (box[1] + box[3]) / 2 + rng.uniform(-0.2, 0.2) * side
    return int(min(max(cx - side / 2, 0), width - side)), int(min(max(cy - side / 2, 0), height - side)), side


def frames_of(path, indices):
    """Yields (index, frame) for the wanted frame indices of a video, in order. Stops the build if one cannot be read:
    the graphics card's decoder can fail when several programs use it, and a frame left out would leave the labels of
    the two passes over the video out of step."""
    capture = cv2.VideoCapture(str(path), cv2.CAP_FFMPEG, [cv2.CAP_PROP_HW_ACCELERATION, cv2.VIDEO_ACCELERATION_ANY])
    wanted, index, last = set(indices), -1, max(indices) if indices else -1
    while index < last:
        index += 1
        if not capture.grab():
            raise SystemExit(f"{path}: frame {index} could not be read")
        if index in wanted:
            ok, frame = capture.retrieve()
            if not ok:
                raise SystemExit(f"{path}: frame {index} could not be decoded")
            yield index, frame
    capture.release()


def label_video(record, path):
    """Decides the labels of every cached frame of one video. Returns {frame index: Labels}."""
    width, height = record["size"]
    cached = {f["index"]: f["boxes"] for f in record["frames"]}
    people, looks, placed = {}, [], []
    # first pass: everyone the teachers saw, and the look of everyone standing where the own character stands
    for index, frame in frames_of(path, sorted(cached)):
        if record["broadcast"]:
            grey_out_broadcast(frame)
        found = people_in(cached[index])
        flat = [is_flat_mark(frame, p.box) for p in found]
        people[index] = (found, flat)
        for k, person in enumerate(found):
            if not flat[k] and (person.is_player() or person.own_votes) and in_own_place(person.box, width, height):
                look = look_of(frame, person.box)
                if look is not None:
                    looks.append(look)
                    placed.append((index, k))
    own = {key for key, decided in zip(placed, own_candidates(looks), strict=True) if decided}
    usual = usual_place([people[index][0][k].box for index, k in own])
    others = [(index, k) for index, (found, _) in people.items() for k, person in enumerate(found)
              if (index, k) not in own and (person.is_player() or person.is_unsure())]
    fixed = {key for key, decided in zip(others, fixed_on_screen([(i, people[i][0][k].box) for i, k in others],
                                                                  len(people)), strict=True) if decided}
    return {index: frame_labels(index, found, flat, own, set(placed), fixed, usual, cached[index])
            for index, (found, flat) in people.items()}


def frame_labels(index, found, flat, own, placed, fixed, usual, boxes):
    """Sorts the people of one frame into the own character, players, and what nobody can vouch for (grey)."""
    labels = Labels()
    own_here = [person.box for k, person in enumerate(found) if (index, k) in own]
    for k, person in enumerate(found):
        if (index, k) in own:
            labels.own.append(person.box)
        elif any(is_part_of(box, person.box) for box in own_here):
            continue   # its own character's box covers it
        elif flat[k] or (index, k) in fixed or any(is_mostly_over(box, person.box) for box in own_here):
            labels.grey.append(person.box)
        elif not own_here and ((index, k) in placed or (usual is not None and centre_inside(person.box, usual))):
            # the own character is not seen, and this one stands where it stands: it may be the own character changed
            labels.grey.append(person.box)
        elif person.own_votes >= 2 and (index, k) not in placed:
            labels.grey.append(person.box)
        elif person.is_unsure():
            labels.grey.append(person.box)
        elif person.is_player():
            labels.bodies.append(person.box)
    labels.heads = heads_in(boxes, labels.bodies)
    return labels


def centre_inside(box, area):
    """True when the centre of a box lies inside another box."""
    cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
    return area[0] <= cx <= area[2] and area[1] <= cy <= area[3]


def build_videos(args, rng, stats):
    """Cuts squares from every cached video."""
    for game, video, split, _, _ in VIDEOS:
        cache = pathlib.Path(args.cache) / f"{game}_{video}.json"
        path = pathlib.Path(args.videos) / game / f"{video}.mp4"
        if not cache.is_file() or not path.is_file():
            raise SystemExit(f"{cache} or {path} is missing: run label_frames.py first")
        written = build_video(json.loads(cache.read_text()), path, pathlib.Path(args.out), rng, stats)
        print(f"{game} {video} ({split}): {written} squares", flush=True)


def build_video(record, path, out_root, rng, stats):
    """Cuts the squares of one video: two near the centre of every frame, one round every player. Returns the count."""
    game, video, split, broadcast = record["game"], record["video"], record["split"], record["broadcast"]
    width, height = record["size"]
    labels = label_video(record, path)
    out = (out_root / "images" / split, out_root / "labels" / split)
    for folder in out:
        folder.mkdir(parents=True, exist_ok=True)
    written = 0
    for index, frame in frames_of(path, sorted(labels)):
        if broadcast:
            grey_out_broadcast(frame)
        here = labels[index]
        squares = [square_at(width, height, CENTRE_JITTER, rng),
                   square_at(width, height, BROADCAST_ASIDE if broadcast else ASIDE_JITTER, rng)]
        squares += [square_round(box, width, height, rng) for box in here.bodies
                    if box[3] - box[1] >= PLAYER_MIN_HEIGHT * height]
        for k, square in enumerate(squares):
            count = write_square(frame, square, here, out, f"{game}_{video}_{index:07d}_{k}", rng,
                                 augment=split == "train", drop_faces=game == "pubgm")
            if count >= 0:
                written += 1
                stats[f"{split}_squares"] += 1
                stats[f"{split}_empty"] += 1 if count == 0 else 0
    stats[f"{split}_bodies"] += sum(len(v.bodies) for v in labels.values())
    stats[f"{split}_own"] += sum(len(v.own) for v in labels.values())
    return written


def build_pc(args, rng, stats):
    """Cuts squares from the PC sets' training and validation pictures, with their own labels. Sets without heads take
    sunxds's heads inside a labelled body, from the cache label_frames.py wrote."""
    for folder, mapping in PC_SETS:
        root = pathlib.Path(args.pc)
        names = read_names(root / folder / "data.yaml")
        cached_heads = {}
        if not has_heads(mapping):
            cache = pathlib.Path(args.cache) / f"pc_{folder}.json"
            if not cache.is_file():
                raise SystemExit(f"{cache} is missing: run label_frames.py with --pc first")
            cached_heads = json.loads(cache.read_text())
        written = collections.Counter()
        for source_split, split, image_path in pc_pictures(root, folder):
            out = (pathlib.Path(args.out) / "images" / split, pathlib.Path(args.out) / "labels" / split)
            for path in out:
                path.mkdir(parents=True, exist_ok=True)
            image = read_pc_picture(image_path)
            height, width = image.shape[:2]
            labels = Labels()
            label_path = image_path.parent.parent / "labels" / f"{image_path.stem}.txt"
            for line in label_path.read_text().splitlines() if label_path.exists() else []:
                parts = line.split()
                if len(parts) != 5:
                    continue
                cls = mapping.get(names[int(parts[0])])
                if cls is None:
                    continue
                cx, cy, w, h = (float(v) for v in parts[1:])
                box = [(cx - w / 2) * width, (cy - h / 2) * height, (cx + w / 2) * width, (cy + h / 2) * height]
                (labels.bodies if cls == 0 else labels.heads).append(box)
            if not has_heads(mapping):
                heads = cached_heads[f"{source_split}/{image_path.stem}"]
                labels.heads = [h[:4] for h in heads if any(centre_inside(h, b) for b in labels.bodies)]
            for k in range(2):
                side = min(int(rng.uniform(*SIDE_RANGE) * height), width)
                x0 = rng.randint(0, width - side)
                y0 = rng.randint(0, height - side) if height > side else 0
                count = write_square(image, (x0, y0, side), labels, out, f"pc_{folder}_{image_path.stem[:40]}_{k}", rng,
                                     augment=split == "train")
                if count >= 0:
                    written[split] += 1
                    stats[f"{split}_squares"] += 1
                    stats[f"{split}_empty"] += 1 if count == 0 else 0
        print(f"pc {folder}: {dict(written)} squares", flush=True)


def main():
    """Builds the squares from the sources named on the command line."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    for name in ("cache", "videos", "pc", "out"):
        parser.add_argument(f"--{name}", required=True)
    parser.add_argument("--seed", type=int, default=20261030)
    parser.add_argument("--only", choices=("videos", "pc", "all"), default="all")
    args = parser.parse_args()
    rng = random.Random(args.seed)
    stats = {f"{split}_{kind}": 0 for split in ("train", "val") for kind in ("squares", "empty", "bodies", "own")}
    if args.only in ("videos", "all"):
        build_videos(args, rng, stats)
    if args.only in ("pc", "all"):
        build_pc(args, rng, stats)
    root = pathlib.Path(args.out).resolve().as_posix()
    names = "names:\n  0: body\n  1: head\n  2: self\n"
    (pathlib.Path(args.out) / "data.yaml").write_text(f"path: {root}\ntrain: images/train\nval: images/val\n{names}",
                                                      encoding="utf-8")
    (pathlib.Path(args.out) / f"stats_{args.only}.json").write_text(json.dumps(stats, indent=1))
    print(stats)


if __name__ == "__main__":
    main()
