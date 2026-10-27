# AimBuddy model store

The app's Store tab lists the models in this folder. It reads one file, `index.json`, and downloads
the files of the model the player picks. The app checks every download against the size and SHA-256
that `index.json` gives, so a model only installs if its files are exactly the ones published here.

`index.json` is generated. Do not edit it by hand.

## Add a model

1. Make a folder named for the model's id: lowercase letters, digits and dashes, 2 to 40 characters, not ending in a dash.
2. Put these files in it:

   | File | What it is |
   | --- | --- |
   | `model.param` | The NCNN network graph. |
   | `model.bin` | The NCNN weights. |
   | `model.json` | What the model is and how to read its output (below). |
   | `icon.png` | Optional. A square PNG of at most 128 KB, shown in the store. |

3. Run `python model/index.py` to rebuild `index.json`, and commit it with the folder.
4. Open a pull request. A check runs `python model/index.py --check` and fails if `index.json` is out of date.

Files are served exactly as committed, so do not change their line endings.

## model.json

```json
{
  "title": "AimBuddy General",
  "description": "Finds people in most 3D shooters without training for your game.",
  "author": "AimBuddy",
  "version": 1,
  "roles": ["body"],
  "inputSize": 256
}
```

- `title` (at most 40 characters), `description` (at most 160) and `author` (at most 40) are shown in the store.
- `version` is a whole number from 1. Raise it whenever the model's files change: the app offers an
  update to everyone who has an older version.
- `roles` lists what each class of the network is, in class order: `body` (an enemy), `head` (an
  enemy's head) or `ignore` (teammates, anything that must not be highlighted). Classes that are not
  listed are ignored. With no `roles`, one class means `body` and two classes mean `body` then `head`.
- `inputSize` is the network's input size in pixels. Leave it out to have the app work it out.
- `endToEnd` is `true` for an export that already removes duplicate boxes (output of N rows of six numbers).

The app saves this file next to the model and reads `roles`, `inputSize` and `endToEnd` from it.

## Licenses

Only add a model whose license lets this project redistribute it. State the license of the weights
in the pull request. The models the project ships are detectors trained with Ultralytics YOLO, which
is licensed under AGPL-3.0.
