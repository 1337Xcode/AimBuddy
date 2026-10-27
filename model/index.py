"""Builds model/index.json, the list the app's store reads, from the model folders next to this file.

Each folder holds model.param, model.bin and model.json (title, description, author, version, and the
roles and inputSize the app reads), and optionally icon.png. Run `python model/index.py` after adding
or changing a model. `python model/index.py --check` fails when index.json is out of date, which is
what the pull request check runs.
"""
import hashlib
import json
import os
import re
import sys

STORE = os.path.dirname(os.path.abspath(__file__))
INDEX = os.path.join(STORE, "index.json")

SCHEMA = 1
ID_PATTERN = re.compile(r"^[a-z0-9][a-z0-9-]{1,39}$")
MAX_FILE_BYTES = 64 * 1024 * 1024
MAX_ICON_BYTES = 128 * 1024
PNG_MAGIC = b"\x89PNG\r\n\x1a\n"
NCNN_MAGIC = b"7767517"   # first line of every NCNN .param file


class Problem(Exception):
    """A model folder that cannot go in the store, with the reason."""


def describe(folder, name):
    """Returns ({path, size, sha256}, bytes) for a file in a model folder, or raises Problem if it is missing or too big."""
    path = os.path.join(STORE, folder, name)
    if not os.path.isfile(path):
        raise Problem(f"{folder}/{name} is missing")
    size = os.path.getsize(path)
    if size == 0 or size > MAX_FILE_BYTES:
        raise Problem(f"{folder}/{name} must be between 1 byte and {MAX_FILE_BYTES // (1024 * 1024)} MB")
    with open(path, "rb") as handle:
        data = handle.read()
    return {"path": f"{folder}/{name}", "size": size, "sha256": hashlib.sha256(data).hexdigest()}, data


def entry_for(folder):
    """Builds the index entry of one model folder."""
    if not ID_PATTERN.match(folder):
        raise Problem(f"{folder}: the folder name is the model's id: lowercase letters, digits and dashes, 2 to 40 characters")
    meta_file, meta_bytes = describe(folder, "model.json")
    try:
        meta = json.loads(meta_bytes.decode("utf-8"))
    except ValueError as error:
        raise Problem(f"{folder}/model.json is not valid JSON: {error}")
    title = str(meta.get("title", "")).strip()
    description = str(meta.get("description", "")).strip()
    author = str(meta.get("author", "")).strip()
    version = meta.get("version")
    if not title or len(title) > 40:
        raise Problem(f"{folder}/model.json needs a title of at most 40 characters")
    if not description or len(description) > 160:
        raise Problem(f"{folder}/model.json needs a description of at most 160 characters")
    if not author or len(author) > 40:
        raise Problem(f"{folder}/model.json needs an author of at most 40 characters")
    if not isinstance(version, int) or isinstance(version, bool) or version < 1:
        raise Problem(f"{folder}/model.json needs a version: a whole number from 1, raised whenever the files change")

    param_file, param_bytes = describe(folder, "model.param")
    bin_file, _ = describe(folder, "model.bin")
    if not param_bytes.startswith(NCNN_MAGIC):
        raise Problem(f"{folder}/model.param is not an NCNN param file")

    entry = {
        "id": folder,
        "title": title,
        "description": description,
        "author": author,
        "version": version,
    }
    if os.path.isfile(os.path.join(STORE, folder, "icon.png")):
        icon_file, icon_bytes = describe(folder, "icon.png")
        if not icon_bytes.startswith(PNG_MAGIC) or icon_file["size"] > MAX_ICON_BYTES:
            raise Problem(f"{folder}/icon.png must be a PNG of at most {MAX_ICON_BYTES // 1024} KB")
        entry["icon"] = icon_file
    entry["param"] = param_file
    entry["bin"] = bin_file
    entry["profile"] = meta_file
    return entry


def build():
    """Returns the text of index.json for the model folders that exist now."""
    folders = sorted(
        name for name in os.listdir(STORE)
        if os.path.isdir(os.path.join(STORE, name)) and not name.startswith((".", "_"))
    )
    models = [entry_for(folder) for folder in folders]
    return json.dumps({"schema": SCHEMA, "models": models}, indent=2, ensure_ascii=False) + "\n"


def main():
    check = "--check" in sys.argv[1:]
    try:
        text = build()
    except Problem as problem:
        print(f"error: {problem}")
        return 1
    current = open(INDEX, encoding="utf-8", newline="").read() if os.path.isfile(INDEX) else ""
    if check:
        if current != text:
            print("error: model/index.json is out of date. Run: python model/index.py")
            return 1
        print("model/index.json is up to date")
        return 0
    with open(INDEX, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    print(f"wrote model/index.json with {len(json.loads(text)['models'])} model(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
