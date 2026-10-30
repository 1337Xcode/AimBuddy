"""Finds the player's own character among the people the teachers saw in one video.

In a third-person game the camera follows the player, so the own character stands low in the middle of the frame in
nearly every frame, looking the same from frame to frame. An enemy who comes that close to the camera stands in the same
place for a moment only, and looks different. So a person in the own character's place is the own character only when
many other frames of the video show someone in that place who looks like them.

A person in that place who does not look like that is someone else only when the frame also shows the own character
apart from them. In a frame without it, the person may be the own character changed (grey after a knock-down, half
covered by a kill banner, in another pose) as well as a close enemy, and nobody can vouch for either.
"""
import cv2
import numpy as np

# Where a third-person game draws the player's own character, as fractions of the frame: its box reaches this low,
# is at least this tall, and its centre lies between these two.
SELF_MIN_BOTTOM = 0.72
SELF_MIN_HEIGHT = 0.18
SELF_CENTRE_X = (0.26, 0.68)

LOOK_BINS = (8, 3, 3)        # hue, saturation, brightness bins of the look: coarse, so light and pose matter little
SAME_LOOK = 0.45             # histogram intersection, 0..1, of two looks of one character
MIN_SAME_LOOKS = 6           # a person is the own character when this many other frames show the same look in its place...
MIN_SAME_SHARE = 0.04        # ...and at least this share of the frames with someone in the place do
PART_INSIDE = 0.2            # a box with this share of its area inside the own character's box...
PART_MIN_AREA = 0.2          # ...and an area this share of the own character's or more is a piece of the own character


def in_own_place(box, width, height):
    """True for a box where a third-person game draws the player's own character."""
    centre = (box[0] + box[2]) / 2 / width
    return (box[3] / height >= SELF_MIN_BOTTOM and (box[3] - box[1]) / height >= SELF_MIN_HEIGHT
            and SELF_CENTRE_X[0] <= centre <= SELF_CENTRE_X[1])


def look_of(image, box):
    """The colours of the middle of the box (the figure, little of the scenery round it), as a normalised histogram,
    upper and lower half apart: a character's top and trousers rarely match a stranger's both."""
    x0, y0, x1, y1 = (int(round(v)) for v in box[:4])
    w, h = x1 - x0, y1 - y0
    inner = image[max(0, y0 + h // 10):max(0, y1 - h // 10), max(0, x0 + w // 5):max(0, x1 - w // 5)]
    if inner.size == 0 or inner.shape[0] < 4:
        return None
    hsv = cv2.cvtColor(inner, cv2.COLOR_BGR2HSV)
    halves = []
    middle = hsv.shape[0] // 2
    for part in (hsv[:middle], hsv[middle:]):
        hist = cv2.calcHist([part], [0, 1, 2], None, list(LOOK_BINS), [0, 180, 0, 256, 0, 256]).flatten()
        halves.append(hist / max(float(hist.sum()), 1.0))
    return np.concatenate(halves) / 2.0


def similarity(a, b):
    """Histogram intersection of two looks, 0 (nothing alike) to 1 (the same colours)."""
    return float(np.minimum(a, b).sum())


def own_candidates(looks):
    """Given the looks of every person seen in the own place over a video (in frame order), says for each whether it is
    the own character: a look many other frames share."""
    count = len(looks)
    needed = max(MIN_SAME_LOOKS, int(round(MIN_SAME_SHARE * count)))
    stacked = np.stack(looks) if count else np.zeros((0, 1))
    decisions = []
    for k in range(count):
        same = np.minimum(stacked, stacked[k]).sum(axis=1) >= SAME_LOOK
        decisions.append(int(same.sum()) - 1 >= needed)
    return decisions


def is_part_of(own_box, box):
    """True for a box that is a piece of the own character rather than someone beside it: the part a kill banner leaves
    in view, or a second box a teacher put on it. A far player seen over the own character's shoulder is too small."""
    w = min(own_box[2], box[2]) - max(own_box[0], box[0])
    h = min(own_box[3], box[3]) - max(own_box[1], box[1])
    if w <= 0 or h <= 0:
        return False
    area = (box[2] - box[0]) * (box[3] - box[1])
    own_area = (own_box[2] - own_box[0]) * (own_box[3] - own_box[1])
    return w * h >= PART_INSIDE * area and area >= PART_MIN_AREA * own_area


def usual_place(own_boxes):
    """The median box of the own character over a video, or None when it was never seen."""
    if not own_boxes:
        return None
    return [float(v) for v in np.median(np.array([b[:4] for b in own_boxes]), axis=0)]
