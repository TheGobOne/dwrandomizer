from dataclasses import dataclass

from Options import PerGameCommonOptions, Toggle


class FreeCharlock(Toggle):
    """Turn this on when the ROM is built with the Free Charlock flag.
    Erdrick's Sword, Erdrick's Armor and the quest items (Silver Harp, Stones of Sunlight, Staff of Rain,
    Erdrick's Token) are then kept out of the Charlock chests."""
    display_name = "Free Charlock"


@dataclass
class DWROptions(PerGameCommonOptions):
    free_charlock: FreeCharlock
