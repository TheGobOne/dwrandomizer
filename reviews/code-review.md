# Code review: Dragon Warrior Randomizer, branch feature/custom-music

Reviewer: Code (6502/NES). Static review only. Nothing was changed, committed or pushed.
Tree state: HEAD 7e8de15 plus one uncommitted line in `common/dwr.c` (line 3989, `init_target = ...`).

How I checked: read the source, disassembled the built ROMs in `test-roms/` with a throwaway 6502
disassembler, counted every `vpatch` size against its data with a script, and compiled with
`gcc -Wall -Wextra -fsyntax-only`. No emulator or ROM was run, so the project rule about enabling
reduce-flashing for test runs did not come into play. No tool was blocked or denied.

Ratings are effort to implement: Easy (minutes, a few lines), Medium (an hour or two, needs testing),
Hard (a day, design change), Very hard (rewrite or deep emulator testing).
"Confirmed" means I verified it in built ROM bytes. "By reading" means from source only.

## Ranked list

| # | Finding | Type | Rating |
|---|---------|------|--------|
| 1 | Run Bank "last spell" hook was deleted in 7e8de15 | Bug, confirmed | Easy |
| 2 | `modify_run_rate` calls the wrong RNG routine ($C115) | Bug, confirmed | Easy |
| 3 | Run Bank RAM init is orphaned by `new_flags_ram_init` | Bug, confirmed | Easy |
| 4 | Four `vpatch` calls claim more bytes than they pass (undefined behavior) | Bug, confirmed | Easy |
| 5 | Credits play-time "frames" digits subtract 257 instead of 1 | Bug, by reading | Easy |
| 6 | Build-menu patch branches land one byte late (stack leak) | Bug, by reading | Easy |
| 7 | Three's Company patch overwrites 8 vanilla bytes | Bug, by reading | Easy |
| 8 | One-byte buffer overruns when decoding the 40-char flags | Bug, by reading | Easy |
| 9 | `find_free_space` is not bank-aware and aborts the process | Bug risk | Medium |
| 10 | Costs-XP routine reserves 14 bytes but writes 17 | Bug risk | Easy |
| 11 | Custom music: unchecked stub search, partial writes, file reading | Bug risk | Easy |
| 12 | Title screen padding continues after a detected error | Bug risk | Easy |
| 13 | Mapper switch into credits can be hit by an NMI | Bug risk, unverified | Medium |
| 14 | Build tooling: hardcoded path, removed Python module, no size guard | Tooling | Easy |
| 15 | Music wrapper copies 46 ZP bytes twice per frame (about 2,000 cycles) | Speed | Hard |
| 16 | `count_frame` can use INC (8 bytes, 4 cycles per frame) | Size and speed | Easy |
| 17 | Three copies of the spike lookup loop (about 37 bytes) | Size | Medium |
| 18 | Credits: three duplicated MON_STAT blocks (about 40 bytes) | Size | Medium |
| 19 | Credits: small redundant instructions (about 20 bytes) | Size | Easy |
| 20 | Clarity: RAM map, free-space notes, wrong comments, dead code | Clarity | Easy |

---

## 1. Run Bank "last spell" hook was deleted in commit 7e8de15

- File: `common/dwr.c:3988` to `3994`.
- Problem: commit 7e8de15 removed the code that stored the last cast spell. The old code patched `$E711`
  (`STA $D7 / LDA $DF`) with `JSR` to `85 D7 18 69 01 8D <last> A5 DF 60`. Now nothing writes `$6807`
  except the init stub. The orphan comment "Remember spells cast in battle" is still there, above
  unrelated code. The last-spell command (5) always finds `last == 0` and goes back to the prompt.
- Evidence: ROMs built before 09:30 (for example `DWR-music-test.nes`) have `20 C4 C4 EA` at `$E711`.
  `DWR-bank-strict-cap3.nes` and `DWR-jingles-test.nes` have the vanilla `85 D7 A5 DF`.
  The only `STA $6807` in the newer ROM is the init stub at `$C4C9`.
- Change: restore the hook. 11 bytes in the fixed bank, `find_free_space(rom->content, 0xc422, 11)`:
  `85 D7 18 69 01 8D lo(last) hi(last) A5 DF 60`, then `vpatch(rom, 0xe711, 4, 0x20, LO(c), HI(c), 0xea)`.
- Benefit: the recast command works again. Cost 11 bytes of ROM.
- Risk: low. It is the code that existed one commit ago.
- Rating: Easy.

## 2. `modify_run_rate` calls the wrong RNG routine

- File: `expansion/bank_3_patch.asm:244` (`jsr cycle_rng`), `expansion/bank_0.asm:63` (the DW label for
  `cycle_rng` is commented out), `expansion/base.asm:97` (the credits ROM's own `cycle_rng`).
- Problem: in the credits build the label `cycle_rng` resolves to the credits ROM's own LFSR. The patch
  is copied into the DW ROM, where that address is unrelated code. The built ROM has
  `20 15 C1` (`jsr $C115`) at `$C412`. In DW, `$C115` is `bne $C14B` in the middle of the NPC sprite
  routine. So the Easy Charlock run-rate change runs half of an NPC routine instead of cycling the
  RNG, and `RANDOM_H` is not re-randomized. `torch_in_battle` in the same file correctly uses `$C55B`.
- Change: use the literal `jsr $c55b`, as `torch_in_battle` does, or add `DW_CYCLE_RNG EQU $c55b`
  and use that. Rebuild with `tools/build_expansion.py` so `common/expansion.c` is regenerated.
  Also add `clc` before `adc $3e` (line 247): the carry coming out of the RNG routine is undefined
  and adds 0 or 1 to the run chance.
- Benefit: removes undefined side effects in battle. Fixes the run-away randomness.
- Risk: low. After the change the routine behaves as the comments say.
- Rating: Easy.

## 3. Run Bank RAM init is orphaned by `new_flags_ram_init`

- File: `common/dwr.c:3989` to `3994` (run_bank), `4038` (new_flags_ram_init), call order at `4372` and `4411`.
- Problem: both functions hook `$CA1A`. `run_bank` runs first and installs `JSR stub` (zeroes
  `$6806-$6808`, then `JMP init_target`). `new_flags_ram_init` runs later and overwrites the same
  3 bytes with `JSR newcode`, and its code hard-codes `JSR $CB47`. The run bank stub is never called.
  Built ROM: nothing references the stub at `$C4C4`. On a new quest the bank, last spell and points
  from the previous character carry over, because that SRAM is not part of the save slots.
- A second problem in HEAD: commit 7e8de15 also removed the line that reads `init_target`, so the stub
  ended in `JMP $0000` in `DWR-bank-strict-cap3.nes`. The uncommitted line 3989 fixes that, but the
  stub is still dead.
- Change: delete the stub and the `$CA1A` hook from `run_bank`, and add three stores to
  `new_flags_ram_init`'s code (A is already 0 at that point): `8D 06 68 8D 07 68 8D 08 68`
  (`newcode` grows from 23 to 32 bytes). Net ROM change: about -8 bytes.
- Benefit: new quests start with an empty bank. Removes an order dependence between two patches.
- Risk: low. Test: finish a character with a bank, start a new quest, check the BANK counter.
- Rating: Easy.

## 4. Four `vpatch` calls claim more bytes than they pass

- File and line:
  - `common/dwr.c:1053` size 129, passes 127 (`build_extras`, stats calc; the `find_free_space` at 1046 also asks for 129).
  - `common/dwr.c:1165` size 63, passes 61 (`vanilla_builds`; the `find_free_space` at 1162 also asks for 63).
  - `common/dwr.c:2647` size 21, passes 20 (`begin_quest_checksum`).
  - `common/dwr.c:3345` size 16, passes 6 (`zoom_and_whistle`; masked because `nop(rom, 0xdb0a, 10)` overwrites the garbage).
- Problem: `vpatch` reads one `va_arg` per byte of `size`. Reading past the arguments is undefined
  behavior. In the first two cases the extra bytes land in the ROM after the code, and that area is
  covered by the CRC at `dwr.c:4455`, so the displayed checksum can differ between compilers or
  platforms for the same seed and flags (only with Build Options on).
- Change: correct the sizes to 127, 61, 20 and 6. Then make it impossible to repeat: wrap the call in a
  macro that derives the size, for example
  `#define VPATCH(rom, a, ...) do { const uint8_t d_[] = {__VA_ARGS__}; patch(rom, a, sizeof d_, d_); } while (0)`.
- Benefit: removes undefined behavior and makes seeds portable across builds.
- Risk: none for the four fixes. The macro is an edit to many call sites (can be done gradually).
- Rating: Easy for the four; Medium for converting every call site.

## 5. Credits play-time "frames" digits subtract 257 instead of 1

- File: `expansion/exp_bank_4.asm:936` to `945` (also comment at 935).
- Problem: the comment says "if it's > 255, subtract 1". The code subtracts 1 from the low byte, then
  runs `sbc #1` on the high byte. That subtracts 1 plus 256, so 257 in total. Example: remainder 300
  gives 43 instead of 299, so the display shows 7 instead of 49. Remainder 256 underflows to `$FFFF` and
  shows a wrong low-byte value. This hits any remainder of 256 to 600, about 57 percent of all runs.
- Change: line 944 `sbc #1` becomes `sbc #0`. Rebuild `expansion.c`.
- Benefit: correct hundredths-of-a-second field in the end-of-game time. Zero bytes.
- Risk: low. Test with the credits test ROM and a known play-time value (`$6F00-$6F02`).
- Rating: Easy.

## 6. Build-menu patch: two branches land one byte late

- File: `common/dwr.c:1027` and `1030` (`newcode_exp_check`).
- Problem: the code is `PHP, PHA, ..., BNE, ..., PLA, PLP, STA $E5, JSR, JMP, PLA, PLP, JMP $F96A`.
  Both `BNE` (displacements `$0F` and `$0B`) land on the second `PLP`, skipping its `PLA`. On the
  "XP is not 0" path the stack keeps one extra byte, and `PLP` loads flags from the saved A.
  `$F96A` reloads A, so flags and A do not matter, but the stack leaks one byte each time a slot with
  XP is selected. If the loop at `$F8A4` does not reset SP, repeated selections (a few hundred) would
  overflow it. I could not verify whether SP is reset there.
- Change: displacements `0x0f` to `0x0e` and `0x0b` to `0x0a`.
- Benefit: balanced stack. No byte change.
- Risk: low. Test: choose a build slot with XP above 0 repeatedly.
- Rating: Easy.

## 7. Three's Company patch overwrites 8 vanilla bytes

- File: `common/dwr.c:1683` to `1697` (`not_sharing_the_world`), also `1658` to `1680`.
- Problem: vanilla `$D4E7` is two dialogs (8 bytes: `20 CB C7 CA 20 CB C7 C2`), followed by
  `A9 00 85 BA 85 BB 85 BC 85 BD ...` (the stat reset). The Three's Company branch writes 16 bytes at
  `$D4E7`: 8 new bytes plus the two dialogs again. The extra 8 bytes land on `A9 00 85 BA 85 BB 85 BC`,
  so the stat reset starts with A holding whatever the dialog code left. Not seen in any test ROM (none
  was built with this flag), so by reading only.
- Change: after the relocated dialogs, append `A9 00 85 BA 85 BB 85 BC` (size 24), or branch to a small
  routine in free space and leave `$D4E7` alone.
- Also in `threes_company` (1668 to 1675): the 8-byte patch writes `20 BC C4` and the very next
  `add_hook` overwrites it. Drop the stale bytes.
- Risk: medium, because the game state after joining the Dragonlord must be tested.
- Rating: Easy.

## 8. One-byte buffer overruns when decoding the 40-char flags

- File: `common/base32.c:194` (`plain[0] = 0` before the validity check), `common/dwr.c:184-187`
  (`flags[25]`), `cli/main.c:100-102` (`raw[25]`).
- Problem: 40 base32 chars decode to 25 bytes. `base32_decode` then reads the NUL at index 40, and
  `decode_sequence` writes `plain[0] = 0` first, so it writes `flags[25]` and `raw[25]`, one past the
  end. In `main.c` it is a stack write of one zero byte.
- Also `base32.c:80` and `136`: `char retval = -1` and `char offset` break on platforms where `char`
  is unsigned (ARM Linux, Raspberry Pi): an invalid character returns 255 instead of -1, and negative
  shift offsets turn positive.
- Change: make the buffers 26 or 32 bytes (`uint8_t flags[32]` in `dwr_types.h:322`, `raw[32]`), or
  stop `decode_sequence` from writing before it checks the character. Change both `char` to `int`.
- Risk: none.
- Rating: Easy.

## 9. `find_free_space` is not bank-aware and aborts the process

- File: `common/dwr.c:4527` to `4549`, callers at `3903`, `3933`, `3947`, `3958` and many `0xc422` calls.
- Problem 1: it scans linearly from `start` to `0xffff`. Run Bank asks from `0x7f0a` (end of bank 1;
  vanilla free space ends at `$7FD8` per `notes/free_space.txt`). If bank 1 fills up, it returns an
  offset in bank 2, and the callers add `0x4000` to get a CPU address, so the code would be mapped in
  the wrong bank. Today Run Bank uses 160 of the 174 free bytes (`$7F0A-$7FB8`), so any new patch
  there would hit this.
- Problem 2: running out of space calls `abort()`. In the web UI or Electron build that kills the
  process instead of returning an error.
- Change: add `find_free_space_in(content, start, end, n)` that fails at `end`, use it with `0x7fd8`
  for the bank 1 callers, and return an error code to `dwr_randomize`.
- Risk: low.
- Rating: Medium.

## 10. Costs-XP routine reserves 14 bytes but writes 17

- File: `common/dwr.c:396` (`costs_xp_newcode = find_free_space(..., 14)`), `418` (writes 17 bytes).
- Problem: first fit is correct only if the free run is at least 17. Today the run is large, so it
  works.
- Change: pass 17.
- Rating: Easy. Also, at lines 395 and 396 both allocations are made before either is written, so they
  return the same address. That is safe only because the two modes never run together.

## 11. Custom music: unchecked and fragile places

File `common/custom_music.c`.

- `785` to `793`: the scan for room for the 51-byte credits stub does not check that it found any. If
  none is free, `u` ends at `0xff00 - 51` and `patch()` overwrites whatever is there. Add an
  `if (n != sizeof(stub))` fallback that prints the same "keeping the original credits music" message.
- `712` to `730`: the "bank in use" check runs per bank inside the write loop, so bank 6 is already
  written when bank 3 fails, and the function returns with half the work done. Check both banks first,
  then write. Easy.
- `448` to `470` (`custom_music_load`): `ftell` can return -1 and `malloc` is unchecked. A failure gives
  `malloc(0)` and an `fread` of `SIZE_MAX`. Add checks. Easy.
- `142` (`skip_value`): recursion depth is unbounded, so a hostile pack of `[[[[...` overflows the
  stack. Add a depth counter (limit 64). Easy.
- `769` to `796`: the credits patch matches hard-coded offsets (`0x204b`, `0x2a63`) in the assembled
  expansion. Any edit to `exp_bank_4.asm` before `start_music` silently turns the credits song off
  (it prints a message). Fix: have `tools/build_expansion.py` write the label addresses to a header
  (it already reads label output for `expansion.h`). Medium.
- `33` (`MAX_PLACED 24`): there are 1 + 7 + 20 = 28 possible placements, so Floor 5 to 7 events can
  never get a song when everything else is placed. The limit comes from the 24-entry tables in the
  wrapper (`gen_driver.py`). If that matters, grow the tables to 32. Easy.
- `689`: `mt_init(time(NULL))` has one-second resolution. Two ROMs generated in the same second get the
  same stream for the credits track and the sprite choice. Use a finer seed. This also exists at
  `dwr.c:4505`. Easy.

Rating: Easy for the first four and the last two, Medium for the symbol export.

## 12. Title screen padding continues after a detected error

- File: `common/dwr.c:1419` to `1438` (`pad_title_screen`), `1388` to `1410` (`center_title_text`).
- Problem: when `needed < 0` it prints "An unexpected error occurred" and keeps going: `ppatch` then
  gets a negative size converted to a huge unsigned count, which would write past the ROM buffer.
  `center_title_text` uses `strncpy(dw_text, text, 33)` into `dw_text[34]` and `uint8_t len`, so text of
  33 or more characters leaves no terminator and `space = 32 - len` wraps. Reachable only if
  `DWR_EDITION` or the seed line gets longer.
- Change: return `pos` (or `abort()`) when `needed < 0`; clamp `len` to 32.
- Rating: Easy.

## 13. Mapper switch into credits can be hit by an NMI (unverified)

- File: `expansion/exp_bank_3.asm:4` to `28`, `expansion/exp_bank_4.asm:55` to `93`.
- Problem: `exp_start_dwr_credits` builds code at `$200` and writes the MMC1 control and PRG registers
  five bits at a time with the DW NMI still enabled. DW's NMI also writes the mapper (it loads bank 1
  for sound, then restores `$6004`). If the NMI lands between two serial writes, the shift register
  is corrupted. The window is about 100 cycles per switch (roughly 0.3 percent per switch). Then
  `credit_start` clears RAM with NMI still on, and its `nmi` runs `famistudio_update` on zeroed state;
  that is safe only because song speed 0 takes the pause path (checked in `famistudio_asm6.asm:3858`).
- Change: write `$2000 = 0` before the mapper switch, and write `$2000 = $80` again at the end of
  RAM setup in `credit_start` (before `exp_wait_for_nmi` is first used). About 10 bytes.
- Risk: medium. It changes startup timing; test the credits with the existing credits test ROM and Lua helper.
- Rating: Medium.

## 14. Build tooling

- `tools/build_expansion.py:7` imports `distutils`, which was removed in Python 3.12 (this machine has
  3.12 and 3.14). `:108` and `expansion/build.sh:5` hard-code `/home/francois/...asm6f`. `:14` reads
  `credits.lua` but the command at `:109` does not pass the flag that writes it, so I could not tell
  how that file is produced (not verified, I have no `asm6f`).
- The patch region (`0xc288` to `0xc422`, 410 bytes) is not size-checked. `bank_3_patch()` in
  `expansion.c` is applied after the randomizer has already placed code at `0xc422` and up; if the
  assembly grows past 410 bytes it overwrites that code silently. Add an assert in
  `generate_c_file`: `len(non-FF b3_patch) <= 0xc422 - 0xc288`.
- Change: use `shutil.which`, read the assembler path from `ASM6F` or `PATH`, add the assert.
- Rating: Easy.

## 15. Music wrapper copies 46 ZP bytes twice per frame

- File: `tools/custom-music/gen_driver.py` (the `wrap` source: `s1` and `s2` loops), `driver.cfg`.
- Problem: each loop iteration costs `lda zp,x` 4 + `sta abs,x` 5 + `lda abs,x` 4 + `sta zp,x` 4 +
  `dex` 2 + `bpl` 3 = 22 cycles, times 46 bytes, about 1,012 per loop, so about 2,030 cycles per frame
  (about 6.8 percent of an NTSC frame of 29,780), on top of the driver itself. It runs inside DW's NMI,
  after the PPU work, so it does not break rendering, but it eats frame budget.
- Change (cheapest first): (a) read the real ZP usage from the linker label file and shrink `size=$2E`
  and the swap count; (b) unroll the loop (`lda $D0+i / sta $6B00+i` per byte costs 7 cycles one way
  and about 5 bytes of code per byte), which brings the total near 1,290 cycles for about 920 bytes of
  code; (c) move the driver's non-pointer variables to RAM so only the pointers need swapping.
- Benefit: up to about 1,500 to 2,000 cycles per frame (option c).
- Risk: medium to high for (c). It touches the third-party driver, so keep it as a patch file.
- Rating: Hard.
- Also add: assert in `gen_driver.py` that the driver BSS ends below `$6B00` (the wrapper saves ZP
  at `$6B00-$6B6D` and uses `$6BF0-$6BFF`), and below `$6F00` (credits play-time snapshot). The linker
  config allows `$6900-$6FFF`, so an overlap would not be reported.

## 16. `count_frame` can use INC

- File: `expansion/bank_3_patch.asm:215` to `231`.
- Change: replace the three `ldx / inx / stx` blocks with `inc $662c / bne ++ / inc $662d / bne ++ /
  inc $662e`.
- Benefit: 21 bytes down to 13 (saves 8), and 4 cycles per frame in the normal path (`ldx+inx+stx` 10
  cycles down to `inc` 6). X is already clobbered at the top of the routine.
- Risk: low. The routine ends in `jmp $FDF4`; check that `$FDF4` does not read X (it could not
  have relied on the old value, which was the low timer byte).
- Rating: Easy.

## 17. Spike lookup loop is copied three times

- File: `common/dwr.c:2088` to `2166` (start of spike encounter at `$CD51`, ran away at `$E8D4`,
  defeated at `$E96B`).
- Change: one subroutine that returns the matching index in X (or `$FF`) and three short callers.
  Each copy is about 26 bytes of the same `LDX #7 / LDA MAP / CMP / BNE / LDA X / CMP / BNE / LDA Y /
  CMP / BNE / DEX / BPL` loop.
- Benefit: about 37 bytes. Cycles are unchanged or slightly worse (extra JSR/RTS, 12 cycles).
- Risk: medium. The three sites use different follow-up code and different branch exits; check
  with `RANDOMIZE_ZONES` on and off and with treasure guards.
- Rating: Medium.

## 18. Credits: three duplicated MON_STAT blocks

- File: `expansion/exp_bank_4.asm:236` to `278`.
- Change: loop three times: `jsr addr_to_bcd / jsr print_bcd`, then (on the first two passes) print a
  blank and add 80 to `TMP_ADDR`.
- Benefit: about 40 bytes of the bank-4 code.
- Risk: low. Compare the screenshot of the stats page before and after.
- Rating: Medium.

## 19. Credits: small redundant instructions

- `exp_bank_4.asm:57` to `62`: the first loop (clears `$F0-$FF`) is redundant because the second loop
  (`sta $00,x`, lines 64 to 67) clears all of `$00-$FF`. Saves 9 bytes (about 150 cycles, one time).
- `exp_bank_4.asm:335`: `ldx #4` before `jsr print_bcd` is dead because `print_bcd` loads X itself. 2 bytes.
- `exp_bank_4.asm:364` to `376`: `pha` at the start and `pla / tay` at the end of `addr_to_bcd` only
  move A into Y, and no caller uses Y. 3 bytes.
- `exp_bank_4.asm:323` to `325`: the `INSTR_PLAY_TIME` handler has no `rts` and falls into the
  `INSTR_SHOW_NUMBER` compare. It works only because A is `$20-$23` (the PPU high byte) at that point
  and never equals `$FC/$FD/$FF`. Add `rts` (1 byte). This is the only one with a real (latent) risk.
- `exp_bank_4.asm:89`: comment says "wait for 240 frames" but the loop at line 90 is 30 frames.
- `credit_start` leaves `NEXT_TILE` at 0 (it clears all ZP), so the first NMI writes tile 0 at PPU
  address `$0000`. Harmless on CHR-ROM; set `NEXT_TILE = NMI_SKIP_TILE` after the clear to be clean.
- Rating: Easy.

## 20. Clarity

All Easy.

- RAM is allocated ad hoc and documented nowhere: `$6800-$6808` (flags, step counter, Run Bank),
  `$6630-$675F` (credit counters), `$662C-$662E` (timer), `$6F00-$6F02` (timer snapshot),
  `$6B00-$6B6D` and `$6BF0-$6BFF` (music wrapper), `$7004` and `$7F01-$7F02` (expansion call), `$03BF`
  (music "initialized" marker), `$A7-$AF` (inventory sort temp). `notes/ram.txt` only lists ZP and says
  "SRAM". Add a table. None of this RAM is saved with the save slots, so a different slot inherits
  the previous character's bank, step count and zoom/whistle towns (the TODO at `dwr.c:3495` is the
  same issue).
- `notes/free_space.txt` still lists bank 1 `7E9E-7FD8` as free and omits Run Bank, custom music and
  Costs XP.
- Wrong comments: `dwr.c:2309` ends with a comma instead of `;` (compiles by accident as a comma
  operator); `dwr.c:2629` ("new address is 0xc7ec" is stale); `bank_3_patch.asm:146`
  ("c9 04 Fairy Water" is `cmp #$05`); `dwr.c:1672` says `JSR $C4C0` but the call goes to a different
  address; `dwr.c:1722` comments `bne` where the byte is `beq`; `dwr.c:3988`
  ("Remember spells cast" above the init stub); the menu-wrap comments in `dwr_menu_wrap` (about `dwr.c:1960` to `2030`) say
  `JMP $A9E4` for `4c 27 aa` and `LDA $64E5 ; ???`; `dwr.c:1529` says `AND #$80` for `BIT $CF`; line 1530
  says `$FF71` where the branch target is `$FF70`.
- `dwr.c:995`: the 3 `inx` pad bytes. `NOP` (`$EA`) is the clean filler; X is not read afterwards
  (checked against the vanilla bytes at `$B894`), so this is harmless but misleading.
- `dwr.c:2239`: `A || B && rand` needs parentheses (works as intended).
- `expansion/exp_bank_4.asm:22` to `28`: `PRODUCT`/`REMAINDER`, `MULTIPLIER`/`DIVISOR` and
  `MULTIPLICAND`/`DIVIDEND` are declared without a size, so each pair shares the same address. The
  play-time maths relies on that. Add a comment; one wrong edit breaks it silently.
- `NMI_SKIP_TILE EQU #$f7`, `BTN_* EQU #$01`: the `#` is part of the symbol, so `cmp NMI_SKIP_TILE`
  assembles as an immediate by text substitution. Fragile; define plain numbers and write `#` at use.
- `expansion/bank_0.asm:40` to `191`: 150 lines of commented-out label definitions. Move to `notes/`.
- gcc `-Wall -Wextra` warnings: unused variables at `dwr.c:67, 393, 822, 2436, 2488 (pal_start and
  pal_end are set but unused), 4033, 4196`; `stair_shuffle.c:162, 186, 324, 431, 522`;
  `dungeon_maps.c:73, 334`; missing braces at `dwr.c:3816, 4062, 4201`; `DWR_VERSION` is redefined by
  `dwr.h:12` and `build.h`. Building with `-O2 -Wall` in CI would also have caught the uninitialized
  `init_target` in HEAD.
- Leaks on error: `dwr_init` (`dwr.c:164-175`) and `dwr_randomize` (`4470`, `4478`, `4525`) do not free
  `header`, `expansion` or `rom` when they return early. Harmless in the CLI.
- `dwr.c:3206`: the hint `strcat` chain fits today (80 of 83 bytes at most) but has no check. Use `snprintf`.
- `challenge.c:12-15` and similar: runs of 32 to 95 literal `0x00` arguments. A `memset` would do.
- Licence note, not code: `tools/custom-music/LICENSE-GPLv2.txt` sits beside a driver whose own
  `LICENSE.md` says it is not GPL, in an MIT repository, and `common/ft_driver.h` embeds the assembled
  driver. Worth a one-time check of what applies.

---

## Checked and found correct (so nobody re-checks them)

I decoded these by hand against the ROM bytes: Run Bank window code `$F0` and `$F8`, up/down/select
hooks, battle dispatch, win-points routine (branch offsets, sizes, stack balance); menu wrap blocks; DW4
and Safer DW1 run mechanics and Costs XP (stack balance and carry); radish finish, forfeit trigger,
unbreakable keys, return escapes, zoom and whistle, levelup refill (sizes and branch offsets); spell
learned loop; stat-calc table dispatch (all six offsets); step counter; `scared_slimes`; credits
`bin_to_bcd`, 24-bit divide, play-time constants (216,360 and 3,606 and 601); credits NMI; credits
start code at `$200`; custom music request hook, frame hook, ZP swap protocol and the NMI interplay (DW's
NMI only does full work when the main thread waits, and `$6005` stops sound updates during a request).

## What I could not review

- No vanilla ROM and no `asm6f`, `ca65` or `ld65` on this machine, so I could not rebuild the
  expansion or the driver. Vanilla bytes were read from the built test ROMs where a patch is not
  applied. Findings 5, 6, 7, 13 and 15 are by reading, not by running.
- Not read line by line: `common/sprites.c` (2,777 lines, only warning-checked), `map.c`, `chests.c`,
  `stair_shuffle.c` logic, `dungeon_maps.c`, `chaos.c`, `flute_music.c`, `polyfit.c`, `crc64.c`,
  `mt19937-64.c`, `credit_music.c` (18,962 lines of generated data), most of `dwr.c` between lines
  570 and 980 (randomizers), 1300 and 1486, 2460 and 2620 (themes), and 2899 and 3228 (NPC changes).
  The `vpatch` size check did cover all of those files (no other mismatches found).
- Third-party and generated code was reviewed only where the randomizer touches it: the Dn-FamiTracker
  driver sources (about 6,000 lines of `.s`) and `famistudio_asm6.asm` (7,547 lines),
  `expansion/music/*.fms`, `expansion.c`, `ft_driver.h`.
- `webui/`, `electron/`, `hud.lua`, `release.sh` and the Python tools other than `build_expansion.py`
  and the `custom-music` scripts are out of scope.
- I did not run the randomizer or any emulator.
