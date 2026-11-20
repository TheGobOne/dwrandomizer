"""The found-message and mailbox text (archipelago/dwr/text.py).

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
    """found_text is the whole sentence (ROM version 005)."""
    def test_own_item(self):
        self.assertEqual(T.found_text("Silver Harp"), "Thou hast found the Silver Harp.")
        self.assertEqual(T.found_text("Stones of Sunlight"), "Thou hast found the Sunlight Stones.")

    def test_other_players_item_names_who_gets_it(self):
        self.assertEqual(T.found_text("Herb", "Alex"), "Herb for Alex.")
        self.assertEqual(T.found_text("Heart Container", "Alex"), "Heart Container for Alex.")

    def test_a_16_letter_owner_fits_whole(self):
        self.assertEqual(T.found_text("Sunlight Stones", "TheGobOneRandomz"), "Sunlight Stones for TheGobOneRandomz.")

    def test_long_owner_is_cut_and_the_item_is_not(self):
        self.assertEqual(T.found_text("Sunlight Stones", "TheGobOneRandomzXY"), "Sunlight Stones for TheGobOneRandomz-.")
        self.assertEqual(T.found_text("Progressive Sword Upgrade", "Somebody"), "Prog Sword Upgr for Somebody.")

    def test_every_sentence_fits_the_buffer(self):
        for item in ("Herb", "Heart Container", "Progressive Sword Upgrade", "Abcdefghijklmnopqrstu"):
            for owner in ("", "A", "Bartholomew", "Abcdefghijklmnop"):
                self.assertLessEqual(len(T.found_text(item, owner)), T.LINE_LEN, (item, owner))

    def test_accents_are_folded_for_the_font(self):
        self.assertEqual(T.found_text("Herb", "Zoë"), "Herb for Zoe.")
        self.assertEqual(T.game_name("Poké Ball (Great)"), "Poke Ball Great")

    def test_owner_with_no_font_characters(self):
        self.assertEqual(T.found_text("Herb", "山田"), "Herb for ?.")
        self.assertEqual(T.found_text("Herb", "山田", 3), "Herb for P3.")

    def test_letters_without_a_combining_accent_are_kept(self):
        self.assertEqual(T.found_text("Herb", "Łukasz"), "Herb for Lukasz.")
        self.assertEqual(T.found_text("Herb", "Ørjan"), "Herb for Orjan.")

    def test_an_item_with_no_font_characters_is_called_item(self):
        self.assertEqual(T.game_name("ダーク"), "item")
        self.assertEqual(T.found_text("ダーク", "Alex"), "item for Alex.")

    def test_a_cut_name_does_not_end_in_a_space(self):
        self.assertEqual(T.game_name("Master Sword Of Light"), "Master Sword Of")


class ReceivedLine(unittest.TestCase):
    """The mailbox sentences for ROM version 005."""
    def test_item_from_another_player_names_them(self):
        self.assertEqual(T.received_line("Heart Container", "Alex", 2), "Heart Container from Alex.")

    def test_long_sender_is_cut(self):
        self.assertEqual(T.received_line("Progressive Sword Upgrade", "Abcdefghijklmnop"),
                         "Prog Sword Upgr from Abcdefghijklmnop.")

    def test_no_sender_is_found(self):
        self.assertEqual(T.received_line("Herb"), "Thou hast found the Herb.")

    def test_batch(self):
        self.assertEqual(T.batch_line(5, "Alex"), "5 items from Alex.")
        self.assertEqual(T.batch_line(12), "Thou hast received 12 items.")
        self.assertLessEqual(len(T.batch_line(120, "Abcdefghijklmnop")), T.LINE_LEN)


class NoteLines(unittest.TestCase):
    def test_every_note_fits_the_buffer(self):
        longest = "Abcdefghijklmnopq"
        for line in (T.cap_line("Magic Key"), T.replaced_line(longest, "Fairy Water"),
                     T.dropped_line("Fairy Water"), T.waiting_line(longest), T.full_line(999),
                     T.received_line(longest, "Abcdefghijklmnop")):
            self.assertLessEqual(len(line), T.LINE_LEN, line)


class KeyItemsAndMarks(unittest.TestCase):
    def test_progression_mark_ends_the_sentence_in_an_exclamation_mark(self):
        self.assertEqual(T.received_line("Herb", "Alex", 2, True), "Herb from Alex!")
        self.assertEqual(T.found_text("Herb", "Alex", 2, True), "Herb for Alex!")
        self.assertEqual(T.found_text("Herb", "Alex", 2), "Herb for Alex.")

    def test_key_items_are_named_or_counted(self):
        self.assertEqual(T.includes_line(["Silver Harp"]), "Includes Silver Harp.")
        self.assertEqual(T.includes_line(["Silver Harp", "Fairy Flute"]), "Includes Silver Harp and Fairy Flute.")
        self.assertEqual(T.includes_line(["Staff of Rain", "Erdrick's Token"]), "Includes Staff of Rain and 1 more.")
        self.assertEqual(T.includes_line(["Silver Harp", "Staff of Rain", "Erdrick's Token"]), "Includes Silver Harp and 2 more.")

    def test_cut_owner_ends_in_a_dash(self):
        self.assertEqual(T.found_text("Herb", "Bartholomew"), "Herb for Bartholomew.")
        self.assertEqual(T.found_text("Sunlight Stones", "Abcdefghijklmnopqrs"), "Sunlight Stones for Abcdefghijklmnop-.")
        self.assertEqual(T.batch_line(5, "Abcdefghijklmnopqrstuvwxyz0123").count("-"), 1)


def wrap(text, width=22):
    """The lines the game's dialog box breaks text into: at word ends, 22 columns wide."""
    lines, line = [], ""
    for word in text.split():
        if line and len(line) + 1 + len(word) > width:
            lines.append(line)
            line = word
        else:
            line = f"{line} {word}" if line else word
    return lines + [line] if line else lines


class Layout(unittest.TestCase):
    """Every sentence fits the buffer and at most 3 lines of the box, after the 2-line greeting."""
    def test_line_len_matches_the_rom(self):
        with open(os.path.join(ROOT, "common", "archipelago.h"), encoding="utf-8") as f:
            header = f.read()
        self.assertIn(f"#define AP_TEXT_LEN   {T.LINE_LEN}", header)

    def test_worst_cases_fit_three_lines(self):
        item, owner = "Abcdefghijklmnop", "Abcdefghijklmnop"
        for line in (T.found_text(item), T.found_text(item, owner), T.received_line(item, owner),
                     T.batch_line(999, owner), T.batch_line(999), T.cap_line("Magic Key"),
                     T.replaced_line(item, "Fairy Water"), T.dropped_line("Fairy Water"),
                     T.waiting_line(item), T.full_line(999), T.includes_line([item, item]),
                     T.found_text("Heart Container", "Bartholomew"), T.received_line("Erdrick's Token", "TheGobOneRandomz")):
            self.assertLessEqual(len(line), T.LINE_LEN, line)
            self.assertLessEqual(len(wrap(line)), 3, (line, wrap(line)))

    def test_batch_without_font_letters_names_the_player_number(self):
        self.assertEqual(T.batch_line(5, "山田", 3), "5 items from P3.")


class ReceivedText(unittest.TestCase):
    def test_item_from_another_player_names_them(self):
        self.assertEqual(T.received_text("Herb", "Alex", 2), "Herb from Alex")

    def test_long_sender_is_cut_with_a_period(self):
        self.assertEqual(T.received_text("Herb", "Bartholomew"), "Herb from Barth.")

    def test_long_item_is_never_shortened_to_make_room_for_the_sender(self):
        self.assertEqual(T.received_text("Erdrick's Sword", "Alex"), "Erdrick's Sword")
        self.assertEqual(T.received_text("Staff of Rain", "Al"), "Staff of Rain")
        self.assertEqual(T.received_text("Staff of Rain", "Bartholomew"), "Staff of Rain")
        self.assertEqual(T.received_text("Master Sword", "Link"), "Master Sword")
        self.assertEqual(T.game_name("Progressive Sword Upgrade"), "Prog Sword Upgr")
        self.assertEqual(T.received_text("Progressive Sword Upgrade", "Alex"), "Prog Sword Upgr")

    def test_sender_is_cut_before_the_item_is(self):
        self.assertEqual(T.received_text("Fairy Water", "Al"), "Fairy Water")
        self.assertEqual(T.received_text("100 Gold", "Friend"), "100 Gold from F.")
        self.assertEqual(T.received_text("Staff of Rain", "A"), "Staff of Rain")

    def test_found_text_cuts_at_a_word_end_and_keeps_a_number(self):
        self.assertEqual(T.game_name("Small Key (Dungeon 1)"), "Small Key 1")
        self.assertEqual(T.game_name("Small Key (Dungeon 2)"), "Small Key 2")
        self.assertEqual(T.game_name("Metroid Super Missile Expansion"), "Metroid Super")

    def test_item_alone_when_no_form_leaves_room(self):
        self.assertEqual(T.received_text("Abcdefghijklmnopq", "Alex"), "Abcdefghijklmnop")

    def test_no_sender_is_the_item_alone(self):
        self.assertEqual(T.received_text("Herb"), "Herb")
        self.assertEqual(T.received_text("Stones of Sunlight"), "Sunlight Stones")

    def test_owner_with_no_font_characters(self):
        self.assertEqual(T.received_text("Herb", "\u5c71\u7530", 3), "Herb from P3")
        self.assertEqual(T.received_text("Herb", "\u5c71\u7530"), "Herb from ?")


if __name__ == "__main__":
    unittest.main()
