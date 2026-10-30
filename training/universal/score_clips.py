"""Scores the targets the app's tracker drew on motion clips (clip_replay.cpp) against the clips' ground truth.

The ground truth gives, for frames of each clip, the players with their boxes, the player's own character and the people
nobody can vouch for, in capture pixels. A player counts in a frame when at least 35% of its box lies in the app's
square. Measured per clip:
  - shown: the share of those frames in which a drawn body target is on the player (on_box below);
  - back after: each time a player comes into view again after 0.25 s or more out of it (behind cover, or outside the
    square), how long until a target is on it, and how often that takes 0.2 s or less. A player in view from the first
    frame of the clip is not counted;
  - other: drawn body targets on nothing in the ground truth, per second. A target on a place where a player was in
    the last 0.5 s is not counted: it marks a player gone behind cover, as it is meant to.

Usage: score_clips.py <ground truth json> <clips folder> <model name> <detections per second>
Reads <clips folder>/<clip>/<model>/meta.txt and tracks_<detections per second>.txt.
"""
import json
import pathlib
import statistics
import sys

MIN_SHARE_IN_SQUARE = 0.35
COMEBACK_GAP_S = 0.25
QUICK_S = 0.2
RECENT_S = 0.5


def area(box):
    """Area of an (x0, y0, x1, y1) box."""
    return max(box[2] - box[0], 0.0) * max(box[3] - box[1], 0.0)


def overlap(a, b):
    """Area two boxes share."""
    return max(min(a[2], b[2]) - max(a[0], b[0]), 0.0) * max(min(a[3], b[3]) - max(a[1], b[1]), 0.0)


def centre_in(inner, outer):
    """True when the centre of one box lies inside another."""
    cx, cy = (inner[0] + inner[2]) / 2, (inner[1] + inner[3]) / 2
    return outer[0] <= cx <= outer[2] and outer[1] <= cy <= outer[3]


def on_box(box, truth):
    """True when a drawn box is on a ground-truth box: they overlap well (IoU 0.3 or more), or the box is a part of it
    (its centre inside and no bigger), or a loose box round it (the truth's centre inside, at most four times its area).
    The same rule judges the still views."""
    shared = overlap(box, truth)
    if shared >= 0.3 * (area(box) + area(truth) - shared):
        return True
    if centre_in(box, truth) and area(box) <= area(truth):
        return True
    return centre_in(truth, box) and area(box) <= 4.0 * area(truth)


def read_meta(path):
    """meta.txt as a dict of word lists."""
    return {line.split()[0]: line.split()[1:] for line in path.read_text(encoding="utf-8").splitlines() if line.strip()}


def read_targets(path):
    """The drawn body targets of every frame, as {frame: [box]} in capture pixels."""
    targets = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        words = line.split()
        frame, count = int(words[0]), int(words[1])
        boxes = []
        for k in range(count):
            _, role, cx, cy, w, h, _ = words[2 + 7 * k:9 + 7 * k]
            if role == "b":
                cx, cy, w, h = float(cx), float(cy), float(w), float(h)
                boxes.append((cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2))
        targets[frame] = boxes
    return targets


def score_clip(truth, meta, targets):
    """Shown frames, frames in view, comeback times and other targets of one clip."""
    x0, y0, side = (int(v) for v in meta["square"])
    square = (x0, y0, x0 + side, y0 + side)
    fps = float(meta["fps"][0])
    frames = truth["frames"]
    shown = in_view = others = 0
    comebacks = []
    for player in truth["players"]:
        last_seen = None
        waiting_since = None
        for frame in frames:
            box = player["boxes"].get(str(frame))
            if box is None or overlap(box, square) < MIN_SHARE_IN_SQUARE * area(box):
                continue
            in_view += 1
            covered = any(on_box(target, box) for target in targets.get(frame, []))
            shown += covered
            if last_seen is not None and (frame - last_seen) / fps >= COMEBACK_GAP_S:
                if waiting_since is not None:
                    comebacks.append(None)   # out of view again before a target was on it
                waiting_since = frame
            if waiting_since is not None and covered:
                comebacks.append((frame - waiting_since) / fps)
                waiting_since = None
            last_seen = frame
        if waiting_since is not None:
            comebacks.append(None)   # never taken up again before the clip ended
    for frame in frames:
        recent = [f for f in frames if frame - RECENT_S * fps <= f <= frame]
        known = [p["boxes"][str(f)] for p in truth["players"] for f in recent if str(f) in p["boxes"]]
        known += truth["own"].get(str(frame), []) + truth["unsure"].get(str(frame), [])
        others += sum(not any(on_box(target, box) for box in known) for target in targets.get(frame, []))
    seconds = len(frames) * (frames[1] - frames[0]) / fps if len(frames) > 1 else 0.0
    return shown, in_view, comebacks, others, seconds


def main():
    """Prints the scores of one model at one detection rate on every clip of the ground truth, then the totals."""
    truth = json.loads(pathlib.Path(sys.argv[1]).read_text())
    clips, model, rate = pathlib.Path(sys.argv[2]), sys.argv[3], sys.argv[4]
    total_shown = total_view = total_others = 0
    total_seconds = 0.0
    all_comebacks = []
    for name, clip_truth in truth.items():
        folder = clips / name / model
        shown, in_view, comebacks, others, seconds = score_clip(clip_truth, read_meta(folder / "meta.txt"),
                                                                read_targets(folder / f"tracks_{rate}.txt"))
        quick = sum(1 for c in comebacks if c is not None and c <= QUICK_S)
        times = sorted(c for c in comebacks if c is not None)
        median = f"{statistics.median(times):.2f} s" if times else "-"
        print(f"{name:20s} shown {100 * shown / max(in_view, 1):5.1f}% of {in_view:4d} frames | back after cover "
              f"{quick}/{len(comebacks)} within {QUICK_S} s, median {median} | other {others / max(seconds, 1e-6):.2f}/s")
        total_shown += shown
        total_view += in_view
        total_others += others
        total_seconds += seconds
        all_comebacks += comebacks
    quick = sum(1 for c in all_comebacks if c is not None and c <= QUICK_S)
    times = sorted(c for c in all_comebacks if c is not None)
    median = f"{statistics.median(times):.2f} s" if times else "-"
    print(f"{'all':20s} shown {100 * total_shown / max(total_view, 1):5.1f}% of {total_view:4d} frames | back after cover "
          f"{quick}/{len(all_comebacks)} within {QUICK_S} s, median {median} | other "
          f"{total_others / max(total_seconds, 1e-6):.2f}/s")


if __name__ == "__main__":
    main()
