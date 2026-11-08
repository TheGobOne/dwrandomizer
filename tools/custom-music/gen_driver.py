#!/usr/bin/env python3
"""Assembles the Dn-FamiTracker driver and wrapper into common/ft_driver.h.
Usage: gen_driver.py CC65_BIN_DIR ASM6F_EXE (run from a scratch directory)

Layout of a music bank (banks 6, 3, 5 and 4 hold identical copies; bank 4 also holds the credits code
  at $A000, so its songs go below and above that):
  $8000-$80ff  left free (bank 3 uses it; banks 4 and 6 hold the credits hook at $8000-$8033)
  $8100-       driver, wrapper and tables
  FT_SONGS-    song data, different per bank

A song's high address byte in the hi table carries its bank in bits 7-6, since songs sit at $80xx-$BFxx:
  %10 bank 6, %11 bank 3, %00 bank 5, %01 bank 4. The wrapper turns that into BANKSEL ($00 bank 6,
  $40 bank 3, $80 bank 5, $C0 bank 4; 0 is bank 6 so a cleared selector is safe) and rebuilds the address with and #$3f, ora #$80.
"""
import os, re, subprocess, sys

CC65, ASM = sys.argv[1:3]
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

src = open(HERE + '/driver/driver.s', encoding='utf8').read()
line = [l for l in src.splitlines() if l.startswith('\t.addr * + 2')][0]
patched = src.replace('ft_music_addr:\n' + line, 'ft_music_addr = $6bf0')
assert patched != src
# The wrapper always selects NTSC, so the PAL table would only take room from the songs.
no_pal = patched.replace('\tPAL_PERIOD_TABLE = 1', '\t;PAL_PERIOD_TABLE = 1')
assert no_pal != patched
# Only the pointers and the busiest work bytes need zero page. The rest live in WRAM, so they cost
# 1 more cycle per use instead of 14 to 28 per frame in the swap below.
ZP_END = '\nvar_currentChannel:'
assert no_pal.count(ZP_END) == 1
no_pal = no_pal.replace(ZP_END, '\n.segment "BSS"' + ZP_END)
open('driver2.s', 'w', encoding='utf8').write(no_pal)

open('drv.cfg', 'w').write('''MEMORY {
 ZP: start=$D0,size=$0E,type=rw,file="";
 RAM: start=$6900,size=$200,type=rw,file="";
 PRG: start=$8100,size=$3F00,type=ro,file=%O;
}
SEGMENTS {
 ZEROPAGE: load=ZP,type=zp;
 BSS: load=RAM,type=bss,define=yes;
 CODE: load=PRG,type=ro;
 DPCM: load=PRG,type=ro,optional=yes;
}
''')
subprocess.check_call([CC65 + '/ca65', '-g', 'driver2.s', '-I', HERE + '/driver', '--bin-include-dir', HERE + '/driver',
                       '-D', 'RELOCATE_MUSIC', '-D', 'USE_OLDVIBRATO', '-D', 'NAMCO_CHANNELS=8', '-D', 'CHANNEL_CONTROL',
                       '-o', 'drv.o'])
subprocess.check_call([CC65 + '/ld65', '-o', 'drv.bin', '-C', 'drv.cfg', '-Ln', 'drv.lbl', 'drv.o'])
L = {m[2]: int(m[1], 16) for m in re.finditer(r'al (\w+) \.(\w+)', open('drv.lbl').read())}
D = open('drv.bin', 'rb').read()
W = 0x8100 + len(D)

# ZP-SWAP-BEGIN
# The driver shares $D0-$DD with the game, and this swap runs every frame, so it is written out
# without loops.
# Scratch bytes are rebuilt by the driver each frame, so only the game's values are kept for them.
assert L['var_Note_Table'] == 0xdc
ZP_SCRATCH = list(range(0xd0, 0xdc))
ZP_KEPT = [0xdc, 0xdd]
# Nothing to do while the game's own music plays and no new song is wanted, so skip the swap too.
swap_in = ' lda SONG\n bpl full\n lda WANT\n cmp CUR\n bne full\n lda $03bf\n cmp #$5a\n bne full\n rts\nfull:\n'
swap_in +=''.join(f' lda ${a:02x}\n sta ${0x6b00 + a - 0xd0:04x}\n' for a in ZP_SCRATCH)
swap_in += ''.join(f' lda ${a:02x}\n sta ${0x6b00 + a - 0xd0:04x}\n lda ${0x6b40 + a - 0xd0:04x}\n sta ${a:02x}\n' for a in ZP_KEPT)
swap_out = ''.join(f' lda ${a:02x}\n sta ${0x6b40 + a - 0xd0:04x}\n lda ${0x6b00 + a - 0xd0:04x}\n sta ${a:02x}\n' for a in ZP_KEPT)
swap_out += ''.join(f' lda ${0x6b00 + a - 0xd0:04x}\n sta ${a:02x}\n' for a in ZP_SCRATCH)
# ZP-SWAP-END

NOISE_ONLY = {0, 3, 4, 7, 8, 10, 11, 18}  # 2 (the town footstep) mutes square 2 as the original game does
SFXMASK = ','.join('$e7' if i in NOISE_ONLY else '$e5' for i in range(32))
open('wrap.asm', 'w').write(f'''
INIT={L['ft_music_init']}
PLAY={L['ft_music_play']}
CH={L['var_Channels']}
PF={L['var_ch_PrevFreqHigh']}
MA=$6bf0
DELAY=$6bf8
JG=$6bfa
BANKSEL=$6bf9
SFX=$6bf6
RAW=$6bf5
SONG=$6bfb
WANT=$6bfc
CUR=$6bfd
G=$6bfe
.base {W}
wrap:
{swap_in} lda $03bf
 cmp #$5a
 beq ok
 lda #$5a
 sta $03bf
 lda #$ff
 sta CUR
 sta SONG
 sta WANT
 lda #1
 sta JG
ok:
 ; A new request is told apart by map once (the request hook used to do this in the fixed bank):
 ; Hauksness has its own song, the old man caves keep what was playing, and floors in a cave
 ; (not Charlock) are the Cave song.
 lda RAW
 beq noremap
 lda #0
 sta RAW
 lda WANT
 cmp #$0e
 bcs noremap
 cmp #$01
 bcc noremap
 ldx $45
 cpx #3
 bne nh
 lda #$1c
 sta WANT
nh:
 cpx #12
 bcc nb
 cpx #15
 bcs nb
 lda CUR
 sta WANT
 jmp noremap
nb:
 cmp #$06
 bcc noremap
 cpx #21
 bcc noremap
 cpx #30
 bcs noremap
 lda #$1d
 sta WANT
noremap:
 lda WANT
 cmp CUR
 beq same
 cmp #$0e
 bne go
 inc DELAY
 lda DELAY
 cmp #60
 bcs sting
 lda #$ff
 sta SONG
 sta CUR
 jmp same
sting:
 lda WANT
go:
 and #$1f
 tax
 lda map,x
 bpl start
 sta SONG
 lda WANT
 sta CUR
 jmp same
start:
 tay
 cpy SONG
 bne new
 lda WANT
 sta CUR
 jmp same
new:
 lda hi,y
 and #$c0
 eor #$80
 cmp BANKSEL
 beq here
 sta BANKSEL
 lda #$ff
 sta SONG
 jmp same
here:
 lda WANT
 sta CUR
 sty SONG
 lda lo,y
 sta MA
 lda hi,y
 and #$3f
 ora #$80
 sta MA+1
 lda #0
 tax
 jsr INIT
same:
 lda SONG
 bmi out
 lda JG
 beq nj
 lda #$ff
 sta PF
 sta PF+1
 lda #0
 sta JG
nj:
 lda #$0f
 sta $4015
 lda $fa
 beq idle
 lda SFX
 and #$1f
 tax
 lda sfxmask,x
 sta CH
 lda #1
 sta G
 jmp pl
idle:
 lda #$ef
 sta CH
 lda G
 beq pl
 lda #0
 sta G
 lda #8
 sta $400f
 lda #$ff
 sta PF+1
pl:
 jsr PLAY
out:
{swap_out} rts
; Channels left on while a game sound effect plays, by effect number (SFX holds the request, $80 + n).
; $E7 for the effects that only use noise (bank 1's table at $8339: 0, 2, 3, 4, 7, 8, 10, 11, 18), so the
; song keeps square 2. $E5 (square 2 and noise off) for the rest, as the original game does for all.
sfxmask: .db {SFXMASK}
map: .dsb 32,$ff
lo: .dsb 24,0
hi: .dsb 24,0
songs:
''')
subprocess.check_call([ASM, '-q', '-l', 'wrap.asm', 'wrap.bin'])
w = open('wrap.bin', 'rb').read()
image = D + w
# tables are the last 80 bytes of the wrapper
songs = W + len(w)
Map = songs - 80
out = ['/** Generated by tools/custom-music/gen_driver.py. Driver source: tools/custom-music/driver */',
       '#ifndef FT_DRIVER_H', '#define FT_DRIVER_H', '#include <stdint.h>',
       '#define FT_BASE 0x%04x' % 0x8100,
       '#define FT_WRAP 0x%04x' % W,
       '#define FT_MAP 0x%04x' % Map,
       '#define FT_LO 0x%04x' % (Map + 32),
       '#define FT_HI 0x%04x' % (Map + 56),
       '#define FT_SONGS 0x%04x' % songs,
       'static const uint8_t ft_image[%d] = {' % len(image)]
for i in range(0, len(image), 16):
    out.append(' ' + ','.join('0x%02x' % b for b in image[i:i + 16]) + ',')
out += ['};', '#endif', '']
open(REPO + '/common/ft_driver.h', 'w', newline='\n').write('\n'.join(out))
print('driver', len(D), 'wrapper', len(w), 'songs at', hex(songs), 'room per bank', 0xc000 - songs)
