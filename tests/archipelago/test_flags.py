"""The flags the APWorld rejects (archipelago/dwr/flags.py) and the site's Archipelago line
(AP_REJECT in site/index.html) must name the same options with the same bits.

Run from the repo root:  python -m unittest tests/archipelago/test_flags.py
"""
import importlib.util
import json
import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
spec = importlib.util.spec_from_file_location("dwr_flags", os.path.join(ROOT, "archipelago", "dwr", "flags.py"))
flags = importlib.util.module_from_spec(spec)
spec.loader.exec_module(flags)
SITE = open(os.path.join(ROOT, "site", "index.html"), encoding="utf-8").read()


def site_json(name):
    """A top-level `const NAME = ...;` value from the site, parsed as JSON."""
    m = re.search(r"const " + name + r"\s*=\s*(.*?);\r?\n", SITE, re.S)
    text = m.group(1)
    if text.startswith("['"):
        text = text.replace("'", '"')   # a JS list of single-quoted ids
    return json.loads(text)


class RejectedFlags(unittest.TestCase):
    def test_site_and_apworld_reject_the_same_bits(self):
        reject = site_json("AP_REJECT")
        flag_map = site_json("FLAG_MAP")
        labels = {it[0]: it[1] for tab in site_json("TABS") for it in tab["items"]}
        site_bits = set()
        for opt in reject:
            # FLAG_MAP is keyed by option id (older sites keyed it by label text).
            f = flag_map[opt] if opt in flag_map else flag_map[labels[opt]]
            site_bits.add((labels[opt], f["byte"], f["mask"] << f["shift"]))
        apworld_bits = {(name, byte, mask) for name, byte, mask, _ in flags.UNSUPPORTED}
        self.assertEqual(site_bits, apworld_bits)


GOB_STANDARD = "IVIAAV2EKADEAMAAAADYKEBWAAAJACAAAAIBQAAM75QA"


def with_bits(code, byte, mask, value):
    """The code with flag byte `byte` set to value under mask, re-encoded at the same length."""
    data = flags.decode(code) + [0] * 2
    data[byte] = (data[byte] & ~mask) | value
    bits = "".join(format(b, "08b") for b in data)[:len(code) * 5]
    return "".join(flags.BASE32[int(bits[i:i + 5], 2)] for i in range(0, len(bits), 5))


class RandomOff(unittest.TestCase):
    def test_gob_standard_is_accepted(self):
        self.assertEqual(flags.unsupported(GOB_STANDARD), [])
        self.assertEqual(flags.random_off(GOB_STANDARD), (GOB_STANDARD, []))

    def test_random_flags_are_turned_off(self):
        for name, byte, mask, _ in flags.UNSUPPORTED:
            maybe = mask & 0xAA
            code = with_bits(GOB_STANDARD, byte, mask, maybe)
            out, names = flags.random_off(code)
            self.assertEqual(names, [name])
            self.assertEqual(out, GOB_STANDARD)
            self.assertEqual(flags.unsupported(out), [])

    def test_flags_that_are_on_stay_rejected(self):
        for name, byte, mask, _ in flags.UNSUPPORTED:
            code = with_bits(GOB_STANDARD, byte, mask, mask & 0x55)
            out, names = flags.random_off(code)
            self.assertEqual((out, names), (code, []))
            self.assertEqual(len(flags.unsupported(out)), 1)

    def test_default_option_is_gob_standard(self):
        with open(os.path.join(ROOT, "archipelago", "dwr", "options.py"), encoding="utf-8") as f:
            text = f.read()
        self.assertIn('default = "' + GOB_STANDARD + '"', text)


if __name__ == "__main__":
    unittest.main()
