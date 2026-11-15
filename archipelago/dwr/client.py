"""BizHawk client for DWR (works with the FCEUX connector too).

ROM side (common/archipelago.c in DWR):
  PRG $7FE0   "DWRAP", 3 version digits, little endian pointer to the slot name
  $6820-21    items received so far (the client raises it after each item)
  $6822-25    chest bits, bit n = chest n
  $6826       search spot bits
"""
import logging
from typing import TYPE_CHECKING

from NetUtils import ClientStatus

import worlds._bizhawk as bizhawk
from worlds._bizhawk.client import BizHawkClient

from .data import BASE, GAME, GWAELIN, LOCATION_NAME_TO_ID

if TYPE_CHECKING:
    from worlds._bizhawk.context import BizHawkClientContext

logger = logging.getLogger("Client")

SIGNATURE = 0x7FE0
MAGIC = b"DWRAP"
ROM_VERSION = b"001"

AP_RECV = 0x6820
AP_BLOCK_SIZE = 7  # $6820-$6826

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
REPLACEABLE = (0x1, 0x2, 0x3, 0x4, 0x6)  # given up to make room for a quest item, in this order


class DWRClient(BizHawkClient):
    game = GAME
    system = "NES"

    def __init__(self) -> None:
        super().__init__()
        self.slot_name = ""

    async def validate_rom(self, ctx: "BizHawkClientContext") -> bool:
        try:
            sig = (await bizhawk.read(ctx.bizhawk_ctx, [(SIGNATURE, 16, "PRG ROM")]))[0]
            if sig[:5] != MAGIC:
                return False
            if sig[5:8] != ROM_VERSION:
                logger.warning("This ROM is DWR AP version %s, the client expects %s.",
                               sig[5:8].decode(errors="replace"), ROM_VERSION.decode())
            name_at = sig[8] | sig[9] << 8
            name = (await bizhawk.read(ctx.bizhawk_ctx, [(name_at, 16, "PRG ROM")]))[0]
        except bizhawk.RequestFailedError:
            return False
        self.slot_name = name.split(b"\x00")[0].decode("ascii", errors="replace")
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
            ram, block = await bizhawk.read(ctx.bizhawk_ctx, [
                (0x0000, 0x100, "RAM"),
                (AP_RECV, AP_BLOCK_SIZE, "System Bus"),
            ])
        except bizhawk.RequestFailedError:
            return
        if ram[MAP] == 0:
            return  # title screen or menus: nothing in RAM is the game yet

        await self._send_checks(ctx, ram, block)
        await self._give_item(ctx, ram, block)

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

    async def _give_item(self, ctx, ram: bytes, block: bytes) -> None:
        received = block[0] | block[1] << 8
        if received >= len(ctx.items_received):
            return
        item = ctx.items_received[received]
        name = ctx.item_names.lookup_in_game(item.item)
        writes = self._effect(name, ram)
        if writes is None:
            logger.info("No room for %s yet. Make space in the inventory to receive it.", name)
            return
        nxt = received + 1
        writes.append((AP_RECV, bytes([nxt & 0xFF, nxt >> 8]), "System Bus"))
        # Only write if the counter has not changed since it was read (a save loaded, a reset).
        ok = await bizhawk.guarded_write(ctx.bizhawk_ctx, writes,
                                         [(AP_RECV, bytes(block[0:2]), "System Bus")])
        if ok:
            sender = ctx.player_names.get(item.player, "?")
            logger.info("Received %s from %s.", name, sender)

    @staticmethod
    def _effect(name: str, ram: bytes):
        """RAM writes that give the item, or None if there is no room for it."""
        if name == "Herb":
            return [(HERBS, bytes([min(ram[HERBS] + 1, 6)]), "RAM")]
        if name == "Magic Key":
            return [(KEYS, bytes([min(ram[KEYS] + 1, 6)]), "RAM")]
        if name == "100 Gold":
            gold = min(ram[GOLD] | ram[GOLD + 1] << 8, 0xFFFF - 100) + 100
            return [(GOLD, gold.to_bytes(2, "little"), "RAM")]
        if name == "Erdrick's Sword":
            return [(EQUIPMENT, bytes([ram[EQUIPMENT] | 0xE0]), "RAM")]
        if name == "Erdrick's Armor":
            return [(EQUIPMENT, bytes([ram[EQUIPMENT] | 0x1C]), "RAM")]
        value = INVENTORY_ITEMS.get(name)
        if value is None:
            return []  # unknown item: count it as given
        inv = bytearray(ram[INVENTORY:INVENTORY + 4])
        slots = [(i, shift) for i in range(4) for shift in (0, 4)]
        held = [(inv[i] >> shift) & 0xF for i, shift in slots]
        if value in QUEST and value in held:
            return []  # already held
        target = next((k for k, v in enumerate(held) if v == 0), None)
        if target is None and value in QUEST:
            for give_up in REPLACEABLE:
                if give_up in held:
                    target = held.index(give_up)
                    break
        if target is None:
            return None if value in QUEST else []  # quest items wait for room, others are dropped
        i, shift = slots[target]
        inv[i] = (inv[i] & (0xF0 >> shift)) | (value << shift)
        return [(INVENTORY + i, bytes([inv[i]]), "RAM")]
