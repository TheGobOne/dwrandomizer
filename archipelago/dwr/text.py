"""Item names as the game shows them. DWR's found message has room for 16 characters."""
import unicodedata

TEXT_LEN = 16

# Names longer than the message has room for, shortened for the game only.
SHORT_NAMES = {"Stones of Sunlight": "Sunlight Stones"}

# Words shortened first when a name is too long. The rest are kept whole.
ABBREV = {"Progressive": "Prog.", "Upgrade": "Upgr"}

# Characters the DW font has besides letters and digits (common/archipelago.c ap_name_tiles).
PUNCTUATION = set(" '.,-!?")

# Letters that Unicode does not split into a base letter and an accent.
LATIN = str.maketrans({"ł": "l", "Ł": "L", "ø": "o", "Ø": "O", "ß": "ss", "æ": "ae", "Æ": "AE",
                       "œ": "oe", "Œ": "OE", "đ": "d", "Đ": "D", "ð": "d", "Ð": "D", "þ": "th",
                       "Þ": "Th", "ı": "i"})


def fold(text: str) -> str:
    """The text in characters the DW font has: accents removed, anything else dropped."""
    text = unicodedata.normalize("NFKD", text.translate(LATIN)).encode("ascii", "ignore").decode()
    text = "".join(c for c in text if c.isalnum() or c in PUNCTUATION)
    return " ".join(text.split())


def _forms(name: str, drop_leading: bool) -> list:
    """Ways to write an item, longest first: whole, with abbreviations, then (if drop_leading)
    without leading words."""
    words = fold(SHORT_NAMES.get(name, name)).split()
    if not words:
        return []
    abbr = [ABBREV.get(w, w) for w in words]
    forms = [" ".join(words), " ".join(abbr)]
    if drop_leading:
        for i in range(1, len(words)):
            forms += [" ".join(words[i:]), " ".join(abbr[i:])]
    return list(dict.fromkeys(forms))


def game_name(name: str) -> str:
    """The item name as the game shows it, or "item" when none of it is in the DW font.

    The first way of writing it that fits 16 characters is used. A name that fits no way is cut."""
    forms = _forms(name, drop_leading=False)
    for form in forms:
        if len(form) <= TEXT_LEN:
            return form
    return forms[0][:TEXT_LEN].rstrip() if forms else "item"


def _owner(name: str, player: int) -> str:
    return fold(name) or (f"P{player}" if player else "?")


def received_text(item: str, sender: str = "", player: int = 0) -> str:
    """The mailbox text for an item another player sent: "Herb from Alex".

    The item and the sender share 16 characters. A shorter way of writing the item is used when
    the sender will not fit, and a long sender name is cut and ends in a period. When no way of
    writing the item leaves room for the sender, the item alone is shown."""
    if not sender:
        return game_name(item)
    owner = _owner(sender, player)
    forms = _forms(item, drop_leading=True)
    # The sender's whole name first, then the first form that leaves any room for a cut one.
    for form in forms:
        if TEXT_LEN - len(form) - len(" from ") >= len(owner):
            return f"{form} from {owner}"
    # A cut sender needs at least two characters before its period, or the item is shortened further.
    for form in forms:
        room = TEXT_LEN - len(form) - len(" from ")
        if room >= 3:
            return f"{form} from {owner[:room - 1].rstrip()}."
    for form in forms:
        room = TEXT_LEN - len(form) - len(" from ")
        if room >= 2:
            return f"{form} from {owner[:room - 1].rstrip()}."
        if room == 1:
            return f"{form} from {owner[0]}"
    return game_name(item)


def found_text(item: str, owner: str = "", player: int = 0) -> str:
    """The text after "Thou hast found the" for a chest or search spot.

    Another player's item says whose it is. A long owner name is cut and ends in a period
    ("Bartholo.'s Herb"). An owner with no DW font characters shows as "P" and the player
    number ("P3's Herb"), or "?" without one. The item is never
    cut to make room for the owner, so an item name of 13 or more characters shows alone."""
    item = game_name(item)
    if owner:
        owner = _owner(owner, player)
        room = TEXT_LEN - len(item) - len("'s ")
        if len(owner) <= room:
            return f"{owner}'s {item}"
        if room >= 2:
            return f"{owner[:room - 1].rstrip()}.'s {item}"
        if room == 1:
            return f"{owner[0]}'s {item}"
    return item
