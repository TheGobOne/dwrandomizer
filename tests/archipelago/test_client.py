"""Unit tests for the DWR Archipelago client's item delivery (archipelago/dwr/client.py).

Runs without an Archipelago install: the few Archipelago modules the client imports are
stubbed. Run from the repo root:  python -m unittest tests/archipelago/test_client.py
"""
import asyncio
import enum
import importlib.util
import logging
import os
import sys
import types
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DWR_DIR = os.path.join(ROOT, "archipelago", "dwr")


def _stub_archipelago():
    base = types.ModuleType("BaseClasses")

    class ItemClassification(enum.IntFlag):
        filler = 0
        progression = 1
        useful = 2
        trap = 4
        skip_balancing = 8
        progression_skip_balancing = 9

    base.ItemClassification = ItemClassification
    sys.modules["BaseClasses"] = base

    net = types.ModuleType("NetUtils")
    net.ClientStatus = types.SimpleNamespace(CLIENT_GOAL=30)
    sys.modules["NetUtils"] = net

    bh = types.ModuleType("worlds._bizhawk")
    bh.RequestFailedError = type("RequestFailedError", (Exception,), {})
    bh.calls = []
    bh.guard_result = True

    async def guarded_write(ctx, writes, guards):
        bh.calls.append((list(writes), list(guards)))
        return bh.guard_result

    bh.guarded_write = guarded_write
    client_mod = types.ModuleType("worlds._bizhawk.client")
    client_mod.BizHawkClient = type("BizHawkClient", (), {"__init__": lambda self: None})
    worlds = types.ModuleType("worlds")
    worlds.__path__ = []
    sys.modules["worlds"] = worlds
    sys.modules["worlds._bizhawk"] = bh
    sys.modules["worlds._bizhawk.client"] = client_mod
    return bh


def _load_client():
    pkg = types.ModuleType("dwrtest")
    pkg.__path__ = [DWR_DIR]
    sys.modules["dwrtest"] = pkg
    for name in ("data", "text", "client"):
        spec = importlib.util.spec_from_file_location("dwrtest." + name,
                                                      os.path.join(DWR_DIR, name + ".py"))
        mod = importlib.util.module_from_spec(spec)
        sys.modules["dwrtest." + name] = mod
        spec.loader.exec_module(mod)
    return sys.modules["dwrtest.client"]


BH = _stub_archipelago()
C = _load_client()


class FakeCtx:
    def __init__(self, names, sender=2, location=-1):
        # Items sent by another player (2) from a location in their world, unless told otherwise.
        self.items_received = [types.SimpleNamespace(item=i, player=sender, location=location)
                               for i in range(len(names))]
        self._names = names
        self.item_names = types.SimpleNamespace(lookup_in_game=lambda item: self._names[item])
        self.player_names = {1: "Tester", 2: "Friend"}
        self.slot = 1
        self.bizhawk_ctx = None


def give(names, ram, received=0, mail_flag=0, guard_ok=True, client=None, chests=0, **ctx_args):
    BH.calls.clear()
    BH.guard_result = guard_ok
    block = bytes([received & 0xFF, received >> 8]) + chests.to_bytes(4, "little") + b"\x00"
    client = client or C.DWRClient()
    asyncio.run(client._give_item(FakeCtx(names, **ctx_args), bytes(ram), block, mail_flag))
    return BH.calls


def addrs(writes):
    return [w[0] for w in writes]


class GiveItem(unittest.TestCase):
    def setUp(self):
        self.ram = bytearray(0x100)
        self.ram[C.MAP] = 1

    def test_herb_write_guards_the_herb_count(self):
        self.ram[C.HERBS] = 3
        calls = give(["Herb"], self.ram)
        self.assertEqual(len(calls), 1)
        writes, guards = calls[0]
        self.assertIn((C.HERBS, bytes([4]), "RAM"), writes)
        self.assertIn((C.HERBS, bytes([3]), "RAM"), guards)
        self.assertIn((C.AP_RECV, b"\x00\x00", "System Bus"), guards)
        self.assertIn(C.MAIL_FLAG, addrs(writes))

    def test_gold_write_guards_both_gold_bytes(self):
        self.ram[C.GOLD] = 0x2C
        self.ram[C.GOLD + 1] = 0x01
        writes, guards = give(["100 Gold"], self.ram)[0]
        self.assertIn((C.GOLD, (300 + 100).to_bytes(2, "little"), "RAM"), writes)
        self.assertIn((C.GOLD, bytes([0x2C, 0x01]), "RAM"), guards)

    def test_inventory_write_guards_the_slot_byte(self):
        writes, guards = give(["Silver Harp"], self.ram)[0]
        ram_writes = [w for w in writes if w[2] == "RAM"]
        self.assertEqual(len(ram_writes), 1)
        addr = ram_writes[0][0]
        self.assertIn((addr, bytes([self.ram[addr]]), "RAM"), guards)

    def test_herb_at_the_cap_gives_100_gold(self):
        self.ram[C.HERBS] = 6
        with self.assertLogs("Client", level="WARNING") as log:
            writes, guards = give(["Herb"], self.ram)[0]
        self.assertIn("100 Gold instead", log.output[0])
        self.assertIn((C.GOLD, (100).to_bytes(2, "little"), "RAM"), writes)
        self.assertIn((C.MAIL_TEXT, C.dw_text("100 Gold", "Friend", 2), "System Bus"), writes)  # "Gold from Friend"
        self.assertNotIn(C.HERBS, addrs(writes))

    def test_own_chest_herb_at_the_cap_still_says_gold(self):
        self.ram[C.HERBS] = 6
        writes, _ = give(["Herb"], self.ram, sender=1, location=C.BASE + 4, chests=1 << 4)[0]
        self.assertIn(C.MAIL_FLAG, addrs(writes))   # the chest said Herb, the game gave gold

    def test_caps_come_from_the_rom(self):
        client = C.DWRClient()
        client.herb_cap, client.key_cap = 3, 9
        self.ram[C.HERBS] = 3
        self.ram[C.KEYS] = 8
        writes, _ = give(["Herb"], self.ram, client=client)[0]
        self.assertNotIn(C.HERBS, addrs(writes))
        writes, _ = give(["Magic Key"], self.ram, client=client)[0]
        self.assertIn((C.KEYS, bytes([9]), "RAM"), writes)

    def test_full_bag_drops_with_a_log_and_no_message(self):
        for i in range(4):
            self.ram[C.INVENTORY + i] = 0x11  # every slot holds a Torch
        with self.assertLogs("Client", level="WARNING") as log:
            writes, guards = give(["Wings"], self.ram)[0]
        self.assertIn("Dropped Wings", log.output[0])
        self.assertEqual(addrs(writes), [C.AP_RECV])
        self.assertEqual(guards, [(C.AP_RECV, b"\x00\x00", "System Bus")])

    def test_quest_item_already_held_gets_no_message(self):
        self.ram[C.INVENTORY] = 0x0A  # Silver Harp
        writes, _ = give(["Silver Harp"], self.ram)[0]
        self.assertEqual(addrs(writes), [C.AP_RECV])

    def test_unknown_item_is_logged(self):
        with self.assertLogs("Client", level="WARNING") as log:
            writes, _ = give(["Mystery Box"], self.ram)[0]
        self.assertIn("Mystery Box", log.output[0])
        self.assertEqual(addrs(writes), [C.AP_RECV])

    def test_full_bag_quest_item_swaps_with_a_log(self):
        for i in range(4):
            self.ram[C.INVENTORY + i] = 0x11  # every slot holds a Torch
        with self.assertLogs("Client", level="WARNING") as log:
            writes, guards = give(["Silver Harp"], self.ram)[0]
        self.assertIn("replaces your Torch", log.output[0])
        self.assertEqual(len([w for w in writes if w[2] == "RAM"]), 1)

    def test_scale_waits_for_room_instead_of_being_dropped(self):
        self.ram[C.INVENTORY:C.INVENTORY + 4] = bytes([0xAA, 0xAA, 0xAA, 0xAA])  # Silver Harps
        self.assertEqual(give(["Dragon's Scale"], self.ram), [])

    def test_ring_replaces_a_torch_when_the_bag_is_full(self):
        self.ram[C.INVENTORY:C.INVENTORY + 4] = bytes([0x11, 0xAA, 0xAA, 0xAA])
        writes, _ = give(["Fighter's Ring"], self.ram)[0]
        self.assertIn((C.INVENTORY, bytes([0x16]), "RAM"), writes)

    def test_quest_item_never_replaces_the_scale_or_ring(self):
        self.ram[C.INVENTORY:C.INVENTORY + 4] = bytes([0x64, 0x64, 0x64, 0x64])  # Scale and Ring
        self.assertEqual(give(["Silver Harp"], self.ram), [])

    def test_quest_item_without_room_waits_and_logs_once(self):
        for i in range(4):
            self.ram[C.INVENTORY + i] = 0xAA  # every slot holds a Silver Harp
        client = C.DWRClient()
        with self.assertLogs("Client", level="INFO") as log:
            self.assertEqual(give(["Staff of Rain"], self.ram, client=client), [])
            self.assertEqual(give(["Staff of Rain"], self.ram, client=client), [])
        self.assertEqual(sum("No room for Staff of Rain" in line for line in log.output), 1)

    def test_own_chest_item_has_no_mailbox_message(self):
        writes, _ = give(["Herb"], self.ram, sender=1, location=C.BASE + 4, chests=1 << 4)[0]
        self.assertIn(C.HERBS, addrs(writes))
        self.assertNotIn(C.MAIL_FLAG, addrs(writes))

    def test_own_unopened_chest_item_gets_a_message(self):
        # !collect or a release sends items from chests the player never opened.
        writes, _ = give(["Herb"], self.ram, sender=1, location=C.BASE + 4, chests=1 << 5)[0]
        self.assertIn(C.MAIL_FLAG, addrs(writes))

    def test_drop_is_logged_only_when_the_write_lands(self):
        self.ram[C.HERBS] = 6
        logger = logging.getLogger("Client")
        with self.assertLogs("Client", level="INFO") as log:
            logger.info("marker")
            give(["Herb"], self.ram, guard_ok=False)
        self.assertFalse(any("instead" in line for line in log.output))

    def test_save_ahead_of_the_server_warns_once(self):
        client = C.DWRClient()
        with self.assertLogs("Client", level="WARNING") as log:
            self.assertEqual(give(["Herb"], self.ram, received=5, client=client), [])
            give(["Herb"], self.ram, received=5, client=client)
        self.assertEqual(sum("has received 5 items" in line for line in log.output), 1)

    def test_received_count_drop_is_logged(self):
        client = C.DWRClient()
        give(["Herb", "Herb", "Herb"], self.ram, received=2, client=client, mail_flag=1)
        with self.assertLogs("Client", level="INFO") as log:
            give(["Herb", "Herb", "Herb"], self.ram, received=0, client=client, mail_flag=1)
        self.assertTrue(any("went from 2 to 0" in line for line in log.output))

    def test_first_poll_and_rising_count_log_nothing(self):
        client = C.DWRClient()
        logger = logging.getLogger("Client")
        with self.assertLogs("Client", level="INFO") as log:
            logger.info("marker")
            give(["Herb", "Herb"], self.ram, received=0, client=client, mail_flag=1)
            give(["Herb", "Herb"], self.ram, received=1, client=client, mail_flag=1)
        self.assertFalse(any("went from" in line for line in log.output))

    def test_own_gwaelin_item_still_gets_a_message(self):
        gwaelin = C.LOCATION_NAME_TO_ID[C.GWAELIN]
        writes, _ = give(["Herb"], self.ram, sender=1, location=gwaelin)[0]
        self.assertIn(C.MAIL_FLAG, addrs(writes))

    def test_open_mailbox_waits(self):
        self.assertEqual(give(["Herb"], self.ram, mail_flag=1), [])

    def test_failed_guard_logs_nothing_received(self):
        logger = logging.getLogger("Client")
        with self.assertLogs("Client", level="INFO") as log:
            logger.info("marker")
            give(["Herb"], self.ram, guard_ok=False)
        self.assertFalse(any("Received" in line for line in log.output))

    def test_long_name_is_shortened_for_the_game(self):
        text = C.dw_text("Stones of Sunlight")
        self.assertEqual(text, C.dw_text("Sunlight Stones"))
        self.assertEqual(len(text), len("Sunlight Stones") + 1)

    def test_every_item_name_fits_the_message(self):
        data = sys.modules["dwrtest.data"]
        text = sys.modules["dwrtest.text"]
        for name in data.ITEMS:
            shown = text.SHORT_NAMES.get(name, name)
            self.assertLessEqual(len(shown), 16, name)


class SeedCheck(unittest.TestCase):
    ROM = b"\x01\x02\x03\x04"

    def check(self, saved, tag=b"\x00\x00", rom_hash=ROM, guard_ok=True):
        BH.calls.clear()
        BH.guard_result = guard_ok
        client = C.DWRClient()
        client.seed_hash = rom_hash
        client.slot_tag = C.slot_tag(rom_hash)
        ok = asyncio.run(client._check_seed(FakeCtx([]), saved, tag))
        return ok, list(BH.calls)

    def test_old_save_without_hash_is_adopted(self):
        for empty in (b"\x00\x00\x00\x00", b"\xff\xff\xff\xff"):
            ok, calls = self.check(empty)
            self.assertTrue(ok)
            writes, guards = calls[0]
            self.assertEqual(writes, [(C.SEED_HASH, self.ROM, "System Bus")])
            self.assertEqual(guards, [(C.SEED_HASH, empty, "System Bus")])

    def test_failed_adopt_waits_a_poll(self):
        ok, _ = self.check(b"\x00\x00\x00\x00", guard_ok=False)
        self.assertFalse(ok)

    def test_same_seed_goes_on(self):
        ok, calls = self.check(self.ROM, tag=C.slot_tag(self.ROM))
        self.assertTrue(ok)
        self.assertEqual(calls, [])

    def test_untagged_slot_goes_on(self):
        self.assertTrue(self.check(self.ROM, tag=b"\x00\x00")[0])
        self.assertTrue(self.check(self.ROM, tag=b"\xff\xff")[0])

    def test_other_seed_stops_with_one_warning(self):
        with self.assertLogs("Client", level="WARNING") as log:
            ok, calls = self.check(b"\x09\x09\x09\x09")
        self.assertFalse(ok)
        self.assertEqual(calls, [])
        self.assertIn("different DWR seed", log.output[0])

    def test_own_slot_after_another_seeds_game_takes_the_hash_back(self):
        other = b"\x09\x09\x09\x09"
        ok, calls = self.check(other, tag=C.slot_tag(self.ROM))
        self.assertFalse(ok)   # this poll's snapshot still has the old bits: send nothing
        writes, guards = calls[0]
        self.assertIn((C.SEED_HASH, self.ROM, "System Bus"), writes)
        self.assertIn((C.AP_RECV + 2, bytes(5), "System Bus"), writes)
        self.assertEqual(guards, [(C.SEED_HASH, other, "System Bus")])

    def test_slot_saved_by_another_seed_stops(self):
        ok, _ = self.check(self.ROM, tag=b"\x33\x02")
        self.assertFalse(ok)

    def test_a_version_003_save_with_one_tag_byte_is_ours(self):
        ok, _ = self.check(self.ROM, tag=bytes([self.ROM[0], 0x00]))
        self.assertTrue(ok)

    def test_second_tag_byte_tells_seeds_apart(self):
        # Same first byte as this ROM's tag, another seed's second byte.
        ok, _ = self.check(self.ROM, tag=bytes([self.ROM[0], 0x99]))
        self.assertFalse(ok)

    def test_rom_without_hash_is_not_checked(self):
        self.assertTrue(self.check(b"\x09\x09\x09\x09", rom_hash=b"")[0])

    def test_slot_tag_matches_the_rom_rule(self):
        self.assertEqual(C.slot_tag(b"\x4c\x8b\x6f\x66"), b"\x4c\x8b")
        self.assertEqual(C.slot_tag(b"\x00\x01\x02\x03"), b"\x5a\x01")
        self.assertEqual(C.slot_tag(b"\xff\x01\x02\x03"), b"\x5a\x01")
        self.assertEqual(C.slot_tag(b"\x4c\x8b\x6f\x66", 1), b"\x4c")   # ROM version 003


if __name__ == "__main__":
    unittest.main()
