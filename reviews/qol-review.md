# QoL review: what is still open

Updated 2026-11-05, after commit 48fae11 and the Command Window mockup choice.
Comparisons to other randomizers are from memory and should be spot-checked before quoting them.

Difficulty scale: Easy, Medium, Hard, Very hard.

## Done

Encounter Rate (danger meter), Quick Spell, bank progress dots and the warp save are in commit 05d102a.

## Parked or dropped

- **Parked:** spoiler log on the website (the `--spoiler` CLI option exists). Race mode and the run receipt.
- **Dropped:** coordinates on the status screen, hint NPC.
- **Not built:** bank stats in the credits, runs banked/used in the stat scroll, a "repeat last command" button.

## Game ideas still open

- **Auto-equip best gear on purchase.** Hard, needs a lot of bank 3 space.
- **Visible key and herb counts on the status window.** Hard, for the same reason.

## Website work

The Command Window mockup already has Run Bank earn level and cap controls, a music pack picker, presets, and grouped comfort options. None of it is connected to the randomizer yet.

| Item | Difficulty |
|---|---|
| Connect the mockup controls, including Encounter Rate, Quick Spell and music pack upload to the wasm file system | Easy for flag controls, Medium for upload |
| Show the out-of-space error, naming flags to turn off | Medium |
| Live ROM space meter, adding up byte costs per flag | Hard |
| Copy link with flags and seed | Easy to Medium |
| "Diff against preset" view | Easy to Medium |
| Music pack report: songs used, bytes per bank, songs dropped and why | Medium |
| Mute-per-song pack preview | Medium |
| Master "Reduce flashing" toggle | Easy |
| Seed history and last used seed in localStorage | Easy |

- **Fairness:** a music pack must not change the checksum. Bank cap and earn level change gameplay, so they must stay in the checksum and show in the summary.
- **Presets:** freeze them per version, because flags are bit positions.

## Code and docs

- **Default flags:** `webui/index.js:121` (`...ACBAAA...`) differs from `DEFAULT_FLAGS` in `common/dwr.h:17` (`...ACAAAA...`). Check which is intended.
- **`find_free_space`:** it takes `uint8_t n`, so a block over 255 bytes would wrap.
- **New battle effects:** battle-start flash, level-up flash, death fade and Noir transitions have no disable option. Medium.

## Limits to keep in mind

- **Bank 3:** free space is mostly used, so the items above compete for it.
- **Battle command window:** it is already one block taller, and another row would push into the scene area.
- **Flag bits:** a new flag needs a spare bit. A new byte changes the flag string length and breaks old seeds.
- **Expansion banks:** these have room for text-heavy or data-heavy features, so put new features there first.
