"""Finds the people in a whole game frame with the teacher models, and sorts them into enemy, own character or unsure.

sunxds (a YOLO12n by SunOne trained on PC shooters, classes player and head) is run on the whole frame at a larger
size than the frame and with test-time augmentation (flips and three scales), so small targets get more pixels than
the app's model will ever give them. A large COCO model adds people sunxds misses; those are marked unsure, not enemy,
because it also finds people that are not players. A person in the place where a third-person game draws the player's
own character (low in the frame, round the middle, tall) is the own character, whichever model found it.
"""
from dataclasses import dataclass

from ultralytics import YOLO

# Where a third-person game draws the player's own character, as fractions of the frame: its box reaches this low,
# is at least this tall, and its centre lies between these two.
SELF_MIN_BOTTOM = 0.72
SELF_MIN_HEIGHT = 0.18
SELF_CENTRE_X = (0.28, 0.66)

# sunxds scores people in mobile games low (0.25 to 0.6) but made no false box at 0.25 or more in the judged test
# views apart from the own character, which the place rule takes out.
ENEMY_SCORE = 0.25      # sunxds player at least this sure: an enemy
UNSURE_SCORE = 0.12     # sunxds player between this and ENEMY_SCORE: unsure, unless a COCO person agrees
AGREE_IOU = 0.4         # an unsure sunxds player that a COCO person of COCO_SCORE covers this well: an enemy
HEAD_SCORE = 0.25       # sunxds head at least this sure, inside an enemy: that enemy's head
COCO_SCORE = 0.35       # a COCO person this sure that sunxds did not find: unsure
SELF_SCORE = 0.25       # any person this sure in the own character's place: the own character


@dataclass
class Box:
    """A box in frame pixels with the score of the model that found it."""

    x0: float
    y0: float
    x1: float
    y1: float
    score: float

    def iou(self, other):
        """Overlap with another box: intersection over union, 0 to 1."""
        w = min(self.x1, other.x1) - max(self.x0, other.x0)
        h = min(self.y1, other.y1) - max(self.y0, other.y0)
        if w <= 0 or h <= 0:
            return 0.0
        inter = w * h
        return inter / ((self.x1 - self.x0) * (self.y1 - self.y0) + (other.x1 - other.x0) * (other.y1 - other.y0) - inter)

    def contains_centre_of(self, other):
        """True when the centre of the other box lies inside this one."""
        cx, cy = (other.x0 + other.x1) / 2, (other.y0 + other.y1) / 2
        return self.x0 <= cx <= self.x1 and self.y0 <= cy <= self.y1


@dataclass
class FrameLabels:
    """What the teachers made of one frame."""

    enemies: list
    heads: list
    own: list
    unsure: list


def in_own_place(box, width, height):
    """True for a box where a third-person game draws the player's own character."""
    centre = (box.x0 + box.x1) / 2 / width
    return (box.y1 / height >= SELF_MIN_BOTTOM and (box.y1 - box.y0) / height >= SELF_MIN_HEIGHT
            and SELF_CENTRE_X[0] <= centre <= SELF_CENTRE_X[1])


class Teachers:
    """The two teacher models, loaded once, on the given device."""

    def __init__(self, sunxds_path, coco_path, device=0):
        self.sunxds = YOLO(sunxds_path)
        self.coco = YOLO(coco_path)
        self.device = device

    def label(self, frame):
        """Labels one BGR frame. Boxes are in frame pixels."""
        height, width = frame.shape[:2]
        size = min(2048, int(round(1.25 * width / 32)) * 32)
        result = self.sunxds.predict(frame, imgsz=size, augment=True, conf=UNSURE_SCORE, iou=0.6, device=self.device,
                                     verbose=False)[0]
        players, heads = [], []
        for (x0, y0, x1, y1), score, cls in zip(result.boxes.xyxy.tolist(), result.boxes.conf.tolist(),
                                                 result.boxes.cls.tolist()):
            (players if int(cls) == 0 else heads).append(Box(x0, y0, x1, y1, score))
        coco_size = min(960, int(round(width / 32)) * 32)
        result = self.coco.predict(frame, imgsz=coco_size, conf=0.20, classes=[0], device=self.device, verbose=False)[0]
        people = [Box(*b, s) for b, s in zip(result.boxes.xyxy.tolist(), result.boxes.conf.tolist())]

        # every box in the own character's place counts for leaving people out; overlapping ones are merged only for the label
        own = [b for b in players + people if b.score >= SELF_SCORE and in_own_place(b, width, height)]
        confirmed = [p for p in people if p.score >= COCO_SCORE]
        candidates = [b for b in players if not is_own(b, own)]
        enemies = [b for b in candidates if b.score >= ENEMY_SCORE or agreed(b, confirmed)]
        unsure = [b for b in candidates if b.score < ENEMY_SCORE and not agreed(b, confirmed)]
        for person in people:
            if person.score >= COCO_SCORE and not is_own(person, own) and not any(person.iou(e) > 0.3 for e in enemies + unsure):
                unsure.append(person)
        enemy_heads = [h for h in heads if h.score >= HEAD_SCORE and any(e.contains_centre_of(h) for e in enemies)
                       and not any(o.contains_centre_of(h) for o in own)]
        return FrameLabels(enemies, enemy_heads, merge(own), unsure)

    def heads_in(self, image):
        """sunxds's heads on a picture whose bodies are already labelled, at a lower bar than for teacher-made bodies."""
        result = self.sunxds.predict(image, imgsz=1280, augment=True, conf=0.3, device=self.device, verbose=False)[0]
        return [Box(*b, s) for b, s, c in zip(result.boxes.xyxy.tolist(), result.boxes.conf.tolist(),
                                              result.boxes.cls.tolist()) if int(c) == 1]


def is_own(box, own):
    """True for a box that is, or sits on, one of the own character's boxes."""
    return any(box.iou(o) > 0.3 or o.contains_centre_of(box) for o in own)


def agreed(box, confirmed):
    """True for a sunxds player that a sure COCO person covers well."""
    return any(box.iou(p) > AGREE_IOU for p in confirmed)


def merge(boxes):
    """Keeps the strongest of boxes that overlap a lot (two models found the same own character)."""
    kept = []
    for box in sorted(boxes, key=lambda b: -b.score):
        if all(box.iou(k) < 0.5 for k in kept):
            kept.append(box)
    return kept
