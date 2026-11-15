"""The .apdwr patch: an AP container holding the DWR build instructions.

Opening it from the Archipelago Launcher (or the BizHawk Client) runs the DWR
CLI on the player's vanilla ROM:

    dwrandomizer --ap-patch dwr.txt <vanilla rom> <output dir>

dwr.txt inside the container is the plain text file the CLI reads, so a
player without the launcher can unzip it and run the CLI by hand.
"""
import glob
import os
import shutil
import subprocess
import tempfile
import zipfile

from worlds.Files import APAutoPatchInterface

from .data import GAME

PATCH_TEXT = "dwr.txt"


class DWRPatch(APAutoPatchInterface):
    game = GAME
    patch_file_ending = ".apdwr"
    result_file_ending = ".nes"

    def __init__(self, *args, text: str = "", **kwargs) -> None:
        super().__init__(*args, **kwargs)
        self.text = text

    def write_contents(self, opened_zipfile: zipfile.ZipFile) -> None:
        super().write_contents(opened_zipfile)
        opened_zipfile.writestr(PATCH_TEXT, self.text)

    def read_contents(self, opened_zipfile: zipfile.ZipFile) -> dict:
        manifest = super().read_contents(opened_zipfile)
        self.text = opened_zipfile.read(PATCH_TEXT).decode("utf-8")
        return manifest

    def patch(self, target: str) -> None:
        from settings import get_settings
        options = get_settings().dwr_options
        if not self.text:
            # Opened from a file: the text is in the zip, not in the constructor.
            with zipfile.ZipFile(self.path) as opened:
                self.text = opened.read(PATCH_TEXT).decode("utf-8")
        rom = options.rom_file
        exe = options.randomizer
        with tempfile.TemporaryDirectory() as work:
            text_path = os.path.join(work, PATCH_TEXT)
            with open(text_path, "w", encoding="utf-8") as f:
                f.write(self.text)
            result = subprocess.run([exe, "--ap-patch", text_path, rom, work],
                                    capture_output=True, text=True)
            built = glob.glob(os.path.join(work, "DWR.*.nes"))
            if result.returncode != 0 or not built:
                raise RuntimeError(f"DWR could not build the ROM:\n{result.stdout}\n{result.stderr}")
            shutil.move(built[0], target)
