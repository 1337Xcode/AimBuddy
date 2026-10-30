"""Exports a trained universal checkpoint to the three files the app and the model store read.

The network has the dataset's three classes, so model.json maps them to the app's roles: body, head, and ignore for
the player's own character (an anchor whose best class is the own character is dropped by the app's decoder). The
NCNN exporter drops YOLO26's end-to-end branch, which leaves the raw output the decoder reads.

Usage: export.py <best.pt> <output folder> <input size> <version>
"""
import json
import pathlib
import shutil
import sys

from ultralytics import YOLO


def main():
    """Exports one checkpoint at one input size and writes its model.json."""
    weights = pathlib.Path(sys.argv[1]).resolve()
    out = pathlib.Path(sys.argv[2]).resolve()
    size = int(sys.argv[3])
    version = int(sys.argv[4])
    model = YOLO(str(weights))
    names = [model.names[k] for k in sorted(model.names)]
    if names != ["body", "head", "self"]:
        raise SystemExit(f"{weights.name} has the classes {names}, not body, head and self")
    exported = pathlib.Path(str(model.export(format="ncnn", imgsz=size, half=False, simplify=True, batch=1)))
    folder = exported if exported.is_dir() else exported.parent
    out.mkdir(parents=True, exist_ok=True)
    shutil.copy2(next(folder.glob("*.param")), out / "model.param")
    shutil.copy2(next(folder.glob("*.bin")), out / "model.bin")
    profile = {
        "title": "AimBuddy General",
        "description": "Finds players in Android and PC shooters, first and third person, and leaves out your own character.",
        "author": "AimBuddy",
        "version": version,
        "roles": ["body", "head", "ignore"],
        "inputSize": size,
    }
    # one key a line, lists on their line, as the store's other model.json files are written
    body = ",\n".join(f"  {json.dumps(key)}: {json.dumps(value)}" for key, value in profile.items())
    (out / "model.json").write_text("{\n" + body + "\n}\n", encoding="utf-8")
    print("exported", out, "from", weights)


if __name__ == "__main__":
    main()
