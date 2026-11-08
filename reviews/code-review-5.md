# DWR code review 5

Branch `feature/custom-music`, HEAD `bd44a67` plus the uncommitted driver change. Items from review 4 are not repeated.

**Next action (10 minutes):** apply finding 1. Then, on a Free Charlock ROM, walk off the Charlock surface and confirm you land outside.

**How findings were checked**

- **Confirmed:** read from the code and from real ROM bytes (vanilla or a generated ROM), or shown by a build.
- **Suspected:** the logic is read, but the outcome depends on play I could not run.
- No emulator was run. All builds were CLI builds with default music on. Experiments were compiled from a copy in my scratchpad, so the repo is unchanged apart from this file.
- Effort: Easy (minutes), Medium (an hour or two plus testing), Hard (a day), Very hard (rewrite or deep testing).

## Ranked list

| # | Finding | Type | Severity | Checked | Effort |
|---|---------|------|----------|---------|--------|
| 1 | Free Charlock: walking off the Charlock surface no longer leaves Charlock | Bug | High | Confirmed (table), play effect partly Suspected | Easy |
| 2 | Free Charlock with Short Charlock: the flag is dropped, with the wrong message and no notice | Bug | Medium | Confirmed | Easy |
| 3 | Free Charlock drops out on plausible flag sets, the site says nothing, and the checksum stays the same | Bug or design | Medium | Confirmed | Easy to Medium |
| 4 | A repeated or kept song can play wrong square pitches after a request | Bug | Medium | Logic Confirmed, audibility Suspected | Easy |
| 5 | Auto Return with Return to Town lands at the last town, not Tantegel | Bug or design | Low | Confirmed | Easy |
| 6 | `module_ok` still under-checks modules | Safety | Low | Confirmed | Easy |
| 7 | Site `fits` undercounts hand-picked floors and misorders one credits case | Bug | Low | Confirmed | Easy |
| 8 | `var_Triangle_Trill` is not reset when a song starts | Bug | Low | Suspected | Easy |

**Status (2026-11-07):** 1, 2, 4, 5 (text), 6, 7 and 8 are fixed, and the site notice from 3 is in. On 3, music stays ahead of Free Charlock when space is short (user decision).

## Findings

### 1. Free Charlock: walking off the Charlock surface no longer leaves Charlock

- **Where:** `common/dwr.c:4009` (the warp edit). Vanilla code is bank 0 `$B219-$B23B`.
- **Problem:** walking off a map runs a search of `warps_to` for the first row whose map is the current map, then warps to that row's `warps_from` (`$B22A-$B23B`). Row 6 is the only row in a normal build with `warps_to.map == CHARLOCK`. `free_charlock` changes that row to the throne room, so the search now fails or finds the wrong row.
- **Evidence from generated ROMs:**

| Flags | Walk off map 2 before | Walk off map 2 with Free Charlock |
|-------|----------------------|-----------------------------------|
| No Stair Shuffle (seeds 12345, 777) | Row 6, overworld | No match, `RTS`, no warp |
| Stair Shuffle, seed 12345 | Row 6, overworld | Row 24, warps to map 18 at 8,1 |
| Stair Shuffle, seed 777 | Row 6, overworld | Row 36, warps to map 25 at 11,2 |

- **Failure scenario:**
  1. Enter Charlock with no Wings and no Return, and run from the Dragonlord.
  2. Climb the stairs to the surface and walk out the south side.
  3. With no shuffle, nothing happens. The hero keeps walking outside the map. Tile effects are skipped because `$B21D` and `$B223` branch away from `$CBF7`. With a shuffle, the hero lands back inside a cave.
  4. The way out is Return or Wings on the surface, or Outside from the caves or throne room, or dying, or beating the Dragonlord. Outside fizzles on map 2 (`$DAD1-$DAD9`), and Return fizzles in the throne room (`$DAF7`).
- **Also reached after a win:** without Auto Return, go back in for the throne room chests and walk out.
- **Change:** add a small bank 0 hook, used only when `warps_to` was changed (`!SHORT_CHARLOCK`, see finding 2).

```c
/* Walking off Charlock still leads outside. Replaces ldx #0 : lda $45 at $B228. */
int w = dwr_try_free_space(rom->content, 0xc422, 18);
vpatch(rom, w, 18,
    0xa2, 0x00, 0xa5, 0x45,              /* ldx #0 : lda $45 (replaced code)  */
    0xc9, CHARLOCK, 0xd0, 0x07,          /* not Charlock: normal search       */
    0xa2, 0x12, 0xa9, 0x02,              /* ldx #$12 : lda #2                 */
    0x4c, 0xe2, 0xd9,                    /* jmp $d9e2                         */
    0x4c, 0x2c, 0xb2);                   /* jmp $b22c                         */
vpatch(rom, 0x3228, 4, 0x4c, LO(w), HI(w), 0xea);
```

- **Benefit:** the normal way out works again. 18 bytes, and only on an off-map step, so no per-frame cost.
- **Risk:** low. It uses the same warp and `A = 2` that vanilla `$B239` uses, and the same stack shape. Reserve the 18 bytes together with the 57, so both go in or neither does.
- **Effort:** Easy.

### 2. Free Charlock with Short Charlock: the flag is dropped, with the wrong message and no notice

- **Where:** `common/dwr.c:3999-4007`; `site/index.html:745`.
- **Problem:** `short_charlock` has already set row 6 to the throne room, so the test `warps_to[i].map == CHARLOCK` never matches and `found` stays -1. The build prints "No room for Free Charlock" though there is room. The site lets both flags be on.
- **Failure scenario (Confirmed by build):** flags `IVIAAVCEKACACAAAAABAAEAUAAQBAAAAABYCAAAA`, seed 12345. `$CA56` is still the vanilla `a5 da f0 12`, and row 6 leads to throne room 10,29. The player asked for Free Charlock and gets none. Short Charlock as a "maybe" flag does the same at random.
- **Change, better option:** make them work together. Use `WARP_CHARLOCK` directly, since row 6 stays the overworld entrance in every build I dumped. Set only `x` and `y`, and set `map` only when not Short:

```c
dw_warp *w = &rom->map.warps_to[WARP_CHARLOCK];
w->map = CHARLOCK_THRONE_ROOM; w->x = x; w->y = y;
```

With Short, `map` is already the throne room, so walking off that floor keeps working and the hook from finding 1 is not needed. Test this: I have not played it.

- **Change, simpler option:** add `shortcharlock:'freecharlock'` to `CONFLICT_WITH` and clear `flags[7] & 0x03` in `update_flags`.
- **Either way:** split the message, so "No room" is printed only when `c < 0`.
- **Benefit:** the flag does what it says, or cannot be chosen. 0 bytes.
- **Risk:** low. Clearing `flags[7]` changes builds for anyone using both today, but they have no working Free Charlock now.
- **Effort:** Easy.

### 3. Free Charlock drops out on plausible flag sets, the site says nothing, and the checksum stays the same

- **Where:** `common/dwr.c:4003-4007`, `:5053`; `site/index.html:883`, `:811`.
- **Problem:** the code needs 57 contiguous bytes, and it runs after the music hooks and sprites. `docs/FLAGS.md:71` says full ROMs leave it out. Three gaps remain:
  - The site's result dialog only reports music. A player cannot tell the flag is gone.
  - The warp edit and hook run after the checksum at `:5018`, so two racers with the same seed and flags can get different gameplay, with matching checksums, if their packs differ.
  - The flag set that loses it is not extreme.
- **Failure scenario (Confirmed by build):** Castlevania pack, seed 4242, flags `IVIAAV2EKACQAEAAAADQAEBUAAQJAAAQABYCAUAA`: Free and Auto Return, Menu Wrap, Run Bank, Quick Spell, Key and Herb, Death Counter, Encounter Rate, Return to Town, Warp Whistle, Hurtmore Doors. The log says "No room for Free Charlock" with 273 bytes free, largest gap 16.
- **Cumulative test (Castlevania pack, seed 4242; each row adds a flag to all rows above it):**

| Step added | Free bytes, largest gap | Lost |
|-----------|------------------------|------|
| Encounter Rate | 433, 92 | nothing |
| Return to Town | 406, 117 | name wrap |
| Warp Whistle | 346, 54 | name wrap |
| Hurtmore Doors | 273, 16 | Free Charlock, name wrap |
| Return Escapes | 339, 29 | music hooks, name wrap |
| Unbreakable Keys | 315, 42 | music hooks, Free Charlock, name wrap |
| Build Options | none | build fails: "Not enough space for these 127 bytes" |

Free Charlock returning at the Return Escapes row is not a recovery. The music hooks failed first, which left it room. Adding a flag can swap which feature survives.

- **Random flag sweep (150 builds each, every build with Free Charlock, Auto Return and Menu Wrap, densities 20-90%):**

| Pack | Free Charlock lost | Music hooks lost | Name wrap lost | Build failed |
|------|-------------------|------------------|----------------|-------------|
| None | 9 | n/a | 61 | 7 |
| Final Fantasy 2 | 31 | 33 | 91 | 8 |

- **Experiments on a scratch copy:**

| Experiment | Result | Verdict |
|-----------|--------|---------|
| Ask for the tighter packing pass on no room (as the credits stub does) | Free Charlock lost 29-31 of 150, music hooks 31 | Does not help. Pre-checksum code has already split the gaps, and it must stay first-fit. |
| Split `name_entry_wrap` into its 4 independent pieces | No row of the two flag ladders I compared gained it | Not worth doing |
| Run `free_charlock` before `custom_music_apply` | Free Charlock lost 9, music hooks lost 54 | Works, at the music's cost. Your call. |

- **Change, in order of value:**
  1. Site: add `if(last.some(t=>t.includes('No room for Free Charlock'))) lines.push({text:'Free Charlock did not fit in this ROM, so Charlock plays as normal.'})`, and add `freecharlock` to `SPACE_FLAGS`. The same for name wrap if you want it.
  2. Decide the priority: gameplay flag over music, by moving the call before `custom_music_apply`.
  3. For races, move it into `apply_stuff_to_rom` so the checksum covers it, and treat no room as `dwr_no_room()`. Then a seed either has the flag or fails with the existing space message. Checksums change only for seeds with Free Charlock on.
- **Benefit:** the player is told, and races stay fair. Under 200 bytes of site text, 0 ROM bytes.
- **Risk:** low for 1. For 2 and 3, music is lost more often on full flag sets, and option 3 can turn silent drops into failed builds.
- **Effort:** 1 is Easy, 2 is Easy, 3 is Medium.

### 4. A repeated or kept song can play wrong square pitches after a request

- **Where:** `tools/custom-music/gen_driver.py:116-121` (keep path), `:144-148` (`same:`); `common/custom_music.c:836`; driver `apu.s:179-192`.
- **Problem:**
  1. The request hook sets `JG = 1`.
  2. On the next NMI the game's sound code runs before the wrapper, with `JG` still 1, so its tracks write the real registers. A new note writes `$4002,y` and `$4003,y` (`$80A8-$80AE`).
  3. The wrapper then clears `JG` and calls `PLAY`.
  4. A new song is safe, because `INIT` sets `var_ch_PrevFreqHigh` to `$FF`. The keep-song path and the `WANT == CUR` path, both from `de195e3`, skip `INIT`.
  5. The driver rewrites `$4002` every frame, but writes `$4003` and `$4007` only when the high period differs from its cache. So the game's high byte stays.
  6. The triangle is not affected, since the driver writes `$400B` each frame with the default length setting.
- **Failure scenario:** a pack with one Dungeon song. Take stairs between floors. The new request's first tick writes the game's period high byte, and square 1 or 2 plays at a wrong pitch until the driver's high byte changes or a note-off resets the cache (`player.s:566`). The same happens on Return, Wings or a warp while the same song plays.
- **How often:** the tick fires only if the tempo add carries (`$805F-$8066`), so with a tempo byte near `$78` that is most requests. I have not heard this, and could not without running the game.
- **Change:** replace the 2 lines at `:147-148`:

```
 lda JG
 beq nj
 lda #$ff          ; a request let the game's track write $4003/$4007
 sta PF
 sta PF+1
 lda #0
 sta JG
nj:
```

Then regenerate `ft_driver.h`, set `BANK_ROOM` to the new room, and rebuild the wasm. `asm6f` is needed, and I did not find it under `C:\Users\Ryan\tools`.

- **Benefit:** the kept song stays in tune. +13 bytes per bank, +1 cycle on playing frames.
- **Risk:** low. Each request causes one `$4003` and one `$4007` write, which resets those squares' duty phase, a very small tick at most.
- **Effort:** Easy.

### 5. Auto Return with Return to Town lands at the last town, not Tantegel

- **Where:** `common/dwr.c:3979`, `:3442`; `site/index.html:373` (flag text); `docs/FLAGS.md:66`; `README.md:18`.
- **Problem:** with Return to Town on, `$DB04` is `lda $6800 : jsr <zoom code>` (dumped from a build), so the tail uses the last town. The text says "you arrive in front of Tantegel". Warp Whistle alone is fine: index 0 gives 35,13, the same as the plain Return point. The comment at `:3420` says "Tantegel throne room", but `$45` stays 1, so it is the overworld.
- **Failure scenario:** save in Cantlin with Return to Town on, then beat the Dragonlord. You land by Cantlin and walk to Tantegel for the ending. No softlock.
- **Change, one of:**
  - Change the text to "in front of Tantegel, or your Return to Town point".
  - Add a 9-byte stub when `RETURN_TO_ZOOM`: `a9 01 85 45 a9 00 4c 07 db` (map 1, index 0, enter at `$DB07`), and aim `$EA03` at it.
- **Benefit:** text and game agree.
- **Risk:** none for the text. The stub does not touch the saved Return index.
- **Effort:** Easy.

### 6. `module_ok` still under-checks modules

- **Where:** `common/custom_music.c:250-272`; driver `init.s:431-458`.
- **Problem:**
  - It accepts `count == 0`.
  - It requires only 4 bytes at each frame pointer, while the driver loads 5 channel pointers, which is 10 bytes.
  - It requires 4 bytes of song header. The fields used at load are longer. I did not count the exact length.
  - It does not check pattern pointers.
- **Failure scenario:** a corrupt or truncated module passes the check and the driver follows pointers into the next song, inside the NMI. Packs made by the normal tools should not hit this.
- **Change:** refuse `count == 0`, use `+ 10` for frames, and test all 5 pattern pointers `< size`. `swap_squares` needs no change.
- **Benefit:** bad packs are refused at build time. 0 ROM bytes.
- **Risk:** low. A valid module passes. Run it across your packs and compare the song count before and after.
- **Effort:** Easy.

### 7. Site `fits` undercounts hand-picked floors and misorders one credits case

- **Where:** `site/index.html:616-617`, `:602`, `:561`, `:631`.
- **Problem:**
  - Hand-picked floors with the same song count once (`'f'+x.song`). `add` writes one copy per floor unless that song is `main`, and `main` is not set unless every floor has a song. Random then fills room that is not really free.
  - The `dungeon` fallback at `:626` adds copies the same way when some floors are left on Default.
  - With Skip Original Credits on and only an Original Credits song, C places that song first (`custom_music.c:719`), while `PLACE_ORDER` has `Ending` ninth. First-fit can then split differently.
- **Failure scenario:** floors 1 and 2 share song A and floors 3 and 4 share song B, with floors 5 to 7 on Default. The site counts two songs and C places four. Floors are placed last, so they fall to the game's music. The result dialog does list them, so the player is told.
- **Change:** count one entry per floor copy, skipping only `main`. Move an `Ending` entry to the front of the sort when Skip Original Credits is on.
- **Benefit:** Random fills what really fits, and fewer slots lose their song.
- **Risk:** low. It changes song choice only for these setups, and the same seed may pick differently.
- **Effort:** Easy.

### 8. `var_Triangle_Trill` is not reset when a song starts

- **Where:** driver `player.s:1034`, `:1127`, `apu.s:272`; `init.s` has no write to it. `var_Linear_Counter` is set at `init.s:36`.
- **Problem:** the value carries over from an earlier song using the trill effect, or from random WRAM on first power-up. This is not a regression, since the old kept zero page bytes were restored from WRAM too.
- **Failure scenario:** after such a song, a new song skips the linear counter update at `player.s:1034-1041` and writes an extra `$400B` when its triangle is killed. Expect small triangle differences at most. I have not traced whether it is audible.
- **Change:** add `sta var_Triangle_Trill` beside `init.s:36`, using a zero value, with a `.replace` in `gen_driver.py` or an edit to the driver source.
- **Benefit:** songs start the same each time. +3 bytes per bank plus the zero load.
- **Risk:** none found.
- **Effort:** Easy. Do it only when regenerating for finding 4.

## Checked and found correct

**Uncommitted driver change**

- `ft_driver.h` matches its source: my ca65 and ld65 run of `gen_driver.py`'s driver half gives 6,712 bytes equal to the start of `ft_image`. Wrapper bytes are in banks 3 and 6 of a built ROM and in `site/dwrandomizer.wasm`.
- **WRAM layout, no overlap:** BSS is `$6900-$6A44`, ending `$BB` bytes under the `$6B00` swap area. The swap uses `$6B00-$6B0B` and `$6B4C-$6B4D`. The wrapper uses `$6BF0-$6BFE`. Other users are `$6800-$680B`, `$6810-$6815` and `$6F00-$6F02`.
- **Save data:** vanilla saves sit from `$6068` (`$F71A-$F72E`), under `$6500`.
- **Vanilla game:** no real access to `$6900-$6A44`. The linear-scan hits on `$6918` are misaligned `clc : adc` data, which I disassembled at `$C2B4`, `$C99D`, `$CC4D` and `$D1D8`.
- **Nothing still needs `$DE-$FD` swapped:** the driver's zero page is now `$D0-$DD` only, and `ld65` would refuse overflow since ZP is `$0E` bytes. `$DC-$DD`, used by spikes in the game, stay in the kept pair. `$FA` is read before `PLAY`, which does not change it.
- **Moved variables:** `ca65` would refuse any `sty var,x` or `(var),y` on them, and it assembled. No moved variable relies on zero page index wrap.
- **Bank bit:** `hi` values are `$80-$BF`, so bit 6 is free. `and #$bf` restores the address. `BANKSEL` values 0 and `$40` work with the frame hook, which only tests zero. A stale `BANKSEL` corrects itself in one frame, and the wrapper is in both banks. `Map = songs - 80` is right (32+24+24). `BANK_ROOM` 8945 equals `$C000 - $9D0F`.
- **`$400F`:** the driver writes it itself by default, and the `G` branch restores it after a sound effect.
- **Cost:** swap is now 224 cycles per playing frame and 160 bytes, by my count of the generated loops (12 scratch bytes at 14 cycles, 2 kept at 28).
- **Reset and stale WRAM:** reset clears `$0300` and zero page (`$FDA7-$FDA9`), so the wrapper sets `CUR`, `SONG` and `WANT` to `$FF` before any `PLAY`. Stale `$6B4C-$6B4D` can only reach a `PLAY` after `INIT` has set them.
- **Credits path:** the song is placed first in bank index 0, so bit 6 is 0. The stub writes `BANKSEL = 0`. Credits clear zero page, and with a custom song `famistudio_init` and `famistudio_update` are patched out, so `$FA` reads 0. The swap saves and restores `$D0-$DD`, so it cannot disturb the credits code. Credits clear only `$0400-$04FF` and zero page, so `$03BF` survives.

**Free Charlock and Auto Return**

- All branches in the 57-byte hook land correctly by hand count, and the `$DA` tail matches vanilla.
- `$CA56` is reached after a walking step or a touch warp (`$CAF3`, `$CDBC`), so entry from the overworld runs it at once. Menu commands return to `$CA9B`, but every way back into the room needs a step first, so the latch clears.
- **Latch:** dying or resetting mid-fight is safe. A continue starts on map 5, so the first hook pass clears it. Random WRAM is cleared the same way. Fleeing Dragonlord form 2 goes through `$E8D1 jmp $B091`, and `RTS` returns to the hook's `jmp $CA56`.
- **Stack:** `JSR $E4DF` returns through `$B097` or `$B08D` on a win, and the same on a run. Auto Return's `$DB31` does the same.
- Auto Return overwrites the Disguised Dragonlord byte at `$EA04`, which is harmless.
- `$680B` is unused by anything else I found.

**`update_flags`, the site and the CLI**

- The bits for Free Charlock and Auto Return are lifted out before the maybe step and restored after. The masks match the site's `FLAG_MAP` for all five conflicts.
- The clears run before the tristate rolls, so no conflict flag can roll back on.
- `CONFLICT_WITH` matches `update_flags`, except for finding 2.

**`name_entry_wrap`**

All four pieces are hand-counted, including `$AAFC`, the second jump and the `$6AED` branch. Vanilla code at `$A9E0`, `$AA0B`, `$AA73` and `$AAF9` matches what the hooks replace or preserve. Tile positions for row and column are right.

**`custom_music.c` and `chosenPack`**

- Scene and event order matches `PLACE_ORDER`, apart from finding 7. The `pick` and `place` limits are correct.
- The credits rule matches the site's text, though the comment at `site/index.html:634` is out of date.
- `moduleStops` handles the no-argument commands `$81`, `$83`, `$8A` and `$92` as the driver does. Default duration resets per pattern (`init.s:456`).

## What I could not verify

- **No emulator.** Nothing was played or heard. The off-map walk in finding 1, the audibility in finding 4 and all fight flows come from reading code.
- **No `asm6f`.** I could not regenerate `wrap.bin`, or test the fixes for findings 4 and 8.
- **Fixes in findings 1, 2 and 5 are not built or run.** Their byte counts and branch offsets are by hand.
- **An experiment crashed.** My scratch build with `name_entry_wrap` split gave an access violation on flag sets that run out of room. Unmodified code built with `-O0`, `-O1`, `-O2` and a stack-protector build did not, and neither did your CLI. It also did not crash under gdb. I did not find the cause, so it is most likely my patch. It may be worth one look if anyone edits that path.
- **Sweep limits:** the random flags ignore the site's own rules and run denser than most players, so the rates in finding 3 are upper bounds. The ladder is the realistic evidence.
- **Not reviewed in depth this round:** `chaos.c`, `chests.c`, `map.c`, `polyfit.c`, `sprites.c`, song and sprite data, `famistudio_asm6.asm`, and most of the FamiTracker driver beyond the paths named here. Review 4's coverage stands for those. `webui/` was not reviewed. `electron/` was only checked far enough to see its wasm entry is a 34-byte link to a build path.
- **Copy Quest and per-slot data** were covered by review 4's items and not re-traced.

## Stress tests worth running

Use default music, and turn on reduce flashing.

1. **Walk-out:** Free Charlock with no Wings or Return. Enter, run, climb to the surface, and walk off each of the four edges. Repeat with Stair Shuffle on. Before the fix this shows finding 1; after it you should land on the Charlock tile outside.
2. **Short combo:** Free Charlock plus Short Charlock, with and without Stair Shuffle. Check the arrival tile, the fight start, walking out after running, and the win warp.
3. **Pitch on stairs:** a pack with one Dungeon song and sustained square notes. Take 20 stairs and cast Return 10 times on the overworld with an Overworld song. Log `$4003` and `$4007` against the driver's period high byte.
4. **Latch:** in the Dragonlord's room, run then Outside; run then walk away and back; die to him; reset mid-fight on both forms and continue. The fight should start only on a fresh entry, and only when he is not yet beaten.
5. **Space ladder through the site:** the finding 3 flags with Castlevania. Confirm what the result dialog says, then repeat one flag at a time and watch which feature drops.
