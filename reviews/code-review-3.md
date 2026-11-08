# Code review 3: Dragon Warrior Randomizer, branch feature/custom-music

Reviewer: Code (6502/NES). The only repository change is this file. Nothing was committed or pushed, and no emulator was launched.

Tree: HEAD 83bfe4a plus the uncommitted working tree. Line numbers match these files:

| File | SHA-1 prefix |
|------|--------------|
| `common/dwr.c` | `a3095168` |
| `common/map.c` | `b7c8ac73` |
| `common/dwr.h` | `71c41fd1` |
| `common/custom_music.c` | `e196674e` |
| `cli/main.c` | `6baa2649` |
| `site/index.html` | `6bbbe594` |

**How each finding was checked:**

- **Confirmed:** I saw it in a build, in finished ROM bytes or in an existing screenshot.
- **By reading:** from source or disassembly only, with no test.

Difficulty means effort: Easy (minutes), Medium (an hour or two plus testing), Hard (a day), Very hard (a rewrite).

## Ranked list

| # | Finding | Type | Checked | Difficulty |
|---|---------|------|---------|------------|
| 1 | Winter Theme plus Run Bank: wins can call the wrong code | Bug, can crash | Confirmed | Easy |
| 2 | Menu Color also recolors coasts, towns, castles and bridges | Bug, visible on every overworld | Confirmed | Medium |
| 3 | The Steps stat resets on every continue | Bug | By reading | Easy |
| 4 | Run Bank is shared by all three save slots | Bug | By reading | Medium |
| 5 | The site hides a lost credits song | Bug | Confirmed in the log, site line by reading | Easy |
| 6 | The site reports every failure as "Not enough ROM space" | Bug | By reading | Easy |
| 7 | The spoiler log gives wrong places and leaves one out | Bug | Confirmed | Easy |
| 8 | `add_bridges` reads past the edge of a map column | Bug, low harm | Confirmed | Easy |
| 9 | CLI typos are silently accepted | Bug, low harm | By reading | Easy |
| 10 | A Winter "give up" is treated as "out of room" | Bug, probably unreachable | By reading | Easy |
| 11 | Safe Start leaves other towns with a swamp border | Bug, cosmetic | By reading | Easy |
| 12 | Music is copied before the hooks are known to fit | Clarity | By reading | Easy |
| 13 | A credits stub failure rebuilds the whole ROM | Speed | Confirmed | Medium |
| 14 | Key and herb window reserves 2 bytes it may not use | Size, 2 bytes | By reading | Easy |
| 15 | Clarity list | Clarity | By reading | Easy |

---

## Bugs

### 1. Winter Theme plus Run Bank: wins can call the wrong code

- **Where:** `common/dwr.c:3947` (static), `4139` (set), `4880` (inner build), `4232` and `4247` (used).
- **Problem:**
  - `run_bank` stores its win routine's address in the static `run_bank_win_code`.
  - With Winter Theme on, the inner no-Winter build runs `apply_stuff_to_rom` again and overwrites that static.
  - `run_bank_win_hook` runs after that, so the outer ROM gets the inner build's address.
  - This goes wrong when the two builds lay out code differently. That happens when the outer build falls back to tight packing and the inner build does not.
- **Failure scenario:**
  - Flags `INISEFA5IRKECEGVAEKAAFJCZVG2KQKRERAWGVIE`, seed 4011.
  - The outer build's win routine is at `$C4AD`, but the ROM has `JSR $C6C9` at `$E98C`.
  - `$C6C9` is the Run Bank dots routine. It starts `PHA / LDA $64CA` and ends `JMP $A62B`.
  - On every won battle, `COUNT_WIN` is skipped and no bank points are earned. I infer a likely crash or garbled state, because `$A62B` is bank-switched code entered from the wrong place.
- **Evidence:** 201 random flag sets with Winter and Run Bank forced on:
  - Without a fix: 186 good, 4 bad, 11 failed to build.
  - With the fix below, in a scratch copy: 190 good, 0 bad, 11 failed to build.
- **Fix:** in `dwr_randomizeWithoutWinterTheme`, save and restore the static next to `fit_best`:

```c
uint16_t saved_win = run_bank_win_code;
...
run_bank_win_code = saved_win;
```

- **Risk:** none. Only the broken builds change, and the checksum does not, because this hook runs after it.
- **Difficulty:** Easy.

### 2. Menu Color also recolors coasts, towns, castles and bridges

- **Where:** `common/dwr.c:4745` to `4755`.
- **Problem:** the comment calls background palette 0's color 2 "the spare one", but it is not spare.
  - Bytes `0x1A47` and `0x1A57` hold the `$10` gray of the overworld and town palettes.
  - The block table at `$F5B5` puts these blocks on palette 0, and they all use color 2:

| Blocks | Tiles | Color 2 pixels per block |
|--------|-------|--------------------------|
| Town (7) | `82`-`85` | 59 |
| Castle (9) | `8A`-`8D` | 58 |
| Bridge (10) | `9B`-`9E` | 64 |
| Shoreline water (24-33) | `46`, `4A`, `58`-`5E`, `6A`, and others | 39 to 91 |

- **Failure scenario:** a player picks Gold. Every coast, town, castle and bridge gets gold pixels, and Pink or Green would be stronger.
- **Evidence:**
  - An existing gold-border screenshot shows gold edges on the water: `scratchpad\mc2\m.log.overworld.png`.
  - I decoded the block table and tiles from a built ROM, and the vanilla ROM has the same tiles and palettes.
- **Second effect (by reading):**
  - The loader at `$C661` to `$C67C` applies the low-HP red `$26` only when `$0A` is 1, which I read as palette entry 1.
  - A chosen border color therefore stops turning red at low HP. Only the text does.
  - The comment "Red is left out, because red means low HP" suggests this was considered. I did not test it, because the earlier low-HP screenshot did not reach low HP on screen.
- **Fix options:**
  - **A, Medium:** when a color is chosen, redraw color 2 pixels as color 1 or 3 in the map tiles of those blocks. Check first that no block on another palette shares a tile. The gray shading is lost and water looks a little different.
  - **B, Easy:** keep it and describe it on the site as "Menu and map accent color", and fix the comment.
  - There is no free fourth color. All four background palettes are used by the map.
- **Risk:** cosmetic only. It runs after the checksum.
- **Difficulty:** Medium for A, Easy for B. This needs your choice.

### 3. The Steps stat resets on every continue

- **Where:** `common/dwr.c:4273` and `4274`, with `expansion/exp_bank_4.asm:872` (shows `$6804`).
- **Problem:**
  - `new_flags_ram_init` zeroes the step counter with no check for a new game.
  - In the vanilla code, `$CA08` to `$CA1A` runs straight through after the load, for new and continued games. The comment at `dwr.c:4237` says the same.
- **Failure scenario:** a player walks 5,000 steps, resets, continues and finishes. The stat scroll shows only the steps since the continue.
- **Fix:** move the two step-counter stores behind a 0 XP check, as Run Bank does at `dwr.c:4240`. Grow the routine and its 23-byte request to match. A 16-bit per-slot copy would also stop slots sharing it (see finding 4).
- **Risk:** this code is covered by the checksum, so the layout and checksum change for all builds. Do it with other seed-changing work.
- **Difficulty:** Easy.

### 4. Run Bank is shared by all three save slots

- **Where:** `common/dwr.c:4229` to `4243`.
- **Problem:** the bank (`$6806`), last spell (`$6807`) and points (`$6808`) are one set of SRAM bytes. The warp list was given per-slot bytes at `$6810` and `$6813`, but these were not. They are also not tied to saving, so they keep values the save does not have.
- **Failure scenario:** play slot 1 to 5 banked runs, start a new game in slot 2 (0 XP clears the bank), then continue slot 1. Its bank is now 0. Going the other way, a continued slot picks up another slot's bank.
- **Fix:** add per-slot copies indexed by `SAVE_SLOT`, as `persist` and `warp_list_load` do. Update them at save and restore them on continue. Spare SRAM after `$6816` must be confirmed first.
- **Risk:** low. Test by saving, resetting and switching slots.
- **Difficulty:** Medium.

### 5. The site hides a lost credits song

- **Where:** `site/index.html:674`, `common/custom_music.c:864`.
- **Problem:**
  - The site looks for `keeping the original music`. The stub message says `keeping the original credits music`, so it does not match.
  - The song was already printed as placed, so the `placed` count looks complete too.
- **Evidence:** 100 dense random flag sets with `pack_themed.json5`: 16 printed the credits message. Nothing in the site's message logic would match it. I read the site code but did not run it.
- **Fix:** add a check for `No room for the credits song` to the message. Also, do not count the song as placed in that case.
- **Risk:** none.
- **Difficulty:** Easy.

### 6. The site reports every failure as "Not enough ROM space"

- **Where:** `site/index.html:669`, with `common/dwr.c:195` (bad ROM), `4911` (write error), `5044` (`dwr_give_up`).
- **Failure scenario:** the "Could not draw a map" give-up, a wrong kind of ROM or a write error all return 0. The player is told to turn flags off, which does not help.
- **Fix:** check the log for `Could not draw a map` and `not a Dragon Warrior ROM`, and show that message. `printErr` output would need capturing too, since the ROM error goes to `stderr`.
- **Risk:** none.
- **Difficulty:** Easy.

### 7. The spoiler log gives wrong places and leaves one out

- **Where:** `common/dwr.c:4684` to `4690` and `4708` to `4711`, with `common/map.c:857` to `864`.
- **Problem:**
  - The heading says "x, y on the overworld". But two caves are inside Tantegel (29, 29) and Garinham (19, 0), and the map number is not printed.
  - `WARP_TANTEGEL_BASEMENT` is one of the eight shuffled caves but is not in the list.
- **Evidence:** seed 42 with `IVIAAVCEKACAAAAAAAAAAEAUAAQBAAAAABYCAAAA` prints `Erdrick's Cave 29, 29` and `Staff Shrine 19, 0` as overworld spots.
- **Fix:** add the basement to `places`. Print "inside Tantegel" or "inside Garinham" when `warp.map` is not 1, using the existing `maps[]` names.
- **Risk:** none. It only changes a text file.
- **Difficulty:** Easy.

### 8. `add_bridges` reads past the edge of a map column

- **Where:** `common/map.c:354` and `357`. The same pattern of missing check is at `map.c:1363` to `1365`.
- **Problem:**
  - With a bridge candidate at `y == 119`, `tiles[x][y+1]` reads `tiles[x][120]`, which is really `tiles[x+1][0]`. With `y == 0`, `tiles[x][y-1]` reads `tiles[x-1][119]`.
  - It stays inside the struct, so there is no crash. The bridge tile can copy an unrelated tile.
  - At `1363`, an all-water map would read `lm_sizes[-1]`. I did not see that happen.
- **Evidence:** a bounds-trap build stopped at line 357 with `y = 119`, `x = 72` on seed 3005 with the default flag string. That was 4 of 15 seeds at normal size. Biome maps did not hit it, because their coast keeps those rows as water.
- **Fix:** `if (y > 0 && ...)` and `else if (y < 119 && ...)`. Add `if (!largest) goto retry_map;` before line 1363.
- **Risk:** changes the map on seeds that hit this, so release it with other seed-changing work.
- **Difficulty:** Easy.

### 9. CLI typos are silently accepted

- **Where:** `cli/main.c:57` to `60`, `61` to `66`, `67` to `72`.
- **Failure scenario:** `--menu-color purpel` builds white, and `--encounters hihg` builds default. `--spoiler yes` and `--key-herb yes` turn the option off. `--bank-level` already rejects bad values.
- **Fix:** match the `--bank-level` pattern. Add the options to `print_usage`, which lists none of them.
- **Risk:** scripts with bad values now stop with an error.
- **Difficulty:** Easy.

### 10. A Winter "give up" is treated as "out of room"

- **Where:** `common/dwr.c:4881` and `4882`.
- **Problem:** the inner build's `dwr_give_up` clears `ran_out_of_room`, but the outer build sees `crc == 0` and calls `dwr_no_room`. The build then retries up to three more times, each with up to 2,000 map attempts, and ends in the wrong message.
- **Reachability:** probably none today. The outer build draws the same map first, so it should give up first. This is inferred, not tested.
- **Fix:** add a static `gave_up` flag, set in `dwr_give_up`. Line 4881 then stops instead of retrying.
- **Risk:** none.
- **Difficulty:** Easy.

### 11. Safe Start leaves other towns with a swamp border

- **Where:** `common/map.c:1271` and `1272`.
- **Problem:** only Tantegel's border is changed. A town within 3 tiles of Tantegel that was placed on swamp sits on grass, but keeps a swamp border inside its own map.
- **Fix:** repeat the border check for any town whose warp is inside the radius, or accept it.
- **Risk:** cosmetic. It is covered by the checksum, so Safe Start seeds that hit it change.
- **Difficulty:** Easy.

---

## Speed, size and clarity

### 12. Music is copied before the hooks are known to fit

- **Where:** `common/custom_music.c:739` to `782`.
- **Problem:** the driver and songs are written to banks 3 and 6, and only then does the hook check run. When it fails, those bytes stay in the ROM unused. That is harmless, but it reads as if the music is installed.
- **Change:** move the reservation block (`764` to `782`) above line 739.
- **Benefit:** clarity and a cleaner ROM. No bytes change in the game code.
- **Risk:** none. The check does not use the RNG, and the hook addresses stay the same.
- **Difficulty:** Easy.

### 13. A credits stub failure rebuilds the whole ROM

- **Where:** `common/custom_music.c:861` to `864`.
- **Problem:** on first fit, a missing 64-byte stub starts a full rebuild, and in my runs the stub often still did not fit. 12 of 80 dense sets rebuilt and still lost the credits song.
- **Change:** search for the stub at the same time as the three hooks, so the build knows early. Or reduce it with review 2's finding 16 (12 bytes, still open), which may also rescue some builds.
- **Benefit:** saves one full build, which is roughly twice the time on affected sets. I did not count how many rebuilds did rescue the song, because my output was truncated.
- **Risk:** low.
- **Difficulty:** Medium.

### 14. Key and herb window reserves 2 bytes it may not use

- **Where:** `common/dwr.c:1853`.
- **Change:** `4 + rows + (DEATH_COUNTER(rom) ? 2 : 0) + 5`.
- **Benefit:** 2 bytes in builds without the death counter.
- **Risk:** the layout and checksum can change for those builds.
- **Difficulty:** Easy.

### 15. Clarity list

- `common/map.c:1372` to `1375`: for Riverlands, this bridge loop can never do anything, because the loop at `1358` already finished. Line `1361` also repeats the call.
- `common/dwr.c:4745` and `4751`: "the spare one" is wrong (finding 2). The second comment names only the quest and name menus, and I could not confirm what palette `0x1A67` is for.
- `common/dwr.c:4761`: the volatile comment sits stacked on another comment, and static variables do not need `volatile` across `longjmp`. This is open from review 2.
- `common/dwr.c:4965` to `4971`: three comments sit before the wrong function. `find_free_space_in`'s comment is now on `search_free_space`.
- `common/map.c:379`: `land_missing` still splits `map_find_land` from its doc comment, which has no `@return`.
- gcc warnings still there: `dwr.c:3389`, `3820` (indentation), `dwr.c:4425`, `stair_shuffle.c:185`, `323`, `521` (unused).

---

## Review 2: status

| # | Status |
|---|--------|
| 1, 2, 3, 7, 8, 9, 10, 11, 12, 13 | Fixed. I checked the code, and the fix for 3 passed the trace below |
| 4 | Fixed: retry limit at `map.c:1301`, and the `lm_sizes[-1]` read in `find_largest_lm` is gone |
| 5 | Partly: iNES header check and `k < 136`. Full CRC check left out on purpose |
| 6 | Left as it is on purpose (last-resort retry) |
| 14 | Left as it is on purpose. The shrink to 132 bytes is correct |
| 15, 16, 18, 19 | Still open, unchanged |
| 17 | Frame hook part done. The dead `CLC` is still at `dwr.c:4204`, and `addr_to_bcd` is still open |
| 20 | Partly done. See finding 15 |

## Checked and found correct

**Tested on the real ROM:**

- **Allocations against fixed writes:** I traced 62 flag sets in three modes (first fit, packed extras, packed everything). No fixed-address patch hit allocated code, and no computed write ran across an allocation's edge. Best fit no longer collides with the ring and necklace patches.
- **Music hook reservation:**
  - 100 dense flag sets with and without a pack: the same 78 built and 22 failed in both, and no checksum differed.
  - The pack never turned a working build into a failure.
  - Packing the extras did not rescue any of the 12 hook failures in the separate 80-set run, so the early give-up is not losing recoverable music.
- **Biomes and Safe Start:**
  - 360 builds (all biomes, three sizes, Safe Start on and off, 15 seeds each), of which 8 stopped on finding 8.
  - On every finished ROM, all places and the Rainbow Drop spot were reachable on foot from Tantegel, using the swamp cave link where needed.
  - Town and cave tiles were intact.
  - No swamp was left near the start with Safe Start on.
  - No bounds, overflow or shift trap fired in biome code.
  - Retries averaged under 0.5 per map.

**Checked by reading:**

- **Hook bytes:** `ys` 10, `rq` 71, `frame` 24, stub 64. All branch offsets land correctly, and the `BIT` skip in `frame` has no side effects.
- **Reservation:** holds with `$00`, restores `$FF`, and the real searches then get the same addresses.
- **Hurtmore Doors:** 132 bytes, loop branch, `found` branch and offset tables are right.
- **`persist`:** clobbers A and X, which is safe. `$F148` is `LDA #1 / JSR $C587 / JMP $F9DF` and takes no input. Code size 181 adds up.
- **Danger meter and `warp_list_load`:** branches land correctly. The meter cannot wrap, because it peaks at 136.
- **Run Bank command window:** the three layouts are 31, 26 and 32 bytes, and window 5 starts exactly 32 bytes after `0x709A`.
- **Window erase:** `$A7A2` reads a window's size from its own header, so the taller key and herb window needs no table change.
- **Retry control flow, `stair_shuffle` frees and `dwr_init` errors:** correct.

## What I could not review

- **No emulator was run**, as you asked. Findings about gameplay effects come from ROM bytes and disassembly. I relied on screenshots already in the scratchpad from an earlier session for finding 2, and did not retake them.
- **Not tested as gameplay:** low-HP border color, key and herb window at the top of the screen with deaths on (row 0 may lose its top line to overscan on a real TV), and the crash in finding 1.
- **Vanilla ROM:** review 2 said there was none, but `C:\ROMs\Dragon_warrior.nes` exists. I used it read-only.
- **Tools:** no `asm6f`, `ca65` or `ld65`, so `expansion.c` and `ft_driver.h` were not compared to their sources.
- **Read only where the randomizer touches them:** the FamiTracker driver, FamiStudio, `credit_music.c`, sprite data, `flute_music.c` data and `polyfit.c` tables.
- **Not re-read this round beyond recent changes:** most of `dwr.c` before line 3300 and `chaos.c`, `dungeon_maps.c`, `sprites.c`, `base32.c`. Review 2 covered these, and no uncommitted changes touch the last four.
- **Sampling:** rates come from random dense flag sets, not typical player flags. In the 100-set dense run, 22 builds failed for lack of space, with or without a pack. A clean scan does not prove a problem is absent.
- **Bounds checks:** the trap build covered bounds, signed overflow, divide by zero and shifts. Alignment checks were left off, because the code casts ROM bytes to `uint16_t` at odd addresses and traps at once. AddressSanitizer was not used, so heap overruns were not checked.
- **Tests and rules:** builds used default music, except the pack tests for findings 5 and 13 and the reservation check. The reduce-flashing rule did not come into play, since no ROM was run.
- **Scratchpad side effects:** my `dis.py` overwrote an earlier file of the same name, and test files remain in the `r3` folder. A safety hook blocked one heredoc command, so I wrote scripts to files instead.
