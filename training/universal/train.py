"""Trains the universal detector on the squares build_dataset.py made: enemy body, enemy head and own character, at 320 px.

The base is the COCO YOLO26n, which keeps what it learned where the shapes allow; the class layer is new.
"""
import argparse
import pathlib

from ultralytics import YOLO


def main():
    """Trains one run with the settings AimBuddy General was trained with."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--base", required=True, help="the checkpoint to start from, for example yolo26n.pt")
    parser.add_argument("--data", required=True, help="data.yaml of the built squares")
    parser.add_argument("--name", default="universal", help="name of the run folder")
    parser.add_argument("--project", default=str(pathlib.Path(__file__).resolve().parents[1] / "outputs" / "universal" / "runs"))
    parser.add_argument("--epochs", type=int, default=60)
    parser.add_argument("--batch", type=int, default=64)
    parser.add_argument("--workers", type=int, default=6)
    args = parser.parse_args()

    YOLO(args.base).train(
        data=args.data,
        imgsz=320,
        epochs=args.epochs,
        batch=args.batch,
        workers=args.workers,
        # MuSGD at 0.01, which Ultralytics picks for YOLO26 on its own at this many steps, made the loss climb once
        # warm-up ended (mAP50 0.67, 0.62, 0.36 over the first three epochs). AdamW at the rate Ultralytics uses for a
        # three-class fine-tune held.
        optimizer="AdamW",
        lr0=0.0014,
        device=0,
        project=args.project,
        name=args.name,
        exist_ok=False,
        seed=20261030,
        cos_lr=True,
        patience=20,
        # The squares are already cut at the app's scale; mosaic adds smaller and partly hidden people on top.
        mosaic=0.5,
        close_mosaic=10,
        scale=0.3,
        translate=0.1,
        degrees=0.0,
        fliplr=0.5,
        mixup=0.0,
        hsv_h=0.015,
        hsv_s=0.5,
        hsv_v=0.4,
        amp=True,
        cache=False,
        plots=True,
    )


if __name__ == "__main__":
    main()
