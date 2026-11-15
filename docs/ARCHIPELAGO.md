# Archipelago support for DWR: research and plan

Status: research only. No ROM behavior has changed. Written 2026-11-14.

## 1. Existing Dragon Warrior APWorld

One exists, and it is built on the same randomizer code DWR is.

- Repo: [Serpikmin/Archipelago-DragonWarrior](https://github.com/Serpikmin/Archipelago-DragonWarrior), world code on the `dragon_warrior` branch under `worlds/dragon_warrior/`.
- Latest release: v1.0.5, 2026-06-01 (v1.0.1 was 2025-11-15). Listed as "Custom" (unofficial, not in Archipelago's main repo) on the [Archipelago wiki](https://archipelago.miraheze.org/wiki/Dragon_Warrior). It has Death Link and a PopTracker pack by Quinian.
- ROM patcher: [Serpikmin/dwrandomizer](https://github.com/Serpikmin/dwrandomizer), a fork of juef17's `All-the-new-things-and-more`. Merge base is `6d0fc164` (2025-03-15). The fork is 32 commits ahead and 37 behind juef17. DWR is 103 commits ahead of juef17, so the patcher lacks everything DWR added.
- Nothing else turned up: no Dragon Warrior world in ArchipelagoMW/Archipelago, and GitHub search only finds Dragon Quest III HD-2D and Rocket Slime worlds.

### How it works

Generation (Python, `__init__.py`, `options.py`, `regions.py`):
- YAML options map to DWR flag bits by editing characters of a 40-char base32 flag string (`determine_flags`). This hard-codes juef17's 2025 flag layout.
- Items are added to the multiworld pool. Every in-game chest holds a dummy "APItem" (the unused Tablet item, renamed).
- The `.apdw` patch file is a zip with `flags.txt`, `search.txt`, `shopsanity.txt` and `deathlink.txt`.

Patching (on the player's machine, `rom.py`):
- The `.apworld` ships a compiled Cython module (`dwr.cp3XX-win_amd64.pyd` or a Linux `.so`) that wraps `dwr_randomize()`. One binary per Python version and OS.
- The fork adds `ap_changes()` to `common/dwr.c` (about 200 lines of `vpatch` hooks), then the Python side writes the slot name at file offset `0xBFF0` and a Death Link marker at `0x7FFF`.

Client (`client.py`): Archipelago's generic BizHawk client plus the stock Lua connector. Mesen is not supported by Archipelago's NES client stack, so AP play needs BizHawk (check Archipelago's current BizHawk client docs for the supported version).

| What | Address | How the client uses it |
|---|---|---|
| Current map | RAM `$45` | Builds chest location IDs |
| Opened chests on this map | SRAM `$601C-$602B` | 8 x/y pairs. Location ID is `map<<16 \| x<<8 \| y` |
| Items received counter | RAM `$0E` | Saved to SRAM `$7000` by a hook in SaveData |
| AP event byte | RAM `$B9` | Set by patched code: search spot (`$81/$41/$21`), Rainbow Drop (`$FF`), shop purchase (equipment value) |
| Level | RAM `$C7` | Levelsanity |
| Gwaelin state | RAM `$DF` bits 0x01, 0x02 | Rescue and return checks |
| Story flags | RAM `$E4` bit 0x04 | Dragonlord dead = goal. Bits 0x10/0x20 repurposed for Death Link |
| Monsters won | SRAM `$66C0` + 2*index | Monstersanity (DWR's stats screen data) |
| Inventory, herbs, gold, equipment, keys | RAM `$C1-$C4`, `$C0`, `$BD`, `$BE`, `$BF` | Received items are written straight into these |

Locations it supports: 31 chests at vanilla coordinates, Gwaelin rescue and Gwaelin's Love, Staff of Rain, Rainbow Drop (locked), Ball of Light (victory), plus optional Searchsanity (3), Levelsanity (up to 29), Shopsanity (equipment per town) and Monstersanity (40).

### Should DWR build on it?

Yes for the APWorld, client and logic. No for its patcher.

Reuse: item and location tables, region logic, the BizHawk client, PopTracker data, and Death Link. That is the slowest part to write and it has had a year of player fixes.

Replace: the patcher. It is a stale fork, so its hooks would have to be re-applied to DWR's code. The plan below ports `ap_changes()` into DWR behind an AP switch and points a fork of the APWorld at DWR's build.

Known weak spots in its design that DWR can fix later:
- Chest IDs come from vanilla x/y coordinates, so Random Chest Locations can't be used. The client also reads the opened-chest list for the current map only, so a check can be missed if the player leaves a map between client polls.
- Received items are poked into RAM with no in-game message and no check for menus or battles.
- Flags are built by editing base32 characters, which breaks whenever DWR's flag layout changes.
- One compiled module per Python version and OS.
- It turns off `rewrite_search_take_code()` and hooks the vanilla search code instead. DWR still calls that function, so search hooks need a new design.

Licenses: DWR and Archipelago are MIT. The APWorld repo inherits Archipelago's MIT license. Credit Serpikmin in any fork. This is a personal project, so no outreach is planned.

## 2. What DWR needs

| Part | Where | Notes |
|---|---|---|
| AP mode in the patcher | `common/dwr.c`, `common/dwr.h`, `cli/main.c` | New entry point, e.g. `dwr_randomize_ap(input, seed, flags, ap_opts, out)`. Calls the same `apply_stuff_to_rom()` then an `ap_changes()` port. Writes `DWAPV` + version at the ROM signature. CLI gets `--ap` options for testing. |
| Python binding | new `ap/` folder | Cython or `ctypes` wrapper around the AP entry point. `ctypes` + a plain shared library is simpler to build for several Python versions. |
| APWorld | fork of Serpikmin's world | Swap the flag builder for one that packs DWR's flag bytes directly (`common/dwr.h` macros), then base32 encode. Add options for DWR-only flags that are safe in AP. |
| Client | Serpikmin's `client.py` | Keep as is for the proof of concept. Later: read a persistent checked-locations bitfield and use a mailbox for received items. |
| Logic | `regions.py` | Today assumes vanilla map, no stair shuffle. DWR's stair shuffle, random map and biomes need either "disabled in AP" or new logic. |

DWR flags that conflict with AP and should start disabled: Random Chest Locations (`flags[0] & 0x30`), Stair Shuffle, the Charlock flags noted in project memory, Cursed Princess and Three's Company (they move or replace quest items), and No Keys.

## 3. DWR locations, items and RAM

### Locations

| Group | Count | Source in DWR | How a check can be detected |
|---|---|---|---|
| Chests | 31 (`CHEST_COUNT`) | `rom->chests`, list in `no_chest_shuffle()` (`common/chests.c`) | Today: SRAM `$601C-$602B` for the current map. Better: ROM hook in TAKE code sets bit `chest_index` in a 4-byte SRAM bitfield. |
| Search spots | 3 | Overworld (Token), Kol (Flute), Hauksness (Armor), table at ROM `0xE136` | Hook in DWR's rewritten SEARCH code (`rewrite_search_take_code`) |
| NPC gifts and events | 3 | Gwaelin rescue, Gwaelin's Love on return, Rainbow Drop trade. The Staff of Rain is one of the 31 chests (a static chest). | RAM `$DF` bits, `$B9` event byte |
| Boss kills | 3 | Golem, Green Dragon, Dragonlord | RAM `$E4` bits 0x02, 0x40, 0x04 |
| Level ups | up to 29 | Level 2 to 30 | RAM `$C7` |
| Weapon shop slots | about 36 | `randomize_shops()`, 5 towns | `$B9` hook on purchase |
| Monsters | 40 | Kill counts at SRAM `$66C0` + 2*index | Read directly |

Item shops (Herb, Torch, Wings, Dragon's Scale, Fairy Water) and key vendors are poor locations because they can be bought repeatedly. Leave them as shops.

### Items

| Kind | Items | RAM |
|---|---|---|
| Quest | Silver Harp, Stones of Sunlight, Staff of Rain, Rainbow Drop, Erdrick's Token, Fairy Flute, Gwaelin's Love | Inventory nibbles `$C1-$C4` (ids in `notes/items.txt`) |
| Equipment | Erdrick's Sword and Armor, progressive weapon, armor, shield | `$BE` (weapon 0xE0, armor 0x1C, shield 0x03 masks) |
| Accessories | Dragon's Scale, Fighter's Ring, Death Necklace, Cursed Belt | Inventory nibbles, worn bits in `$CF` |
| Keys | Magic Key (count, or one unlock with Unbreakable Keys) | `$BF` |
| Filler | Herb, Torch, Fairy Water, Wings, gold | `$C0`, inventory, `$BC-$BD` |
| Goal | Ball of Light (Dragonlord dead) | `$E4` bit 0x04 |

### RAM and SRAM that DWR already uses

From `notes/ram.txt`: `$6800-$6815` (return/whistle, step counter, per-slot copies), `$6900-$6BFF` (custom music driver), `$6670-$675F` (monster fought/won/died stats). Serpikmin's patch uses `$7000` for the received counter and RAM `$0E`, which DWR's search code also uses as a flag. DWR's search code also uses `$0E`, so the AP block is in work RAM instead: `$6820-$682D` (see Phase 1 status).

## 4. Phased plan

| Phase | Work | Estimate | Exit check |
|---|---|---|---|
| 0 | This research | done | This file |
| 1. Proof of concept | Port `ap_changes()` into DWR behind a CLI `--ap` switch, fix hook addresses that DWR moved, keep Serpikmin's IDs. Build one ROM, run it with Serpikmin's v1.0.5 client in BizHawk against a local AP server, solo seed. | 4 to 6 h | 3 chests, 1 search spot and Gwaelin send checks; 3 received items show up |
| 2. APWorld fork | Fork the world, replace `determine_flags` with a DWR flag packer, add a `ctypes` binding and a Windows build of DWR as a shared library. | 4 to 6 h | `Generate.py` makes a 2-player seed, patching runs from the `.apdw` |
| 3. Reliable checks | Persistent chest bitfield by chest index, mailbox for received items with an in-game "received" message, IDs that don't depend on coordinates. | 1 to 2 days | Random Chest Locations works; no lost checks across map changes |
| 4. DWR flag coverage | Logic for stair shuffle, random map and biomes, or mark them off in AP. Option list on the site. | 1 to 3 days | Each enabled flag has a generated, beaten test seed |
| 5. Release | Docs, setup guide, PopTracker check, builds for Linux and macOS, credit Serpikmin. | 1 day | Public release, after the user approves |

Only Phase 1 is in scope until the user approves the rest.

First concrete step (about 30 minutes): build a DWR ROM with Serpikmin's default flags, then compare the bytes at each of the 25 or so hook addresses in Serpikmin's `ap_changes()` (`0xE226`, `0xFA18`, `0xFB6B`, `0xD627`, `0xEDB8`, `0xCCF6` and the rest) with a vanilla ROM. Any address where DWR already changed the bytes needs a new hook. Known overlaps so far: `0x196B` (DWR's `shuffle_key_prices` writes there) and the search code at `0xE10B-0xE141` (DWR rewrites it). DWR also hooks `0xCCF0` for credits, next to Serpikmin's `0xCCF6` hook.

## Phase 1 status (2026-11-15)

Correction: the first hook check read the wrong offsets. DWR's output ROM puts the 64 KB expansion before the fixed bank, so fixed-bank address `A` is at file offset `A + 0x10010`. With the right mapping, 17 of Serpikmin's 26 hook sites still hold vanilla bytes. The ones that changed are DWR's search code (`0xE11D-0xE188`), the chest TAKE code (`0xE35C`), the Rainbow Drop call (`0xD385`) and the randomized chest contents.

Built on branch `feature/archipelago`, with its own hooks on DWR's code:

| Part | Where | What it does |
|---|---|---|
| AP mode | `common/archipelago.c`, CLI `--ap-slot <name>` | Runs last in `apply_stuff_to_rom()` |
| Chests | hook at `$E221` (chest found, Y = index * 4) | Sets bit n of `$6822-$6825`, marks the chest open, shows "Thou hast found the APItem." No item or gold is given. |
| Search spots | hook at `$E12D` (search entry X matched) | Sets bit X of `$6826`, same message |
| Items received counter | `$6820-$6821`, saved per slot at `$6828 + slot * 2` | Save hook at `$FA18`, restored on continue through the `$CA1A` chain. A new game (0 XP) clears it. |
| ROM signature | PRG `$7FE0` | `DWRAP001`, then a pointer to the 16-byte slot name |
| Throne room | tile (4,7) | Door removed, since the key chest is an AP check |
| Staff shrine | `$D2FC` | The old man lets you in with the harp even if you already have the staff |
| APWorld | `archipelago/dwr/` (game "DWR") | 31 chests, 3 search spots, Gwaelin rescue, Dragonlord goal. Logic: harp for the shrine chest; Stones, Staff and Token for Charlock. |
| Client | `archipelago/dwr/client.py` | Reads the AP block, sends checks, writes received items into RAM, then raises the counter with a guarded write |

Checked so far:
- Mesen: opening Throne Room Chest 1 sets `$6822 = $10` (chest 4), gives no gold, shows the APItem message.
- `ArchipelagoGenerate` makes a solo seed (35 items).
- The client connects through FCEUX, reads the signature and logs in with the slot name from the ROM.

- FCEUX play test: the 3 throne room chests reach the server, and the Fairy Flute, Torch and 100 Gold arrive in the inventory.
- Save, reset and continue: no items given twice. The counter comes back through LoadSavedData (`$FB6B`), which only runs on a continue.

- FCEUX automated run (`tests/fceux/aptest.lua`, test ROM with `--ap-slot GobDWR`, seed 777): the Kol, Hauksness and overworld search spots send Silver Harp, Herb and Wings. Rescue Gwaelin sends 100 Gold. Setting the Dragonlord bit sends the goal ("Team #1 has completed all of their games").

Not checked yet: nothing in Phase 1's list is left unchecked. Search checks were only confirmed in the client log; the search byte at `$6826` was read before the check landed, so the log is the source.

Known limits of the proof of concept:
- The game says "APItem" for every check. Showing the real item name needs a patch file with a location-to-name table (Phase 2).
- Items are written straight into RAM, with no in-game message.
- Key doors are not in logic (keys can be bought).
- No patch file: the player builds the ROM with the DWR CLI and their slot name.

## Test notes

Use the project test rules for every AP test ROM: reduce flashing on, default music, max stats and a permanent repel. AP play testing uses FCEUX with the user's FCEUX connector (`connector_fceux_generic.lua`) and the AP BizHawk client. Mesen `--testrunner` scripts are still fine for checking the ROM-side hooks.
