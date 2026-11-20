"""The .apdwr patch: an AP container holding the DWR build instructions.

Opening it from the Archipelago Launcher (or the BizHawk Client) runs the DWR
CLI on the player's vanilla ROM:

    dwrandomizer [--music <pack.json5>] --ap-patch dwr.txt <vanilla rom> <output dir>

dwr.txt inside the container is the plain text file the CLI reads, so a
player without the launcher can unzip it and run the CLI by hand. --music
comes from host.yaml's dwr_options.music_pack on the machine that builds the
ROM; the song list is written next to the ROM as <rom name>.music.txt.
"""
import glob
import logging
import os
import shutil
import subprocess
import tempfile
import zipfile

from worlds.Files import APAutoPatchInterface

from .data import GAME

PATCH_TEXT = "dwr.txt"
logger = logging.getLogger("DWR")


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
        missing = [f"{key}: {path}" for key, path in (("rom_file", rom), ("randomizer", exe))
                   if not os.path.isfile(str(path))]
        if missing:
            raise RuntimeError("DWR could not build the ROM. These files in host.yaml (dwr_options) "
                               "were not found:\n" + "\n".join(missing))
        with tempfile.TemporaryDirectory() as work:
            text_path = os.path.join(work, PATCH_TEXT)
            with open(text_path, "w", encoding="utf-8") as f:
                f.write(self.text)
            music = music_args(str(getattr(options, "music_pack", "") or ""))
            tail = ["--ap-patch", text_path, rom, work]
            result = subprocess.run([exe] + music + tail, capture_output=True, text=True,
                                    encoding="utf-8", errors="replace")
            built = glob.glob(os.path.join(work, "DWR.*.nes"))
            if music and (result.returncode != 0 or not built):
                # A pack that DWR cannot read should not cost the player their ROM.
                why = (result.stdout.strip().splitlines() or [result.stderr.strip()])[-1]
                logger.warning("DWR could not use the music pack, building with the game's music: %s", why)
                result = subprocess.run([exe] + tail, capture_output=True, text=True,
                                        encoding="utf-8", errors="replace")
                built = glob.glob(os.path.join(work, "DWR.*.nes"))
                music = []
            if result.returncode != 0 or not built:
                raise RuntimeError(f"DWR could not build the ROM:\n{result.stdout}\n{result.stderr}")
            check_rom_version(built[0], exe)
            shutil.move(built[0], target)
            if music:
                report_music(result.stdout, os.path.splitext(target)[0] + ".music.txt")


# The oldest DWR AP ROM version this APWorld's text fits: 005 takes whole sentences of up to 38
# characters. An older CLI cuts them to 16 and puts "Thou hast found the" in front.
MIN_ROM_VERSION = b"005"
SIGNATURE_AT = 0x10 + 0x7FE0  # iNES header, then PRG $7FE0: "DWRAP" and 3 version digits


def check_rom_version(path: str, exe: str) -> None:
    """Stops when the built ROM is from a DWR CLI older than this APWorld."""
    with open(path, "rb") as f:
        f.seek(SIGNATURE_AT)
        sig = f.read(8)
    if sig[:5] != b"DWRAP" or sig[5:8] < MIN_ROM_VERSION:
        version = sig[5:8].decode(errors="replace") if sig[:5] == b"DWRAP" else "none"
        raise RuntimeError(f"The DWR randomizer at {exe} built an AP version {version} ROM, but this "
                           f"dwr.apworld needs version {MIN_ROM_VERSION.decode()} or newer. Update "
                           "dwrandomizer.exe to the version that came with this dwr.apworld.")


# Lines the DWR CLI prints when some or all of the pack is left out.
MUSIC_PROBLEMS = ("keeping the original music", "no room", "leaving it out")


def music_args(raw: str) -> list:
    """--music and the pack path, or nothing when no pack is set or the file is missing."""
    if not raw:
        return []
    path = os.path.expanduser(raw)
    if not os.path.isabs(path):
        try:
            import Utils
            path = Utils.user_path(path)
        except ImportError:
            pass
    if not os.path.isfile(path):
        logger.warning("DWR music pack not found, keeping the game's music: %s", path)
        return []
    return ["--music", path]


def music_lines(stdout: str) -> list:
    """The CLI's song report: every line from "Adding custom music" until the next step."""
    lines, on = [], False
    for line in stdout.splitlines():
        if line.startswith("Adding custom music"):
            on = True
        elif on and not (line.startswith("  ") or line.startswith("Music ") or line.startswith("No ")):
            break
        if on:
            lines.append(line)
    return lines


def report_music(stdout: str, list_path: str) -> None:
    """Logs songs that were left out and writes the song list next to the ROM."""
    lines = music_lines(stdout)
    for line in lines:
        if any(p in line.lower() for p in MUSIC_PROBLEMS):
            logger.warning("DWR music: %s", line.strip())
    if lines:
        try:
            with open(list_path, "w", encoding="utf-8") as f:
                f.write("\n".join(lines) + "\n")
        except OSError:
            pass
