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
    for name in ("data", "client"):
        spec = importlib.util.spec_from_file_location("dwrtest." + name,
                                                      os.path.join(DWR_DIR, name + ".py"))
        mod = importlib.util.module_from_spec(spec)
        sys.modules["dwrtest." + name] = mod
        spec.loader.exec_module(mod)
    return sys.modules["dwrtest.client"]


BH = _stub_archipelago()
C = _load_client()


class FakeCtx:
    def __init__(self, names):
        self.items_received = [types.SimpleNamespace(item=i, player=1) for i in range(len(names))]
        self._names = names
        self.item_names = types.SimpleNamespace(lookup_in_game=lambda item: self._names[item])
        self.player_names = {1: "Tester"}
        self.bizhawk_ctx = None


def give(names, ram, received=0, mail_flag=0, guard_ok=True):
    BH.calls.clear()
    BH.guard_result = guard_ok
    block = bytes([received & 0xFF, received >> 8, 0, 0, 0, 0, 0])
    client = C.DWRClient()
    asyncio.run(client._give_item(FakeCtx(names), bytes(ram), block, mail_flag))
    return BH.calls


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

    def test_gold_write_guards_both_gold_bytes(self):
        self.ram[C.GOLD] = 0x2C
        self.ram[C.GOLD + 1] = 0x01
        writes, guards = give(["100 Gold"], self.ram)[0]
        self.assertIn((C.GOLD, (300 + 100).to_bytes(2, "little"), "RAM"), writes)
        self.assertIn((C.GOLD, bytes([0x2C, 0x01]), "RAM"), guards)

    def test_inventory_write_guards_the_slot_byte(self):
        self.ram[C.INVENTORY] = 0x00
        writes, guards = give(["Silver Harp"], self.ram)[0]
        ram_writes = [w for w in writes if w[2] == "RAM"]
        self.assertEqual(len(ram_writes), 1)
        addr = ram_writes[0][0]
        self.assertIn((addr, bytes([self.ram[addr]]), "RAM"), guards)

    def test_herb_at_six_waits(self):
        self.ram[C.HERBS] = 6
        with self.assertLogs("Client", level="INFO") as log:
            calls = give(["Herb"], self.ram)
        self.assertEqual(calls, [])
        self.assertIn("No room for Herb", log.output[0])

    def test_key_at_six_waits(self):
        self.ram[C.KEYS] = 6
        self.assertEqual(give(["Magic Key"], self.ram), [])

    def test_full_bag_drops_with_a_log(self):
        for i in range(4):
            self.ram[C.INVENTORY + i] = 0x11  # every slot holds a Torch
        with self.assertLogs("Client", level="WARNING") as log:
            writes, guards = give(["Wings"], self.ram)[0]
        self.assertIn("Dropped Wings", log.output[0])
        self.assertEqual([w for w in writes if w[2] == "RAM"], [])
        self.assertEqual(guards, [(C.AP_RECV, b"\x00\x00", "System Bus")])

    def test_full_bag_quest_item_swaps_with_a_log(self):
        for i in range(4):
            self.ram[C.INVENTORY + i] = 0x11  # every slot holds a Torch
        with self.assertLogs("Client", level="WARNING") as log:
            writes, guards = give(["Silver Harp"], self.ram)[0]
        self.assertIn("replaces your Torch", log.output[0])
        self.assertEqual(len([w for w in writes if w[2] == "RAM"]), 1)

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


if __name__ == "__main__":
    unittest.main()
