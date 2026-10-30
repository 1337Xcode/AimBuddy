"""The universal model's labelling rules (training/universal): who is the player's own character, what is fixed on the
screen, which teachers' boxes make a player, and how a square is cut and painted."""

import importlib.util
import random
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "universal"))

HAS_OPENCV = importlib.util.find_spec("cv2") is not None and importlib.util.find_spec("numpy") is not None
if HAS_OPENCV:
    import cv2
    import numpy as np

    import cut_squares  # noqa: E402
    import flat_marks  # noqa: E402
    import fusion  # noqa: E402
    import hud  # noqa: E402
    import own_character  # noqa: E402


def figure(top_bgr, bottom_bgr):
    """A 200 x 200 picture of a figure at [60, 20, 140, 180]: one colour above its middle, another below."""
    image = np.zeros((200, 200, 3), np.uint8)
    image[20:100, 60:140] = top_bgr
    image[100:180, 60:140] = bottom_bgr
    return image


@unittest.skipUnless(HAS_OPENCV, "the universal pipeline needs opencv and numpy")
class OwnCharacterTest(unittest.TestCase):
    def test_a_tall_figure_low_in_the_middle_stands_in_the_own_place(self):
        # a 1280 x 720 frame: the third-person camera draws the player low and a little left of the middle
        self.assertTrue(own_character.in_own_place([520, 380, 660, 720], 1280, 720))

    def test_an_enemy_up_the_screen_or_off_to_the_side_does_not(self):
        self.assertFalse(own_character.in_own_place([700, 300, 730, 380], 1280, 720))    # small, mid height
        self.assertFalse(own_character.in_own_place([1100, 400, 1240, 720], 1280, 720))  # tall and low, at the edge

    def test_a_look_seen_in_many_frames_is_the_own_character_and_a_passing_one_is_not(self):
        own = own_character.look_of(figure((0, 0, 200), (80, 40, 40)), [60, 20, 140, 180])
        enemy = own_character.look_of(figure((0, 180, 0), (0, 200, 200)), [60, 20, 140, 180])
        decisions = own_character.own_candidates([own] * 20 + [enemy] * 2)
        self.assertEqual(decisions, [True] * 20 + [False] * 2)

    def test_a_piece_of_the_own_character_is_part_of_it_and_a_far_player_over_its_shoulder_is_not(self):
        own = [500, 300, 620, 700]
        self.assertTrue(own_character.is_part_of(own, [500, 520, 620, 700]))    # what a kill banner leaves in view
        self.assertFalse(own_character.is_part_of(own, [560, 310, 580, 350]))   # small, far behind it
        self.assertFalse(own_character.is_part_of(own, [640, 300, 760, 700]))   # beside it


@unittest.skipUnless(HAS_OPENCV, "the universal pipeline needs opencv and numpy")
class FixedOnScreenTest(unittest.TestCase):
    def test_a_box_that_comes_back_at_one_place_is_part_of_the_screen_and_a_runner_is_not(self):
        button = [(index, [1100, 500, 1150, 560]) for index in range(10)]
        runner = [(index, [100 + 30 * index, 300, 140 + 30 * index, 400]) for index in range(10)]
        self.assertEqual(hud.fixed_on_screen(button + runner, 100), [True] * 10 + [False] * 10)


@unittest.skipUnless(HAS_OPENCV, "the universal pipeline needs opencv and numpy")
class TeacherEvidenceTest(unittest.TestCase):
    def test_two_models_on_one_person_make_one_player(self):
        people = fusion.people_in({"sunxds": [[100, 100, 140, 200, 0.30, 0]], "coco": [[102, 98, 141, 203, 0.60, 0]]})
        self.assertEqual(len(people), 1)
        self.assertEqual(people[0].scores, {"sunxds": 0.30, "coco": 0.60})
        self.assertTrue(people[0].is_player())

    def test_the_coco_model_alone_makes_a_person_nobody_can_vouch_for(self):
        people = fusion.people_in({"coco": [[100, 100, 140, 200, 0.60, 0]]})
        self.assertFalse(people[0].is_player())
        self.assertTrue(people[0].is_unsure())

    def test_both_trained_models_calling_it_the_own_character_are_two_votes(self):
        people = fusion.people_in({"gen2": [[100, 100, 140, 200, 0.70, 2]], "sunft": [[101, 100, 140, 201, 0.60, 2]]})
        self.assertEqual(people[0].own_votes, 2)


@unittest.skipUnless(HAS_OPENCV, "the universal pipeline needs opencv and numpy")
class FlatMarkTest(unittest.TestCase):
    def test_a_figure_in_one_flat_colour_is_a_mark(self):
        image = np.zeros((200, 200, 3), np.uint8)
        image[40:180, 70:130] = (0, 220, 230)   # the yellow a broadcast draws a hidden player in
        self.assertTrue(flat_marks.is_flat_mark(image, [65, 35, 135, 185]))

    def test_a_shaded_many_coloured_figure_is_not(self):
        image = np.zeros((200, 200, 3), np.uint8)
        image[40:180, 70:130] = np.random.default_rng(1).integers(0, 256, (140, 60, 3), dtype=np.uint8)
        self.assertFalse(flat_marks.is_flat_mark(image, [65, 35, 135, 185]))


@unittest.skipUnless(HAS_OPENCV, "the universal pipeline needs opencv and numpy")
class FrameLabelsTest(unittest.TestCase):
    def setUp(self):
        self.own = fusion.Person(box=[500, 300, 620, 700], scores={"sunxds": 0.6})
        # a close player both trained models take for the own character, as they learned from the first labels
        self.beside = fusion.Person(box=[700, 300, 800, 700], scores={"sunxds": 0.5, "gen2": 0.7, "sunft": 0.7},
                                    own_votes=2)
        self.piece = fusion.Person(box=[500, 520, 620, 700], scores={"sunxds": 0.4})

    def test_someone_beside_the_own_character_is_a_player_and_a_piece_of_it_is_left_alone(self):
        found = [self.own, self.beside, self.piece]
        labels = cut_squares.frame_labels(1, found, [False] * 3, {(1, 0)}, {(1, 0), (1, 1)}, set(), self.own.box, {})
        self.assertEqual(labels.own, [self.own.box])
        self.assertEqual(labels.bodies, [self.beside.box])
        self.assertEqual(labels.grey, [])

    def test_someone_in_its_place_when_it_is_not_seen_is_unsure(self):
        labels = cut_squares.frame_labels(2, [self.beside], [False], set(), {(2, 0)}, set(), self.own.box, {})
        self.assertEqual(labels.bodies, [])
        self.assertEqual(labels.grey, [self.beside.box])

    def test_a_player_fixed_on_the_screen_is_unsure(self):
        labels = cut_squares.frame_labels(3, [self.beside], [False], set(), set(), {(3, 0)}, None, {})
        self.assertEqual(labels.grey, [self.beside.box])


@unittest.skipUnless(HAS_OPENCV, "the universal pipeline needs opencv and numpy")
class SquareTest(unittest.TestCase):
    def test_a_head_filling_a_square_box_is_a_face_card(self):
        lines = [(0, [40, 40, 60, 62]), (1, [45, 42, 55, 52])]
        self.assertTrue(cut_squares.shows_face(lines))

    def test_a_standing_player_with_a_small_head_is_kept(self):
        lines = [(0, [40, 20, 50, 60]), (1, [42, 20, 48, 26])]
        self.assertFalse(cut_squares.shows_face(lines))

    def test_the_part_inside_the_square_and_its_share(self):
        part, visible = cut_squares.clip_to([90, 0, 110, 40], 0, 0, 100)   # half of it left of the edge at 100
        self.assertEqual(part, [90, 0, 100, 40])
        self.assertAlmostEqual(visible, 0.5)

    def test_a_box_outside_the_square_has_no_part(self):
        part, visible = cut_squares.clip_to([120, 0, 140, 40], 0, 0, 100)
        self.assertIsNone(part)
        self.assertEqual(visible, 0.0)

    def test_grey_paint_never_covers_a_labelled_player(self):
        frame = np.full((400, 400, 3), 50, np.uint8)
        frame[100:300, 100:200] = 200
        labels = cut_squares.Labels()
        labels.bodies = [[100, 100, 200, 300]]
        labels.grey = [[150, 150, 250, 250]]   # an unsure box on the player's arm and beyond
        with tempfile.TemporaryDirectory() as folder:
            out = (Path(folder), Path(folder))
            count = cut_squares.write_square(frame, (0, 0, 400), labels, out, "square", random.Random(1), augment=False)
            picture = cv2.imread(str(Path(folder) / "square.jpg")).astype(np.float32)
            written = (Path(folder) / "square.txt").read_text()
        self.assertEqual(count, 1)
        self.assertEqual(written, "0 0.375000 0.500000 0.250000 0.500000\n")
        # 400 px squares are written at 320 px: the player is x 80-160, y 80-240; the grey part outside it x 160-200
        self.assertGreater(picture[96:224, 88:152].mean(), 190)
        self.assertLess(abs(picture[128:192, 168:192].mean() - 114), 6)

    def test_a_covered_player_keeps_a_label_on_what_stays_in_view(self):
        for seed in range(12):
            crop = np.full((320, 320, 3), 50, np.uint8)
            crop[60:260, 100:160] = 200
            lines = cut_squares.cover_one(crop, [(0, [100, 60, 160, 260])], random.Random(seed))
            self.assertEqual(len(lines), 1)
            a, b, c, d = lines[0][1]
            self.assertTrue(100 <= a and 60 <= b and c <= 160 and d <= 260)
            self.assertTrue(0.39 <= (c - a) * (d - b) / (60 * 200) <= 0.71)
            covered = np.ones((320, 320), bool)
            covered[int(b):int(d), int(a):int(c)] = False
            covered[:60] = covered[260:] = covered[:, :100] = covered[:, 160:] = False
            # the covering piece comes from the empty scenery round the player
            self.assertLess(crop[covered].mean(), 60)


if __name__ == "__main__":
    unittest.main()
