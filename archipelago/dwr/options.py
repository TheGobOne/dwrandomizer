from dataclasses import dataclass

from Options import FreeText, PerGameCommonOptions


class DWRFlags(FreeText):
    """The DWR flag string the ROM is built with (copy it from the DWR site).
    Not supported: Random Chest Locations, Stair Shuffle, Cursed Princess, Three's Company, Radish Finish and Disguised
    Dragonlord. Generation stops with an error if any of them is on; one set to Random is turned off,
    with a warning in the log.
    Free Charlock is read from this string: with it on, Erdrick's Sword, Erdrick's Armor and the
    quest items are kept out of the Charlock chests."""
    display_name = "DWR Flags"
    default = "IVIAAV2EKADEAMAAAADYKEBWAAAJACAAAAIBQAAM75QA"  # Gob Standard


@dataclass
class DWROptions(PerGameCommonOptions):
    dwr_flags: DWRFlags
