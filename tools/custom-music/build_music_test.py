#!/usr/bin/env python3
"""Proof of concept: patch a built DWR ROM with the Dn-FamiTracker driver and two pack songs.
Usage: build_music_test.py BASE.nes PACK.json5 OUT.nes CC65_BIN_DIR ASM6F_EXE
Run from a scratch directory. Songs and the request-to-song map are hardcoded for now."""
import os,re,subprocess,base64,sys
BASE,PACK,OUT,CC65,ASM=sys.argv[1:6]
HERE=os.path.dirname(os.path.abspath(__file__))
s=open(HERE+'/driver/driver.s',encoding='utf8').read()
old=[l for l in s.splitlines() if l.startswith('\t.addr * + 2')][0]
s=s.replace('ft_music_addr:\n'+old,'ft_music_addr = $6bf0')
assert 'ft_music_addr = $6bf0' in s
open(HERE+'/driver/driver2.s','w',encoding='utf8').write(s)
bin=CC65+'/'
subprocess.check_call([bin+'ca65','-g',HERE+'/driver/driver2.s','-D','RELOCATE_MUSIC','-D','USE_OLDVIBRATO','-D','NAMCO_CHANNELS=8','-D','CHANNEL_CONTROL','-o','d2.o'])
subprocess.check_call([bin+'ld65','-o','d2.bin','-C',HERE+'/driver.cfg','-Ln','d2.lbl','d2.o'])
L={m[2]:int(m[1],16) for m in re.finditer(r'al (\w+) \.(\w+)',open('d2.lbl').read())}
p=open(PACK,encoding='utf-8-sig').read()
g=p[p.index('title: "Teenage Mutant Ninja Turtles"'):p.index('title: "Teenage Mutant Ninja Turtles 2"')]
def song(t):
    m=re.search(r'title: "'+t+r'[^"]*".*?primary_square_chan: (\d).*?data: "([^"]*)"',g,re.S)
    d=bytearray(base64.b64decode(m[2]))
    if m[1]=='1':
        w=lambda o:d[o]|d[o+1]<<8
        sh=w(w(0));fl=w(sh);seen=set()
        for f in range(d[sh+2]):
            q=w(fl+2*f)
            if q not in seen:
                seen.add(q);d[q:q+2],d[q+2:q+4]=d[q+2:q+4],d[q:q+2]
    return bytes(d)
S=[song('Level Theme 1'),song('Difficult Fight')]
D=open('d2.bin','rb').read();W=0x8000+len(D)
open('wrap.asm','w').write(f'''
INIT={L['ft_music_init']}
PLAY={L['ft_music_play']}
CH={L['var_Channels']}
PF={L['var_ch_PrevFreqHigh']}
MA=$6bf0
FA=$6bff
G=$6bfe
CUR=$6bfd
WANT=$6bfc
SONG=$6bfb
.base {W}
wrap:
 lda $fa
 sta FA
 ldx #$2d
s1: lda $d0,x
 sta $6b00,x
 lda $6b40,x
 sta $d0,x
 dex
 bpl s1
 lda $03bf
 cmp #$5a
 beq ok
 lda #$5a
 sta $03bf
 lda #$ff
 sta CUR
 sta SONG
ok:
 lda WANT
 cmp CUR
 beq same
 sta CUR
 tax
 lda map,x
 sta SONG
 bmi same
 tax
 lda lo,x
 sta MA
 lda hi,x
 sta MA+1
 lda #0
 tax
 jsr INIT
same:
 lda SONG
 bmi out
 lda #$0f
 sta $4015
 lda FA
 beq idle
 lda #$f5
 sta CH
 lda #1
 sta G
 jmp pl
idle:
 lda #$ff
 sta CH
 lda #8
 sta $400f
 lda G
 beq pl
 lda #0
 sta G
 lda #$ff
 sta PF+1
pl:
 jsr PLAY
out:
 ldx #$2d
s2: lda $d0,x
 sta $6b40,x
 lda $6b00,x
 sta $d0,x
 dex
 bpl s2
 rts
map:
 .db $ff
 .db 0,0,0,0,0,0,0,0,0,0,0,0,0
 .db 1,1,1
 .db $ff,$ff,$ff,$ff,$ff,$ff,$ff,$ff,$ff,$ff,$ff,$ff,$ff,$ff,$ff
lo: .db <song0,<song1
hi: .db >song0,>song1
song0: .incbin "s0.bin"
song1: .incbin "s1.bin"
''')
open('s0.bin','wb').write(S[0]);open('s1.bin','wb').write(S[1])
subprocess.check_call([ASM,'-q','wrap.asm','wrap.bin'])
w=open('wrap.bin','rb').read();print('used to',hex(W+len(w)));assert W+len(w)<=0xc000
d=bytearray(open(BASE,'rb').read())
b=16+6*0x4000;assert all(x==0xff for x in d[b:b+0x4000])
d[b:b+len(D)]=D;d[b+len(D):b+len(D)+len(w)]=w
v=16+0x4000-0x8000
f=lambda x:16+7*0x4000+x-0xc000
def put(c):
    s=next(a for a in range(0xc422,0xff00) if all(d[f(a+k)]==0xff for k in range(len(c))))
    d[f(s):f(s)+len(c)]=bytes(c);return s
JG=0x6bfa
ys=put([0xad,JG&255,JG>>8,0xd0,2,0xa0,0x20,0x4c,0xcb,0x80])
for a in(0x8070,0x807d,0x8084):
    assert d[v+a:v+a+3]==bytes([0x20,0xcb,0x80]);d[v+a+1]=ys&255;d[v+a+2]=ys>>8
body=[0x8d,0xfc,0x6b,0xa2,0,0xc9,0x11,0x90,2,0xa2,1,0x8e,JG&255,JG>>8,
 0xa2,0x30,0x8e,0,0x40,0x8e,4,0x40,0x8e,0x0c,0x40,0xa2,0x80,0x8e,8,0x40]
rq=put([0xa2,0xff,0x8e,5,0x60,0xc9,0x80,0xb0,len(body)]+body+[0x60])
assert d[v+0x81a0:v+0x81a5]==bytes([0xa2,0xff,0x8e,5,0x60])
d[v+0x81a0:v+0x81a5]=bytes([0x20,rq&255,rq>>8,0xea,0xea])
s=put([0xa9,1,0x20,0x96,0xff,0x20,0x28,0x80,0xa9,6,0x20,0x96,0xff,0x20,W&255,W>>8,0x60])
d[f(0xfee8):f(0xfee8)+8]=bytes([0x20,s&255,s>>8]+[0xea]*5)
open(OUT,'wb').write(d);print('ok')
