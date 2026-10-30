"""Joins what the four teacher models saw in one frame into one list of people, each with the evidence for it, and
decides from that evidence which are players and which nobody can vouch for.

Evidence per person: the best score each model gave a box on it. sunxds and the COCO model are the first teachers;
gen2 and sunft are models trained on their labels, which find mobile players the first teachers score low. A person
the two trained models both see, or one of them and the COCO model, is a player: a single model is not enough.
"""
from dataclasses import dataclass, field

MERGE_IOU = 0.5

# Evidence that makes a person a player.
SUNXDS_SURE = 0.25        # sunxds on its own
STUDENTS_BOTH = 0.45      # both trained models at least this sure
STUDENT_WITH_COCO = 0.40  # one trained model at least this sure, and the COCO model...
COCO_SURE = 0.35          # ...at least this sure
SUNXDS_WITH_HELP = 0.12   # sunxds this sure with the COCO model or a trained model as sure as above

# Evidence too weak for a player but too strong to call the place empty: painted grey.
UNSURE = {"sunxds": 0.12, "coco": 0.35, "gen2": 0.25, "sunft": 0.25}


def iou(a, b):
    """Intersection over union of two [x0, y0, x1, y1, ...] boxes."""
    w = min(a[2], b[2]) - max(a[0], b[0])
    h = min(a[3], b[3]) - max(a[1], b[1])
    if w <= 0 or h <= 0:
        return 0.0
    inter = w * h
    return inter / ((a[2] - a[0]) * (a[3] - a[1]) + (b[2] - b[0]) * (b[3] - b[1]) - inter)


@dataclass
class Person:
    """A person the teachers saw: the box of the surest model, and each model's best score on it."""

    box: list
    scores: dict = field(default_factory=dict)
    own_votes: int = 0     # trained models that called it the player's own character

    def score(self, model):
        """The model's best score on this person, 0 when it gave none."""
        return self.scores.get(model, 0.0)

    def is_player(self):
        """Enough evidence that this is a person in the game."""
        s, c, g, f = (self.score(m) for m in ("sunxds", "coco", "gen2", "sunft"))
        student = max(g, f)
        return (s >= SUNXDS_SURE or (g >= STUDENTS_BOTH and f >= STUDENTS_BOTH)
                or (student >= STUDENT_WITH_COCO and c >= COCO_SURE)
                or (s >= SUNXDS_WITH_HELP and (c >= COCO_SURE or student >= STUDENT_WITH_COCO)))

    def is_unsure(self):
        """Some evidence, not enough for a player."""
        return not self.is_player() and any(self.score(m) >= bar for m, bar in UNSURE.items())


def people_in(boxes):
    """Joins the people boxes of all models (sunxds player, COCO person, trained models' body and own character) into
    one list, surest first."""
    found = []
    for model, cls_ok in (("sunxds", (0,)), ("gen2", (0, 2)), ("sunft", (0, 2)), ("coco", (0,))):
        for b in boxes.get(model, []):
            if b[5] not in cls_ok:
                continue
            match = max(found, key=lambda p: iou(p.box, b), default=None)
            if match is None or iou(match.box, b) < MERGE_IOU:
                match = Person(box=list(b[:4]))
                found.append(match)
            if b[4] > match.score(model):
                match.scores[model] = b[4]
            if model in ("gen2", "sunft") and b[5] == 2 and b[4] >= 0.5:
                match.own_votes += 1
    return sorted(found, key=lambda p: -max(p.scores.values()))


def heads_in(boxes, bodies):
    """Head boxes (sunxds and the trained models) that lie inside one of the given bodies, merged."""
    heads = []
    for model in ("sunxds", "gen2", "sunft"):
        for b in boxes.get(model, []):
            if b[5] != 1 or b[4] < 0.25:
                continue
            cx, cy = (b[0] + b[2]) / 2, (b[1] + b[3]) / 2
            if not any(p[0] <= cx <= p[2] and p[1] <= cy <= p[3] for p in bodies):
                continue
            if all(iou(h, b) < MERGE_IOU for h in heads):
                heads.append(list(b[:4]))
    return heads
