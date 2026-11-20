"""Item names and the sentences the game shows for them.

An item name has at most 16 characters. ROM version 005 and later show a whole sentence of up to
38 characters ("Heart Container for Alex."); older ROMs add "Thou hast found the" to a
16 character text."""
import unicodedata

TEXT_LEN = 16
LINE_LEN = 38  # common/archipelago.h AP_TEXT_LEN

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
    """The mailbox text for an item another player sent, for ROMs before version 005: "Herb from Alex".

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


def _sentence(head: str, owner: str, tail: str = ".") -> str:
    """head + owner + tail, with the owner cut so the whole fits LINE_LEN. The item in head is
    never cut: an item is at most TEXT_LEN characters, which leaves room for at least 15 letters
    of the owner."""
    room = LINE_LEN - len(head) - len(tail)
    return f"{head}{owner[:room].rstrip()}{tail}"


def found_text(item: str, owner: str = "", player: int = 0) -> str:
    """The whole sentence a chest or search spot shows, after "Fortune smiles upon thee, <hero>."
    (ROM version 005 and later).

    The hero's own item: "Thou hast found the Herb." Another player's item says who gets it:
    "Heart Container for Alex." A long owner name is cut to fit. An owner with no DW font
    characters shows as "P" and the player number ("P3"), or "?" without one."""
    item = game_name(item)
    if not owner:
        return f"Thou hast found the {item}."
    return _sentence(f"{item} for ", _owner(owner, player))


def received_line(item: str, sender: str = "", player: int = 0) -> str:
    """The mailbox sentence for an item from another player: "Heart Container from Alex."
    (ROM version 005 and later). The sender is cut to fit; the item is not. Without a sender (the
    server, a !collect): "Thou hast found the Herb." """
    if not sender:
        return found_text(item)
    item = game_name(item)
    return _sentence(f"{item} from ", _owner(sender, player))


def batch_line(count: int, sender: str = "") -> str:
    """The mailbox sentence for a batch of 2 or more: "5 items from Alex." or "12 items."
    (ROM version 005 and later)."""
    name = fold(sender) if sender else ""
    return _sentence(f"{count} items from ", name) if name else f"{count} items."
