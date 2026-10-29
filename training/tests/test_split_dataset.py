"""The dataset split keeps the frames of one video together, so that validation never holds near copies of training images."""

import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src"))

import split_dataset  # noqa: E402


def _make_frames(folder: Path, videos: dict[str, int]) -> None:
    folder.mkdir(parents=True, exist_ok=True)
    for video, count in videos.items():
        for index in range(count):
            stem = f"{video}_frame_{index:05d}"
            (folder / f"{stem}.jpg").write_bytes(b"\xff\xd8")
            (folder / f"{stem}.txt").write_text("0 0.5 0.5 0.1 0.2\n", encoding="utf-8")


def _split_of(dataset: Path, stem: str) -> str:
    found = [name for name in ("train", "valid", "test") if (dataset / name / "images" / f"{stem}.jpg").exists()]
    assert len(found) == 1, (stem, found)
    return found[0]


class SplitDatasetTest(unittest.TestCase):
    def test_the_clip_of_a_numbered_frame_is_its_video(self):
        self.assertEqual(split_dataset.clip_of("match_one_frame_00042"), "match_one")
        self.assertEqual(split_dataset.clip_of("frame_frame_7"), "frame")

    def test_a_name_that_is_not_a_numbered_frame_is_its_own_clip(self):
        self.assertEqual(split_dataset.clip_of("negative_0042"), "negative_0042")
        self.assertEqual(split_dataset.clip_of("lobby_frame_x"), "lobby_frame_x")

    def test_all_frames_of_one_video_land_in_the_same_split(self):
        videos = {f"video{i:03d}": 6 for i in range(200)}
        with tempfile.TemporaryDirectory() as root:
            source = Path(root) / "frames"
            dataset = Path(root) / "dataset"
            _make_frames(source, videos)
            self.assertEqual(split_dataset.split(source, dataset, 0.8, 0.15, 0.05, move=False), 0)
            used = set()
            for video, count in videos.items():
                splits = {_split_of(dataset, f"{video}_frame_{index:05d}") for index in range(count)}
                self.assertEqual(len(splits), 1, f"{video} was split across {splits}")
                used |= splits
            # With two hundred videos every split gets some: the split is by video, not all in one place.
            self.assertEqual(used, {"train", "valid", "test"})

    def test_running_again_with_new_frames_never_moves_an_old_one(self):
        with tempfile.TemporaryDirectory() as root:
            source = Path(root) / "frames"
            dataset = Path(root) / "dataset"
            _make_frames(source, {f"video{i:02d}": 10 for i in range(20)})
            split_dataset.split(source, dataset, 0.8, 0.15, 0.05, move=False)
            before = {p.stem: _split_of(dataset, p.stem) for p in source.glob("*.jpg")}
            _make_frames(source, {"video00": 15, "video99": 10})   # more frames of an old video, and a new video
            split_dataset.split(source, dataset, 0.8, 0.15, 0.05, move=False)
            for stem, name in before.items():
                self.assertEqual(_split_of(dataset, stem), name)
            # The new frames of an old video follow it, and a new video is kept whole.
            for index in range(10, 15):
                self.assertEqual(_split_of(dataset, f"video00_frame_{index:05d}"), before["video00_frame_00000"])
            new = {_split_of(dataset, f"video99_frame_{index:05d}") for index in range(10)}
            self.assertEqual(len(new), 1)


    def test_one_video_that_leaves_valid_empty_is_an_error_that_says_so(self):
        # A video whose name sends it to train: all of it goes there and valid stays empty.
        name = next(n for n in ("clipA", "clipB", "clipC", "clipD") if split_dataset._bucket_for(n, 0.8, 0.15) == "train")
        with tempfile.TemporaryDirectory() as root:
            source = Path(root) / "frames"
            dataset = Path(root) / "dataset"
            _make_frames(source, {name: 30})
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                code = split_dataset.split(source, dataset, 0.8, 0.15, 0.05, move=False)
            self.assertEqual(code, 3)
            self.assertIn("validation split is empty", output.getvalue())
            self.assertIn("--by-frame", output.getvalue())

    def test_by_frame_fills_every_split_even_from_one_video(self):
        with tempfile.TemporaryDirectory() as root:
            source = Path(root) / "frames"
            dataset = Path(root) / "dataset"
            _make_frames(source, {"onlyvideo": 400})
            with contextlib.redirect_stdout(io.StringIO()):
                code = split_dataset.split(source, dataset, 0.8, 0.15, 0.05, move=False, by_frame=True)
            self.assertEqual(code, 0)
            used = {_split_of(dataset, f"onlyvideo_frame_{index:05d}") for index in range(400)}
            self.assertEqual(used, {"train", "valid", "test"})


if __name__ == "__main__":
    unittest.main()
