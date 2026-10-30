"""Finds the person-like things that stay at one place on the screen through a video: the crouch and prone buttons, gun
icons, the minimap, a broadcast's player cards. The teachers take some of them for people. A player in the game moves
on the screen whenever the player or the camera moves, so a box that comes back at the same place in many frames is part
of the screen, not someone in the game.
"""
import numpy as np

SAME_PLACE_IOU = 0.7     # two boxes this alike in place and size are the same place on screen
FIXED_MIN_FRAMES = 5     # a place is fixed when this many frames have a box there...
FIXED_MIN_SHARE = 0.03   # ...and at least this share of the video's frames do
CHUNK = 512              # boxes compared at a time, to bound memory


def fixed_on_screen(entries, frame_count):
    """Given (frame index, box) for every person in a video, says for each whether its place is fixed on screen."""
    if not entries:
        return []
    boxes = np.array([box[:4] for _, box in entries], dtype=np.float64)
    frames = np.array([index for index, _ in entries])
    areas = (boxes[:, 2] - boxes[:, 0]) * (boxes[:, 3] - boxes[:, 1])
    needed = max(FIXED_MIN_FRAMES, FIXED_MIN_SHARE * frame_count)
    fixed = []
    for start in range(0, len(boxes), CHUNK):
        chunk = boxes[start:start + CHUNK]
        width = np.minimum(chunk[:, None, 2], boxes[None, :, 2]) - np.maximum(chunk[:, None, 0], boxes[None, :, 0])
        height = np.minimum(chunk[:, None, 3], boxes[None, :, 3]) - np.maximum(chunk[:, None, 1], boxes[None, :, 1])
        inter = np.clip(width, 0, None) * np.clip(height, 0, None)
        union = areas[start:start + CHUNK, None] + areas[None, :] - inter
        same = inter >= SAME_PLACE_IOU * np.maximum(union, 1e-6)
        fixed.extend(np.unique(frames[row]).size >= needed for row in same)
    return fixed
