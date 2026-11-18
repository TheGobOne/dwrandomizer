
; .include defs.asm
; .include macros.asm

TIMERS_COUNT EQU 8
TIMERS_ADDR  EQU $d0

; *** VARIABLES ***
.enum $00
    SCROLLX      .dsb 2
    SCROLLY      EQU #0   ; Y scroll is always 0
.ende
.enum TIMERS_ADDR
    TIMERS       .dsb TIMERS_COUNT*2
.ende
; .enum $200
;     SPRITES    .dsb 256
; .ende

; *** PRG ROM ***
; This asm build has 8 banks. The randomizer takes banks 3-6 of it for the start of rom->expansion and
; adds banks 7-14 itself, so bank_7.asm below is the fixed bank, which is bank 15 in the output ROM
; ("bank 7" in the C code is the far-call bank).
.include bank_0.asm
.include empty.asm
.include empty.asm
.include exp_bank_3.asm
.include exp_bank_4.asm
.include exp_bank_5.asm
.include empty.asm
.include bank_7.asm

; *** CHR ROM ***
.incbin chr_rom.chr

