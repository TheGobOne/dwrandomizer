/*
 * Archipelago (AP) mode, proof of concept.
 *
 * Chests and search spots stop giving items. Opening one sets a bit in work
 * RAM that the AP client reads and sends to the server as a location check.
 * Items from the multiworld are written into RAM by the client, which keeps
 * a counter of how many it has delivered. The counter is kept per save slot.
 *
 * Work RAM used (between DWR's $6800-$6815 block and the music driver at $6900):
 *   $6820-$6821  items received (live)
 *   $6822-$6825  chest bits, bit n = chest n of rom->chests (31 chests)
 *   $6826        search spot bits, bit n = search table entry n
 *   $6827        1 between a continue's load and game start
 *   $6828-$682d  items received, saved copy for save slots 0-2
 *   $682e        mailbox: 1 when the client has a "received" message waiting
 *   $6830-$6840  unused (the found-message buffer before ROM version 005)
 *   $6842-$6845  seed hash of the ROM that started this game (written by the
 *                ROM at a new game), so a save from another seed is caught
 *   $6846-$684b  slot tags, 2 bytes each: seed hash bytes 0 and 1 of the ROM that started
 *                each slot (stride 2, like $6828), for the client to compare
 *   $6850-$6876  mailbox text, a sentence in DW tiles, ended by $FA
 *   $6880-$68a6  found-message buffer, the location's sentence ended by $FA
 *
 * The ROM signature at PRG $7FE0: "DWRAP", 3 version digits, then a little
 * endian pointer to the 32 byte slot name (UTF-8, zero padded), the music byte
 * (0 no pack, 1 custom music went in, 2 frame-only music, 3 the pack was left
 * out), the 4 byte seed hash (little endian, never 0), then the herb
 * cap in the high nibble and the key cap in the low nibble.
 *
 * Based on Serpikmin's ap_changes() for the Dragon Warrior APWorld, rewritten
 * for DWR's chest and search code.
 */

#include <ctype.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "dwr.h"
#include "patch.h"
#include "dungeon_maps.h"
#include "archipelago.h"
#include "expansion.h"

#define LO(a) ((a) & 0xff)
#define HI(a) (((a) >> 8) & 0xff)

#define AP_RECV       0x6820
#define AP_CHESTS     0x6822
#define AP_SEARCH     0x6826
#define AP_LOADED     0x6827
#define AP_SLOT_RECV  0x6828
#define AP_MAIL_FLAG  0x682e
#define AP_SEED_HASH  0x6842
#define AP_SLOT_TAG   0x6846
#define SAVE_SLOT     0x6039

#define AP_VERSION    "005"  /* 005: the found message and the mailbox print whole sentences */
#define AP_NAME_LEN   32  /* UTF-8 bytes. Archipelago cuts slot names to 16 characters; longer byte strings are cut at a character */
/* The unused Tablet: its index in the item name list, and its 6 letters. */
#define TABLET_NAME   24
#define TABLET_TEXT   0x7b56

#define AP_LOC_BASE   0xd0000
#define AP_LOC_SEARCH 0x40

static BOOL ap_enabled = FALSE;
static char ap_slot_name[AP_NAME_LEN + 1] = "";
static BOOL ap_have_text = FALSE;
static char ap_flags[64] = "";
static BOOL ap_have_seed = FALSE;
static uint64_t ap_seed;
static int ap_free_charlock = -1;
static char ap_text[AP_TEXT_LOCS][AP_TEXT_LEN + 1];
/* Where the AP code starts in bank 3 for this build ($C000 when AP is off); the
 * found-message names follow it. Custom music reads it after ap_changes has run. */
static uint16_t ap_bank3_start = 0xc000;
/* AP code in bank 3, from ap_bank3_start up, below the item names: the game-start routine (see
 * ap_save_and_load), the bit table and found-item routine (ap_chest_and_search). Each runs through a
 * small fixed-bank stub that maps bank 3 and maps the old bank back. */
#define AP_START_SIZE 85
#define AP_B3_BITS    (AP_START_SIZE)
#define AP_B3_FOUND   (AP_B3_BITS + 8)
#define AP_FOUND_SIZE 64
/* Game-start code other features hand over (dwr_ap_start_init), then a jmp to the game-start routine. */
#define AP_B3_INIT    (AP_B3_FOUND + AP_FOUND_SIZE)
#define AP_INIT_MAX   40
#define AP_B3_CODE    (AP_B3_INIT + AP_INIT_MAX + 3)
static uint8_t ap_init_code[AP_INIT_MAX];
static int ap_init_len = 0;
/* Where the found-message routine sets $A3 from A, so the mailbox can share its tail. */
static uint16_t ap_show_tail = 0;

/* Forgets the game-start code of the last build. Each build calls it before its features run. */
void dwr_ap_start_init_reset(void)
{
    ap_init_len = 0;
}

/* Takes code that must run once at game start ($CA1A) and runs it from bank 3, ahead of the AP
 * game-start routine, so it needs no room in the fixed bank. The code must work at any address, use
 * only RAM, and end by falling through (no rts, no jmp). A, X and Y are free. FALSE when this is not
 * an Archipelago build or the list is full: the caller then places its own hook. */
BOOL dwr_ap_start_init(const uint8_t *code, int n)
{
    if (!ap_enabled || ap_init_len + n > AP_INIT_MAX)
        return FALSE;
    memcpy(ap_init_code + ap_init_len, code, (size_t)n);
    ap_init_len += n;
    return TRUE;
}

void dwr_set_ap(BOOL enabled, const char *slot_name)
{
    ap_enabled = enabled;
    if (!enabled) {
        /* Off clears everything a patch file set, so a later build in the same
         * process (such as the site) starts clean. */
        ap_slot_name[0] = '\0';
        ap_have_text = FALSE;
        ap_flags[0] = '\0';
        ap_have_seed = FALSE;
        ap_free_charlock = -1;
        ap_bank3_start = 0xc000;
        memset(ap_text, 0, sizeof(ap_text));
    }
    if (slot_name) {
        size_t len = strlen(slot_name);

        if (len > AP_NAME_LEN) {
            /* Cut at a whole UTF-8 character. The APWorld rejects names this
             * long, so this only guards a hand-made patch file. */
            len = AP_NAME_LEN;
            while (len > 0 && ((unsigned char)slot_name[len] & 0xc0) == 0x80)
                len--;
        }
        memcpy(ap_slot_name, slot_name, len);
        ap_slot_name[len] = '\0';
    }
}

BOOL dwr_ap_enabled(void)
{
    return ap_enabled;
}

BOOL dwr_load_ap_patch(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256];
    unsigned long id;
    int n, pos, len, whole_sentences = 0, i;

    if (!f)
        return FALSE;
    memset(ap_text, 0, sizeof(ap_text));
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!strcmp(line, "text 2")) {
            /* The patch holds whole sentences (written by DWR AP version 005 and later). */
            whole_sentences = 1;
        } else if (!strncmp(line, "slot ", 5)) {
            dwr_set_ap(TRUE, line + 5);
        } else if (!strncmp(line, "flags ", 6)) {
            strncpy(ap_flags, line + 6, sizeof(ap_flags) - 1);
        } else if (!strncmp(line, "seed ", 5)) {
            ap_have_seed = sscanf(line + 5, "%" SCNu64, &ap_seed) == 1;
        } else if (!strncmp(line, "free_charlock ", 14)) {
            ap_free_charlock = atoi(line + 14) ? 1 : 0;
        } else if (sscanf(line, "loc %lx %n", &id, &pos) == 1) {
            if (id >= AP_LOC_BASE && id < AP_LOC_BASE + CHEST_COUNT)
                n = id - AP_LOC_BASE;
            else if (id >= AP_LOC_BASE + AP_LOC_SEARCH
                    && id < AP_LOC_BASE + AP_LOC_SEARCH + 3)
                n = CHEST_COUNT + id - AP_LOC_BASE - AP_LOC_SEARCH;
            else
                continue;  /* Gwaelin and other events have no found message. */
            len = strlen(line + pos);
            if (len > AP_TEXT_LEN)
                len = AP_TEXT_LEN;
            memcpy(ap_text[n], line + pos, len);
            ap_text[n][len] = '\0';
            ap_have_text = TRUE;
            /* Version 005 patches made before the "text 2" line existed still hold sentences, and every
             * sentence ends in ".". The item names in older patches never do. */
            if (len && ap_text[n][len - 1] == '.')
                whole_sentences = 1;
        }
    }
    fclose(f);
    if (ap_have_text && !whole_sentences) {
        /* An older patch holds only the item name ("Herb"): make it the sentence "Thou hast found the Herb." */
        char sentence[AP_TEXT_LEN + 1];
        for (i = 0; i < AP_TEXT_LOCS; i++) {
            if (ap_text[i][0]) {
                snprintf(sentence, sizeof(sentence), "Thou hast found the %s.", ap_text[i]);
                memcpy(ap_text[i], sentence, sizeof(sentence));
            }
        }
    }
    return ap_enabled;
}

const char *dwr_ap_patch_flags(void)
{
    return ap_flags[0] ? ap_flags : NULL;
}

BOOL dwr_ap_patch_seed(uint64_t *seed)
{
    if (ap_have_seed)
        *seed = ap_seed;
    return ap_have_seed;
}

int dwr_ap_patch_free_charlock(void)
{
    return ap_free_charlock;
}

/*
 * Chest and search code. Both end in message $D9, which dwr_ap_found_frame
 * rewrites to "Fortune smiles upon thee, <hero>.", a new line, then $F9. $F9's
 * handler is rewritten to print the $FA-terminated text that $A3/$A4 point to.
 * Each chest and search spot gets its own sentence from the .apdwr patch file
 * ("Thou hast found the Herb.", "Heart Container for Alex."), or
 * "Thou hast found an item." without one.
 *
 * Bank 1 has no room for the names, so they sit at the top of expansion bank
 * 3: a low and a high pointer table, then the names end to end, ending at
 * $BFFF. The show routine maps bank 3 and calls the found routine there, which
 * sets the location bit and copies the name into work RAM at AP_NAME_BUF. It then
 * maps the old bank back and points $A3/$A4 at the copy.
 */
#define AP_NAME_BUF      0x6880  /* AP_TEXT_LEN + 1 bytes, below the music driver at $6900 */
#define AP_NAME_BANK     3
#define LOAD_PRG_BANK    0xff91  /* store_and_load_prg_bank: A = bank */
#define FOUND_FRAME      0xab5b  /* message $D9 from its $FD: "<FD>Thou hast found the <F7>.<FC>" */
#define F9_HANDLER       0x77e8  /* bank 1, $B7E8, 17 bytes */
_Static_assert(AP_BANK3_NAMES == 0xc000 - AP_TEXT_LOCS * (AP_TEXT_LEN + 1 + 2),
        "AP_BANK3_NAMES must be the room for the longest names");

/* $7FEA: 1 when the pack's music has its wait loop, 2 when it runs from the frame only, 3 when the pack was left out. */
void dwr_ap_mark_music(dw_rom *rom, int state)
{
    if (ap_enabled && !memcmp(&rom->content[0x7fe0], "DWRAP", 5))
        rom->content[0x7fea] = (uint8_t)state;
}

uint16_t dwr_ap_bank3_start(void)
{
    return ap_enabled ? ap_bank3_start : 0xc000;
}

size_t ascii2dw(uint8_t *string);

/* The text for location i, in DW tiles and ended by $FA. Returns its length
 * with the $FA. */
static size_t ap_name_tiles(int i, uint8_t *buf)
{
    const char *text = ap_have_text && ap_text[i][0] ? ap_text[i] : "Thou hast found an item.";
    size_t j, len = strlen(text);

    if (len > AP_TEXT_LEN)
        len = AP_TEXT_LEN;
    for (j = 0; j < len; j++) {
        char c = text[j];
        /* Letters, digits and a few marks exist in the font, the rest become spaces. */
        buf[j] = (isalnum((unsigned char)c) || strchr(" '.,-!?", c)) ? c : ' ';
    }
    buf[len] = 0;
    ascii2dw(buf);
    buf[len] = 0xfa;
    return len + 1;
}

static void ap_chest_and_search(dw_rom *rom)
{
    uint16_t bits, chest, show, search, found, tail, at, name_lo, name_hi;
    uint8_t *bank3 = &rom->expansion[(AP_NAME_BANK - 3) * 0x4000];
    uint8_t names[AP_TEXT_LOCS][AP_TEXT_LEN + 2];
    size_t lens[AP_TEXT_LOCS], total = 2 * AP_TEXT_LOCS;
    int i;

    /* Names end to end, so short names leave more of bank 3 for music. */
    for (i = 0; i < AP_TEXT_LOCS; i++)
        total += lens[i] = ap_name_tiles(i, names[i]);
    /* The AP code in bank 3 (AP_B3_CODE bytes) sits just below the names. */
    ap_bank3_start = (uint16_t)(0xc000 - total - AP_B3_CODE);
    name_lo = ap_bank3_start + AP_B3_CODE;
    name_hi = name_lo + AP_TEXT_LOCS;
    at = name_hi + AP_TEXT_LOCS;
    /* Everything from the AP code up to $BFFF must be free before anything is written there. */
    for (i = ap_bank3_start - 0x8000; i < 0x4000; i++) {
        if (bank3[i] != 0xff)
            dwr_give_up("AP: bank 3 is not empty where the AP code and item names go.");
    }
    for (i = 0; i < AP_TEXT_LOCS; i++) {
        if (bank3[at - 0x8000] != 0xff)
            dwr_give_up("AP: bank 3 is not empty where the item names go.");
        memcpy(&bank3[at - 0x8000], names[i], lens[i]);
        bank3[name_lo - 0x8000 + i] = LO(at);
        bank3[name_hi - 0x8000 + i] = HI(at);
        at = (uint16_t)(at + lens[i]);
    }

    /* The found-item code. In the fixed bank: a chest entry ($E221, Y = chest * 4), a search entry
     * ($E12D, X = search entry), the common part and the tail the mailbox shares. In bank 3: the bit
     * table and the routine that sets the location bit and copies the name to AP_NAME_BUF. $E355 marks
     * the chest open and plays its sound, so it runs from the fixed bank with the game's bank mapped. */
    bits = ap_bank3_start + AP_B3_BITS;
    found = ap_bank3_start + AP_B3_FOUND;
    {
        static const uint8_t bit_table[8] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};
        const uint8_t found_code[] = {
            0xe0, CHEST_COUNT,                  /* 0:  cpx #31: a search?    */
            0xb0, 0x17,                         /* 2:  bcs srch (27)         */
            0x8a, 0x29, 0x07, 0xa8,             /* 4:  txa, and #7, tay      */
            0xb9, LO(bits), HI(bits),           /* 8:  lda bits,y            */
            0x48,                               /* 11: pha                   */
            0x8a, 0x4a, 0x4a, 0x4a, 0xa8,       /* 12: txa, lsr x3, tay      */
            0x68,                               /* 17: pla                   */
            0x19, LO(AP_CHESTS), HI(AP_CHESTS), /* 18: ora AP_CHESTS,y       */
            0x99, LO(AP_CHESTS), HI(AP_CHESTS), /* 21: sta AP_CHESTS,y       */
            0x4c, LO(found + 41), HI(found + 41), /* 24: jmp name (41)       */
            0x8a, 0x38, 0xe9, CHEST_COUNT, 0xa8, /* 27: srch: txa, sec, sbc #31, tay */
            0xb9, LO(bits), HI(bits),           /* 32: lda bits,y            */
            0x0d, LO(AP_SEARCH), HI(AP_SEARCH), /* 35: ora AP_SEARCH         */
            0x8d, LO(AP_SEARCH), HI(AP_SEARCH), /* 38: sta AP_SEARCH         */
            0xbd, LO(name_lo), HI(name_lo),     /* 41: name: lda lo,x        */
            0x85, 0xa3,                         /* 44: sta $a3               */
            0xbd, LO(name_hi), HI(name_hi),     /* 46: lda hi,x              */
            0x85, 0xa4,                         /* 49: sta $a4               */
            0xa0, 0x00,                         /* 51: ldy #0                */
            0xb1, 0xa3,                         /* 53: - lda ($a3),y         */
            0x99, LO(AP_NAME_BUF), HI(AP_NAME_BUF), /* 55: sta buf,y         */
            0xc8,                               /* 58:   iny                 */
            0xc9, 0xfa,                         /* 59:   cmp #$fa            */
            0xd0, 0xf6,                         /* 61:   bne - (53)          */
            0x60                                /* 63: rts                   */
        };
        memcpy(&bank3[bits - 0x8000], bit_table, sizeof(bit_table));
        _Static_assert(sizeof found_code == AP_FOUND_SIZE, "found code size");
        memcpy(&bank3[found - 0x8000], found_code, sizeof(found_code));
    }

    /* The tail sets $A3 from A and shows message $D9. The mailbox jumps here too. */
    tail = find_free_space(rom->content, FIXED_FREE_START, 11);
    vpatch(rom, tail, 11,
        0x85, 0xa3,                         /* sta $a3                   */
        0xa9, HI(AP_NAME_BUF),              /* lda #>buf                 */
        0x85, 0xa4,                         /* sta $a4                   */
        0xa9, 0xd9,                         /* lda #$d9: found message   */
        0x4c, 0x42, 0xd2                    /* jmp $d242                 */
    );
    ap_show_tail = tail;

    /* $F9 prints the text at ($A3). dwr_ap_found_frame puts $F9 in message $D9 at the end of the build. */
    vpatch(rom, F9_HANDLER, 17,
        0xa0, 0x00,                         /* ldy #0                    */
        0xb1, 0xa3,                         /* - lda ($a3),y             */
        0x99, 0x2c, 0x65,                   /*   sta $652c,y             */
        0xc8,                               /*   iny                     */
        0xc9, 0xfa,                         /*   cmp #$fa                */
        0xd0, 0xf6,                         /*   bne -                   */
        0xa2, 0x00,                         /* ldx #0                    */
        0x4c, 0x67, 0xb6                    /* jmp $b667                 */
    );

    /* A < $FF for a chest, $FF for a search, X = name index. */
    show = find_free_space(rom->content, FIXED_FREE_START, 34);
    vpatch(rom, show, 34,
        0x85, 0x0e,                         /* sta $0e                   */
        0xa9, 0x10,                         /* lda #$10: any real item   */
        0x85, 0xde,                         /* sta $de                   */
        0x8a, 0x48,                         /* txa, pha                  */
        0x20, 0x55, 0xe3,                   /* jsr $e355                 */
        0x68, 0xaa,                         /* pla, tax                  */
        0xad, 0x04, 0x60,                   /* lda $6004: current bank   */
        0x48,                               /* pha                       */
        0xa9, AP_NAME_BANK,                 /* lda #3                    */
        0x20, LO(LOAD_PRG_BANK), HI(LOAD_PRG_BANK), /* jsr load bank (keeps X) */
        0x20, LO(found), HI(found),         /* jsr found (bank 3)        */
        0x68,                               /* pla: old bank             */
        0x20, LO(LOAD_PRG_BANK), HI(LOAD_PRG_BANK), /* jsr load bank     */
        0xa9, LO(AP_NAME_BUF),              /* lda #<buf                 */
        0x4c, LO(tail), HI(tail)            /* jmp tail                  */
    );

    /* Hooked at $E221, where the chest at Y/4 was found. A = X = chest index. */
    chest = find_free_space(rom->content, FIXED_FREE_START, 7);
    vpatch(rom, chest, 7,
        0x98, 0x4a, 0x4a, 0xaa,             /* tya, lsr, lsr, tax        */
        0x4c, LO(show), HI(show)            /* jmp show                  */
    );

    /* Hooked at $E12D, where search entry X matched. Search names follow the chests. */
    search = find_free_space(rom->content, FIXED_FREE_START, 10);
    vpatch(rom, search, 10,
        0x8a, 0x18,                         /* txa, clc                  */
        0x69, CHEST_COUNT,                  /* adc #31: search names     */
        0xaa,                               /* tax                       */
        0xa9, 0xff,                         /* lda #$ff: a search        */
        0x4c, LO(show), HI(show)            /* jmp show                  */
    );

    vpatch(rom, 0xe221, 3, 0x4c, LO(chest), HI(chest));
    vpatch(rom, 0xe12d, 3, 0x4c, LO(search), HI(search));

    /* All 3 search spots hold something, so the search code finds them. */
    rom->search_table->item[0] = HERB;
    rom->search_table->item[1] = HERB;
    rom->search_table->item[2] = HERB;
}

/* Message $D9 is "Fortune smiles upon thee, <F8>.<FD>Thou hast found the <F7>.<FC>". The part from
 * its $FD becomes "<FD><F9><FC>": the sentence from the found buffer or the mailbox, on its own line.
 * That is 21 bytes shorter, so messages $DA-$DF move down by 21. The game finds a message by counting
 * $FC and $FF bytes from the start of its block of 16, so the move changes no other block. The 21
 * bytes freed after $DF are never read. */
void dwr_ap_found_frame(dw_rom *rom)
{
    uint8_t *frame = &rom->content[FOUND_FRAME];
    const int old_len = 24, new_len = 3;  /* <FD>, 20 letters, <F7>, ".", <FC>  vs  <FD><F9><FC> */
    int end, ends;
    /* <FD>Thou hast found the <F7>.<FC>, as in the vanilla ROM. */
    static const uint8_t vanilla[24] = {
        0xfd, 0x37, 0x11, 0x18, 0x1e, 0x5f, 0x11, 0x0a, 0x1c, 0x1d, 0x5f, 0x0f,
        0x18, 0x1e, 0x17, 0x0d, 0x5f, 0x1d, 0x11, 0x0e, 0x5f, 0xf7, 0x47, 0xfc };
    _Static_assert(sizeof vanilla == 24, "old_len bytes of message $D9");

    if (!ap_enabled)
        return;
    if (memcmp(frame, vanilla, old_len)) {
        /* The vanilla message is not where the rewrite expects it: a broken ROM would print doubled
         * text while its signature says version 005, so stop the build instead. */
        dwr_give_up("AP: the found message is not the one this build expects.");
    }
    /* The end of message $DF: 6 more message ends after $D9's. */
    for (end = old_len, ends = 0; ends < 6; end++) {
        if (FOUND_FRAME + end >= 0xc000)
            dwr_give_up("AP: the messages after the found message run past bank 2.");
        if (frame[end] == 0xfc || frame[end] == 0xff)
            ends++;
    }
    frame[1] = 0xf9;
    frame[2] = 0xfc;
    memmove(&frame[new_len], &frame[old_len], (size_t)(end - old_len));
    memset(&frame[end - (old_len - new_len)], 0x5f, (size_t)(old_len - new_len));
}

/*
 * The received counter goes into the save slot on save, and LoadSavedData
 * ($FB6B) brings it back. $FB6B also runs at power-on ($F781 checks every slot
 * through $FAC1) and on erase ($F8FB), so it cannot mark a continue. The
 * Continue menu's load ($F8BE: jsr $FAC1, rts) sets AP_LOADED instead.
 * Game start ($CA1A) runs after it: without AP_LOADED it is a new game, which
 * clears the counter and the location bits. XP is not used, so a save made at
 * 0 XP continues correctly.
 */
/* A slot's tag: the seed hash's first two bytes, except that the first is
 * never 0 or $FF, which mean "no tag". */
static void ap_slot_tag(uint32_t hash, uint8_t tag[2])
{
    tag[0] = hash & 0xff;
    tag[1] = (hash >> 8) & 0xff;
    if (tag[0] == 0 || tag[0] == 0xff)
        tag[0] = 0x5a;
}

static uint32_t ap_seed_hash(dw_rom *rom)
{
    uint32_t hash = (uint32_t)(rom->seed ^ (rom->seed >> 32));

    return (hash && hash != 0xffffffff) ? hash : 1;   /* 0 and $FF.. in RAM mean no hash yet */
}

static void ap_save_and_load(dw_rom *rom)
{
    uint16_t save, restore, start, entry, chain, cont;
    int stub, in3;
    uint32_t hash = ap_seed_hash(rom);
    uint8_t tag[2];

    ap_slot_tag(hash, tag);

    /* Y is free on entry: the moved code starts with ldy #0. */
    save = find_free_space(rom->content, FIXED_FREE_START, 22);
    vpatch(rom, save, 22,
        0xad, LO(SAVE_SLOT), HI(SAVE_SLOT),           /* lda slot        */
        0x0a, 0xa8,                                   /* asl, tay        */
        0xad, LO(AP_RECV), HI(AP_RECV),               /* lda recv        */
        0x99, LO(AP_SLOT_RECV), HI(AP_SLOT_RECV),     /* sta slot_recv,y */
        0xad, LO(AP_RECV + 1), HI(AP_RECV + 1),
        0x99, LO(AP_SLOT_RECV + 1), HI(AP_SLOT_RECV + 1),
        0xa0, 0x00,                                   /* ldy #0 (moved)  */
        0xa5, 0xba,                                   /* lda $ba (moved) */
        0x60                                          /* rts             */
    );
    /* SaveData at $FA18 was ldy #0, lda $ba. */
    vpatch(rom, 0xfa18, 4, 0x20, LO(save), HI(save), 0xea);

    restore = find_free_space(rom->content, FIXED_FREE_START, 22);
    vpatch(rom, restore, 22,
        0xad, LO(SAVE_SLOT), HI(SAVE_SLOT),           /* lda slot        */
        0x0a, 0xa8,                                   /* asl, tay        */
        0xb9, LO(AP_SLOT_RECV), HI(AP_SLOT_RECV),     /* lda slot_recv,y */
        0x8d, LO(AP_RECV), HI(AP_RECV),
        0xb9, LO(AP_SLOT_RECV + 1), HI(AP_SLOT_RECV + 1),
        0x8d, LO(AP_RECV + 1), HI(AP_RECV + 1),
        0xa0, 0x00,                                   /* ldy #0 (moved)  */
        0xb1, 0x22,                                   /* lda ($22),y (moved) */
        0x60                                          /* rts             */
    );
    /* LoadSavedData at $FB6B was ldy #0, lda ($22),y. */
    vpatch(rom, 0xfb6b, 4, 0x20, LO(restore), HI(restore), 0xea);

    /* The Continue menu loads the chosen slot with jsr $FAC1 at $F8BE, then rts. */
    if (rom->content[0xf8be] == 0x20 && rom->content[0xf8bf] == 0xc1
            && rom->content[0xf8c0] == 0xfa) {
        cont = find_free_space(rom->content, FIXED_FREE_START, 9);
        vpatch(rom, cont, 9,
            0x20, 0xc1, 0xfa,                         /* jsr $fac1 (moved) */
            0xa9, 0x01,                               /* lda #1          */
            0x8d, LO(AP_LOADED), HI(AP_LOADED),       /* sta loaded      */
            0x60                                      /* rts             */
        );
        vpatch(rom, 0xf8be, 3, 0x4c, LO(cont), HI(cont));  /* jmp; $F8C1 rts is now unused */
    } else {
        printf("AP: unexpected code at $F8BE, a continue will be treated as a new game\n");
    }

    /* New Quest stores its slot with sta $6039 at $F8CE. A Continue that was
     * reset before the game started leaves the loaded flag set, so clear it here
     * too, or this new game would keep the old counter and location bits. The
     * counter is zeroed too: New Quest saves the hero before the game starts. */
    if (rom->content[0xf8ce] == 0x8d && rom->content[0xf8cf] == 0x39
            && rom->content[0xf8d0] == 0x60) {
        uint16_t quest = find_free_space(rom->content, FIXED_FREE_START, 18);
        vpatch(rom, quest, 18,
            0x8d, 0x39, 0x60,                         /* sta $6039 (moved) */
            0xa9, 0x00,                               /* lda #0          */
            0x8d, LO(AP_LOADED), HI(AP_LOADED),       /* sta loaded      */
            0x8d, LO(AP_RECV), HI(AP_RECV),           /* sta recv        */
            0x8d, LO(AP_RECV + 1), HI(AP_RECV + 1),
            0xad, 0x39, 0x60,                         /* lda $6039: A and flags as before */
            0x60                                      /* rts             */
        );
        vpatch(rom, 0xf8ce, 3, 0x20, LO(quest), HI(quest));
    } else {
        printf("AP: unexpected code at $F8CE, a reset during Continue can carry its state into a new game\n");
    }

    /* Game start ($CA1A, a jsr to the code that loads the throne room). The AP
     * work runs first, so nothing sees the old state, then it jumps on. X and Y
     * are free here: nothing in the chain reads them.
     * New game: tag the slot with this ROM's seed, clear the location bits and
     * write this ROM's seed hash. New Quest already zeroed the counter, and its
     * save zeroed the slot's saved counter.
     * Continue: when this ROM tagged the slot but another seed started a game
     * since (the hash differs), the location bits are that game's, so clear
     * them and write this ROM's hash. Only New Game tags a slot, so a slot
     * keeps the seed it was started with. */
    chain = rom->content[0xca1b] | (rom->content[0xca1c] << 8);
    /* Bank 3 holds the routine when a 19-byte stub fits in the fixed bank: the stub switches to bank 3,
     * runs the routine, switches back and jumps to the game's code. Otherwise the routine stays in the
     * fixed bank, as before. */
    stub = dwr_try_free_space(rom->content, FIXED_FREE_START, 19);
    in3 = stub >= 0;
    /* In the fixed bank the routine ends in the seed hash, whose last byte can be $FF. One byte of 0 after it
     * keeps the free space search from handing that byte out. */
    start = in3 ? ap_bank3_start : (uint16_t)find_free_space(rom->content, FIXED_FREE_START, AP_START_SIZE + 1);
    /* The handed-over game-start code runs first and then jumps to the routine. */
    entry = start;
    if (ap_init_len) {
        const uint8_t go[3] = {0x4c, LO(start), HI(start)};

        if (in3) {
            uint8_t *b3 = &rom->expansion[(AP_NAME_BANK - 3) * 0x4000];

            entry = ap_bank3_start + AP_B3_INIT;
            memcpy(&b3[entry - 0x8000], ap_init_code, (size_t)ap_init_len);
            memcpy(&b3[entry - 0x8000 + ap_init_len], go, 3);
        } else {
            entry = find_free_space(rom->content, FIXED_FREE_START, (uint8_t)(ap_init_len + 3));
            patch(rom, entry, (size_t)ap_init_len, ap_init_code);
            patch(rom, entry + ap_init_len, 3, go);
        }
    }
    uint8_t code[] = {
        0xad, LO(SAVE_SLOT), HI(SAVE_SLOT),           /* 0:  lda slot        */
        0x0a, 0xaa,                                   /* 3:  asl, tax        */
        0xa9, 0x00,                                   /* 5:  lda #0          */
        0x8d, LO(AP_MAIL_FLAG), HI(AP_MAIL_FLAG),     /* 7:  stale mail flag */
        0xac, LO(AP_LOADED), HI(AP_LOADED),           /* 10: ldy loaded      */
        0x8d, LO(AP_LOADED), HI(AP_LOADED),           /* 13: sta loaded (0)  */
        0xd0, 0x22,                                   /* 16: bne cont (52)   */
        0xa9, tag[0],                                 /* 18: lda #tag        */
        0x9d, LO(AP_SLOT_TAG), HI(AP_SLOT_TAG),       /* 20: sta slot_tag,x  */
        0xa9, tag[1],                                 /* 23: lda #tag+1      */
        0x9d, LO(AP_SLOT_TAG + 1), HI(AP_SLOT_TAG + 1), /* 25: sta slot_tag+1,x */
        0xa9, 0x00,                                   /* 28: wipe: lda #0    */
        0xa2, AP_SEARCH - AP_CHESTS,                  /* 30: ldx #4          */
        0x9d, LO(AP_CHESTS), HI(AP_CHESTS),           /* 32: - sta chests,x (and search) */
        0xca,                                         /* 35:   dex           */
        0x10, 0xfa,                                   /* 36:   bpl - (32)    */
        0xa2, 0x03,                                   /* 38: ldx #3          */
        0xbd, LO(start + 81), HI(start + 81),         /* 40: - lda hash,x    */
        0x9d, LO(AP_SEED_HASH), HI(AP_SEED_HASH),     /* 43:   sta seed_hash,x */
        0xca,                                         /* 46:   dex           */
        0x10, 0xf7,                                   /* 47:   bpl - (40)    */
        in3 ? 0x60 : 0x4c, in3 ? 0xea : LO(chain), in3 ? 0xea : HI(chain),  /* 49: go: rts, or jmp chain */
        0xbd, LO(AP_SLOT_TAG), HI(AP_SLOT_TAG),       /* 52: cont: lda slot_tag,x */
        0xc9, tag[0],                                 /* 55: cmp #tag        */
        0xd0, 0xf6,                                   /* 57: bne go (49)     */
        0xbd, LO(AP_SLOT_TAG + 1), HI(AP_SLOT_TAG + 1), /* 59: lda slot_tag+1,x */
        0xc9, tag[1],                                 /* 62: cmp #tag+1      */
        0xd0, 0xef,                                   /* 64: bne go (49)     */
        0xa2, 0x03,                                   /* 66: ldx #3          */
        0xbd, LO(start + 81), HI(start + 81),         /* 68: - lda hash,x    */
        0xdd, LO(AP_SEED_HASH), HI(AP_SEED_HASH),     /* 71:   cmp seed_hash,x */
        0xd0, 0xd0,                                   /* 74:   bne wipe (28) */
        0xca,                                         /* 76:   dex           */
        0x10, 0xf5,                                   /* 77:   bpl - (68)    */
        0x30, 0xe0,                                   /* 79: bmi go (49)     */
        hash & 0xff, (hash >> 8) & 0xff, (hash >> 16) & 0xff, hash >> 24  /* 81: hash */
    };
    _Static_assert(sizeof code == AP_START_SIZE, "start routine size");
    if (in3) {
        uint8_t *bank3 = &rom->expansion[0];
        uint8_t stub_code[] = {
            0xad, 0x04, 0x60,                         /* lda $6004: the current bank  */
            0x48,                                     /* pha                          */
            0xa9, AP_NAME_BANK,                       /* lda #3                       */
            0x20, LO(LOAD_PRG_BANK), HI(LOAD_PRG_BANK), /* jsr load bank              */
            0x20, LO(entry), HI(entry),               /* jsr the routine in bank 3    */
            0x68,                                     /* pla: the old bank            */
            0x20, LO(LOAD_PRG_BANK), HI(LOAD_PRG_BANK), /* jsr load bank              */
            0x4c, LO(chain), HI(chain)                /* jmp the game's code          */
        };
        _Static_assert(sizeof stub_code == 19, "start stub size");
        memcpy(&bank3[start - 0x8000], code, AP_START_SIZE);
        patch(rom, (uint16_t)stub, sizeof(stub_code), stub_code);
        vpatch(rom, 0xca1a, 3, 0x20, LO(stub), HI(stub));
    } else {
        patch(rom, start, AP_START_SIZE, code);
        vpatch(rom, start + AP_START_SIZE, 1, 0x00);
        vpatch(rom, 0xca1a, 3, 0x20, LO(entry), HI(entry));
    }
}

/* "DWRAP", version, pointer to the slot name. */
static void ap_signature(dw_rom *rom)
{
    uint16_t name = find_free_space_in(rom->content, 0x7f0a, BANK_1_FREE_END,
            AP_NAME_LEN);
    uint8_t padded[AP_NAME_LEN] = { 0 };

    uint32_t hash = ap_seed_hash(rom);
    /* The carry caps Random Herb Carry and Random Key Carry may have set. */
    uint8_t caps = (uint8_t)((rom->content[0xd6fa] << 4) | (rom->content[0xd80e] & 0x0f));

    memcpy(padded, ap_slot_name, strlen(ap_slot_name));
    patch(rom, name, AP_NAME_LEN, padded);
    vpatch(rom, 0x7fe0, 16, 'D', 'W', 'R', 'A', 'P',
            AP_VERSION[0], AP_VERSION[1], AP_VERSION[2],
            LO(name), HI(name), 0,
            hash & 0xff, (hash >> 8) & 0xff, (hash >> 16) & 0xff, hash >> 24, caps);
}

/*
 * Mailbox for received items. The client gives the item through RAM, writes
 * a sentence ("Heart Container from Alex.") to AP_MAIL_TEXT and sets AP_MAIL_FLAG.
 * After the hero's next step the game shows message $D9 with that sentence and goes
 * back to the field.
 *
 * The hook replaces lda $e4, and #$04 at $CDBF, in the after-step code, which
 * the field loop calls with jsr. Town, cave and stair tiles jump away before
 * $CDBF, so the message never interrupts a map change. To show the message the
 * hook drops its own return address and jumps to $D242 (show message), which
 * ends in $CF6A's rts back to the field loop, like a command does.
 *
 * $D242 prints into the dialog window and expects it open: every command opens
 * it first with jsr $C6F0, .byte 2. Without it the text goes over whatever the
 * last window left on screen, with no frame, and the NPCs keep walking over it.
 * The game's own $CBEE does lda #$FF, sta $96 (NPCs stop), opens window 2 and
 * returns, so the hook calls it. lsr tests and clears the flag in one go, so
 * the client must write 1.
 */
#define AP_MAIL_TEXT 0x6850
_Static_assert(((AP_MAIL_TEXT) >> 8) == ((AP_NAME_BUF) >> 8), "the mailbox shares the show routine's $A4");
_Static_assert(AP_TEXT_LEN + 1 <= 40, "a sentence and its $FA fit the game's text buffer at $652C-$6553");
_Static_assert(AP_MAIL_TEXT + AP_TEXT_LEN < AP_NAME_BUF, "the mailbox text ends below the found buffer");
_Static_assert(AP_NAME_BUF + AP_TEXT_LEN < 0x6900, "the found buffer ends below the music driver at $6900");

static void ap_mailbox(dw_rom *rom)
{
    uint16_t hook;

    static const uint8_t open_dialog[9] = {0xa9, 0xff, 0x85, 0x96, 0x20, 0xf0, 0xc6, 0x02, 0x60};

    if (rom->content[0xcdbf] != 0xa5 || rom->content[0xcdc0] != 0xe4
            || rom->content[0xcdc1] != 0x29 || rom->content[0xcdc2] != 0x04) {
        printf("AP mailbox: unexpected code at $CDBF, received items will have no message\n");
        return;
    }
    if (memcmp(&rom->content[0xcbee], open_dialog, sizeof open_dialog)) {
        printf("AP mailbox: unexpected code at $CBEE, received items will have no message\n");
        return;
    }
    if (!ap_show_tail) {
        printf("AP mailbox: no found-message routine, received items will have no message\n");
        return;
    }
    /* The show routine's tail sets $A3 from A, $A4 to $68 and shows $D9. */
    hook = find_free_space(rom->content, FIXED_FREE_START, 20);
    vpatch(rom, hook, 20,
        0x4e, LO(AP_MAIL_FLAG), HI(AP_MAIL_FLAG),     /* lsr flag: 1 to 0, C = 1 */
        0xb0, 0x05,                                   /* bcs show        */
        0xa5, 0xe4,                                   /* lda $e4 (moved) */
        0x29, 0x04,                                   /* and #$04 (moved) */
        0x60,                                         /* rts             */
        0x68, 0x68,                                   /* show: pla, pla  */
        0x20, 0xee, 0xcb,                             /* jsr $cbee: stop NPCs, open window 2 */
        0xa9, LO(AP_MAIL_TEXT),                       /* lda #<text      */
        0x4c, LO(ap_show_tail), HI(ap_show_tail)      /* jmp show tail   */
    );
    /* The nop keeps the beq at $CDC3, which tests the moved and #$04. */
    vpatch(rom, 0xcdbf, 4, 0x20, LO(hook), HI(hook), 0xea);
}

void ap_changes(dw_rom *rom)
{
    int i;

    if (!ap_enabled)
        return;

    printf("Setting up Archipelago mode...\n");

    /* No chest despawns because the player holds its item: every chest
     * table entry is a Herb. The hook above never gives it. */
    for (i = 0; i < CHEST_COUNT; i++)
        rom->chests[i].item = HERB;

    /* The throne room key chest is an AP check now, so remove the door. */
    set_dungeon_tile(rom, TANTEGEL_THRONE_ROOM, 4, 7, TOWN_TILE_BRICK);

    /* The Staff of Rain shrine opens with the harp even if the staff or the
     * Rainbow Drop came from the multiworld first. */
    vpatch(rom, 0xd2fc, 3, 0x4c, 0x16, 0xd3);

    /* Best fit: the AP pieces go in the small gaps, so the large ones stay free
     * for the custom music code, whose largest piece is the 107-byte wait loop. */
    {
        BOOL old = dwr_set_packing(TRUE);

        ap_chest_and_search(rom);
        ap_save_and_load(rom);
        ap_mailbox(rom);
        ap_signature(rom);
        dwr_set_packing(old);
    }
}
