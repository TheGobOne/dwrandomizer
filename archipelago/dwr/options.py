from dataclasses import dataclass

from Options import FreeText, PerGameCommonOptions, Toggle


class DWRFlags(FreeText):
    """The DWR flag string the ROM is built with (copy it from the DWR site).
    Logic assumes Random Chest Locations, Stair Shuffle, Cursed Princess, Three's Company and
    Open Charlock are off."""
    display_name = "DWR Flags"
    default = "IVICEV2EKADEAMAAAADQAEBWAAAJACAAAAIBQAAA"


class FreeCharlock(Toggle):
    """Turn this on when the ROM is built with the Free Charlock flag.
    Erdrick's Sword, Erdrick's Armor and the quest items (Silver Harp, Stones of Sunlight, Staff of Rain,
    Erdrick's Token) are then kept out of the Charlock chests."""
    display_name = "Free Charlock"


@dataclass
class DWROptions(PerGameCommonOptions):
    free_charlock: FreeCharlock
    dwr_flags: DWRFlags
