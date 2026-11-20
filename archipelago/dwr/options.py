from dataclasses import dataclass

from Options import FreeText, PerGameCommonOptions, Toggle


class DWRFlags(FreeText):
    """The DWR flag string the ROM is built with (copy it from the DWR site).
    Not supported: Random Chest Locations, Stair Shuffle, Cursed Princess, Three's Company, Radish Finish and Disguised
    Dragonlord. Generation stops with an error if any of them is on; one set to Random is turned off,
    with a warning in the log.
    Free Charlock is read from this string: with it on, Erdrick's Sword, Erdrick's Armor and the
    quest items are kept out of the Charlock chests."""
    display_name = "DWR Flags"
    default = "IVIAAV2EKADEAMAAAADYKEBWAAAJACAAAAIBQAAM75QA"  # Gob Standard


class ProgressionMark(Toggle):
    """Ends the sentence about a progression item with "!" instead of ".", so the player can tell
    it from filler in the game text (the found message and the mailbox). Off by default."""
    display_name = "Progression Mark"
    default = 0


@dataclass
class DWROptions(PerGameCommonOptions):
    dwr_flags: DWRFlags
    progression_mark: ProgressionMark
