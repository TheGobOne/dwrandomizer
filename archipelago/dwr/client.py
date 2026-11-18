"""BizHawk client for DWR (works with the FCEUX connector too).

ROM side (common/archipelago.c in DWR, which has the full map):
  PRG $7FE0   "DWRAP", 3 version digits, pointer to the 32 byte UTF-8 slot name, custom music
              flag (1: with the wait loop, 2: frame music only, 3: a pack was given but left out),
              4 byte seed hash, herb cap << 4 | key cap
  $6820-21    items received so far (the client raises it after each item)
  $6822-25    chest bits, bit n = chest n
  $6826       search spot bits
  $682E       mailbox flag: 1 while a received item's message waits for the next step
  $6842-45    seed hash of the ROM that started this game (written by the ROM at a new game)
  $6846-4B    slot tags, 2 bytes each: seed hash bytes 0 and 1 of the ROM that last saved the slot
              (ROM version 003: byte 0 only)
  $6850       mailbox text: the item name in DW tiles, ended by $FA
"""
import logging
from typing import TYPE_CHECKING

from NetUtils import ClientStatus

import worlds._bizhawk as bizhawk
from worlds._bizhawk.client import BizHawkClient

from .data import BASE, GAME, GWAELIN, LOCATION_NAME_TO_ID
from .text import received_text

if TYPE_CHECKING:
    from worlds._bizhawk.context import BizHawkClientContext

logger = logging.getLogger("Client")

SIGNATURE = 0x7FE0
MAGIC = b"DWRAP"
SLOT_NAME_LEN = 32  # bytes of UTF-8 the ROM keeps for the slot name
ROM_VERSIONS = (b"002", b"003", b"004")  # 002: no seed hash or carry caps; 003: 1-byte slot tags

AP_RECV = 0x6820
AP_BLOCK_SIZE = 7  # $6820-$6826
MAIL_FLAG = 0x682E  # 1 while a received item's message is waiting to be shown. Write exactly 1: the hook shifts it right, so 2 or $FF would be lost or shown on several steps
SEED_HASH = 0x6842  # 4 bytes: the seed hash of the ROM this game was started with
SLOT_TAGS = 0x6846  # 6 bytes: the tag of each save slot, 2 bytes each
SAVE_SLOT = 0x6039  # the save slot in use (0-2)
MAIL_TEXT = 0x6850  # the item name in DW tiles, ended by $FA

# DW's font order (common/dwr.c alphabet): a DW tile code is the index in this string.
DW_FONT = ("0123456789abcdefghijklmnopqrstuvwxyz"
           "ABCDEFGHIJKLMNOPQRSTUVWXYZ[]'*>_:=_.,-_?!;)(``/'___________ \n")


def slot_tag(seed_hash: bytes, size: int = 2) -> bytes:
    """The tag the ROM writes into a slot when it saves it (archipelago.c ap_slot_tag)."""
    if not seed_hash:
        return b""
    first = 0x5A if seed_hash[0] in (0, 0xFF) else seed_hash[0]
    return bytes([first, seed_hash[1]])[:size]


def untagged(tag: bytes) -> bool:
    """A slot no tagging ROM has saved: its first tag byte is 0 or $FF."""
    return not tag or tag[0] in (0, 0xFF)


def dw_text(name: str, sender: str = "", player: int = 0) -> bytes:
    """The item name as DW tiles, ended by $FA, for the mailbox. A sender's name is added: "Herb from Alex"."""
    codes = [DW_FONT.index(c) if c in DW_FONT else DW_FONT.index(" ") for c in received_text(name, sender, player)]
    return bytes(codes + [0xFA])

MAP = 0x45          # 0 until a game is running
GOLD = 0xBC         # 2 bytes
EQUIPMENT = 0xBE
KEYS = 0xBF
HERBS = 0xC0
INVENTORY = 0xC1    # 4 bytes, 2 items per byte
STATUS = 0xDF       # 0x01: carrying Gwaelin, 0x02: brought her back
STORY = 0xE4        # 0x04: Dragonlord defeated

# Inventory item numbers
INVENTORY_ITEMS = {
    "Torch": 0x1, "Fairy Water": 0x2, "Wings": 0x3, "Dragon's Scale": 0x4,
    "Fairy Flute": 0x5, "Fighter's Ring": 0x6, "Erdrick's Token": 0x7,
    "Silver Harp": 0xA, "Stones of Sunlight": 0xC, "Staff of Rain": 0xD,
}
QUEST = {0x5, 0x7, 0xA, 0xC, 0xD}
UNIQUE = QUEST | {0x4, 0x6}  # one of each in the pool: they wait for room instead of being dropped
# Given up to make room for a quest item, in this order. Not the Dragon's Scale or Fighter's Ring:
# there is one of each, and a worn one would leave its bonus behind.
REPLACEABLE = (0x1, 0x2, 0x3)


def opened_in_game(location: int, block: bytes) -> bool:
    """True when the location is one of this game's chests or search spots and its bit is set:
    the game then said "Thou hast found" itself. An item from !collect or a release is not."""
    n = location - BASE
    if 0 <= n < 31:
        return bool(int.from_bytes(block[2:6], "little") >> n & 1)
    if 0x40 <= n < 0x43:
        return bool(block[6] >> (n - 0x40) & 1)
    return False


class DWRClient(BizHawkClient):
    game = GAME
    system = "NES"
    patch_suffix = ".apdwr"  # the BizHawk Client opens .apdwr patches

    def __init__(self) -> None:
        super().__init__()
        self.slot_name = ""
        self.sig = b""            # the ROM signature last validated
        self.seed_hash = b""      # from the ROM signature; empty for ROMs made before it existed
        self.slot_tag = b""
        self.herb_cap = 6
        self.key_cap = 6
        self.music_noted = False
        self.stale_noted = False
        self.waiting_noted = -1   # the received index whose "no room" was logged
        self.ahead_noted = False
        self.last_received = -1   # the save's received count at the last poll, -1 before the first
        self.notes = []           # warnings from _effect, logged once its write succeeds

    async def validate_rom(self, ctx: "BizHawkClientContext") -> bool:
        try:
            sig = (await bizhawk.read(ctx.bizhawk_ctx, [(SIGNATURE, 16, "PRG ROM")]))[0]
            if sig[:5] != MAGIC:
                return False
            name_at = sig[8] | sig[9] << 8
            name = (await bizhawk.read(ctx.bizhawk_ctx, [(name_at, SLOT_NAME_LEN, "PRG ROM")]))[0]
        except bizhawk.RequestFailedError:
            return False
        sig = bytes(sig)
        if sig != self.sig:
            # Another ROM: its notes are new.
            self.sig = sig
            self.music_noted = False
            self.stale_noted = False
            self.waiting_noted = -1
            self.ahead_noted = False
            self.last_received = -1
            if sig[5:8] not in ROM_VERSIONS:
                logger.warning("This ROM is DWR AP version %s, the client knows %s. Rebuild the ROM "
                               "from your .apdwr file, or update dwr.apworld.", sig[5:8].decode(errors="replace"),
                               ", ".join(v.decode() for v in ROM_VERSIONS))
        if sig[5:8] not in ROM_VERSIONS:
            return False  # another layout: reading it would send wrong checks
        self.slot_name = name.split(b"\x00")[0].decode("utf-8", errors="replace")
        if sig[10] and not self.music_noted:
            self.music_noted = True
            if sig[10] == 3:
                logger.warning("The music pack did not fit in this ROM, so it plays the original music.")
            elif sig[10] == 2:
                logger.info("This ROM runs its custom music from the frame only, so busy frames can be slower.")
            else:
                logger.info("This ROM has custom music, which changes frame timing a little.")
        self.seed_hash = sig[11:15] if any(sig[11:15]) else b""
        self.slot_tag = slot_tag(self.seed_hash, 1 if sig[5:8] == b"003" else 2)
        caps = sig[15] if sig[5:8] != b"002" else 0
        self.herb_cap, self.key_cap = (caps >> 4, caps & 0x0F) if caps else (6, 6)
        ctx.game = self.game
        ctx.items_handling = 0b111  # receive everything, own items included
        ctx.want_slot_data = False
        return True

    async def set_auth(self, ctx: "BizHawkClientContext") -> None:
        if self.slot_name:
            ctx.auth = self.slot_name

    async def game_watcher(self, ctx: "BizHawkClientContext") -> None:
        if ctx.server is None or ctx.slot is None:
            return
        try:
            ram, block, mail, saved_hash, slot, tags = await bizhawk.read(ctx.bizhawk_ctx, [
                (0x0000, 0x100, "RAM"),
                (AP_RECV, AP_BLOCK_SIZE, "System Bus"),
                (MAIL_FLAG, 1, "System Bus"),
                (SEED_HASH, 4, "System Bus"),
                (SAVE_SLOT, 1, "System Bus"),
                (SLOT_TAGS, 6, "System Bus"),
            ])
        except bizhawk.RequestFailedError:
            return
        if ram[MAP] == 0:
            return  # title screen or menus: nothing in RAM is the game yet
        tag = bytes(tags[slot[0] * 2:slot[0] * 2 + len(self.slot_tag)]) if slot[0] < 3 else b""
        if not await self._check_seed(ctx, bytes(saved_hash), tag):
            return

        await self._send_checks(ctx, ram, block)
        await self._give_item(ctx, ram, block, mail[0])

    def tag_is_ours(self, tag: bytes) -> bool:
        """The slot was saved by this seed. A save from ROM version 003 has one tag byte, and
        its second byte is 0: that matches when the first byte does."""
        if tag == self.slot_tag:
            return True
        return len(self.slot_tag) == 2 and tag[:1] == self.slot_tag[:1] and tag[1:2] == b"\x00"

    async def _check_seed(self, ctx, saved: bytes, tag: bytes) -> bool:
        """False when this game belongs to another seed: its chest bits would send wrong checks.

        The ROM writes its seed hash at a new game and tags a save slot each time it saves it.
        A save from before the hash has 0 or $FF there, and takes this ROM's hash."""
        if not self.seed_hash:
            return True  # a ROM from before the hash
        if saved in (b"\x00\x00\x00\x00", b"\xff\xff\xff\xff"):
            return await bizhawk.guarded_write(ctx.bizhawk_ctx, [(SEED_HASH, self.seed_hash, "System Bus")],
                                               [(SEED_HASH, saved, "System Bus")])
        if saved != self.seed_hash and tag == self.slot_tag:
            # This slot was saved by this ROM, but another seed started a game in another slot
            # since. The chest and search bits are that game's, so clear them and take the hash.
            # The ROM does this itself at Continue; this covers a save loaded some other way.
            # Return False either way: this poll's snapshot still holds the old bits.
            await bizhawk.guarded_write(
                ctx.bizhawk_ctx,
                [(SEED_HASH, self.seed_hash, "System Bus"), (AP_RECV + 2, bytes(5), "System Bus")],
                [(SEED_HASH, saved, "System Bus")])
            return False
        if saved != self.seed_hash or not (untagged(tag) or self.tag_is_ours(tag)):
            if not self.stale_noted:
                self.stale_noted = True
                logger.warning("This save was made with a different DWR seed. No checks or items are "
                               "sent for it. Start a New Game, or load the ROM this save belongs to.")
            return False
        self.stale_noted = False
        return True

    async def _send_checks(self, ctx, ram: bytes, block: bytes) -> None:
        found = set()
        chest_bits = int.from_bytes(block[2:6], "little")
        for n in range(31):
            if chest_bits >> n & 1:
                found.add(BASE + n)
        for n in range(3):
            if block[6] >> n & 1:
                found.add(BASE + 0x40 + n)
        if ram[STATUS] & 0x03:
            found.add(LOCATION_NAME_TO_ID[GWAELIN])
        new = found - ctx.checked_locations
        if new:
            await ctx.send_msgs([{"cmd": "LocationChecks", "locations": sorted(new)}])
        if not ctx.finished_game and ram[STORY] & 0x04:
            ctx.finished_game = True
            await ctx.send_msgs([{"cmd": "StatusUpdate", "status": ClientStatus.CLIENT_GOAL}])

    async def _give_item(self, ctx, ram: bytes, block: bytes, mail_flag: int) -> None:
        received = block[0] | block[1] << 8
        if 0 <= received < self.last_received:
            # A New Game clears the count; loading another save can lower it.
            logger.info("The save's received count went from %d to %d (a New Game or another save), "
                        "so items from number %d on are given again.", self.last_received, received, received + 1)
        self.last_received = received
        if mail_flag:
            return  # the last item's message is still open: wait for it to close
        if received > len(ctx.items_received) and ctx.items_received and not self.ahead_noted:
            self.ahead_noted = True
            logger.warning("This save has received %d items, but the server has sent %d. It belongs to "
                           "another room or an older game: start a New Game for this room.",
                           received, len(ctx.items_received))
        if received >= len(ctx.items_received):
            return
        item = ctx.items_received[received]
        name = ctx.item_names.lookup_in_game(item.item)
        self.notes = []
        self.shown = name   # what the game gives, when it is not the item itself
        writes = self._effect(name, ram)
        if writes is None:
            if self.waiting_noted != received:
                self.waiting_noted = received
                logger.info("No room for %s yet. Make space in the inventory to receive it.", name)
            return
        nxt = received + 1
        given = bool(writes)
        writes.append((AP_RECV, bytes([nxt & 0xFF, nxt >> 8]), "System Bus"))
        # The game already said "Thou hast found" for your own opened chests and search spots
        # (unless it gives something else), and an item that was not given gets no message at all.
        found_here = item.player == ctx.slot and opened_in_game(item.location, block)
        sender = ctx.player_names.get(item.player, "?")
        if given and (not found_here or self.shown != name):
            # Another player's item names them in the message. Your own items do not.
            from_other = item.player != ctx.slot
            writes.append((MAIL_TEXT, dw_text(self.shown, sender if from_other else "", item.player), "System Bus"))
            writes.append((MAIL_FLAG, b"\x01", "System Bus"))
        # Only write if nothing changed since the snapshot: the counter (a save loaded, a reset),
        # and every game byte the effect rewrites (a shop purchase or herb use in between).
        guards = [(AP_RECV, bytes(block[0:2]), "System Bus")]
        for addr, data, domain in writes:
            if domain == "RAM":
                guards.append((addr, bytes(ram[addr:addr + len(data)]), "RAM"))
        ok = await bizhawk.guarded_write(ctx.bizhawk_ctx, writes, guards)
        if ok:
            for note in self.notes:
                logger.warning(note)
        if ok and given:
            logger.info("Received %s from %s.", name if self.shown == name else
                        f"{self.shown} (for {name})", sender)

    def _effect(self, name: str, ram: bytes):
        """RAM writes that give the item: [] when it is not given (dropped or already held),
        None when it has to wait for room (quest items, Dragon's Scale, Fighter's Ring)."""
        if name in ("Herb", "Magic Key"):
            addr, cap = (HERBS, self.herb_cap) if name == "Herb" else (KEYS, self.key_cap)
            if ram[addr] < cap:
                return [(addr, bytes([ram[addr] + 1]), "RAM")]
            self.notes.append(f"You already hold {cap} of {name}, the most this seed allows: "
                              "100 Gold instead.")
            self.shown = name = "100 Gold"
        if name == "100 Gold":
            gold = min(ram[GOLD] | ram[GOLD + 1] << 8, 0xFFFF - 100) + 100
            return [(GOLD, gold.to_bytes(2, "little"), "RAM")]
        if name == "Erdrick's Sword":
            return [(EQUIPMENT, bytes([ram[EQUIPMENT] | 0xE0]), "RAM")]
        if name == "Erdrick's Armor":
            return [(EQUIPMENT, bytes([ram[EQUIPMENT] | 0x1C]), "RAM")]
        value = INVENTORY_ITEMS.get(name)
        if value is None:
            self.notes.append(f"{name} is not a DWR item this client knows, so it was skipped.")
            return []
        inv = bytearray(ram[INVENTORY:INVENTORY + 4])
        slots = [(i, shift) for i in range(4) for shift in (0, 4)]
        held = [(inv[i] >> shift) & 0xF for i, shift in slots]
        if value in UNIQUE and value in held:
            return []  # already held
        target = next((k for k, v in enumerate(held) if v == 0), None)
        if target is None and value in UNIQUE:
            for give_up in REPLACEABLE:
                if give_up in held:
                    target = held.index(give_up)
                    lost = next(n for n, v in INVENTORY_ITEMS.items() if v == give_up)
                    self.notes.append(f"The inventory is full: {name} replaces your {lost}.")
                    break
        if target is None:
            if value in UNIQUE:
                return None  # quest items, the Dragon's Scale and Fighter's Ring wait for room
            self.notes.append(f"Dropped {name}: the inventory is full.")
            return []
        i, shift = slots[target]
        inv[i] = (inv[i] & (0xF0 >> shift)) | (value << shift)
        return [(INVENTORY + i, bytes([inv[i]]), "RAM")]
