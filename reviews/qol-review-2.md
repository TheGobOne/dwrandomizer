# QoL review 2: Biomes, the planned safe start, and what is still open

Reviewed 2026-11-06 on branch feature/custom-music, including the uncommitted working tree (`common/map.c`, `common/dwr.c`, `common/dwr.h`, `common/custom_music.c`, `site/index.html`).
I read the code only. I did not build it or run any seeds, so anything marked "check" needs a test.
Comparisons to other randomizers are from memory and should be spot-checked before quoting them.

Difficulty scale: Easy, Medium, Hard, Very hard.

## What changed since `reviews/qol-review.md`

Done since the first review (now in the working tree):

- **Biome setting** (Continent, Caldera, Wasteland, Riverlands, Random) in `common/map.c` (`biome_terrain` and helpers) and `BIOME()` in `common/dwr.h`. It uses two "maybe" pairs in flag byte 2 (bits 0x01/0x02 and 0x10/0x20), so `Random` is two maybe bits and picks all four biomes evenly.
- **Out-of-space message on the site** (`spaceMessage()` in `site/index.html`) now names flags to turn off. This was a "Medium" item in review 1.
- **Retry with tighter packing** when the ROM is full (best fit, in `dwr_randomize` and `find_free_space_in`). See finding 2: it has a fairness side effect.
- **Free-space waste fixed.** Several features (`radish_finish`, `return_escapes`, `hurtmore_doors`, `levelup_refill`, `unbreakable_keys`, `npc_guillotine`, `zoom_and_whistle`) used to reserve space before checking whether their flag was on. They now check first, and Hurtmore Doors shrank from 173 to 132 bytes. This helps the Bank 3 squeeze.
- **Site items from review 1 now done:** music pack picks per scene, Copy button feedback, ROM header check, a "changed from normal" count (`isChanged`), and the Key and Herb counter (`keyherb`, flag byte 8 bit 4).
- **Credits tag fix:** the stub in `custom_music.c` keeps the driver silent until the credits ask for their song.

Still open from review 1, unchanged:

- `DEFAULT_FLAGS` in `common/dwr.h:17` (`...ACAAAA...`) still differs from `webui/index.js:121` (`...ACBAAA...`).
- `find_free_space` still takes `uint8_t n`.
- Battle flash, level-up flash, death fade and Noir transitions still have no disable option.
- Auto-equip best gear is still parked.

New gaps from the Biome work: `README.md` has no Biome section, and `webui/index.js` (the older UI) has no Biome control. The site has it. The `--spoiler` log and the CLI options table in the README do not mention it either.

## Ranked suggestions

### 1. Make the checksum independent of the music pack (fairness bug)

- **Player problem:** two racers use the same seed and flags. One adds a large music pack and gets a different checksum, so the race looks mismatched.
- **Cause:** in `dwr_randomize` the retry order is: music-only tight packing first, then, if that still fails, `fit_game = TRUE`. Packing the game code tightly changes the code bytes, and the checksum is taken over them (`crc64` over the first 0x10000 bytes). So a big pack can silently change the checksum.
- **Proposal:** when only the music stage fails, never retry with tight game packing. Drop the pack's songs, keep the original music and say so (the site already has this message for dropped picks). Keep tight game packing only for a failure before the checksum, where flags alone decide it.
- **Setting:** no. **Default:** n/a.
- **Racing and fairness:** this is the point. The same seed and flags must give one checksum, whatever music is attached. The same flags always take the same path, so it is deterministic today. It just depends on the pack.
- **Difficulty:** Easy.
- **Space limit:** none, it removes work.
- **Check:** also confirm that a build which needed best fit before the checksum gets the same checksum on CLI and site. It should, but test one flag set near the limit (Build Options, Hurtmore Doors, Warp Whistle, Run Bank together).

### 2. Tantegel safe zone (the planned flag)

- **Player problem:** swamp deals damage on every step. Wasteland puts swamp in 3 of 16 fill tiles, and Big Swamp turns most grass into swamp. A hero at level 1 can start next to swamp, or on swamp, which also makes the Tantegel inside border swamp (`place_tantegel` returns `border_for(old_tile)`). The first trip to Brecconary can cost the run before it starts.
- **Proposal, in two steps:**
  - **Step 1 (Medium):** after Tantegel is placed, turn swamp within a radius of about 4 into grass. Skip tiles that are caves, towns or the castle, skip Charlock's own 3x3 swamp ring, and skip water. Also fix Tantegel's own border tile. Swamp is walkable, so this does not change land mass sizes or the reachability checks.
  - **Step 2 (Hard):** guarantee a swamp-free path from Tantegel to Brecconary. Brecconary is placed after Tantegel in `place_landmarks`, so this has to run at the end. Use a breadth-first search over walkable non-swamp tiles, and if none exists, carve grass along the shortest walkable path. Do step 1 first.
- **Setting:** yes, one flag, "Safe Start". **Default:** off for Continent, so old seeds keep the same map. Consider making it automatic (no flag bit) whenever Biome is not Continent, since no old seeds use biomes. That saves a flag bit and a UI row. If it is a flag, show it next to Biome and Big Swamp.
- **Racing and fairness:** it is part of map generation from the seed, so every racer gets the same map. It must be in the checksum and the summary. It makes Big Swamp and Wasteland races less luck-dependent at the start, but it does not make them easy.
- **Difficulty:** Medium for step 1, Hard for step 2.
- **Space limit:** it is generator code on the PC side, so no ROM space. It does need one spare flag bit if it is a flag. Bits that look free in `FLAG_MAP` (site) are byte 3 bits 0-1, byte 8 bit 5, byte 11 bit 1, byte 14 bit 0 and byte 22 bit 4. Check against `dwr.h` before choosing, and pick one in the same area as the other map options.
- **Comparison:** the Zelda 1 and Final Fantasy randomizers also try to keep the opening area safe, by guaranteeing a usable first item or a reachable first town. Verify before quoting.

### 3. Caldera, Wasteland and Riverlands: guard rails

- **Player problem:** a few cases could give a bad seed or a hang.
  - **Caldera:** Tantegel and the towns are placed on any land mass tile, so Tantegel can land inside the basin next to Charlock. That removes the whole journey.
  - **Tiny maps:** `retry_map` has no retry cap. On Very Small (size 60) the mountain ring and coast cut leave little walkable land against the `size * 42` minimum, so Caldera or Riverlands may retry for a very long time. I could not run this, so check it by generating many Very Small seeds on each biome.
  - **No Random Map:** Biome is silently ignored when Random Map is off.
- **Proposal:**
  - Make Caldera keep Tantegel, and ideally all towns, outside the ring (distance check against `r_out`). Charlock stays in the middle.
  - Add a retry cap (for example 300 tries) that ends with a clear error or falls back to a size that works.
  - On the site, grey out Biome when Random Map is off. In the CLI, print a warning.
- **Setting:** no. **Default:** n/a.
- **Racing and fairness:** the generator uses `mt_rand` only with whole numbers (`biome_isqrt`), so CLI and site agree. A cap must be deterministic, so base it on the retry count, not time.
- **Difficulty:** Easy to Medium.
- **Space limit:** none.

### 4. Encounter pairing for Wasteland and Caldera

- **Player problem:** Wasteland is mostly desert, and a Caldera basin is filled with desert. In the Encounter Rate table, hills and desert are the fastest tier, and `Default` keeps the original rolls. A Wasteland run on Default encounters can be exhausting.
- **Proposal:** show a hint on the site when Biome is Wasteland or Caldera: "Pair with Encounter Rate Low or Medium." Do not force it. Optionally add the biome to the README next to the Encounter table.
- **Setting:** no, advice only. **Default:** unchanged.
- **Racing and fairness:** none, text only.
- **Difficulty:** Easy.
- **Space limit:** none.

### 5. Document Biome everywhere it is missing

- **Player problem:** the README, the older `webui/index.js` UI, the spoiler log and the CLI table do not mention Biome, so players of other front ends cannot find it.
- **Proposal:**
  - README section with one paragraph per biome.
  - A `--biome` CLI option (`continent`, `caldera`, `wasteland`, `riverlands`, `random`), in the same style as `--encounters`.
  - Biome name in the spoiler log and in the site summary.
  - Add the control to `webui/index.js`, or note that biomes are only on the site.
- **Setting:** n/a. **Racing and fairness:** the biome is already in the flag string, so it is in the checksum. Show it in the summary so racers can check.
- **Difficulty:** Easy.
- **Space limit:** none. The older UI has limited tab space (see below).

### 6. Map preview on the site

- **Player problem:** with biomes, players want to see what they got, and racers want to confirm everyone has the same map.
- **Proposal:** after generation, draw the overworld as a small image next to the checksum (one pixel per tile, a few colors). Expose the map from wasm as a byte array. Optionally let the spoiler log carry a text map.
- **Setting:** yes, off by default and hidden in a "Race" or sealed mode. **Racing and fairness:** a preview is a spoiler of the whole map. It needs to be off for races, or only appear after the ROM is downloaded for solo play.
- **Difficulty:** Medium.
- **Space limit:** none, it is site-only.
- **Comparison:** Zelda 1 Randomizer and Final Fantasy Randomizer sites show seed and flag details but hide spoilers in race mode. Verify before quoting.

### 7. Race mode and run receipt (parked in review 1)

- **Player problem:** racers need proof they ran the same seed and flags, and no spoilers.
- **Proposal:** a "Race" toggle on the site that hides the spoiler log and map preview, and prints a short receipt: version, flags, seed, checksum, Bank cap and earn level, Biome, Safe Start.
- **Setting:** yes. **Default:** off. **Racing and fairness:** that is the purpose. Music pack must not change the checksum (finding 1).
- **Difficulty:** Easy to Medium.
- **Space limit:** none, site-only.

### 8. Connected site features still not done

These are unchanged from review 1 unless noted. All are site-only, so they do not use ROM space.

| Item | Difficulty |
|---|---|
| Live ROM space meter (the `SPACE_FLAGS` list in the site is a static guess; real sizes are in `find_free_space` calls) | Hard |
| Copy link with flags and seed | Easy to Medium |
| "Diff against preset" view | Easy to Medium |
| Music pack report: songs used, bytes per bank, songs dropped and why | Medium |
| Master "Reduce flashing" toggle | Easy |
| Seed history and last used seed in localStorage | Easy |

- **Presets:** freeze them per version, because flags are bit positions. Biome and Safe Start now take bits, so a preset made before this build must keep Biome at Continent.

### 9. Remaining effects with no disable option

- **Player problem:** battle-start flash, level-up flash, death fade and Noir transitions still flash with no option. The Command Window site has a "Comfort" group that could hold them.
- **Proposal:** one "Reduce all flashing" option that covers these, and keep the three separate ones.
- **Setting:** yes. **Default:** off. **Racing and fairness:** cosmetic, so it must stay out of the checksum.
- **Difficulty:** Medium. **Space limit:** Bank 3 space is tight, so look for a PPU palette write that can be skipped cheaply instead of new code.

### 10. Auto-equip best gear on purchase (parked)

Hard. It needs a lot of Bank 3 space. Look at the expansion banks first, as in review 1.

## Code and docs notes

- **Default flags mismatch:** still present, see above. Decide which is intended.
- **`find_free_space` takes `uint8_t n`:** safe today (largest block is 178 bytes), but a block over 255 would wrap. Change it to `uint16_t`.
- **`static volatile` flags in `dwr.c`:** fine for the wasm `longjmp` case. Keep the comment.
- **Best-fit allocator:** `fit_best` only runs after a failure, so existing seeds keep the same bytes. Good. Keep it that way.
- **Biome and Random Map:** `BIOME()` is read only when `RANDOM_MAP` is on. If Random Map is a "maybe" value that resolves to off, the Biome is ignored too. State this in the README.

## Limits to keep in mind

- **Bank 3:** free space is mostly used. The savings above helped, but any new in-game feature competes with Warp Whistle, Run Bank and Quick Spell.
- **Battle command window:** already one block taller, so no more rows.
- **Flag bits:** a new flag needs a spare bit. A new byte changes the flag string length and breaks old seeds. Biome already used two pairs in byte 2, which is now full (0xc0, 0x30, 0x0c, 0x03).
- **Expansion banks:** put text-heavy or data-heavy features there first.
- **Map generation features** (Safe Start, biome guard rails, previews) cost no ROM space, so prefer them while Bank 3 is full.

## Top picks

1. Fix the checksum depending on pack size (Easy, fairness).
2. Build Safe Start, radius first (Medium), then a path to Brecconary (Hard).
3. Add Biome guard rails: Tantegel outside the Caldera ring, a retry cap, Biome greyed out without Random Map (Easy to Medium).
4. Document Biome in the README, CLI and spoiler log, with an Encounter Rate hint (Easy).
5. Race mode with a receipt, then map preview off in race mode (Easy to Medium).
