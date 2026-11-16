"""DWR flag string checks for the APWorld.

The flag string is base32 (RFC 4648 alphabet, 5 bits per character, most significant bit first).
It decodes to the bytes DWR reads as flags[] (checked against DWR's own base32_decode). Most
options are two bits: the low bit is "on" and the high bit is "maybe", so any bit in the mask
means the option can be on.
"""
from typing import List

BASE32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"

# (flag name, byte, mask, why). Byte and mask match common/dwr.h and FLAG_MAP in site/index.html.
UNSUPPORTED = [
    ("Random Chest Locations", 0, 0x30, "a chest can move into Charlock, and the logic would call it reachable early"),
    ("Stair Shuffle", 2, 0x0C, "dungeon stairs are not in the AP logic yet"),
    ("Cursed Princess", 8, 0x0C, "the win is not the Dragonlord, so the client cannot see the goal"),
    ("Three's Company", 8, 0x03, "the win is not the Dragonlord, so the client cannot see the goal"),
    ("Radish Finish", 20, 0xC0, "the win is not the Dragonlord, so the client cannot see the goal"),
    ("Disguised Dragonlord", 16, 0x0C, "not tested with AP yet"),
]
FREE_CHARLOCK = (11, 0x02)


def clean(flags: str) -> str:
    """The flag string without spaces, line breaks or "=" padding, in upper case."""
    return "".join(flags.split()).rstrip("=").upper()


def decode(flags: str) -> List[int]:
    """Return the flag bytes for a base32 flag string (extra trailing bits are dropped)."""
    bits = 0
    count = 0
    out = []
    for c in clean(flags):
        value = BASE32.find(c)
        if value < 0:
            raise ValueError(f"'{c}' is not a DWR flag character")
        bits = (bits << 5) | value
        count += 5
        if count >= 8:
            count -= 8
            out.append((bits >> count) & 0xFF)
    if len(out) < 25:
        raise ValueError("the DWR flag string is too short (copy the whole string from the DWR site)")
    return out


def unsupported(flags: str) -> List[str]:
    """'Name: why' for each flag in this string that the APWorld does not support."""
    data = decode(flags)
    return [f"{name}: {why}" for name, index, mask, why in UNSUPPORTED if data[index] & mask]


def free_charlock(flags: str) -> bool:
    index, mask = FREE_CHARLOCK
    return bool(decode(flags)[index] & mask)
