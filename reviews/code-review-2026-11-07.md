# DWR code review, 2026-11-07

Branch `feature/custom-music`. HEAD was `ce3839f` when I finished. I read the uncommitted bank 5 work as it stood (it is still changing). Items already found and fixed in reviews 1 to 5 are not repeated.

**Next action (5 minutes):** fix finding 1. Make `custom_music_has_credits` follow the same rule the credits pick uses (see the change below).

## How to read this

- **Confirmed:** I traced it in the code, or measured it in a ROM in `test-roms/`.
- **Suspected:** I read the logic, but the effect needs play I could not run.
- **Unverified:** it depends on vanilla ROM bytes or emulator behavior I could not check. There is no vanilla ROM in the repo or nearby folders.
- **Effort:** Easy is minutes. Medium is an hour or two plus testing. Hard is a day. Very hard is a rewrite or deep testing.
- **Moving targets:** `common/custom_music.c`, `common/ft_driver.h`, `tools/custom-music/gen_driver.py`, `cli/main.c` and `site/index.html` changed while I read them. Line numbers for those files are from the copy I saw, with `custom_music.c` at 1,428 lines.
- No code was changed, built or run, and no emulator was used. I only read generated ROMs in `test-roms/` to measure free space.

## Top 5 by impact

| # | Finding | Type | Checked | Effort |
|---|---------|------|---------|--------|
| 1 | The stat scroll plays from an empty song when the pack has credits tags but no credits song gets placed | Bug | Confirmed in code. What it sounds like is unverified | Easy |
| 2 | The bank 3 patch always puts about 179 bytes of flag-only routines in the fixed bank (74 of them are dead with the default flags) | Space | Confirmed | Medium |
| 3 | Run Bank alone carries Quick Spell's 99 fixed-bank bytes. Quick Spell alone carries Run Bank's 30 fixed-bank bytes and 43 bank 1 bytes | Space | Confirmed | Easy |
| 4 | The music request hook's per-map remap (52 fixed-bank bytes) can move into the wrapper in the music banks | Space | Confirmed size. Timing suspected safe | Medium |
| 5 | Return to Town and Warp Whistle: a 181-byte request when only 121 are needed, plus table-driven code that saves 30 to 46 bytes | Space and false "out of room" | Confirmed | Easy to Medium |

How tight the fixed bank is: in the four newest test ROMs, the fixed bank had 92, 97, 193 and 206 bytes left in runs of 6 or more (`DWR_wait_loop_test_*.nes`, $C422-$FFF0). Bank 1 had 12 to 46 bytes left. Every fixed-bank byte below counts.

---

## 1. Correctness bugs

### 1.1 Stat scroll plays from an empty song (Top 1)

- **Where:** `common/custom_music.c:1416` (`custom_music_has_credits`), `:1148-1160` (the credits pick), `:1386-1389` (no room for the stub). `common/dwr.c:2820-2823` (`setup_expansion`).
- **Problem:** `setup_expansion` skips the built-in credits song when `custom_music_has_credits()` is TRUE. That function says yes to any song tagged `StatScroll` or `OriginalCredits`. The credits pick in `custom_music_apply` is stricter. It takes a `StatScroll` song first. It takes an `OriginalCredits` song only when credits are not skipped and the pack has at least 2 of them (`count_tagged(events[1].tags) >= 2`). When no song is picked, `event_song[0]` stays -1 and the credits code is not patched. FamiStudio in bank 4 then calls `famistudio_init` on `$8000`, which is all `$FF` because `add_music` never ran.
- **Ways to reach it (all confirmed in code):**
  1. Site: a song hand-picked for Original Credits, Stat Scroll left on Default, Skip Original Credits off. The site sends one copy tagged `OriginalCredits`. The C code finds no `StatScroll` song, and the count is 1, not 2. The ending gets the song and the stat scroll gets nothing.
  2. CLI `--music` with Skip Original Credits on, and a pack whose only credits songs are tagged `OriginalCredits`. `has_credits` runs in `setup_expansion`, before `pick_like_site` replaces `songs[]`, so it still sees the full pack.
  3. CLI `--music-exact` with one `OriginalCredits` song.
  4. Any build that hits "No room for the credits song" while packing (`:1386-1389`). The message says "keeping the original credits music", but none was added.
- **What the player hears:** FamiStudio reads `$FF` headers and pointers. It is probably junk or silence. A crash is unverified.
- **Change:** have `custom_music_has_credits` follow the same rule as the pick:

```c
BOOL custom_music_has_credits(BOOL skip_credits)
{
    static const char *const stat[4] = {"StatScroll"}, *const orig[4] = {"OriginalCredits"};
    /* Same rule as the credits pick in custom_music_apply. */
    return count_tagged(stat) > 0 || (!skip_credits && count_tagged(orig) >= 2);
}
```

  For the CLI, also run the site-style pick before `setup_expansion`, or check `pack[]` with the stat-only rule when credits are skipped. For the no-room path, change the message to "the credits play no music". Or better, call `add_music` there when bank 5 holds no songs.
- **Benefit:** the stat scroll always has a real song.
- **Risk:** a stricter test adds the built-in credits song in more builds. The new bank 5 song room only exists when the built-in song is left out (`custom_music.c:1132-1141`), so those builds lose bank 5. That is correct, but the site's space estimate must follow the same rule.
- **Effort:** Easy (the rule). Medium (reordering for the CLI).

### 1.2 Fairy Flute sale is always on, but the site ties it to DK Flute

- **Where:** `common/dwr.c:2986-2989` (`sellable_flute`), called with no flag check at `:4860`. On the site, the `discardflute` text in `TABS` (`site/index.html:377`) reads "...or sell to a shop for 1g".
- **Problem:** every ROM gets a 2-gold flute price, so every ROM can sell the flute. The site says only DK Flute does this. With DK Flute off, a player can still sell the flute, which may be needed for the Golem. The patch runs before the checksum, so this is a gameplay change on every seed.
- **Change:** if the sale belongs to DK Flute, wrap it: `if (DISCARDABLE_FLUTE(rom)) sellable_flute(rom);`. If it is meant for every seed, change the site text and the DK Flute description.
- **Risk:** none either way. Pick one.
- **Effort:** Easy. **Confidence:** Confirmed mismatch. Whether it was intended is for you to decide.

### 1.3 Site song room is 4 bytes too big for the in-progress driver

- **Where:** `site/index.html:561` (`BANK_ROOM=8770`), `common/ft_driver.h:10` (`FT_SONGS 0x9dc2` in the working copy, so room is 0xc000-0x9dc2 = 8,766). At HEAD, `FT_SONGS` was 0x9dbe, which is 8,770.
- **Problem:** the bank 5 work grew the wrapper by 4 bytes. The site can now accept a pick of 8,767 to 8,770 bytes, or a bank total that close to full, that the C code then rejects (`read_item` at `custom_music.c:403`, `pick_fits` at `:818-823`). The site's `fits` (`index.html:604-607`) and the C `pick_fits` (`custom_music.c:820`) both still model 2 banks. Bank 5 room also depends on where the credits stub lands (`music_bank_end[2]`) and on whether the built-in credits song is added (see 1.1).
- **Change:** have `gen_driver.py` also write a small `site/ft_room.js` (or a comment the site reads) holding the room. Model bank 5 in both pickers with the same condition.
- **Effort:** Easy (constant). Medium (3-bank model). **Confidence:** Confirmed for the constant. The bank 5 parity is in progress.

### 1.4 `custom_music_clear` after a site-style pick: double free and stale pack (latent)

- **Where:** `common/custom_music.c:484-490`, `:916-925`, `:979-985`.
- **Problem:** `pick_like_site` copies `songs[]` into `pack[]`, then rebuilds `songs[]` as per-slot copies that share `data` pointers. Two slots can share one song. `custom_music_clear` then frees the same pointer twice and never frees `pack[]` or resets `pack_count`. A later load would reuse the old pack.
- **Reach:** none today. The CLI exits after one build, and the site never calls `custom_music_choose_like_site`. It becomes live if the site, or a test loop, ever uses the site-style pick.
- **Change:** in `custom_music_clear`, free `pack[i].data` for `i < pack_count`, set `pack_count = 0`, and only reset `song_count` for the copies.
- **Effort:** Easy. **Confidence:** Confirmed latent.

### 1.5 CLI random seed has about 30 bits on Windows

- **Where:** `cli/main.c` (seed when none is given): `seed = ((uint64_t)rand() << 32) | rand()` after `srand(time(NULL))`.
- **Problem:** `RAND_MAX` is 32767 with MSVC and MinGW, so the seed has 15+15 bits and two runs in the same second get the same seed.
- **Change:** build it from `mt_init(time(NULL) ^ clock())` and `mt_rand`, or from four `rand()` calls of 16 bits each.
- **Effort:** Easy. **Confidence:** Confirmed by reading. Only affects CLI users who leave out the seed.

### Checked and found correct (so nobody re-checks)

- **Flag bits:** all 25 bytes in `dwr.h` have no overlaps. The site's `FLAG_MAP` (`index.html:417`) matches every macro: masks, shifts, value tables for Run Mechanics, Bonk Damage, Crit Chance, Gold Chest and Build Options, and the inverted Fast Flute bit. Hints exposes 1 of its 2 bits, but C only implements values 0 and 1 (`map.c:1362-1369`), so nothing is lost.
- **Branch offsets and sizes,** counted byte by byte: DW4 run (35), Costs XP (44+17), Safer DW1, Return Escapes (22), Return to Town and Warp Whistle (25/72/12/60), `warp_list_load` (38), `danger_meter` (67), Free Charlock (57 and 18), Responsive Controls (109/59/8), Run Bank (43, 30, 49+50, 45, 41, 31, 63, 32, 11), build stats (127, 25), and the music hooks (RQ 90, frame 29, NMI side 26, overrun 14). All branch targets land where the comments say.
- **Costs XP divide by 8** (`dwr.c:463-472`): the third ROR skips the high byte. That is safe because the highest enemy XP is below 1,024: remake max 350, normalized max 400, chaos polynomial max 371 at x=40, which I computed.
- **`BANK_SELECT`** (`custom_music.c`, near the `music_wait_loop` comment): `$00` goes to 6, `$40` to 3, `$80` to 5. The two BIT skips read `$05A9` and `$06A9`, which are RAM with no side effects. Garbage in battery RAM still picks a music bank, which is why the decode exists.
- **Sprite tables:** the function list order matches `dwr_sprite_names` (`sprites.c:13-110`).
- **Treasure guards:** they only look at search spots 0 and 1. That is correct, because spot 2 (Hauksness) already has the Axe Knight in spike entry 0.

---

## 2. Space

### 2.1 Bank 3 patch: flag-only routines always in the fixed bank (Top 2)

- **Where:** `common/expansion.c:27-64` (`bank_3_patch`, 403 bytes at $C288, written by `setup_expansion` at `dwr.c:2816` for every ROM). Source: `expansion/bank_3_patch.asm`. Addresses: `common/expansion.h`.
- **Problem:** these routines are only called when their flag is on (all `add_hook` calls checked):

| Routine | Bytes | Used by |
|---|---|---|
| `torch_in_battle` $C331 | 72 | Torches in Battle |
| `threes_company_check` $C379 | 18 | Three's Company |
| `store_princess_item` + `cursed_princess_check` $C38B | 26 | Cursed Princess |
| `scared_slimes` $C3A5 | 13 | Scared Metal Slimes |
| `display_deaths` $C3BC | 33 | Death Counter |
| `modify_run_rate` $C40A | 17 | Easy Charlock |
| **Total** | **179** | |

  With the default flags (Torches and Death Counter on), 74 bytes are dead.
- **Change:** take these six out of the fixed block. Give each one its own byte array in `build_expansion.py` output, placed with `find_free_space` only when its flag is on. Their only absolute targets are vanilla addresses, and their branches are relative, so they can be moved. Then `add_hook` gets the returned address instead of the enum. Move the free-space start down to the new end of the block.
- **Benefit:** 74 bytes with default flags. Up to 179 bytes.
- **Risk:** these routines would then be placed before the checksum and use first-fit space. That changes the layout of every later piece of code, so the same flags give a different ROM than the last release. Checksums change anyway with the version.
- **Effort:** Medium. The generator must emit them separately.

### 2.2 Run Bank and Quick Spell carry each other's code (Top 3)

- **Where:** `common/dwr.c:4245-4266` (window code $F0 with dots: 43 bytes in bank 1 and 30 in the fixed bank) and `:4268-4285` (window code $F8 with the name table: 99 fixed-bank bytes).
- **Problem:** `$F0` (bank count) only appears in the `bank_only` and `both` window layouts. `$F8` (last spell) only appears in `quick_only` and `both` (`:4225-4236`). Both pieces are written whenever either flag is on.
- **Change:** wrap the $F0 block in `if (RUN_BANK(rom))` and the $F8 block in `if (QUICK_SPELL(rom))`. The pointers at `0x66df`/`0x66e1` can stay vanilla, because the unused code never appears in the window data.
- **Benefit:** Run Bank alone: 99 fixed-bank bytes. Quick Spell alone: 30 fixed-bank bytes and 43 bank 1 bytes. Bank 1 has 12 to 46 bytes left.
- **Risk:** low. Later code lands at different addresses.
- **Effort:** Easy. **Confidence:** Confirmed.

### 2.3 Move the music request remap into the music banks (Top 4)

- **Where:** `common/custom_music.c:1309-1321`, the second half of the 90-byte RQ hook. It starts at `cmp #$0e : bcs` and covers the map checks for Hauksness, the basement and shrines, and the caves.
- **Problem:** 52 of the 90 bytes only rewrite `RAM_WANT` from `$45` (map) and the request. The wrapper already runs every frame in the music bank, where there is room. It sees `WANT != CUR` on the next frame and can do the same rewrite first.
- **Change:** keep only the store and silence part (38 bytes) in the fixed bank. Add the remap at the `ok:` label in `gen_driver.py`'s wrapper, before `cmp CUR`. It needs a "raw request" byte, so the remap runs once per request and not every frame.
- **Benefit:** about 52 fixed-bank bytes. The cost is about 55 bytes of song room in each of the 3 music banks.
- **Risk:** the decision moves one frame later. It is correct if `$45` already holds the new map when the request is made. That is suspected, not proven. Test with stairs, old man caves, Hauksness, and a cave reached from a town.
- **Effort:** Medium.

### 2.4 Return to Town and Warp Whistle: request size and table-driven code (Top 5)

- **Where:** `common/dwr.c:3452` (`find_free_space(..., 0xc82b, 181)`), inn code `:3520-3562` (72 bytes), flute code `:3587-3626` (60 bytes).
- **Problem 1:** with only Return to Town on, the code uses 121 bytes but asks for a 181-byte hole. That can report "out of room" or force best fit when 121 would fit.
  - **Change:** request `WARP_WHISTLE(rom) ? 181 : 121`.
  - **Effort:** Easy.
- **Problem 2:** the inn code is five 11-byte compare blocks, and the flute code shifts a bit by hand.
  - **Change:** use one 6-byte table, `bits: 01 02 04 08 10 20`.
  - Flute loop: `iny : cpy #6 : bcc + : ldy #0 : + lda bits,y : and ram_v : beq loop : sty ram_j`. That is 18 bytes instead of 31, saving 13.
  - Inn: if the order in `zoom_data` swaps Kol and Brecconary, then index = map - 6 for maps 7 to 11. The code becomes `lda $45 : sec : sbc #6 : beq done : cmp #6 : bcs done : sta ram_i : tax : lda bits,x : ora ram_v : sta ram_v`, then the existing tail. That is 33 bytes instead of 72, saving 39.
  - **Benefit:** 46 bytes if the order changes, or about 30 bytes with a 5-byte map table and no reordering.
  - **Risk:** the reorder changes the whistle cycle order and the meaning of a saved slot's visited bits. Old saves from this version would point to the wrong town once. Keep the order, and the 5-byte map table, if that matters.
  - **Effort:** Medium.

### 2.5 Smaller savings

| Where | Change | Bytes | Risk | Effort |
|---|---|---|---|---|
| `dwr.c:4492-4518` `new_flags_ram_init` | Emit the ram_i/ram_j/ram_v stores (16 bytes) only when Return to Town or Warp Whistle is on | 16 fixed bank (common flags) | Low | Easy |
| Bank 3 patch end $C41B vs every `find_free_space(..., 0xc422, ...)` | Use a `FIXED_FREE_START` define that `build_expansion.py` sets to the patch end | 7 fixed bank | Low. Layout shifts | Easy |
| `bank_3_patch.asm` `threes_company_check` | Both branches print `$A1`: `jsr $c7cb : .db $a1 : lda $df : lsr : bcc + : jmp $ccb8 : + rts` | 5 (only helps once 2.1 is done) | Low | Easy |
| `dwr.c:2724` `begin_quest_checksum` | Asks for 40, writes 39 | 1 | None | Easy |
| `dwr.c:3641` `hurtmore_doors` | Jump into the vanilla key routine instead of copying its 64-byte tail | About 59 (review 2 #14, declined in review 3) | As before | Medium |

### 2.6 Speed

Nothing found is worth changing. The per-frame costs I counted:

| Code | Cost |
|---|---|
| Wait-loop tick (register and bank save) | About 70 cycles |
| Responsive Controls controller read, per step wait | About 170 cycles |
| Spike search per step | Under 200 cycles |
| Danger meter, worst case per fight | About 50 cycles |

The ZP swap was already cut down (`gen_driver.py`, `ZP-SWAP`).

---

## 3. Simplification and dead code

| Where | What | Effort |
|---|---|---|
| `dwr.c:110-114` | Biome "Random" check is dead. The maybe loop just above (`:84-87`, `i < 12` includes byte 2) already clears `$22` with `&= 0x55`, and turns the two maybe bits into two coin flips. The result is the same uniform pick of 4 biomes, so delete the block and fix the comment. Confirmed. | Easy |
| `dwr.c:2682` in `winter_theme` | The 621-byte write at `$C288` is wiped by `memset(0xc288..0xc529)` at `:4807`, which runs after it. Also overwritten later: `$E1DC` (`:2686`, by `:2002`), `$F0FC` (`:2694`, by the Fighter's Ring), `$FF54`/`$FF7D` (`:2698-2699`), and the enemy XP/gold bytes (`:2640-2676`, by `update_drops`). It only costs source size, not ROM. | Easy |
| `dwr.c:149-184`, `:382-406` | Commented-out printf block and `dwr_str_replace` | Easy |
| `dwr.c:5291-5321` | `check_free_space` and `print_rom_data` are never called, and their format strings are broken (`"%04x" PRIx16` prints `%04xx`) | Easy |
| `dwr.c:2326` vs `:2343` | Treasure Guards tests "maybe" two ways (`& 0xAA` and `== 2`). Pick one. | Easy |
| `expansion/exp_bank_3.asm:109-141` | Commented-out `exp_torch_in_battle` copy | Easy |
| `notes/ram.txt` | New RAM is not listed: $6800-$6815 (flags, per-slot warp lists), $6900-$6AFF (driver BSS), $6B00-$6B4D (ZP swap), $6BF0-$6BFE (music state), $6F00, $7004, $7F01-$7F02. I found no collisions among these, but vanilla use of $6900-$6BFF is unverified. | Easy |
| `flute_music.c` "Chip N' Dale" | Four `fb fb` volume bytes where every other song uses `fb ff`. Probably a typo (slightly quieter notes). Unverified. | Easy |
| `custom_music.c:702` and `site/index.html:561` | `PICK_ROOM` and `BANK_ROOM` are the same number in two places. Generate the site value (see 1.3). | Easy |

---

## 4. All suggestions, ranked

| # | Item | Type | Bytes | Effort | Reason for the rating |
|---|---|---|---|---|---|
| 1 | 1.1 credits rule | Bug | none | Easy | One function, plus the reorder for the CLI |
| 2 | 2.1 bank 3 patch split | Space | 74 to 179 fixed bank | Medium | Generator and hook changes |
| 3 | 2.2 Run Bank / Quick Spell guards | Space | 99, or 30+43 | Easy | Two `if`s |
| 4 | 2.3 remap into wrapper | Space | 52 fixed bank | Medium | Asm in two places, needs play tests |
| 5 | 2.4 Return/Whistle | Space, bug | 60 (request), 30 to 46 | Easy to Medium | Request is one line. Tables need tests |
| 6 | 1.2 flute sale | Bug or design | none | Easy | One `if` or one text change |
| 7 | 1.3 site room / 3 banks | Parity | none | Easy to Medium | Constant now, model later |
| 8 | 2.5 small savings | Space | 1 to 16 each | Easy | Local edits |
| 9 | 1.4 clear after pick | Latent bug | none | Easy | A few lines |
| 10 | 1.5 CLI seed | Bug, low | none | Easy | A few lines |
| 11 | Section 3 | Clarity | none | Easy | Deletions and notes |

---

## What I could not review

- **Vanilla bytes:** there is no vanilla ROM here, so every claim that a hook replaces specific vanilla instructions is unverified. Examples: `$CA56`, `$CDFD`, `$AB64` (does the non-battle path of the selection hook still need the 3 bytes it replaced?), `$EE94` region size, `$6039` as the save slot, and the `no_builds` window size at `$7194`.
- **Play:** no emulator was run. Timing claims (2.3, the wait-loop race, the NMI during the wrapper) are by reading.
- **Third-party code:** the Dn-FamiTracker driver (`tools/custom-music/driver/*.s`) and `famistudio_asm6.asm` were only skimmed. I reviewed how they are hooked in, not their insides.
- **Data files:** the song bytes in `credit_music.c`, the sprite data in `sprites.c`, and the map and dungeon tables were not checked byte by byte. I only spot-checked `map.c`, `stair_shuffle.c`, `chests.c`, `dungeon_maps.c` and `chaos.c` for bounds.
- **Not reviewed:** `electron/` and `webui/` (the old UI).
- **Bank 5 work:** still changing. I checked its design (credits stub above `FT_SONGS+$4000`, bank 5 ending below the stub and the vectors) but not its final form.
