"""Prepares motion clips for the app's tracker: for each clip and model, the square the app reads from every frame, a
small copy of it for the camera motion estimator, and the model's raw output on it. clip_replay.cpp
(app/src/test/native/bench) replays them through the app's own decoder, camera motion estimator and tracker, and
score_clips.py scores the targets it draws.

A clip is a stretch of a video, {"name", "game", "video", "start", "end"} with frame numbers, end not included. Frames
are scaled to 720 px tall, as a phone captures them, and the square is the default detection zone: 400 screen pixels
across on a 1080 px tall screen, which is 266 capture pixels, kept between the model's input size and 1.75 times it.

Usage: clip_frames.py <clips json> <videos folder> <out folder> <model folder> [<model folder> ...]
Writes <out>/<clip>/<model>/meta.txt, motion.bin (128 x 128 RGBA a frame) and raw.bin (the output, float32, a frame).
"""
import json
import pathlib
import sys

import cv2
import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).parent))   # python -I leaves the script's folder off the path
from app_detector import AppDetector  # noqa: E402

CAPTURE_HEIGHT = 720
SCREEN_PER_CAPTURE = 1.5          # a 1080 px tall screen captured at 720 px
ZONE_DIAMETER_SCREEN_PX = 400.0   # the app's default detection zone
MAX_SQUARE_TO_INPUT = 1.75
MOTION_SIDE = 128                 # the camera motion estimator reduces the square to 64 x 64 anyway


def square_side(input_size):
    """The side of the app's square in capture pixels, as computeRoi() in geometry.h chooses it."""
    side = int(ZONE_DIAMETER_SCREEN_PX / SCREEN_PER_CAPTURE + 0.5)
    side = max(input_size, min(side, int(input_size * MAX_SQUARE_TO_INPUT), CAPTURE_HEIGHT))
    return side & ~1


def captured_frames(path, start, end):
    """Yields the frames start..end-1 of a video, scaled to the capture height."""
    capture = cv2.VideoCapture(str(path))
    capture.set(cv2.CAP_PROP_POS_FRAMES, start)
    if int(capture.get(cv2.CAP_PROP_POS_FRAMES)) != start:
        raise SystemExit(f"{path} cannot be read from frame {start}")
    for _ in range(start, end):
        ok, frame = capture.read()
        if not ok:
            raise SystemExit(f"{path} ends before frame {end}")
        height, width = frame.shape[:2]
        scale = CAPTURE_HEIGHT / height
        yield cv2.resize(frame, (int(round(width * scale)), CAPTURE_HEIGHT), interpolation=cv2.INTER_AREA)
    capture.release()


def prepare(clip, path, out, detectors):
    """Writes one clip for every model."""
    fps = cv2.VideoCapture(str(path)).get(cv2.CAP_PROP_FPS)
    sinks = {}
    for name in detectors:
        folder = out / clip["name"] / name
        folder.mkdir(parents=True, exist_ok=True)
        sinks[name] = (folder, open(folder / "motion.bin", "wb"), open(folder / "raw.bin", "wb"))
    shape = {}
    count = 0
    for frame in captured_frames(path, clip["start"], clip["end"]):
        height, width = frame.shape[:2]
        for name, detector in detectors.items():
            side = square_side(detector.size)
            x0, y0 = (width - side) // 2, (height - side) // 2
            square = frame[y0:y0 + side, x0:x0 + side]
            small = cv2.resize(square, (MOTION_SIDE, MOTION_SIDE), interpolation=cv2.INTER_AREA)
            sinks[name][1].write(cv2.cvtColor(small, cv2.COLOR_BGR2RGBA).tobytes())
            raw = detector.raw(square)
            shape[name] = (raw.shape, (width, height), (x0, y0, side))
            sinks[name][2].write(np.ascontiguousarray(raw, dtype="<f4").tobytes())
        count += 1
    for name, (folder, motion, raw) in sinks.items():
        motion.close()
        raw.close()
        (rows, cols), (width, height), (x0, y0, side) = shape[name]
        detector = detectors[name]
        (folder / "meta.txt").write_text(
            f"fps {fps}\nframes {count}\nfirst {clip['start']}\ncapture {width} {height}\n"
            f"screen_per_capture {SCREEN_PER_CAPTURE}\nsquare {x0} {y0} {side}\nmotion {MOTION_SIDE}\n"
            f"input {detector.size}\noutput {rows} {cols}\n"
            f"roles {','.join(detector.roles)}\nend_to_end {int(detector.end_to_end)}\n", encoding="utf-8")


def main():
    """Prepares every clip of the list for every model."""
    clips = json.loads(pathlib.Path(sys.argv[1]).read_text())
    videos, out = pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
    detectors = {pathlib.Path(folder).name: AppDetector(folder) for folder in sys.argv[4:]}
    for clip in clips:
        prepare(clip, videos / clip["game"] / f"{clip['video']}.mp4", out, detectors)
        print(clip["name"], clip["end"] - clip["start"], "frames", flush=True)


if __name__ == "__main__":
    main()
