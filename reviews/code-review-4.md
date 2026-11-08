# DWR code review 4

Branch `feature/custom-music`. Requested at HEAD `f15b152`. HEAD moved to `aa6bade` while I worked, but that commit only adds docs and sprites: `git diff f15b152 aa6bade -- common cli expansion tools` is empty, so every line number here holds for both.

**Next action (2 minutes to read, 10 minutes to fix):** apply finding 1, then cast Return or use Wings on the overworld with a pack that has an Overworld song.

**How findings were checked**

- **Confirmed:** read from the code, and from a disassembly of real ROM bytes where the 6502 matters.
- **Suspected:** the logic is read from the code, but the outcome or how often it happens depends on play or data I could not run.
- **New / Repeat:** I formed these findings first. Only then did I read `reviews/code-review*.md` to mark repeats.
- No emulator was run. I built the CLI with gcc into the scratchpad, generated a ROM with a pack and Key and Herb on, and disassembled that. All ratings are effort: Easy (minutes), Medium (an hour or two plus testing), Hard (a day), Very hard (rewrite or deep testing).

## Ranked list

| # | Finding | Type | Severity | Checked | New? | Effort |
|---|---------|------|----------|---------|------|--------|
| 1 | Repeat of the current request plays game music over the custom song | Bug | High | Confirmed | New | Easy |
| 2 | Running in the first second of a fight leaves the custom song off | Bug | Medium | Logic confirmed, timing suspected | New | Easy |
| 3 | DPCM channel stays enabled and reads junk samples | Bug | Medium | Suspected | New | Easy |
| 4 | Run Bank, last spell and Steps are shared by all save slots | Bug | Medium | Confirmed | Repeat (review 3 #4, partly) | Medium |
| 5 | Copy Quest leaves the target slot with an old warp list | Bug | Medium | Suspected | New | Medium |
| 6 | Stale request plays an old song after power-on or reset | Bug | Low | Confirmed | Related to review 2 line 264 | Easy |
| 7 | Reserved-tag modules skip the size floor, and no module header is checked | Bug | Low | Confirmed | New | Easy |
| 8 | `read_key` leaves `key` unset on a bad quoted key | Bug | Low | Confirmed | New | Easy |
| 9 | Random bonk damage of 0 stops bonks being counted | Bug | Low | Confirmed | New | Easy |
| 10 | CLI cannot turn an option off | Bug | Low | Confirmed | New | Easy |
| 11 | Basement and shrines drop to game music, Hauksness does not | Bug or design | Low | Suspected | New | Easy |
| 12 | Credits take the only `Ending` song, so the ending has none | Bug or design | Low | Suspected | New | Easy |
| 13 | ROM files are still named `DWRando.` | Rule | Low | Confirmed | New | Easy |
| 14 | Empty `if` in `rotate_mirror_map` | Bug, harmless | Low | Confirmed | New | Easy |
| 15 | `gen_driver.py` does not check the driver RAM limit | Safety | Low | Confirmed | Repeat (review 1 #15), still open | Easy |
| 16 | `build_music.py` does not check the 8 KB credits song limit | Safety | Low | Confirmed | New | Easy |
| 17 | Same song restarts on every stair and at the throne room | Behavior | Low | Confirmed | New | Easy |
| 18 | Move non-pointer driver variables out of zero page | Speed, size | Info | Suspected figures | Extends review 1 #15 | Medium |
| 19 | Early exit test order | Speed | Info | Confirmed | New | Easy |
| 20 | PAL period table is built in but never used | Size | Info | Suspected figures | New | Easy |
| 21 | The `$400F` write in the wrapper looks redundant | Speed, size | Info | Suspected | New | Easy |
| 22 | Small byte savings in the patches | Size | Info | Confirmed | Partly repeat | Easy |
| 23 | Merge the `banks` table into `hi` | Size | Info | Confirmed | New | Medium |
| 24 | Song data is limited by a second driver copy | Size | Info | Confirmed | New | Very hard |
| 25 | Clarity list | Clarity | Info | Confirmed | Partly repeat | Easy |

## Bugs

### 1. Repeat of the current request plays game music over the custom song

- **Where:** `tools/custom-music/gen_driver.py:83-85`, `:108-109`, `:128-130`; `common/custom_music.c:801`, `:786`.
- **Problem:** the request hook sets `JG` ($6BFA) to 1 on every music request, and only the `start:` path clears it. When `WANT == CUR`, the wrapper takes `beq same` and keeps playing with `JG` still 1. With `JG` set, the `ys` hook stops redirecting the game's tracks to `$402x`, so they write the real APU.
- **Failure scenario:** with a pack that has an Overworld song, cast Return or use Wings on the overworld. `$DB31` jumps to `$B08D`, which falls through to `$B10E-$B113`: `ldx $45 / lda $B1AE,x / brk 04 17`, a request for id 5 again. `CUR` is already 5, so the game's overworld theme and the custom song fight over the same registers until the next request.
- **Also reached by:**
  - The warp whistle on the overworld. The flute restores map music at `$DDB9`, then the warp requests it again.
  - Stair Shuffle links between floors with the same id, such as two "Cave 1" floors.
- **Change:** clear `JG` whenever a song is playing, not only when one starts. Remove lines 108-109 and add to `same:`:

```
same:
 lda SONG
 bmi out
 lda #0
 sta JG
```

- **Benefit:** fixes the clash. 0 bytes net, +6 cycles on playing frames.
- **Risk:** low. `JG` stays 1 for the one frame of a bank change, when `SONG` is `$FF` and nothing is playing anyway. The battle sting still works, since `SONG` is `$FF` during the delay. Needs `ft_driver.h` regenerated, which needs cc65.
- **Effort:** Easy.

### 2. Running in the first second of a fight leaves the custom song off

- **Where:** `tools/custom-music/gen_driver.py:88-94`.
- **Problem:** while waiting out the 60-frame sting, the wrapper sets `SONG = $FF` but leaves `CUR` at the map's id. If the map request comes before the 60 frames end, `WANT == CUR` and `SONG` is negative. The early exit then takes that as "nothing to do".
- **Failure scenario:** a battle starts on the overworld (`WANT = $0E`, `CUR = 5`). The fight ends through `$E8A4` or `$EFD9` within 60 frames, which requests id 5. The game's overworld music plays instead of the custom song until the next map or battle. Reaching this in 1 second needs fast text and speed hacks, so it should be rare. I could not time it without running the game.
- **Change:** in the delay branch also store `$FF` to `CUR`, using the `lda #$ff` already there. Add `sta CUR` after line 93.
- **Benefit:** the map song restarts correctly. +3 bytes per bank.
- **Risk:** low. `WANT` is `$0E` during the delay, so the `WANT != CUR` path is unchanged.
- **Effort:** Easy.

### 3. DPCM channel stays enabled and reads junk samples

- **Where:** `tools/custom-music/gen_driver.py:135`, `:141`; driver `apu.s:389-393`, `:448`, `:462-464`.
- **Problem:** the wrapper sets `var_Channels` to `$F5` or `$FF`, and both have bit 4 set, so the driver's DPCM code runs. Packs are not checked for DPCM use (`custom_music.c:378-384`), and no sample data is placed. A DPCM note writes `$4012` and `$4013`, then `$1F` to `$4015`, so the DMC reads whatever is at `$C000+` in the fixed bank until the wrapper's `$0F` write on the next frame. The `$4011` write can also pop.
- **Failure scenario:** a song from a rip with a drum sample channel clicks on every hit. DMC DMA can also corrupt joypad reads. I did not check whether any of your packs use DPCM.
- **Change:** use `#$E5` and `#$EF`.
- **Benefit:** removes the clicks and DMA. 0 bytes. Also skips the DPCM path, saving a few cycles per frame.
- **Risk:** none for songs without DPCM. Songs with it lose their drums, which they do not have today in any useful form.
- **Effort:** Easy.

### 4. Run Bank, last spell and Steps are shared by all save slots

- **Where:** `common/dwr.c:4235-4237`, `:4246-4249`; `:4281-4283`.
- **Problem:** `$6804-$6808` are single WRAM values. They are cleared only when the loaded game has 0 XP, and there is no per-slot copy like the warp list has.
- **Failure scenario:** bank 5 runs in slot 1, then continue slot 2. Slot 2 now has those 5 runs, slot 1's last spell, and slot 1's step count in the credits.
- **Change:** save and load per slot the way `persist` and `warp_list_load` do. Three bytes per slot suffice for bank, last spell and points. Steps need two more.
- **Benefit:** correct saves. About 40 bytes of new code.
- **Risk:** medium. Test with Copy and Erase (see finding 5).
- **Effort:** Medium. Repeat of review 3 #4, still open. The Steps part is new.

### 5. Copy Quest leaves the target slot with an old warp list

- **Where:** `common/dwr.c:3393-3398`, `:4160-4167`.
- **Problem:** `SLOT_VISITED` and `SLOT_RETURN` change only at an inn, at a save, and on loading a 0 XP game. Nothing hooks the menu's Copy Quest, which I did not trace.
- **Failure scenario:** slot 2 once visited Rimuldar. Copy slot 1 over slot 2, then continue slot 2. The whistle can now warp to a town that game never visited, and Return can go to the wrong town.
- **Change:** hook the copy routine to copy the two bytes, or store the list inside the save data.
- **Benefit:** correct warps. About 15 bytes.
- **Risk:** low.
- **Effort:** Medium, mostly finding the routine.

### 6. Stale request plays an old song after power-on or reset

- **Where:** `tools/custom-music/gen_driver.py:74-81`.
- **Problem:** when `$03BF` is not `$5A`, the wrapper sets `CUR` and `SONG` to `$FF` but leaves `WANT` alone. `WANT` lives in battery-backed WRAM, so it survives a reset and is random on fresh RAM. `WANT != CUR` then starts that song.
- **Failure scenario:** press Reset in Kol. The reset clears `$03BF` (`$FDA9`), the wrapper restarts the town song, and it plays until the title's request. On fresh WRAM, a random id can pick any song.
- **Change:** add `sta WANT` after line 81. `JG` should also be set to 1 there so jingles before the first request stay audible: `lda #1 / sta JG`.
- **Benefit:** clean start. +3 bytes, or +8 with `JG`.
- **Risk:** none found.
- **Effort:** Easy. Review 2 line 264 noted the bank choice part only.

### 7. Reserved-tag modules skip the size floor, and no module header is checked

- **Where:** `common/custom_music.c:381-383`, `:244-262`.
- **Problem:** `swap_squares` is the only header check, and it runs only when `primary_square_chan` is 1. A song with a reserved tag has no minimum size.
- **Failure scenario:** a pack entry `data: "AAAA", uses: ["Inn"]` is placed as a 3-byte song. `ft_music_init` then follows pointers past its end, inside the NMI. The result is junk sound, and possibly a hang.
- **Change:** split a `module_ok()` out of `swap_squares` and call it for every song. Check the song list, header, frame list and each frame pointer.
- **Benefit:** a bad pack is refused at build time, not found in play.
- **Risk:** low. A pack that plays today has valid offsets.
- **Effort:** Easy.

### 8. `read_key` leaves `key` unset on a bad quoted key

- **Where:** `common/custom_music.c:121-126`, read at `:335`, `:372`, `:432`, `:510`.
- **Problem:** if `read_string` fails, `buf` is never written, and the callers run `strcmp` on an uninitialized array.
- **Failure scenario:** a pack ending inside an unterminated `"key`. The parse fails soon after, but this is undefined behavior first.
- **Change:** `buf[0] = '\0';` at the top of `read_key`.
- **Benefit:** removes the undefined read.
- **Risk:** none.
- **Effort:** Easy.

### 9. Random bonk damage of 0 stops bonks being counted

- **Where:** `common/challenge.c:135-140`, `common/dwr.c:2761-2762`.
- **Problem:** `setup_expansion` skips `INC_BONK_CTR` when the flag is on, since the damage code counts bonks. But when mode 5 rolls 0, `damage_bonks` returns without installing that code.
- **Failure scenario:** a seed rolling 0 of the 81 possible values shows Bonks 0 in the credits, however many bonks happen.
- **Change:** when the roll is 0, write a flag that `setup_expansion` checks, or add the bonk hook directly in that branch and have `setup_expansion` keep it.
- **Benefit:** correct stat.
- **Risk:** none. Do not change the roll range, since that changes seeds.
- **Effort:** Easy.

### 10. CLI cannot turn an option off

- **Where:** `cli/main.c:133`, `:137-146`.
- **Problem:** `--key-herb off` and `--quick-spell off` only skip the `|=`. `--encounters default` alone and `--bank-level normal` alone do not enter the block at all.
- **Failure scenario:** a flag string with Key and Herb on plus `--key-herb off` builds with it on and gives no message.
- **Change:** start these at -1 for "not given", then set or clear the bit whenever the option was given.
- **Benefit:** options do what they say.
- **Risk:** none.
- **Effort:** Easy.

### 11. Basement and shrines drop to game music, Hauksness does not

- **Where:** `common/custom_music.c:725-729`, `:804-810`.
- **Problem:** the hook remaps the Tantegel Basement and both shrines to `$1D`, and Hauksness to `$1C`. `$1C` falls back to `map[9]`, while `$1D` has no fallback.
- **Failure scenario:** in the ROM I built, `map[$1C] = 0` and `map[$1D] = $FF`. With a Castle song, walking down from Tantegel switches to the game's castle theme. This may be what you want, so I list it for a decision.
- **Change:** if not intended, add `map[REQUEST_OLD_MAN_CAVE] = map[3];` beside line 726.
- **Benefit:** consistent music.
- **Risk:** none.
- **Effort:** Easy.

### 12. Credits take the only `Ending` song, so the ending has none

- **Where:** `common/custom_music.c:640-641`, `:685`.
- **Problem:** the Credits event accepts `Credits` or `Ending` and is picked first, then the song is marked used.
- **Failure scenario:** a pack with one song tagged `Ending` plays it in the credits. Request `$10` keeps the game's music.
- **Change:** if not intended, have Credits look for `Credits` first and take `Ending` only after the ending event has had its pick. Keep the bank 6 placement for the credits song.
- **Benefit:** tags act as named.
- **Risk:** low. Song choice changes only for packs in this position.
- **Effort:** Easy.

### 13. ROM files are still named `DWRando.`

- **Where:** `common/dwr.c:4832`.
- **Problem:** the project rule says output files use DWR. My build wrote `DWRando.12345.<flags>.nes`.
- **Change:** `"%s/DWR.%"PRIu64".%s.nes"`.
- **Benefit:** follows the rule.
- **Risk:** check any script that globs on the old prefix.
- **Effort:** Easy.

### 14. Empty `if` in `rotate_mirror_map`

- **Where:** `common/dungeon_maps.c:292-295`.
- **Problem:** with the stray line, the code reads `if (map_index == SWAMP_CAVE) if (!rotatemirror) return;`. Other maps with a roll of 0 do the whole rewrite for nothing.
- **Change:** delete line 292.
- **Benefit:** clarity, and a small speedup at build time. The ROM bytes do not change, because a roll of 0 rewrites the same tiles.
- **Risk:** none.
- **Effort:** Easy.

### 15. `gen_driver.py` does not check the driver RAM limit

- **Where:** `tools/custom-music/gen_driver.py:24`.
- **Problem:** the linker allows RAM up to `$6FFF`, but the wrapper uses `$6B00-$6B6D` and `$6BF0-$6BFF`, and the credits use `$6F00-$6F02`. A driver option that grows the variables overlaps them with no error. Today's image looks fine: operand scanning finds no driver RAM reference at or above `$6B00` other than the shadow and `$6BF0`.
- **Change:** set the RAM size to `$200`, so the linker itself refuses. That is one character group on line 24.
- **Benefit:** a build error in place of corrupted music.
- **Risk:** none.
- **Effort:** Easy. Repeat, from review 1 #15, still open.

### 16. `build_music.py` does not check the 8 KB credits song limit

- **Where:** `tools/build_music.py`, written into `credit_music.c` at line 18959; `expansion.c` fills from `expansion[0x6000]`.
- **Problem:** `add_music` copies the song to `expansion[0x4000]` with no check. Code starts 8 KB later at `$A000`. The largest song today is `$1C7F`, so nothing is wrong yet.
- **Failure scenario:** a song over `$2000` bytes overwrites the start of the credits code on the seeds that pick it.
- **Change:** raise an error in `Music.assemble` if `len(self.music) > 0x2000`.
- **Benefit:** catches it when the song is added.
- **Risk:** none.
- **Effort:** Easy.

## Behavior

### 17. Same song restarts on every stair and at the throne room

- **Where:** `tools/custom-music/gen_driver.py:83-127`.
- **Problem:** the wrapper compares request ids, not songs. Every dungeon floor (`$06-$0C`) can map to one song, as can castle and throne room (`2`, `3`). Each change of id restarts the song from its start.
- **Failure scenario:** the Final Fantasy 2 pack I built with maps ids 6 through `$0D` to song 0, so every stair restarts it.
- **Change:** after `tay` and clearing `JG`, add `cpy SONG / beq keep`, where `keep:` is `lda WANT / sta CUR / jmp same`. The sting delay sets `SONG` to `$FF`, so battle songs still start fresh.
- **Benefit:** seamless music. +14 bytes per bank. It also skips about one `INIT` per stair.
- **Risk:** low. If you prefer the restart, skip this.
- **Effort:** Easy.

## Speed and size

### 18. Move non-pointer driver variables out of zero page

- **Where:** `tools/custom-music/driver/driver.s:190-203`, `:284-288`; `gen_driver.py:46-53`.
- **Problem:** the swap costs 1,022 cycles per playing frame and 730 bytes per bank (I counted the generated loops: 19 scratch bytes at 7+7 cycles, 27 kept bytes at 14+14). Only indirect pointers need zero page: `var_Temp_Pointer`, `var_Temp_Pointer2`, `var_Temp_Pattern`, `var_Note_Table` and probably `var_Temp16`. `$E5-$FD` holds length counters, DPCM state, `var_ch_Harmonic` and `var_ch_PhaseReset`, and `$DF-$E4` is the division work area.
- **Change:** move those under `.segment "BSS"`. ca65 will flag any `sty var,x` or `stx var,y`, which have no absolute form. Then trim `ZP_SCRATCH` and `ZP_KEPT`, and `FA` is no longer needed, since `$FA` stays the game's.
- **Benefit:** moving `$E5-$FD` alone removes 25 × 28 = **700 cycles per frame** and 500 bytes. Each access then costs 1 more byte and 1 more cycle, so the net will be lower. I could not measure that here without cc65.
- **Risk:** medium. Listen to full songs after the change, with effects that use harmonics.
- **Effort:** Medium. This is the largest speed win left, and extends review 1 #15.

### 19. Early exit test order

- **Where:** `tools/custom-music/gen_driver.py:49`, `:72-73`.
- **Change:** test `SONG` first (`lda SONG / bpl full`), and move `lda $fa / sta FA` to just after `full:`.
- **Benefit:** **10 cycles per frame** while a song plays, **7** on the idle path. 0 bytes.
- **Risk:** none. `FA` is only read on the full path.
- **Effort:** Easy.

### 20. PAL period table is built in but never used

- **Where:** `driver.s:77-79`, `periods.s:15`, `init.s:196-214`, `player.s:1307-1322`; `gen_driver.py:126` passes `X = 0`.
- **Change:** do not define `PAL_PERIOD_TABLE`.
- **Benefit:** about 192 bytes per bank for the table (estimate: 96 notes × 2), plus its branches. Those bytes go to songs.
- **Risk:** low. The wrapper always selects NTSC already.
- **Effort:** Easy, needs cc65.

### 21. The `$400F` write in the wrapper looks redundant

- **Where:** `tools/custom-music/gen_driver.py:143-144`; driver `apu.s:372-378`.
- **Problem:** with `var_ch_LengthCounter` at its default of 8, `and #$03` is 0, so the driver writes `$400F` itself each frame noise is active. The wrapper writes it again on the idle path.
- **Change:** remove the 2 lines, or move them inside the `G` branch so they run once after a sound effect.
- **Benefit:** 6 cycles per frame and 5 bytes per bank.
- **Risk:** medium. The line was added for a noise problem, per `test-roms/DWR-TMNT-noise.nes`. Listen for noise after a sound effect ends.
- **Effort:** Easy.

### 22. Small byte savings in the patches

| Where | Change | Saves | Status |
|-------|--------|-------|--------|
| `dwr.c:4210` | Delete `0x18` (`CLC`). The loop only exits through `BCC`, so carry is clear. Fix size and branch. | 1 byte, 2 cycles | Repeat, open since review 2 |
| `dwr.c:4043` | Delete the `0xEA`. Fix the size, the `F0 1A` branch and the name table address. | 1 byte, 2 cycles | New |
| `custom_music.c:801` | `0xE8` (`INX`) in place of `0xA2, 0x01`. X is 0 there. Fix the `0x3D` branch and the size. | 1 byte | New |

- **Risk:** low. Every one changes a hand-counted branch, and the size passed to `vpatch`.
- **Effort:** Easy.

### 23. Merge the `banks` table into `hi`

- **Where:** `gen_driver.py:110-113`, `:121-124`, `:158`; `custom_music.c:733-737`, `:776`.
- **Change:** song addresses are `$80-$BF`, so bit 6 of `hi` can hold the bank. Drop the 24-byte `banks` table.
- **Benefit:** about 18 bytes per bank, after about 6 bytes of masking.
- **Risk:** low.
- **Effort:** Medium, for a small gain. Skip unless you are short of room.

### 24. Song data is limited by a second driver copy

- **Where:** `gen_driver.py:5-8`, `custom_music.c:770-777`.
- **Problem:** the driver reads songs through the `$8000` window, so it sits in the same bank as the songs, and 7,853 bytes are spent twice.
- **Change:** move the driver's data reads behind one routine in the fixed bank, or put the driver in WRAM.
- **Benefit:** 7,853 bytes for songs.
- **Risk:** high. Every `lda (ptr),y` in the driver gets slower, and fixed-bank room is short (851 bytes free in the build I checked).
- **Effort:** Very hard. I do not recommend it.

## Clarity

### 25. Clarity list

Effort for all: Easy. Risk: none, unless noted.

- `custom_music.c:751`: sizes `{10, 71, 24}` repeat the numbers at `:785`, `:794` and `:815`, and the search runs twice. Use named constants and reuse `at[k]`. **New.**
- `custom_music.c:28` and `:567`: `FIRST_JINGLE` is `0x10`, while the comment says below `$11`. **New.**
- `gen_driver.py:20`: writes `driver2.s` into the source tree, though the usage says to run from a scratch directory. It is gitignored. Write it to the working directory and use `-I`. **New.**
- `gen_driver.py:35`: `NAMCO_CHANNELS=8` does nothing without `USE_N163`. **New.**
- `tools/custom-music/build_music_test.py` and `driver.cfg`: an older layout, and it would break the current ROM. Delete. **Repeat** (review 2 line 307).
- `map.c:280`: `map_land_mass` is unused (gcc warns). `map.c:219-230` and `stair_shuffle.c:210-243`, `:248-254`, `:588-698` are dead. **Partly repeat.**
- `stair_shuffle.c:14-15`: redefines `CHEST_COUNT` and `ROM_SIZE`. **New.**
- `dwr.c:3329` and `map.c:202`: two copies of `tile_is_walkable`. **Repeat** (review 2 line 294).
- `dwr.c:3369`: the `#define` lines separate `zoom_and_whistle` from its doc comment. **New.**
- `dwr.c:1738` and `:1746`: comments say `JSR` for a `JMP`, and `bne` for a `BEQ`. **New.**
- `notes/free_space.txt`: does not list the spike rewrite gaps (`$CD63`, `$E929`, `$E97C`), and calls bank 3's code limit `$C4F5`, while the code uses `$C422`. **New.**
- gcc also warns at `dwr.c:3199` (`chosen_NPC`, guarded, so it is a false alarm) and at `:2513`, `:2518`, `:2690` (truncation, on purpose). Adding `chosen_NPC = 25` would quiet the first. **New.**

## Status of 18-25 (2026-11-07)

- **Done:** 18, 19, 20, 21 (the `$400F` write now runs once, after a sound effect), 23.
- **Dropped by the owner:** 22, 24, 25.

## Checked and found correct

These are the recent areas you asked about. No change needed.

- **Zero page split:** driver layout from `driver.s:179-192`. `$D0-$DB` and `$DE-$E4` are work bytes that the driver sets before use within a frame. `$DC-$DD` is `var_Note_Table`, set only when a song loads, and `$E5-$FD` holds song state. So one-way scratch and two-way kept is right. The disassembled image matches the generator.
- **Early exit:** cannot skip a pending song, because every delay or bank path leaves `WANT != CUR`. Finding 2 is the one exception, through a new request. Costs 37 cycles.
- **Sound gating:** the frame hook sits after `$FEE3 lda $6005 / bne`, so the driver never runs while a request is being set up. `X` from the request hook is safe, because `$81A5` does `TAX` right after.
- **Reset marker:** `$03BF` cannot be reached by the PPU buffer, since `$C692` caps it at `$B2`. Reset clears it at `$FDA9`.
- **Credits stub:** 52 bytes, and offsets 21, 22, 29, 30 and 32 are right. The stub exists at the same address in banks 5 and 7. `sw` ends in the same `RTS` in both. The credits song is always placed in bank 6, because it is placed first. Credits clear zero page, so `$FA` reads 0. The credits main loop does no mapper writes, since the only `INSTR_LOAD_CHR` use is commented out at `exp_bank_4.asm:422`, so the new NMI mapper writes have nothing to collide with.
- **`rq` and `ys` hooks:** 71 and 10 bytes. All branches land correctly.
- **`key_herb_counter`:** I generated a ROM and disassembled it. `$EE7D` is `jsr $C45B / nop / nop`, and vanilla there is `lda #$0B / jsr $A7A2`. All normal fight exits pass through it: wins at `$EA8D` and `$EB18`, enemy flees at `$EFE1`, and running at `$E943` into `$EE5A`. A spike run reloads the map through `$B097`, so nothing is left behind. `$64DF` is the code's low 3 bits (`$A2F4`), so `$9A` gives 2 and `$9B` gives 3. `$BD,X` reaches keys and herbs. Keys and herbs cap at 9, so one digit is enough.
- **`death_counter`:** `DISPLAY_DEATHS` keeps `$64DF == 0` going to gold at `$A8BA`, and the chain from `key_herb_counter` handles both cases.
- **`danger_meter`:** 67 bytes, all branches right. All tile types reach `$CDFD` with a mask, and the `$CE63` patch branches there correctly. The meter peaks at 136, so it cannot wrap.
- **`spike_rewrite`, `find_spike`:** all three call sites, table offsets, the 27-byte search and the `N` flag return are right. `$E98C` keeps its 3 NOPs for `COUNT_WIN`.
- **Fighter's ring:** the `ADC #2` has no `CLC`, but `$F0FA` runs `clc / adc $cd` just before. Carry would need defense over 255, and it peaks near 175.
- **Token text:** `dwr_token_dialogue` truncation always ends at the original `$FC`. The snprintf warnings are harmless.
- **Other patches counted by hand with correct sizes and branches:** run mechanics, builds, radish, hurtmore doors, zoom and whistle, warp load, run bank (all parts), forfeit, search and take, show spells, chaos running, bonks.
- **`key_access`:** set to 0 in `stair_shuffle_init`, so `malloc`'d `dw_rom` is fine there. `base32_decode` stays inside 25 bytes.

## What I could not review

- **No emulator.** Nothing here was heard or seen in play. All audio outcomes come from reading the code.
- **No cc65 or asm6f.** I could not regenerate `ft_driver.h` or `expansion.c`, so neither is proven to match its source. I did disassemble the wrapper from `ft_driver.h` and compared it by eye. Figures in findings 18 and 20 are estimates.
- **Read only where the randomizer touches them:** the FamiTracker driver (`*.s`), `famistudio_asm6.asm`, `exp_bank_4.asm` (instructions and NMI only), `polyfit.c`, `crc64.c`, `mt19937-64.c`, `tools/ips.py`, `tools/polyfit.py`, `patchgen.py`.
- **Data not checked line by line:** `credit_music.c`, song lists in `flute_music.c`, sprite data in `sprites.c`, the winter theme bytes at `dwr.c:2575-2673`, the tail of `expansion.c`.
- **Not traced:** Copy Quest (finding 5), and the rare vanilla race when NMI lands inside a game bank switch (`$FEF6-$FF0D` handles part of it). The music hook adds more mapper writes to the NMI but does not change that exposure as far as I can read.
- **Out of scope:** `webui/`, `electron/`, `site/`, `hud.lua`.
- **One build only:** a single seed and flag set, with a pack and Key and Herb on. No sweep across flag sets.
- **Base ROM:** I found the vanilla ROM at `C:\ROMs\Dragon_warrior.nes` after reading review 3, and used it read-only. All findings had been formed by then using a built test ROM.
