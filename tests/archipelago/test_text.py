"""The found-message text for chests and search spots (archipelago/dwr/text.py).

Run from the repo root:  python -m unittest tests/archipelago/test_text.py
"""
import importlib.util
import os
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
spec = importlib.util.spec_from_file_location("dwr_text", os.path.join(ROOT, "archipelago", "dwr", "text.py"))
T = importlib.util.module_from_spec(spec)
spec.loader.exec_module(T)


class FoundText(unittest.TestCase):
    def test_own_item(self):
        self.assertEqual(T.found_text("Silver Harp"), "Silver Harp")
        self.assertEqual(T.found_text("Stones of Sunlight"), "Sunlight Stones")

    def test_other_players_item_with_their_name_when_it_fits(self):
        self.assertEqual(T.found_text("Herb", "Alex"), "Alex's Herb")

    def test_the_item_name_is_never_cut_for_the_owner(self):
        self.assertEqual(T.found_text("Silver Harp", "TheGobOne"), "T.'s Silver Harp")
        self.assertEqual(T.found_text("Sunlight Stones", "TheGobOne"), "Sunlight Stones")

    def test_long_names_are_cut_to_16(self):
        self.assertEqual(len(T.found_text("Progressive Sword Upgrade", "Somebody")), 16)

    def test_long_owner_is_cut_with_a_period(self):
        self.assertEqual(T.found_text("Herb", "Bartholomew"), "Bartholo.'s Herb")
        self.assertEqual(len(T.found_text("Herb", "Bartholomew")), 16)

    def test_accents_are_folded_for_the_font(self):
        self.assertEqual(T.found_text("Herb", "Zo\u00eb"), "Zoe's Herb")
        self.assertEqual(T.game_name("Pok\u00e9 Ball (Great)"), "Poke Ball Great")

    def test_owner_with_no_font_characters_is_left_out(self):
        self.assertEqual(T.found_text("Herb", "\u5c71\u7530"), "Herb")


if __name__ == "__main__":
    unittest.main()
