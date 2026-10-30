"""The universal model's labelling rules: where the player's own character is, which squares show a broadcast face card,
and how much of a box a square keeps."""

import importlib.util
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "universal"))

HAS_ULTRALYTICS = importlib.util.find_spec("ultralytics") is not None
if HAS_ULTRALYTICS:
    import build_dataset  # noqa: E402
    import teachers  # noqa: E402


@unittest.skipUnless(HAS_ULTRALYTICS, "the universal pipeline needs ultralytics")
class OwnCharacterPlaceTest(unittest.TestCase):
    def test_a_tall_figure_low_in_the_middle_is_the_own_character(self):
        # a 1280 x 720 frame: the third-person camera draws the player low and a little left of the middle
        own = teachers.Box(520, 380, 660, 720, 0.8)
        self.assertTrue(teachers.in_own_place(own, 1280, 720))

    def test_an_enemy_up_the_screen_or_off_to_the_side_is_not(self):
        far = teachers.Box(700, 300, 730, 380, 0.8)       # small, mid height
        aside = teachers.Box(1100, 400, 1240, 720, 0.8)   # tall and low, but at the right edge
        self.assertFalse(teachers.in_own_place(far, 1280, 720))
        self.assertFalse(teachers.in_own_place(aside, 1280, 720))


@unittest.skipUnless(HAS_ULTRALYTICS, "the universal pipeline needs ultralytics")
class BroadcastFaceTest(unittest.TestCase):
    def test_a_head_filling_a_square_box_is_a_face_card(self):
        lines = [(0, (40, 40, 60, 62)), (1, (45, 42, 55, 52))]
        self.assertTrue(build_dataset.shows_face(lines))

    def test_a_standing_player_with_a_small_head_is_kept(self):
        lines = [(0, (40, 20, 50, 60)), (1, (42, 20, 48, 26))]
        self.assertFalse(build_dataset.shows_face(lines))


@unittest.skipUnless(HAS_ULTRALYTICS, "the universal pipeline needs ultralytics")
class ClipTest(unittest.TestCase):
    def test_the_part_inside_the_square_and_its_share(self):
        box = teachers.Box(90, 0, 110, 40, 1.0)   # half of it left of the square's right edge at 100
        part, visible = build_dataset.clip_to(box, 0, 0, 100)
        self.assertEqual(part, (90, 0, 100, 40))
        self.assertAlmostEqual(visible, 0.5)

    def test_a_box_outside_the_square_has_no_part(self):
        part, visible = build_dataset.clip_to(teachers.Box(120, 0, 140, 40, 1.0), 0, 0, 100)
        self.assertIsNone(part)
        self.assertEqual(visible, 0.0)


if __name__ == "__main__":
    unittest.main()
