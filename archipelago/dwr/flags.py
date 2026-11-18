"""DWR flag string checks for the APWorld.

The flag string is base32 (RFC 4648 alphabet, 5 bits per character, most significant bit first).
It decodes to the bytes DWR reads as flags[] (checked against DWR's own base32_decode). Most
options are two bits: the low bit is "on" and the high bit is "maybe", so any bit in the mask
means the option can be on.
"""
from typing import List, Tuple

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


def random_off(flags: str) -> Tuple[str, List[str]]:
    """Turns off each unsupported flag that is only set to Random (its maybe bit without its on bit).

    Returns the new flag string, the same length as the old one, and the names of the flags turned off.
    A flag that is on stays on, so unsupported() still rejects it.
    """
    text = clean(flags)
    data = decode(text)
    bits = [(BASE32.find(c) >> (4 - k)) & 1 for c in text for k in range(5)]
    names = []
    for name, index, mask, _ in UNSUPPORTED:
        value = data[index] & mask
        if value and not value & 0x55:   # the high bit of a pair is "maybe", the low bit "on"
            names.append(name)
            for bit in range(8):
                if mask & value & (0x80 >> bit):
                    bits[index * 8 + bit] = 0
    if not names:
        return text, []
    out = "".join(BASE32[int("".join(map(str, bits[i:i + 5])), 2)] for i in range(0, len(bits), 5))
    return out, names


def free_charlock(flags: str) -> bool:
    index, mask = FREE_CHARLOCK
    return bool(decode(flags)[index] & mask)
