# Code review 2: Dragon Warrior Randomizer, branch feature/custom-music

Reviewer: Code (6502/NES). Nothing in the repository was changed, committed or pushed, apart from adding this file.

Tree: HEAD 83bfe4a plus the uncommitted working tree.
Line numbers match this working tree, which has these SHA-1 prefixes:

| File | SHA-1 prefix |
|------|--------------|
| `common/dwr.c` | `ab194b10` |
| `common/map.c` | `fcdfc047` |
| `common/dwr.h` | `e1173376` |
| `common/custom_music.c` | `3ea769b9` |

The tree changed twice while I worked (09:37 and 09:46). The changes were the spoiler biome line, the Tantegel caldera check and Safe Start.
Tests ran on a snapshot from after the 09:37 change. I read the Safe Start code, but I did not test it.

Ratings are effort: Easy (minutes), Medium (an hour or two plus testing), Hard (a day, design change), Very hard (rewrite or deep emulator testing).

**How each finding was checked:**

- **Confirmed:** I saw it happen in a build or a trace.
- **By reading:** from source only.

**How I tested:**

- I compiled everything with gcc 16 into the scratchpad.
- There is no vanilla ROM here, so I made a stand-in from a built test ROM. I restored the free space and the removed 136th NPC.
- Native builds and the site's WebAssembly ran under node.
- No emulator was run, so nothing was tested as gameplay. The project's reduce-flashing rule did not come into play.
- Tests used default music, except the music-pack tests in finding 2.

## Ranked list

| # | Finding | Type | Rating |
|---|---------|------|--------|
| 1 | Disguised Dragonlord can pick the removed guard: no Dragonlord, unwinnable seed | Bug, confirmed | Easy |
| 2 | A music retry loses every song | Bug, confirmed | Easy |
| 3 | Best fit puts name-print code where the ring and necklace patches write | Bug, confirmed in trace, not yet reached by real flags | Easy |
| 4 | The map retry has no limit, and `lm_sizes[-1]` is read | Bug, confirmed on the stand-in ROM | Medium |
| 5 | ROM input is not validated, and `NPCData` is read uninitialized | Bug, confirmed on the stand-in ROM | Easy |
| 6 | Cosmetic options can change the checksum through the retry | Bug, by reading | Easy |
| 7 | Winter inner build failure writes a ROM with CRC 000000 and reports failure | Bug, by reading | Easy |
| 8 | Caldera Tantegel check also runs on non-random maps | Bug, confirmed | Easy |
| 9 | Saving does not persist the Return index | Bug, by reading | Easy |
| 10 | Credits stub "no room" never triggers the retry and wastes the song | Bug, by reading | Easy |
| 11 | Out-of-bounds table reads | Bug, by reading | Easy |
| 12 | Riverlands area check runs before the bridges | Speed and bug, measured | Easy |
| 13 | Lookups and one write still ahead of flag checks | Size and bug risk | Easy |
| 14 | Hurtmore Doors: share the rest of the key routine | Size, 59 bytes | Easy to Medium |
| 15 | Inn code in `zoom_and_whistle` can be table driven | Size, about 28 bytes | Medium |
| 16 | Credits stub: one mapper subroutine | Size, 12 bytes twice | Medium |
| 17 | Frame hook and danger meter small savings | Size, 4 bytes, 2 cycles per frame | Easy |
| 18 | Music wrapper zero page swap (open from review 1) | Speed, about 2,000 cycles per frame | Hard |
| 19 | Spike loops and credits duplication (open from review 1) | Size, about 80 bytes | Medium |
| 20 | Clarity list | Clarity | Easy |

---

## Bugs

### 1. Disguised Dragonlord can pick the removed guard

- **Where:** `common/dwr.c:3091` (choice loop), `3035` to `3038` (the guard), `3049` to `3055` (Inn in Charlock).
- **Problem:**
  - NPC 20 is the guard with control byte `$65`. The code gives it `[3] = 0xc1` and `[4] = 0xff`, which means "never placed" unless Inn in Charlock is on.
  - Bit `$80` makes it eligible for the Dragonlord. If chosen, `NPCData[20][2] = 0x70` is never written to the ROM, and the real Dragonlord (NPC 25) becomes the hint NPC.
  - The seed has no Dragonlord to fight.
  - `location` is also never set, so `strcat` at about line 3260 reads an uninitialized buffer into the hint.
- **Evidence:**
  - Table counts on the stand-in ROM put row 20 at the start of area 2 and row 25 in area 5, which is the Dragonlord's row.
  - Flags `IVIAAVCEKACAAAAAAAAAAEAUAASBAAAAABYCAAAA` with seed 178 produce no row with control byte `$70`. That was 1 of 400 seeds. The hint text ends in garbage.
- **Change:** `} while (!(NPCData[chosen_NPC][3] & swappable_check) || NPCData[chosen_NPC][4] == 0xff);`
- **Benefit:** removes an unwinnable seed.
- **Risk:** low. Only seeds that hit this case change.
- **Rating:** Easy.

### 2. A music retry loses every song

- **Where:** `common/custom_music.c:618` (`s->bank >= 0` skips the song), `322` (the only place `bank` is reset), `common/dwr.c:4896` (retry).
- **Problem:** `songs[]` is static. The first attempt marks the picked songs as placed. If stage 2 then runs out of room, the retry sees them as already placed and picks none.
- **Evidence (synthetic 9-song pack):**
  - Flags `IVIAAVCVKACAAFIAKVKQAEAUKV2RKKP7ZRYSB7YI`: the first attempt places 9 songs and fails on the 27-byte hook. The second prints `No custom music fits, keeping the original music.`
  - In 60 dense flag sets with the pack, 51 retried. Without the pack, none did.
  - The player gets a working ROM with their pack silently gone.
- **Change:** at the top of `custom_music_apply`: `for (i = 0; i < song_count; i++) songs[i].bank = -1;`
- **Benefit:** the retry keeps the same songs. It also makes a second build without a reload safe.
- **Risk:** none.
- **Rating:** Easy.

### 3. Best fit puts name-print code where the ring and necklace patches write

- **Where:** `common/dwr.c:4563` (`instant_name_print` runs first), `4581` and `4583` (fixed writes at `$FF54` and `$FF7D`), `4932` to `4953` (best fit).
- **Problem:** first fit fills from the low addresses, so it never reaches `$FF54`. Best fit takes the smallest gap first:
  - The 15-byte request gets `$FF7D`.
  - On a vanilla ROM, the 9-byte request gets `$FF54`.
  - `dwr_fighters_ring` and `dwr_death_necklace` then overwrite both. The hero-name routines would JSR into ring code.
- **Evidence:**
  - Trace with `fit_game` on and default flags: `ALLOC ff7d 15`, `W 0ff7d 15`, later `W 0ff7d 17`.
  - On the stand-in ROM the 9-byte request landed at `$E925`, which `spike_rewrite` then overwrites. That gap is not free on a vanilla ROM.
  - Reachability: this needs Fast Text off, Build Options unchanged and game packing on. None of 60 dense flag sets reached game packing (the pack sets recovered by packing only the extras), so this is waiting for the next feature that uses space.
- **Change:** call `instant_name_print` right after `dwr_death_necklace`. The trace shows no other lookup between them, so first-fit builds should keep the same bytes. Confirm with a checksum comparison.
- **Benefit:** avoids silently broken ROMs.
- **Risk:** low.
- **Rating:** Easy.

### 4. The map retry has no limit, and `lm_sizes[-1]` is read

- **Where:** `common/map.c:1298` (`retry_map`), `690` (`find_largest_lm`).
- **Problem:**
  - With one land mass, `*next` stays 0 and line 690 reads `lm_sizes[-1]`, which is undefined behavior. If that value is 250 or more, `next` stays 0.
  - Then `place(..., next, 0)` can never succeed, `land_missing` is set and the map retries forever. The bounded land search stops each place, but nothing bounds the retries.
- **Evidence:**
  - Random Map off on the stand-in ROM (one land mass): 1,600 or more `No room` retries in 8 seconds, killed by a timeout. On the site this would freeze the tab. A true vanilla map has more than one land mass, so this needs a wrong input today.
  - Biome maps reach `next == 0` about 1 to 4 times per map before Charlock's moat adds a mass. It did no harm in these runs, because the moat runs before towns are placed.
  - The native build and the site's WebAssembly gave the same checksums on all 96 cases I compared (8 flag sets, 12 seeds).
- **Change:**
  - `if (!*next || lm_sizes[*next - 1] < MIN_LM_SIZE) *next = *largest;`
  - Add a retry counter (for example 2,000) that fails the build the same way running out of room does. Expose a small helper from `dwr.c`.
- **Benefit:** no hang.
- **Risk:** the first line matches today's behavior wherever `lm_sizes[-1]` is below 250.
- **Rating:** Medium (Easy for the one line).

### 5. ROM input is not validated, and `NPCData` is read uninitialized

- **Where:** `common/dwr.c:183` (`dwr_init`), `2976` (`NPCData[136][6]`), `2985` to `3045`.
- **Problem:**
  - Any file of at least 81,936 bytes is accepted.
  - `npc_shenanigans` assumes exactly 136 NPCs. With fewer, later loops use uninitialized rows. With more, `k` writes past the array.
- **Evidence:** my first stand-in had 135 NPCs. Native and WebAssembly output then differed on every seed, in the NPC table at `$1736`. With 136 rows they matched.
- **Change:** check the PRG CRC against the known PRG0 and PRG1 values and return an error. Stop the loop when `k == 136`.
- **Benefit:** a clear error instead of a bad ROM or a hang (finding 4).
- **Risk:** low. The check must allow both revisions, because `other_patches` converts PRG1 to PRG0.
- **Rating:** Easy.

### 6. Cosmetic options can change the checksum

- **Where:** `common/dwr.c:4905` (`else if (!fit_game)`).
- **Problem:**
  - If stage 2 fails even with packed extras, the retry re-packs the game code, which is covered by the checksum.
  - Two players with the same seed and flags could then get different checksums and code layouts, only because one loaded a music pack.
  - It also breaks the "matches a build made without the Winter theme" comment, because that inner build never escalates this way.
  - Not seen in my runs.
- **Change:** on that branch, fail instead of escalating. Only a stage 1 failure may turn on `fit_game`.
- **Benefit:** the checksum stays a real check.
- **Risk:** a few very full builds report "no room" instead of working.
- **Rating:** Easy.

### 7. Winter inner failure writes a ROM with CRC 000000

- **Where:** `common/dwr.c:4840`.
- **Problem:** if `dwr_randomizeWithoutWinterTheme` returns 0, the outer build carries on, writes the ROM with a zero CRC and returns 0. The site then reports no room and leaves the file in its virtual file system.
- **Change:** after the call, if `crc` is 0, set `ran_out_of_room = TRUE` and `longjmp` to the handler.
- **Benefit:** correct failure handling.
- **Risk:** none.
- **Rating:** Easy.

### 8. Caldera Tantegel check also runs on non-random maps

- **Where:** `common/map.c:920`. Compare `place_charlock` at `498`, which is inside the `RANDOM_MAP` branch.
- **Problem:** with Random Map off, or a Random Map "maybe" that resolves off, and the Caldera bit set, the check still rejects spots near (60, 60).
  - On the vanilla spot list that excludes (43, 43), (48, 41) and (29, 57), so Tantegel's own vanilla spot can never be used.
  - The ignored Biome bit also changes the map.
- **Evidence:** with Random Map off on the stand-in ROM, the message printed more than 500 times per run.
- **Change:** `if (RANDOM_MAP(map) && BIOME(map) == BIOME_CALDERA)`. Also share `map->size * 27 / 100` (lines 923 and 1089) in one macro.
- **Benefit:** the Biome bit does what the site says when there is no random map.
- **Risk:** none. Biome has not been released.
- **Rating:** Easy.

### 9. Saving does not persist the Return index

- **Where:** `common/dwr.c:3506` to `3512`.
- **Problem:** saving sets `ram_i` to Tantegel but does not call `persist`, which only runs at inns. Stay at the Kol inn, save at the king, reset and continue: `warp_list_load` restores Kol. The function comment says Return goes to the last place you saved or used an inn.
- **Change:** add `0x20, LO(persist), HI(persist)` before the JSR `$F148`. `code_size` goes from 9 to 12, and the lookup at line 3389 goes from 178 to 181.
- **Benefit:** the fix costs 3 bytes.
- **Risk:** low. Test by saving, resetting, then casting Return.
- **Rating:** Easy.

### 10. Credits stub "no room" never triggers the retry

- **Where:** `common/custom_music.c:828` to `829`.
- **Problem:**
  - The stub search is always first fit and only prints a message, so packing is never tried for it.
  - The credits song is already placed first in bank 6, where its bytes and a table slot go unused.
  - The stub grew from 51 to 64 bytes in this change, so this is more likely now. It showed up in the flag set from finding 2.
- **Change:** when the stub has no room, set `ran_out_of_room` and `longjmp` while `fit_extras` is off. Keep the current fallback once it is on. Also use best fit for the stub when `fit_extras` is on.
- **Benefit:** the credits song survives.
- **Risk:** low.
- **Rating:** Easy.

### 11. Out-of-bounds table reads

- `common/dwr.c:1291`: `i <= 31` over 31 chests. Index 31 reads a byte of Slime's enemy data, and could write it if the value matched a quest item. Use `i < CHEST_COUNT`.
- `common/dwr.c:1296`: `i <= 3` over 3 search items. Index 3 is the `$FF` at `$E142`. Use `i < 3`.
- `common/chests.c:531` to `536`: `j` runs past `chest_spots` before the check at line 538. Move the check into the loops.

Both `dwr.c` lines are harmless today. Risk: none. Rating: Easy.

---

## Speed and size

### 12. Riverlands area check runs before the bridges

- **Where:** `common/map.c:1356` (check), `1362` (bridges).
- **Problem:** rivers cut the land, so `largest + next` is low until the bridges join it. Good maps are thrown away.
- **Measured (100 maps per row):**

| Biome and size | Area retries per map | Worst retries on one map | Average time |
|----------------|---------------------|--------------------------|--------------|
| Riverlands, size 60 | 1.80 | 11 | 44 ms |
| Wasteland, size 60 | 0.06 | 2 | 17 ms |
| Caldera, size 60 | 0.09 | 1 | 23 ms |

- **Change:** for Riverlands, run the bridges first, recount, then check the area.
- **Benefit:** about 25 ms per small map.
- **Risk:** Biome seeds change, which is fine because it is unreleased.
- **Rating:** Easy.

### 13. Lookups and one write still ahead of flag checks

The moved lookups are correct and did not change the first-fit layout, because they do not write. These remain:

- `common/dwr.c:3373` to `3384`: `zoom_and_whistle` allocates and writes 16 bytes before the flag check. Move it below the check. **Saves 16 bytes** in builds without Return to Zoom or Warp Whistle.
- `common/dwr.c:1838`: `key_herb_counter` asks for 30 bytes before its flag check. A full ROM fails here even with the flag off.
- `common/challenge.c:123`: `damage_bonks` asks for 48 bytes before its flag check. The same failure applies.
- `common/dwr.c:405` to `406`: `dwx_run_mechanics` asks for 8 and 17 bytes, and each mode uses at most one. Move them into the branches.

Risk: moving the 16-byte write changes the code layout, and so the checksum, for those builds. Rating: Easy.

### 14. Hurtmore Doors: share the rest of the key routine

- **Where:** `common/dwr.c:3584` to `3613`.
- **Problem:** the new code still copies 64 bytes of the vanilla routine at `$DCAA` to `$DCEA`.
- **Change:** at `found`, use `LDX #0 / JMP $DCAC` (5 bytes).
  - Do not jump to `$DCAA`. `unbreakable_keys` puts a 4-byte JSR at `$DCA8`, so `$DCAA` holds its high byte.
  - The catch is the sound: vanilla plays `$94`, and this copy plays `$8A`. Accept the door sound, or keep Hurtmore's sound with a small hook at `$DCD8` plus a RAM flag.
- **Benefit:** **59 bytes**, or about 40 if the sound is kept.
- **Risk:** low if the sound change is accepted.
- **Rating:** Easy with the door sound, Medium to keep the sound.

### 15. Inn code in `zoom_and_whistle` can be table driven

- **Where:** `common/dwr.c:3457` to `3499`.
- **Problem:** five 11-byte `CPX / BNE / LDX / STX / ORA` blocks.
- **Change:** `LDA $45 / LDY #5 / loop: CMP maps,Y / BEQ found / DEY / BNE loop`. Then, at `found`: `STY ram_i / LDA ram_v / ORA bits,Y / STA ram_v`. A 10-byte table replaces the immediates.
- **Benefit:** **about 28 bytes** (72 down to about 44). Cycles do not matter here.
- **Risk:** moderate. The Tantegel index must stay excluded. Test with an emulator at every inn.
- **Rating:** Medium.

### 16. Credits stub: one mapper subroutine

- **Where:** `common/custom_music.c:803` to `817`.
- **Change:** one 20-byte routine (five `STA $FFFF` with four `LSR`, then `RTS`), called twice as `LDA #n / JSR sw`.
- **Benefit:** **12 bytes**, in both bank 7 and bank 5 (64 down to 52). It costs 12 extra cycles per call in the credits NMI, which is fine.
- **Risk:** the stub becomes non-const, because `sw` depends on `u`. The JSR return addresses stay valid because the same bytes are mapped in both banks.
- **Rating:** Medium.

### 17. Small savings

- `common/custom_music.c:851`: `AD banksel / 29 01 / F0 04 / A9 03 / D0 02 / A9 06` becomes `AD banksel / F0 03 / A9 03 / 2C / A9 06`.
  - `banks,y` only ever writes 0 or 1, so the `AND` can go, and `$2C` skips the next load.
  - **Saves 3 bytes** and 2 cycles per frame.
  - On power-up, SRAM garbage picks bank 3 or 6 for a frame or two before the first request, with no song playing. That is harmless.
- `common/dwr.c:4193`: the `0x18` (`CLC`) is dead, because the loop only exits through `BCC`. **Saves 1 byte** and 2 cycles per trigger. Change sizes 52 to 51.
- `expansion/exp_bank_4.asm:365`, `374`, `375`: the `PHA / PLA / TAY` pair in `addr_to_bcd`. Y is not used by callers. Saves 3 bytes. Needs an `asm6f` rebuild.

Risk: low. Rating: Easy.

### 18. Music wrapper zero page swap (open from review 1)

`tools/custom-music/gen_driver.py:60` to `66` and `147` to `153` copy 46 zero page bytes twice per frame, at about 2,030 cycles. See review 1, finding 15. I checked the driver's RAM use from the image: absolute operands end at about `$6A1F`, so the driver does not reach `$6B00` today. The assert is still worth adding, because the linker config allows up to `$6FFF`. Rating: Hard.

### 19. Duplication (open from review 1)

- Spike loops, `common/dwr.c:2149`, `2171`, `2216`: about 37 bytes. Medium.
- Credits `MON_STAT`, `expansion/exp_bank_4.asm:250` to `277`: about 40 bytes of bank 4. Medium.

---

## 20. Clarity

All Easy.

- `common/dwr.c:4720`: the comment is wrong. Static variables keep their values across `longjmp`, so `volatile` is not needed on `fit_best`, `ran_out_of_room` and `fail_stage`. Two comment blocks are also stacked here.
- `common/dwr.c:4921`: an orphan comment that repeats the one at 4721. `4933`: `uint32_t at = 0` is set again on the next line. `4950`: `getenv("DWR_TRACE_ALLOC")` debug code ships in the release and only reports best fit.
- `common/map.c:379`: `land_missing` sits between `map_find_land`'s doc comment and the function, and the doc has no `@return` for the new `BOOL`.
- `common/map.c:1146`: "reached the far coast" also fires on any earlier river.
- `common/map.c:1120`: `dist` is reused as a loop counter.
- `common/map.c:280`: `map_land_mass` is unused (gcc warns).
- `common/map.c:219` to `230`: commented-out code.
- `common/map.c`: `find_walkable_area` at the top of `place_landmarks` repeats the call made just before it.
- `common/dwr.h:31`: new macros follow the old pattern with `x` not in parentheses. Fine today.
- `common/dwr.c:3318`: a second copy of `tile_is_walkable`. `3393` to `3408`: raw warp indexes instead of `WARP_*`.
- gcc warnings:
  - `dwr.c:3386` and `3809`: misleading indentation (tabs and spaces).
  - `dwr.c:4414`: unused `town_index`.
  - `stair_shuffle.c:185`, `323`, `521`: unused variables.
  - `stair_shuffle.c:312`: no return at the end.
  - `dwr.c:2679`: truncation warning, which is intended and could be silenced by printing the top 24 bits.
- `common/stair_shuffle.c:436`: `y < map->width` should be `height`. Harmless today, because every map in `indexes` has width at least equal to height (the Garin's Grave floor that differs is 14 by 12).
- `common/stair_shuffle.c:67`, `77`: comments say `$6680`, and the code uses `$662F`.
- `common/stair_shuffle.c:393`: `maps` and `warps_from` are never freed, about 16 KB per attempt, and retries make it worse on the site.
- `common/stair_shuffle.c:548` to `551`: `chest_paths` starts `CHARLOCK_THRONE_ROOM` at 10, 19, while `do_shuffle` uses 10, 29 for that map and 10, 19 for `CHARLOCK`. It may mean `CHARLOCK`. Check before changing, because it changes seeds.
- `notes/ram.txt` and `notes/free_space.txt` still do not cover the RAM map, Run Bank, custom music or the new lookups.
- `tools/build_music.py:7`, `16`, `18`: `distutils` (removed in Python 3.12) and hard-coded `/home/francois` paths.
- `tools/custom-music/build_music_test.py` and `driver.cfg` describe an older layout (`$8000`, no banks) than `gen_driver.py` (`$8100`). Delete them or mark them as history.

---

## Review 1 findings: status

| # | Finding | Status |
|---|---------|--------|
| 1 | Run Bank last-spell hook deleted | Fixed, `dwr.c:4104` |
| 2 | Wrong RNG routine in `modify_run_rate` | Fixed, `bank_3_patch.asm` (`jsr $c55b`, `clc`) |
| 3 | Run Bank RAM init orphaned | Fixed, `run_bank_win_hook` chains on `$CA1A` |
| 4 | `vpatch` claims more bytes than it passes | Fixed: my counter found 2,132 calls with no mismatch. The `VPATCH` macro was not adopted |
| 5 | Credits play time `sbc #1` | Fixed, `exp_bank_4.asm:944` |
| 6 | Build-menu branches land late | Fixed, `dwr.c` `0x0e` and `0x0a` |
| 7 | Three's Company overwrites vanilla bytes | Fixed, moved to free space |
| 8 | One-byte overruns in flag decoding | Fixed, `base32.c:195` |
| 9 | `find_free_space` not bank-aware, aborts | Fixed: `find_free_space_in` plus `longjmp`. `abort()` remains only with no handler |
| 10 | Costs XP reserves too few bytes | Fixed, 17 |
| 11 | Custom music fragile places | Mostly fixed. Still open: `MAX_PLACED 24` against 28 possible placements (`custom_music.c:34`), and second-resolution `mt_init(time(NULL))` (`custom_music.c:714`, `dwr.c:4853`) |
| 12 | Title screen padding | Fixed |
| 13 | Mapper switch and NMI | Fixed, `exp_bank_3.asm` and `exp_bank_4.asm:87` |
| 14 | Build tooling | `build_expansion.py` fixed, with a size limit. `build_music.py` still open |
| 15 | Wrapper zero page swap | Open (finding 18) |
| 16 | `count_frame` INC | Fixed |
| 17 | Spike loops | Open (finding 19) |
| 18 | `MON_STAT` duplication | Open (finding 19) |
| 19 | Small credits instructions | Mostly fixed. `addr_to_bcd` still open (finding 17) |
| 20 | Clarity | Partly fixed. See finding 20 |

---

## Checked and found correct

- **Biome flags:**
  - `update_flags` resolves bits `$22` into `$11`, and `BIOME` reads `$10` and `$01`.
  - The site's mask (`0x33`) and values match.
  - No other macro uses those bits.
  - The count of RNG calls is unchanged.
- **Biome maps:**
  - No out-of-bounds tile writes by reading, at sizes 60, 90 and 120.
  - The caldera block stays clear of the passes.
  - 1,200 maps had no land or Charlock retries.
- **Bounded land search:** works and is never reached in normal maps.
- **Hurtmore code:**
  - 132 arguments.
  - Offsets 60, 64 and 68 and both branches are right.
  - The stack is balanced on every path.
  - The search order is up, down, left, right, as in vanilla.
  - `$DC8E` is the vanilla "no door" code.
- **Silence stub:**
  - 64 bytes.
  - `BEQ +6` lands on the switch code.
  - `WANT` is never cleared by the wrapper, so the check holds on later frames.
  - The wrapper turns `$4015` back on every frame.
- **Retry control flow:**
  - It always ends.
  - `rom` is unchanged after `setjmp`.
  - A retry rebuilds from the file.
  - First-fit builds match the same seed on native and WebAssembly (96 of 96).
- **Allocator risk from FF data:** runs of 6 or more `$FF` in the built fixed bank are all intended free space. Tables with a trailing `$FF` are not a problem today.
- **Safe Start:** by reading only. It runs before `map_encode`, keeps the maybe step's RNG count, and skips Charlock's moat.
- **Run Bank `$CA1A` chain and `warp_list_load`:** branch offsets are right.

## What I could not review

- **No emulator run.** Hurtmore Doors, the silence stub, biomes in play and Safe Start are checked as code, not as gameplay.
- **No vanilla ROM.** The stand-in has leftover patches, so its free-space layout is close to vanilla but not exact. Findings that rely on it say so.
- **No `asm6f`, `ca65` or `ld65`.** I did not rebuild the expansion or driver, so `common/expansion.c` and `ft_driver.h` were not compared to their sources.
- **Read only where the randomizer touches them:**
  - Dn-FamiTracker driver (about 6,000 lines).
  - `famistudio_asm6.asm`.
  - `expansion/music/*.fms`.
  - `credit_music.c`.
  - Sprite and challenge data bytes.
  - `flute_music.c` song data.
  - `polyfit.c` factor tables.
- **Skimmed:** `crc64.c`, `mt19937-64.c`, `tools/ips.py`, `tools/patchgen.py`, `tools/polyfit.py`, `tools/build_music.py`.
- **Not in scope:** `webui/`, `site/` (other than the flag wiring), `electron/`, `hud.lua`, `release.sh`.
- **Music tests** used a synthetic pack, so no real module was played.
- **Retry reachability** comes from random flag sets, not an exhaustive search.
- **Blocked commands:** a local safety hook blocked one heredoc, one cleanup `rm -rf` and one PowerShell process filter. I wrote scripts as files and did not delete anything. One of my test runs hung (finding 4), and I stopped it with `taskkill` after checking the paths and start times of the processes. Test files remain in the scratchpad.
