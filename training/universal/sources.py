"""What the universal model learns from: the gameplay videos, how often frames are taken from each, the broadcast overlay
bands painted over before anything looks at a frame, and the labelled PC sets. Shared by label_frames.py and
cut_squares.py.
"""
import cv2

GREY = (114, 114, 114)

# Where broadcast overlays sit, as shares of the frame: top, bottom, left, right.
BROADCAST_BANDS = (0.06, 0.15, 0.14, 0.14)

# game, video, split, seconds between frames, broadcast. Free Fire matches change slowly and are long; the PUBG Mobile
# highlights are short and dense in fighting, so they are sampled more often.
VIDEOS = [
    ("freefire", "5mJrCitRC_w", "train", 1.0, False),
    ("freefire", "6s5w3MWmqSQ", "train", 1.0, False),
    ("freefire", "Cm2r7uPPeuk", "train", 1.0, False),
    ("freefire", "F9d35_J7I3I", "train", 1.0, False),
    ("freefire", "IfGyARltkSs", "train", 1.0, False),
    ("freefire", "P_itov3W6l4", "train", 1.0, False),
    ("freefire", "Sk0TAusKxHc", "train", 1.0, False),
    ("freefire", "Y5xb6QiUsbs", "train", 1.0, False),
    ("freefire", "f-4M7Ff0hxE", "train", 1.0, False),
    ("freefire", "jmvmum3eurI", "train", 1.0, False),
    ("freefire", "tmIOqcqS9kQ", "train", 1.0, False),
    ("freefire", "wdil2Ny12Aw", "train", 1.0, False),
    ("freefire", "6XhS2eiXu1c", "val", 1.0, False),
    ("freefire", "O5fHr3-GhP0", "val", 1.0, False),
] + [("pubgm", f"chunk_{i:03d}", "train", 0.5, True) for i in range(9)] + [
    ("pubgm", "chunk_009", "val", 0.5, True),
] + [("pubgm", name, "train", 0.25, True) for name in
     ("highlight", "highlight2", "highlight4", "highlight5", "highlight6", "highlight8", "highlight10", "highlight11",
      "highlight13", "highlight17", "hightlight16")] + [
    ("pubgm", "highlight19", "train", 0.25, False),   # a phone recording, no broadcast overlay
    ("pubgm", "highlight9", "val", 0.25, True),
    ("pubgm", "highlight14", "val", 0.25, True),
]


def grey_out_broadcast(frame):
    """Paints the bands where broadcasts put their overlays grey, in place."""
    height, width = frame.shape[:2]
    top, bottom, left, right = BROADCAST_BANDS
    frame[: int(top * height)] = GREY
    frame[int((1 - bottom) * height):] = GREY
    frame[:, : int(left * width)] = GREY
    frame[:, int((1 - right) * width):] = GREY


# PC game sets (CC BY 4.0): folder, class name to our class (0 body, 1 head; None: not a person, left as it is).
PC_SETS = [
    ("csgo-videogame", {"CT": 0, "T": 0}),
    ("apex-videogame", {"avatar": 0, "object": None}),
    ("halo-infinite-angel-videogame", {"enemy": 0, "friendly": 0, "enemy-head": 1, "friendly-head": 1}),
    ("farcry6-videogame", {"person": 0, "assassin": 0}),
]
PC_SPLITS = {"train": "train", "valid": "val"}   # the sets' own test pictures are kept for judging


def has_heads(mapping):
    """True for a PC set whose labels include heads."""
    return 1 in mapping.values()


def pc_pictures(root, folder):
    """Yields (source split, our split, picture path) for one PC set, in a fixed order."""
    for source_split, split in PC_SPLITS.items():
        folder_path = root / folder / source_split / "images"
        if folder_path.exists():
            for path in sorted(folder_path.glob("*.jpg")):
                yield source_split, split, path


def read_pc_picture(path):
    """A PC set picture at its game's shape: the sets were stretched to 640 x 640 from 16:9 screenshots."""
    image = cv2.imread(str(path))
    height = image.shape[0]
    return cv2.resize(image, (int(round(height * 16 / 9)), height), interpolation=cv2.INTER_LINEAR)


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
