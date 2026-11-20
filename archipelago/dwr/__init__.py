"""DWR (Dragon Warrior Randomizer) world for Archipelago. Proof of concept.

Every chest and search spot sets a bit the client reads, and items come in
through the client. Generation writes a .apdwr file with the slot name, the
DWR flags and seed, and the item name for each location's found message.
The player builds the ROM from it with the DWR CLI:

    dwrandomizer --ap-patch <file.apdwr> <vanilla rom> <output dir>

With Free Charlock on in the flags, Erdrick's Sword and Armor and the quest
items stay out of the Charlock chests.

flags.py rejects the DWR flags the logic does not cover: Random Chest Locations
(a chest can move into Charlock), Stair Shuffle, and the flags that change how
you win. Random Map and Open Charlock are fine: the Charlock rule only asks for
more than Open Charlock needs.
Magic Keys can be bought, so key doors are not in logic.
"""
import logging
import os
from typing import Any, ClassVar, Dict

from BaseClasses import Item, Location, Region
from worlds.AutoWorld import World

from Options import OptionError

from .client import DWRClient  # noqa: F401  registers the BizHawk client
from . import flags as dwr_flags
from .text import found_text
from .host_settings import DWRSettings
from .options import DWROptions
from .patch import DWRPatch
from .data import (CHARLOCK_CHESTS, FILLER, GAME, GOAL, ITEM_NAME_TO_ID, ITEMS,
                   LOCATION_NAME_TO_ID, SLOT_FLAGS_OFF, SLOT_PROGRESSION_MARK)


class DWRItem(Item):
    game = GAME


class DWRLocation(Location):
    game = GAME


class DWRWorld(World):
    """Dragon Warrior for the NES, randomized by DWR."""
    game = GAME
    settings_key = "dwr_options"
    settings: ClassVar[DWRSettings]
    options_dataclass = DWROptions
    options: DWROptions
    topology_present = False
    item_name_to_id = ITEM_NAME_TO_ID
    location_name_to_id = LOCATION_NAME_TO_ID

    free_charlock: bool = False
    flags_text: str = ""   # the DWR flags this slot is built with, after random_off
    flags_turned_off: list   # flags the player set to Random that Archipelago turns off; set in generate_early

    def fill_slot_data(self) -> Dict[str, Any]:
        # The client shows these once the player connects. Empty when nothing was turned off.
        turned_off = getattr(self, "flags_turned_off", [])
        data = {SLOT_FLAGS_OFF: list(turned_off)} if turned_off else {}
        if self.options.progression_mark.value:
            data[SLOT_PROGRESSION_MARK] = True
        return data

    def write_spoiler_header(self, spoiler_handle) -> None:
        # The spoiler log is the only place a player can see this without the client. Nothing is written
        # when no Random flag was turned off.
        turned_off = getattr(self, "flags_turned_off", [])
        if turned_off:
            spoiler_handle.write("DWR flags set to Random and turned off, because Archipelago does not support "
                                 f"them: {', '.join(turned_off)}\n")

    def generate_early(self) -> None:
        self.flags_turned_off = []
        # The ROM keeps the slot name in 32 bytes of UTF-8 for the client to log in with.
        if len(self.player_name.encode("utf-8")) > 32:
            raise OptionError(f"{self.player_name}: the slot name is "
                              f"{len(self.player_name.encode('utf-8'))} bytes of UTF-8, and DWR keeps 32. "
                              "Use fewer non-Latin characters.")
        text = dwr_flags.clean(self.options.dwr_flags.value)
        try:
            text, turned_off = dwr_flags.random_off(text)
            if turned_off:
                logging.warning(f"{self.player_name}: these DWR flags were set to Random and are not supported "
                                f"with Archipelago, so they are off: {', '.join(turned_off)}")
            self.flags_text = text
            self.flags_turned_off = list(turned_off)
            unknown = dwr_flags.unknown_values(text)
            if unknown:
                logging.warning(f"{self.player_name}: these DWR flag values are not recognized, so the game "
                                f"uses each option's normal setting: {', '.join(unknown)}")
            bad = dwr_flags.unsupported(text)
            self.free_charlock = dwr_flags.free_charlock(text)
        except ValueError as error:
            raise OptionError(f"{self.player_name}: DWR flags: {error}") from error
        if bad:
            raise OptionError(f"{self.player_name}: these DWR flags are not supported with Archipelago: "
                              + "; ".join(bad))

    def create_item(self, name: str) -> Item:
        data = ITEMS[name]
        return DWRItem(name, data.classification, data.code, self.player)

    def create_regions(self) -> None:
        menu = Region("Menu", self.player, self.multiworld)
        for name, code in LOCATION_NAME_TO_ID.items():
            menu.locations.append(DWRLocation(self.player, name, code, menu))
        goal = DWRLocation(self.player, GOAL, None, menu)
        goal.place_locked_item(DWRItem("Victory", ITEMS["Silver Harp"].classification, None, self.player))
        menu.locations.append(goal)
        self.multiworld.regions.append(menu)

    def create_items(self) -> None:
        for name, data in ITEMS.items():
            for _ in range(data.count):
                self.multiworld.itempool.append(self.create_item(name))

    def set_rules(self) -> None:
        player = self.player

        def rainbow_drop(state) -> bool:
            # The Southern Shrine sage trades these for the Rainbow Drop, which builds the bridge to Charlock.
            return state.has_all(("Stones of Sunlight", "Staff of Rain", "Erdrick's Token"), player)

        self.get_location("Staff of Rain Shrine Chest").access_rule = \
            lambda state: state.has("Silver Harp", player)
        for name in CHARLOCK_CHESTS:
            self.get_location(name).access_rule = rainbow_drop
        self.get_location(GOAL).access_rule = rainbow_drop
        if self.free_charlock:
            # Free Charlock skips the castle, so nothing needed to win or to dress for the fight may be in it.
            blocked = {"Erdrick's Sword", "Erdrick's Armor", "Silver Harp", "Stones of Sunlight",
                       "Staff of Rain", "Erdrick's Token"}
            for name in CHARLOCK_CHESTS:
                self.get_location(name).item_rule = lambda item: not (item.player == player and item.name in blocked)
        self.multiworld.completion_condition[player] = lambda state: state.has("Victory", player)

    def get_filler_item_name(self) -> str:
        return FILLER

    def generate_output(self, output_directory: str) -> None:
        """Write <name>.apdwr, the patch container (see patch.py).

        Its dwr.txt has "slot", "flags", "seed" and "free_charlock" lines, then
        one "loc <location id hex> <text>" per chest and search spot, where text
        is what the game shows in the found message: the item name, plus the
        receiving player's name if it is not this player's item.
        """
        player = self.player
        lines = [
            f"slot {self.multiworld.player_name[player]}",
            f"flags {self.flags_text}",
            f"seed {self.random.getrandbits(63)}",
            f"free_charlock {1 if self.free_charlock else 0}",
            "text 2",  # the texts below are whole sentences (DWR AP version 005); older patches lack this line
        ]
        for location in self.multiworld.get_locations(player):
            if location.address is None or location.item is None:
                continue
            item = location.item
            owner = self.multiworld.player_name[item.player] if item.player != player else ""
            mark = bool(self.options.progression_mark.value) and bool(item.advancement)
            lines.append(f"loc {location.address:x} {found_text(item.name, owner, item.player, mark)}")
        name = self.multiworld.get_out_file_name_base(player) + DWRPatch.patch_file_ending
        DWRPatch(os.path.join(output_directory, name), player=player,
                 player_name=self.multiworld.player_name[player],
                 text="\n".join(lines) + "\n").write()
