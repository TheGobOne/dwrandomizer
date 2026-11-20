"""Unit tests for the music parts of archipelago/dwr/patch.py.

Run from the repo root:  python -m unittest tests/archipelago/test_patch.py
"""
import importlib.util
import os
import sys
import tempfile
import types
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_client  # noqa: E402  (stubs the Archipelago modules and loads dwrtest.data)

files = types.ModuleType("worlds.Files")
files.APAutoPatchInterface = type("APAutoPatchInterface", (), {"__init__": lambda self, *a, **k: None})
sys.modules["worlds.Files"] = files
spec = importlib.util.spec_from_file_location("dwrtest.patch", os.path.join(test_client.DWR_DIR, "patch.py"))
P = importlib.util.module_from_spec(spec)
sys.modules["dwrtest.patch"] = P
spec.loader.exec_module(P)

OUTPUT = """Setting up Archipelago mode...
Adding custom music...
  Credits      The Legend of Zelda: Ending (1630 bytes, bank 6)
Music bank 5: 8621 bytes for songs.
  Town         no room for its song, leaving it out.
  Hauksness    Super Mario Bros. 3: Toad's House (988 bytes, bank 4)
Setting player sprite to Loto...
"""


class Music(unittest.TestCase):
    def test_no_pack_means_no_args(self):
        self.assertEqual(P.music_args(""), [])

    def test_missing_pack_warns_and_skips(self):
        with self.assertLogs("DWR", level="WARNING") as log:
            self.assertEqual(P.music_args("C:/nowhere/none.json5"), [])
        self.assertIn("not found", log.output[0])

    def test_existing_pack_is_passed(self):
        with tempfile.NamedTemporaryFile(suffix=".json5", delete=False) as f:
            path = f.name
        try:
            self.assertEqual(P.music_args(path), ["--music", path])
        finally:
            os.remove(path)

    def test_song_list_stops_at_the_next_step(self):
        lines = P.music_lines(OUTPUT)
        self.assertEqual(lines[0], "Adding custom music...")
        self.assertEqual(len(lines), 5)
        self.assertFalse(any("sprite" in line for line in lines))

    def test_left_out_songs_are_logged_and_list_is_written(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "rom.music.txt")
            with self.assertLogs("DWR", level="WARNING") as log:
                P.report_music(OUTPUT, path)
            self.assertTrue(any("Town" in line for line in log.output))
            with open(path, encoding="utf-8") as f:
                self.assertIn("Hauksness", f.read())


class RomVersion(unittest.TestCase):
    def rom(self, sig: bytes) -> str:
        with tempfile.NamedTemporaryFile(suffix=".nes", delete=False) as f:
            f.write(bytes(P.SIGNATURE_AT) + sig + bytes(32))
            return f.name

    def check(self, sig: bytes) -> None:
        path = self.rom(sig)
        try:
            P.check_rom_version(path, "dwrandomizer.exe")
        finally:
            os.remove(path)

    def test_current_version_passes(self):
        self.check(b"DWRAP005")
        self.check(b"DWRAP006")

    def test_older_cli_stops_the_build(self):
        with self.assertRaises(RuntimeError) as err:
            self.check(b"DWRAP004")
        self.assertIn("version 004", str(err.exception))

    def test_rom_without_a_signature_stops_the_build(self):
        with self.assertRaises(RuntimeError):
            self.check(b"NOTDWR00")


if __name__ == "__main__":
    unittest.main()
