"""Item names as the game shows them. DWR's found message has room for 16 characters."""
import unicodedata

TEXT_LEN = 16

# Names longer than the message has room for, shortened for the game only.
SHORT_NAMES = {"Stones of Sunlight": "Sunlight Stones"}

# Characters the DW font has besides letters and digits (common/archipelago.c ap_name_tiles).
PUNCTUATION = set(" '.,-!?")


def fold(text: str) -> str:
    """The text in characters the DW font has: accents removed, anything else dropped."""
    text = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode()
    text = "".join(c for c in text if c.isalnum() or c in PUNCTUATION)
    return " ".join(text.split())


def game_name(name: str) -> str:
    """The item name as the game shows it."""
    return fold(SHORT_NAMES.get(name, name))[:TEXT_LEN]


def found_text(item: str, owner: str = "") -> str:
    """The text after "Thou hast found the" for a chest or search spot.

    Another player's item says whose it is. A long owner name is cut and ends in a period
    ("Bartholo.'s Herb"). The item is never cut to make room for the owner, so an item name of
    14 or more characters shows alone."""
    item = game_name(item)
    owner = fold(owner)
    if owner:
        room = TEXT_LEN - len(item) - len("'s ")
        if len(owner) <= room:
            return f"{owner}'s {item}"
        if room >= 2:
            return f"{owner[:room - 1].rstrip()}.'s {item}"
        if room == 1:
            return f"{owner[0]}'s {item}"
    return item
