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
            f = flag_map[labels[opt]]
            site_bits.add((labels[opt], f["byte"], f["mask"] << f["shift"]))
        apworld_bits = {(name, byte, mask) for name, byte, mask, _ in flags.UNSUPPORTED}
        self.assertEqual(site_bits, apworld_bits)


if __name__ == "__main__":
    unittest.main()
