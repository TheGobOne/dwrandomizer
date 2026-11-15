"""Items and locations for the DWR APWorld (proof of concept).

Location IDs match the ROM's AP bits:
  chests   BASE + n        n = index in DWR's chest table (rom->chests, 31 chests)
  search   BASE + 0x40 + n n = search table entry (0 overworld, 1 Kol, 2 Hauksness)
  events   BASE + 0x80 + n
"""
from typing import NamedTuple

from BaseClasses import ItemClassification

BASE = 0xD0000
GAME = "DWR"

CHESTS = [
    "Tantegel Treasury Chest 1",      # 0  (map 4, 1,13)
    "Tantegel Treasury Chest 2",      # 1  (1,15)
    "Tantegel Treasury Chest 3",      # 2  (2,14)
    "Tantegel Treasury Chest 4",      # 3  (3,15)
    "Throne Room Chest 1",            # 4  (4,4)
    "Throne Room Chest 2",            # 5  (5,4)
    "Throne Room Chest 3",            # 6  (6,1), the starting key chest
    "Rimuldar Chest",                 # 7
    "Garinham Chest 1",               # 8
    "Garinham Chest 2",               # 9
    "Garinham Chest 3",               # 10
    "Charlock Throne Room Chest 1",   # 11
    "Charlock Throne Room Chest 2",   # 12
    "Charlock Throne Room Chest 3",   # 13
    "Charlock Throne Room Chest 4",   # 14
    "Charlock Throne Room Chest 5",   # 15
    "Charlock Throne Room Chest 6",   # 16
    "Tantegel Basement Chest",        # 17
    "Staff of Rain Shrine Chest",     # 18
    "Garin's Grave Chest 1",          # 19
    "Garin's Grave Chest 2",          # 20
    "Garin's Grave Chest 3",          # 21
    "Garin's Grave Chest 4",          # 22
    "Garin's Grave Chest 5",          # 23
    "Charlock Cave Chest",            # 24
    "Mountain Cave Chest 1",          # 25
    "Mountain Cave Chest 2",          # 26
    "Mountain Cave Chest 3",          # 27
    "Mountain Cave Chest 4",          # 28
    "Mountain Cave Chest 5",          # 29
    "Erdrick's Cave Chest",           # 30
]
SEARCH_SPOTS = ["Overworld Search Spot", "Kol Search Spot", "Hauksness Search Spot"]
GWAELIN = "Rescue Gwaelin"
GOAL = "Defeat the Dragonlord"

LOCATION_NAME_TO_ID = {name: BASE + i for i, name in enumerate(CHESTS)}
LOCATION_NAME_TO_ID.update({name: BASE + 0x40 + i for i, name in enumerate(SEARCH_SPOTS)})
LOCATION_NAME_TO_ID[GWAELIN] = BASE + 0x80

CHARLOCK_CHESTS = {CHESTS[i] for i in (11, 12, 13, 14, 15, 16, 24)}


class ItemData(NamedTuple):
    code: int
    classification: ItemClassification
    count: int  # copies in the pool


P, U, F = ItemClassification.progression, ItemClassification.useful, ItemClassification.filler

ITEMS = {
    "Silver Harp":        ItemData(BASE + 0x01, P, 1),
    "Stones of Sunlight": ItemData(BASE + 0x02, P, 1),
    "Staff of Rain":      ItemData(BASE + 0x03, P, 1),
    "Erdrick's Token":    ItemData(BASE + 0x04, P, 1),
    "Fairy Flute":        ItemData(BASE + 0x05, U, 1),
    "Erdrick's Sword":    ItemData(BASE + 0x06, U, 1),
    "Erdrick's Armor":    ItemData(BASE + 0x07, U, 1),
    "Dragon's Scale":     ItemData(BASE + 0x08, U, 1),
    "Fighter's Ring":     ItemData(BASE + 0x09, U, 1),
    "Herb":               ItemData(BASE + 0x10, F, 6),
    "Magic Key":          ItemData(BASE + 0x11, F, 6),
    "Torch":              ItemData(BASE + 0x12, F, 3),
    "Fairy Water":        ItemData(BASE + 0x13, F, 3),
    "Wings":              ItemData(BASE + 0x14, F, 3),
    "100 Gold":           ItemData(BASE + 0x15, F, 5),
}
ITEM_NAME_TO_ID = {name: data.code for name, data in ITEMS.items()}
FILLER = "Herb"
