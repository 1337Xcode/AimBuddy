"""Tells a figure a broadcast paints in one flat colour (the yellow or cyan mark of a player behind a wall) from a player
the game draws. A mark is a single strong hue with almost no shading inside its outline; a drawn player, even in a bright
outfit, has shading, edges and other colours.
"""
import cv2
import numpy as np

MIN_SATURATION = 110     # 0..255: a mark is a vivid colour
MIN_VALUE = 110          # 0..255: and a bright one
HUE_TOLERANCE = 7        # 0..180: pixels this close to the box's main hue count as that hue
FLAT_SHARE = 0.40        # a box with this share or more of its pixels in one flat, vivid hue is a mark


def flat_share(image, box):
    """Share of the pixels inside the middle of the box that are one vivid, unshaded hue. 0 for a box too small to judge."""
    x0, y0, x1, y1 = (int(round(v)) for v in box[:4])
    w, h = x1 - x0, y1 - y0
    if w < 6 or h < 6:
        return 0.0
    # the middle of the box: a mark's outline and the scenery round it stay out
    inner = image[max(0, y0 + h // 6):max(0, y1 - h // 6), max(0, x0 + w // 6):max(0, x1 - w // 6)]
    if inner.size == 0:
        return 0.0
    hsv = cv2.cvtColor(inner, cv2.COLOR_BGR2HSV)
    vivid = (hsv[..., 1] >= MIN_SATURATION) & (hsv[..., 2] >= MIN_VALUE)
    if vivid.sum() < 0.1 * vivid.size:
        return 0.0
    hues = hsv[..., 0][vivid].astype(np.int32)
    main = int(np.bincount(hues, minlength=180).argmax())
    distance = np.minimum(np.abs(hsv[..., 0].astype(np.int32) - main), 180 - np.abs(hsv[..., 0].astype(np.int32) - main))
    same = vivid & (distance <= HUE_TOLERANCE)
    # unshaded: the brightness barely changes from one pixel to the next inside the mark
    grey = cv2.cvtColor(inner, cv2.COLOR_BGR2GRAY).astype(np.float32)
    edges = np.abs(cv2.Laplacian(grey, cv2.CV_32F, ksize=3))
    smooth = edges < 12.0
    return float((same & smooth).sum()) / float(same.size)


def is_flat_mark(image, box):
    """True for a box that holds a figure painted in one flat colour."""
    return flat_share(image, box) >= FLAT_SHARE
