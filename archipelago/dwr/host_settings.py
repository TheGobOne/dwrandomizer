"""host.yaml settings for DWR: where the vanilla ROM and the DWR CLI are."""
import settings


class DWRSettings(settings.Group):
    class RomFile(settings.UserFilePath):
        """File name of the Dragon Warrior (USA) ROM. DWR builds the randomized ROM from it."""
        description = "Dragon Warrior (USA) ROM File"

    class Randomizer(settings.UserFilePath):
        """Path to dwrandomizer.exe, the DWR command line randomizer."""
        description = "DWR randomizer (dwrandomizer.exe)"

    rom_file: RomFile = RomFile("Dragon_warrior.nes")
    randomizer: Randomizer = Randomizer("dwrandomizer.exe")
