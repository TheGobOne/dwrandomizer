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


def _forms(name: str) -> list:
    """Ways to write an item, longest first: whole, then with abbreviations. No word is dropped,
    so the item never reads as a different one."""
    words = fold(SHORT_NAMES.get(name, name)).split()
    if not words:
        return []
    abbr = [ABBREV.get(w, w) for w in words]
    return list(dict.fromkeys([" ".join(words), " ".join(abbr)]))


def _cut(form: str) -> str:
    """A form too long for the message, cut at a word end. A trailing number is kept, so
    "Small Key Dungeon 1" and "... 2" stay apart."""
    if len(form) <= TEXT_LEN:
        return form
    words = form.split()
    if len(words) > 1 and words[-1].isdigit():
        head = words[:-1]
        while head and len(" ".join(head + [words[-1]])) > TEXT_LEN:
            head.pop()
        if head:
            return " ".join(head + [words[-1]])
    cut = form[:TEXT_LEN]
    if form[TEXT_LEN] != " " and " " in cut:
        cut = cut[:cut.rfind(" ")]
    return cut.rstrip()


def game_name(name: str) -> str:
    """The item name as the game shows it, or "item" when none of it is in the DW font.

    The first way of writing it that fits 16 characters is used. A name that fits no way is cut
    at a word end."""
    forms = _forms(name)
    for form in forms:
        if len(form) <= TEXT_LEN:
            return form
    return _cut(forms[0]) if forms else "item"


def _owner(name: str, player: int) -> str:
    return fold(name) or (f"P{player}" if player else "?")


def received_text(item: str, sender: str = "", player: int = 0) -> str:
    """The mailbox text for an item another player sent: "Herb from Alex".

    The item and the sender share 16 characters. The item is never shortened to make room: a
    long sender name is cut and ends in a period ("Torch from Bart."). When even a one
    letter sender does not fit, the item is shown alone."""
    if not sender:
        return game_name(item)
    owner = _owner(sender, player)
    forms = [f for f in _forms(item) if len(f) <= TEXT_LEN]
    for form in forms:
        room = TEXT_LEN - len(form) - len(" from ")
        if room >= len(owner):
            return f"{form} from {owner}"
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
