"""DWR flag string checks for the APWorld.

The flag string is base32 (RFC 4648 alphabet, 5 bits per character, most significant bit first).
It decodes to the bytes DWR reads as flags[] (checked against DWR's own base32_decode). Most
options are two bits: the low bit is "on" and the high bit is "maybe", so any bit in the mask
means the option can be on.
"""
from typing import List, Tuple

BASE32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"

# (flag name, byte, mask, on bits, why). Byte and mask match common/dwr.h and FLAG_MAP in site/index.html.
# The on bits are the part of the mask that means "on" (the low bit of a pair); the rest is "maybe".
UNSUPPORTED = [
    ("Random Chest Locations", 0, 0x30, 0x10, "a chest can move into Charlock, and the logic would call it reachable early"),
    ("Stair Shuffle", 2, 0x0C, 0x04, "dungeon stairs are not in the AP logic yet"),
    ("Cursed Princess", 8, 0x0C, 0x04, "the win is not the Dragonlord, so the client cannot see the goal"),
    ("Three's Company", 8, 0x03, 0x01, "the win is not the Dragonlord, so the client cannot see the goal"),
    ("Radish Finish", 20, 0xC0, 0x40, "the win is not the Dragonlord, so the client cannot see the goal"),
    ("Disguised Dragonlord", 16, 0x0C, 0x04, "not tested with AP yet"),
    ("Zelda II Map", 26, 0x10, 0x10, "the logic assumes the DWR overworld, not the cut bridge or the moved places"),
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
    # A 40-letter string has 25 bytes; the flags in byte 26 on are off in it.
    return [f"{name}: {why}" for name, index, mask, _, why in UNSUPPORTED if index < len(data) and data[index] & mask]


def random_off(flags: str) -> Tuple[str, List[str]]:
    """Turns off each unsupported flag that is only set to Random (its maybe bit without its on bit).

    Returns the new flag string, the same length as the old one, and the names of the flags turned off.
    A flag that is on stays on, so unsupported() still rejects it.
    """
    text = clean(flags)
    data = decode(text)
    bits = [(BASE32.find(c) >> (4 - k)) & 1 for c in text for k in range(5)]
    names = []
    for name, index, mask, on, _ in UNSUPPORTED:
        if index >= len(data):
            continue
        value = data[index] & mask
        if value and not value & on:   # only the "maybe" bit is set
            names.append(name)
            for bit in range(8):
                if mask & value & (0x80 >> bit):
                    bits[index * 8 + bit] = 0
    if not names:
        return text, []
    out = "".join(BASE32[int("".join(map(str, bits[i:i + 5])), 2)] for i in range(0, len(bits), 5))
    return out, names


def unknown_values(flags: str) -> List[str]:
    """Names of the dropdown options whose value no option uses. The game builds them with the option's
    normal setting instead (the same as common/dwr.c update_flags and the site), so the ROM differs
    from what the code seems to say. Build Options 5 to 7, Run Mechanics 5 and 6, Gold Chest 6 and 7,
    Leveling Speed 3, Crit Chance 7, and a Biome pattern the site has no choice for (only checked
    when byte 25 is 0, because a Biome past Riverlands is kept there)."""
    data = decode(flags)
    byte = lambda i: data[i] if i < len(data) else 0
    biome = byte(2) & 0x33
    checks = [
        ("Build Options", (byte(24) & 0x1C) >> 2 >= 5, "Original"),
        ("Run Mechanics", (byte(21) & 0xE0) >> 5 in (5, 6), "Vanilla"),
        ("Gold Chest", (byte(21) & 0x1C) >> 2 >= 6, "the normal 500 to 755 G"),
        ("Leveling Speed", byte(13) & 0x30 == 0x30, "Normal"),
        ("Crit Chance", (byte(17) & 0x70) >> 4 == 7, "Vanilla"),
        ("Biome", not byte(25) and biome not in (0x00, 0x01, 0x10, 0x11, 0x22), "Continent"),
    ]
    return [f"{name} (used as {normal})" for name, odd, normal in checks if odd]


def free_charlock(flags: str) -> bool:
    index, mask = FREE_CHARLOCK
    return bool(decode(flags)[index] & mask)
