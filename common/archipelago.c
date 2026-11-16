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
 *   $682e        mailbox: nonzero when the client has a "received" message waiting
 *   $6830-$6840  found-message name buffer
 *   $6842-$6845  seed hash of the ROM that started this game (written by the
 *                ROM at a new game), so a save from another seed is caught
 *   $6846-$684b  slot tags: low byte of the seed hash of the ROM that last saved
 *                each slot (stride 2, like $6828), for the client to compare
 *   $6850-$6860  mailbox text, the item name in DW tiles, ended by $FA
 *
 * The ROM signature at PRG $7FE0: "DWRAP", 3 version digits, then a little
 * endian pointer to the 32 byte slot name (UTF-8, zero padded), 1 if custom
 * music went in, the 4 byte seed hash (little endian, never 0), then the herb
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

#define AP_VERSION    "003"
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
/* Where the found-message data starts in bank 3 for this build ($C000 when AP
 * is off). Custom music reads it after ap_changes has run. */
static uint16_t ap_bank3_start = 0xc000;
/* Where the found-message routine sets $A3 from A, so the mailbox can share its tail. */
static uint16_t ap_show_tail = 0;

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
    int n, pos, len;

    if (!f)
        return FALSE;
    memset(ap_text, 0, sizeof(ap_text));
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!strncmp(line, "slot ", 5)) {
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
        }
    }
    fclose(f);
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
 * Chest and search code. Both end in message $D9, "Thou hast found the
 * <name>." Its name control code ($F7, which reads a name index from $A3) is
 * changed to $F9, and $F9's handler is rewritten to print the $FA-terminated
 * text that $A3/$A4 point to. Each chest and search spot gets its own text:
 * the item name from the .apdwr patch file, or "APItem" without one.
 *
 * Bank 1 has no room for the names, so they sit at the top of expansion bank
 * 3: a low and a high pointer table, then the names end to end, ending at
 * $BFFF. The show routine maps bank 3, copies the name into work RAM at
 * AP_NAME_BUF, maps the old bank back and points $A3/$A4 at the copy.
 */
#define AP_NAME_BUF      0x6830  /* 17 bytes, below the music driver */
#define AP_NAME_BANK     3
#define LOAD_PRG_BANK    0xff91  /* store_and_load_prg_bank: A = bank */
#define FOUND_NAME_CODE  0xab70  /* the $F7 byte in message $D9 */
#define F9_HANDLER       0x77e8  /* bank 1, $B7E8, 17 bytes */
_Static_assert(AP_BANK3_NAMES == 0xc000 - AP_TEXT_LOCS * (AP_TEXT_LEN + 1 + 2),
        "AP_BANK3_NAMES must be the room for the longest names");

void dwr_ap_mark_music(dw_rom *rom)
{
    if (ap_enabled && !memcmp(&rom->content[0x7fe0], "DWRAP", 5))
        rom->content[0x7fea] = 1;
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
    const char *text = ap_have_text && ap_text[i][0] ? ap_text[i] : "APItem";
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
    uint16_t bits, chest, show, search, at, name_lo, name_hi;
    uint8_t *bank3 = &rom->expansion[(AP_NAME_BANK - 3) * 0x4000];
    uint8_t names[AP_TEXT_LOCS][AP_TEXT_LEN + 2];
    size_t lens[AP_TEXT_LOCS], total = 2 * AP_TEXT_LOCS;
    int i;

    /* Names end to end, so short names leave more of bank 3 for music. */
    for (i = 0; i < AP_TEXT_LOCS; i++)
        total += lens[i] = ap_name_tiles(i, names[i]);
    ap_bank3_start = (uint16_t)(0xc000 - total);
    name_lo = ap_bank3_start;
    name_hi = name_lo + AP_TEXT_LOCS;
    at = name_hi + AP_TEXT_LOCS;
    for (i = 0; i < AP_TEXT_LOCS; i++) {
        if (bank3[at - 0x8000] != 0xff)
            printf("AP: bank 3 at $%04X was not empty, the item names overwrite it\n", at);
        memcpy(&bank3[at - 0x8000], names[i], lens[i]);
        bank3[name_lo - 0x8000 + i] = LO(at);
        bank3[name_hi - 0x8000 + i] = HI(at);
        at = (uint16_t)(at + lens[i]);
    }

    /* Four separate pieces: bit table (8), chest (31), show (61), search (19),
     * so they fit in small gaps. The name pointer tables sit in bank 3 with the
     * names, which saves fixed bank room. */
    bits = find_free_space(rom->content, FIXED_FREE_START, 8);
    vpatch(rom, bits, 8, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80);
    show = find_free_space(rom->content, FIXED_FREE_START, 61);
    memset(&rom->content[show], 0, 61);   /* held while the others are placed */
    chest = find_free_space(rom->content, FIXED_FREE_START, 31);
    memset(&rom->content[chest], 0, 31);
    search = find_free_space(rom->content, FIXED_FREE_START, 19);

    /* $D9 prints its name with $F9, and $F9 prints the text at ($A3). */
    vpatch(rom, FOUND_NAME_CODE, 1, 0xf9);
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

    /* Hooked at $E221, where the chest at Y/4 was found. Goes to show with
     * X = chest index. */
    vpatch(rom, chest, 31,
        0x98,                               /* tya                       */
        0x4a, 0x4a,                         /* lsr, lsr: chest index     */
        0x48,                               /* pha                       */
        0x29, 0x07,                         /* and #7                    */
        0xaa,                               /* tax                       */
        0xbd, LO(bits), HI(bits),           /* lda bits,x                */
        0x85, 0xde,                         /* sta $de (scratch)         */
        0x68, 0x48,                         /* pla, pha                  */
        0x4a, 0x4a, 0x4a,                   /* lsr x3: byte index        */
        0xaa,                               /* tax                       */
        0xa5, 0xde,                         /* lda $de                   */
        0x1d, LO(AP_CHESTS), HI(AP_CHESTS), /* ora AP_CHESTS,x           */
        0x9d, LO(AP_CHESTS), HI(AP_CHESTS), /* sta AP_CHESTS,x           */
        0x68, 0xaa,                         /* pla, tax: chest index; A = the
                                               index too, never $FF, so show
                                               takes it as a chest       */
        0x4c, LO(show), HI(show)            /* jmp show                  */
    );

    /* A < $FF for a chest, $FF for a search, X = name index. $E355 marks the
     * chest open and plays the sound for a chest, and overwrites $A3. */
    vpatch(rom, show, 61,
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
        0xbd, LO(name_lo), HI(name_lo),     /* lda lo,x (bank 3)         */
        0x85, 0xa3,                         /* sta $a3                   */
        0xbd, LO(name_hi), HI(name_hi),     /* lda hi,x (bank 3)         */
        0x85, 0xa4,                         /* sta $a4                   */
        0xa0, 0x00,                         /* ldy #0                    */
        0xb1, 0xa3,                         /* - lda ($a3),y             */
        0x99, LO(AP_NAME_BUF), HI(AP_NAME_BUF), /* sta buf,y             */
        0xc8,                               /*   iny                     */
        0xc9, 0xfa,                         /*   cmp #$fa                */
        0xd0, 0xf6,                         /*   bne -                   */
        0x68,                               /* pla: old bank             */
        0x20, LO(LOAD_PRG_BANK), HI(LOAD_PRG_BANK), /* jsr load bank     */
        0xa9, LO(AP_NAME_BUF),              /* lda #<buf                 */
        0x85, 0xa3,                         /* sta $a3                   */
        0xa9, HI(AP_NAME_BUF),              /* lda #>buf                 */
        0x85, 0xa4,                         /* sta $a4                   */
        0xa9, 0xd9,                         /* lda #$d9: found message   */
        0x4c, 0x42, 0xd2                    /* jmp $d242                 */
    );
    /* The mailbox jumps to the sta $a3 above with its own low byte in A. */
    ap_show_tail = show + 50;
    if (rom->content[ap_show_tail] != 0x85 || rom->content[ap_show_tail + 1] != 0xa3)
        ap_show_tail = 0;

    /* Hooked at $E12D, where search entry X matched. */
    vpatch(rom, search, 19,
        0xbd, LO(bits), HI(bits),           /* lda bits,x                */
        0x0d, LO(AP_SEARCH), HI(AP_SEARCH), /* ora AP_SEARCH             */
        0x8d, LO(AP_SEARCH), HI(AP_SEARCH), /* sta AP_SEARCH             */
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

/*
 * The received counter goes into the save slot on save, and LoadSavedData
 * ($FB6B) brings it back. $FB6B also runs at power-on ($F781 checks every slot
 * through $FAC1) and on erase ($F8FB), so it cannot mark a continue. The
 * Continue menu's load ($F8BE: jsr $FAC1, rts) sets AP_LOADED instead.
 * Game start ($CA1A) runs after it: without AP_LOADED it is a new game, which
 * clears the counter and the location bits. XP is not used, so a save made at
 * 0 XP continues correctly.
 */
static uint8_t ap_slot_tag(uint32_t hash)
{
    uint8_t tag = hash & 0xff;

    return (tag == 0 || tag == 0xff) ? 0x5a : tag;   /* 0 and $FF mean "no tag" */
}

static uint32_t ap_seed_hash(dw_rom *rom)
{
    uint32_t hash = (uint32_t)(rom->seed ^ (rom->seed >> 32));

    return (hash && hash != 0xffffffff) ? hash : 1;   /* 0 and $FF.. in RAM mean no hash yet */
}

static void ap_save_and_load(dw_rom *rom)
{
    uint16_t save, restore, start, chain, cont, copy;
    uint32_t hash = ap_seed_hash(rom);

    /* Y is free on entry: the moved code starts with ldy #0. The slot tag says
     * which seed last saved this slot. */
    save = find_free_space(rom->content, FIXED_FREE_START, 27);
    vpatch(rom, save, 27,
        0xad, LO(SAVE_SLOT), HI(SAVE_SLOT),           /* lda slot        */
        0x0a, 0xa8,                                   /* asl, tay        */
        0xad, LO(AP_RECV), HI(AP_RECV),               /* lda recv        */
        0x99, LO(AP_SLOT_RECV), HI(AP_SLOT_RECV),     /* sta slot_recv,y */
        0xad, LO(AP_RECV + 1), HI(AP_RECV + 1),
        0x99, LO(AP_SLOT_RECV + 1), HI(AP_SLOT_RECV + 1),
        0xa9, ap_slot_tag(hash),                      /* lda #tag        */
        0x99, LO(AP_SLOT_TAG), HI(AP_SLOT_TAG),       /* sta slot_tag,y  */
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

    /* Copy Quest copies a slot's save block from slot $6039 to slot $6031 with
     * no save or load, so the AP counter and tag are copied here. The hook
     * replaces lda $6031 at $F6EA, before sta $6039 makes the copy current. */
    if (rom->content[0xf6ea] == 0xad && rom->content[0xf6eb] == 0x31
            && rom->content[0xf6ec] == 0x60 && rom->content[0xf6ed] == 0x8d) {
        copy = find_free_space(rom->content, FIXED_FREE_START, 32);
        vpatch(rom, copy, 32,
            0xad, 0x39, 0x60, 0x0a, 0xa8,             /* source slot * 2 in Y */
            0xad, 0x31, 0x60, 0x0a, 0xaa,             /* copy slot * 2 in X   */
            0xb9, LO(AP_SLOT_RECV), HI(AP_SLOT_RECV), /* counter          */
            0x9d, LO(AP_SLOT_RECV), HI(AP_SLOT_RECV),
            0xb9, LO(AP_SLOT_RECV + 1), HI(AP_SLOT_RECV + 1),
            0x9d, LO(AP_SLOT_RECV + 1), HI(AP_SLOT_RECV + 1),
            0xb9, LO(AP_SLOT_TAG), HI(AP_SLOT_TAG),   /* tag              */
            0x9d, LO(AP_SLOT_TAG), HI(AP_SLOT_TAG),
            0xad, 0x31, 0x60,                         /* lda $6031 (moved) */
            0x60                                      /* rts              */
        );
        vpatch(rom, 0xf6ea, 3, 0x20, LO(copy), HI(copy));
    } else {
        printf("AP: unexpected code at $F6EA, Copy Quest will not copy the item counter\n");
    }

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

    /* Game start ($CA1A, a jsr to the code that loads the throne room). The AP
     * work runs first, so nothing sees the old state, then it jumps on. A new
     * game clears the counter, the location bits and this slot's saved counter,
     * tags the slot, and writes this ROM's seed hash. */
    chain = rom->content[0xca1b] | (rom->content[0xca1c] << 8);
    start = find_free_space(rom->content, FIXED_FREE_START, 57);
    vpatch(rom, start, 57,
        0xa9, 0x00,                                   /* lda #0          */
        0x8d, LO(AP_MAIL_FLAG), HI(AP_MAIL_FLAG),     /* stale mail flag */
        0xae, LO(AP_LOADED), HI(AP_LOADED),           /* ldx loaded      */
        0x8d, LO(AP_LOADED), HI(AP_LOADED),           /* sta loaded (0)  */
        0xd0, 0x25,                                   /* bne go: continue */
        0xad, LO(SAVE_SLOT), HI(SAVE_SLOT),           /* lda slot        */
        0x0a, 0xaa,                                   /* asl, tax        */
        0xa9, ap_slot_tag(hash),                      /* lda #tag        */
        0x9d, LO(AP_SLOT_TAG), HI(AP_SLOT_TAG),       /* sta slot_tag,x  */
        0xa9, 0x00,                                   /* lda #0: A stays 0 for the loop */
        0x9d, LO(AP_SLOT_RECV), HI(AP_SLOT_RECV),     /* clear slot_recv,x */
        0x9d, LO(AP_SLOT_RECV + 1), HI(AP_SLOT_RECV + 1),
        0xa2, AP_SEARCH - AP_RECV,                    /* ldx #6          */
        0x9d, LO(AP_RECV), HI(AP_RECV),               /* - sta recv,x: counter, chest and search bits */
        0xca,                                         /*   dex           */
        0x10, 0xfa,                                   /*   bpl -         */
        0xa2, 0x03,                                   /* ldx #3          */
        0xbd, LO(start + 53), HI(start + 53),         /* - lda hash,x    */
        0x9d, LO(AP_SEED_HASH), HI(AP_SEED_HASH),     /*   sta seed_hash,x */
        0xca,                                         /*   dex           */
        0x10, 0xf7,                                   /*   bpl -         */
        0x4c, LO(chain), HI(chain),                   /* go: jmp chain   */
        hash & 0xff, (hash >> 8) & 0xff, (hash >> 16) & 0xff, hash >> 24  /* hash */
    );
    vpatch(rom, 0xca1a, 3, 0x20, LO(start), HI(start));
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
 * its name to AP_MAIL_TEXT and sets AP_MAIL_FLAG. After the hero's next step
 * the game shows "Thou hast found the <name>." and goes back to the field.
 *
 * The hook replaces lda $e4, and #$04 at $CDBF, in the after-step code, which
 * the field loop calls with jsr. Town, cave and stair tiles jump away before
 * $CDBF, so the message never interrupts a map change. To show the message the
 * hook drops its own return address and jumps to $D242 (show message), which
 * ends in $CF6A's rts back to the field loop, like a command does.
 */
#define AP_MAIL_TEXT 0x6850
_Static_assert(((AP_MAIL_TEXT) >> 8) == ((AP_NAME_BUF) >> 8), "the mailbox shares the show routine's $A4");

static void ap_mailbox(dw_rom *rom)
{
    uint16_t hook;

    if (rom->content[0xcdbf] != 0xa5 || rom->content[0xcdc0] != 0xe4
            || rom->content[0xcdc1] != 0x29 || rom->content[0xcdc2] != 0x04) {
        printf("AP mailbox: unexpected code at $CDBF, received items will have no message\n");
        return;
    }
    if (!ap_show_tail) {
        printf("AP mailbox: no found-message routine, received items will have no message\n");
        return;
    }
    /* The show routine's tail sets $A3 from A, $A4 to $68 and shows $D9. */
    hook = find_free_space(rom->content, FIXED_FREE_START, 22);
    vpatch(rom, hook, 22,
        0xad, LO(AP_MAIL_FLAG), HI(AP_MAIL_FLAG),     /* lda flag        */
        0xd0, 0x05,                                   /* bne show        */
        0xa5, 0xe4,                                   /* lda $e4 (moved) */
        0x29, 0x04,                                   /* and #$04 (moved) */
        0x60,                                         /* rts             */
        0x68, 0x68,                                   /* show: pla, pla  */
        0xa9, 0x00,                                   /* lda #0          */
        0x8d, LO(AP_MAIL_FLAG), HI(AP_MAIL_FLAG),     /* sta flag        */
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
     * for the custom music code, which needs about 150 bytes in one piece. */
    {
        BOOL old = dwr_set_packing(TRUE);

        ap_chest_and_search(rom);
        ap_save_and_load(rom);
        ap_mailbox(rom);
        ap_signature(rom);
        dwr_set_packing(old);
    }
}
