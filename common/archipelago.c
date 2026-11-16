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
 *   $6850-$6860  mailbox text, the item name in DW tiles, ended by $FA
 *
 * The ROM signature at PRG $7FE0: "DWRAP", 3 version digits, then a little
 * endian pointer to the 16 byte slot name.
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
#define SAVE_SLOT     0x6039

#define AP_VERSION    "002"
#define AP_NAME_LEN   16
/* The unused Tablet: its index in the item name list, and its 6 letters. */
#define TABLET_NAME   24
#define TABLET_TEXT   0x7b56

/* Locations with a found message: 31 chests, then the 3 search spots. */
#define AP_TEXT_LOCS  (CHEST_COUNT + 3)
#define AP_TEXT_LEN   16
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

void dwr_set_ap(BOOL enabled, const char *slot_name)
{
    ap_enabled = enabled;
    if (slot_name) {
        strncpy(ap_slot_name, slot_name, AP_NAME_LEN);
        ap_slot_name[AP_NAME_LEN] = '\0';
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
 * Bank 1 has no room for the names, so they sit in expansion bank 3 in
 * 17-byte slots. The show routine maps bank 3, copies the name into work RAM
 * at AP_NAME_BUF, maps the old bank back and points $A3/$A4 at the copy.
 */
#define AP_NAME_BUF      0x6830  /* 17 bytes, below the music driver */
#define AP_NAME_BANK     3
#define AP_NAME_SLOTS    0x3000  /* offset in bank 3, CPU $B000 */
#define LOAD_PRG_BANK    0xff91  /* store_and_load_prg_bank: A = bank */
#define FOUND_NAME_CODE  0xab70  /* the $F7 byte in message $D9 */
#define F9_HANDLER       0x77e8  /* bank 1, $B7E8, 17 bytes */

size_t ascii2dw(uint8_t *string);

/* Writes name slot i in bank 3: the text in DW tiles, then $FA. */
static void ap_put_name(dw_rom *rom, int i, const char *text)
{
    uint8_t buf[AP_TEXT_LEN + 2];
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
    memcpy(&rom->expansion[(AP_NAME_BANK - 3) * 0x4000 + AP_NAME_SLOTS
            + i * (AP_TEXT_LEN + 1)], buf, len + 1);
}

static void ap_chest_and_search(dw_rom *rom)
{
    uint16_t bits, lo, hi, chest, show, search, at;
    int i;

    /* Layout: bit table (8), pointer tables (2 x 34), chest (30), show (61),
     * search (19). */
    bits = find_free_space(rom->content, FIXED_FREE_START,
            8 + 2 * AP_TEXT_LOCS + 30 + 61 + 19);
    lo = bits + 8;
    hi = lo + AP_TEXT_LOCS;
    chest = hi + AP_TEXT_LOCS;
    show = chest + 30;
    search = show + 61;

    vpatch(rom, bits, 8, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80);

    for (i = 0; i < AP_TEXT_LOCS; i++) {
        ap_put_name(rom, i, ap_have_text && ap_text[i][0] ? ap_text[i] : "APItem");
        at = 0x8000 + AP_NAME_SLOTS + i * (AP_TEXT_LEN + 1);
        rom->content[lo + i] = LO(at);
        rom->content[hi + i] = HI(at);
    }

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

    /* Hooked at $E221, where the chest at Y/4 was found. Falls into show
     * with X = chest index. */
    vpatch(rom, chest, 30,
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
        0x68, 0xaa,                         /* pla, tax: chest index     */
        0xa9, 0x00                          /* lda #0: a chest           */
    );

    /* A = 0 for a chest, $ff for a search, X = name index. $E355 marks the
     * chest open and plays the sound for a chest, and overwrites $A3. */
    vpatch(rom, show, 61,
        0x85, 0x0e,                         /* sta $0e                   */
        0xa9, 0x10,                         /* lda #$10: any real item   */
        0x85, 0xde,                         /* sta $de                   */
        0x8a, 0x48,                         /* txa, pha                  */
        0x20, 0x55, 0xe3,                   /* jsr $e355                 */
        0x68, 0xaa,                         /* pla, tax                  */
        0xbd, LO(lo), HI(lo),               /* lda lo,x                  */
        0x85, 0xa3,                         /* sta $a3                   */
        0xbd, LO(hi), HI(hi),               /* lda hi,x                  */
        0x85, 0xa4,                         /* sta $a4                   */
        0xad, 0x04, 0x60,                   /* lda $6004: current bank   */
        0x48,                               /* pha                       */
        0xa9, AP_NAME_BANK,                 /* lda #3                    */
        0x20, LO(LOAD_PRG_BANK), HI(LOAD_PRG_BANK), /* jsr load bank     */
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
static void ap_save_and_load(dw_rom *rom)
{
    uint16_t save, restore, start, chain, cont;

    save = find_free_space(rom->content, FIXED_FREE_START, 26);
    vpatch(rom, save, 26,
        0x8a, 0x48,                                   /* txa, pha        */
        0xad, LO(SAVE_SLOT), HI(SAVE_SLOT),           /* lda slot        */
        0x0a, 0xaa,                                   /* asl, tax        */
        0xad, LO(AP_RECV), HI(AP_RECV),               /* lda recv        */
        0x9d, LO(AP_SLOT_RECV), HI(AP_SLOT_RECV),     /* sta slot_recv,x */
        0xad, LO(AP_RECV + 1), HI(AP_RECV + 1),
        0x9d, LO(AP_SLOT_RECV + 1), HI(AP_SLOT_RECV + 1),
        0x68, 0xaa,                                   /* pla, tax        */
        0xa0, 0x00,                                   /* ldy #0 (moved)  */
        0xa5, 0xba,                                   /* lda $ba (moved) */
        0x60                                          /* rts             */
    );
    /* SaveData at $FA18 was ldy #0, lda $ba. */
    vpatch(rom, 0xfa18, 4, 0x20, LO(save), HI(save), 0xea);

    restore = find_free_space(rom->content, FIXED_FREE_START, 26);
    vpatch(rom, restore, 26,
        0x8a, 0x48,                                   /* txa, pha        */
        0xad, LO(SAVE_SLOT), HI(SAVE_SLOT),           /* lda slot        */
        0x0a, 0xaa,                                   /* asl, tax        */
        0xbd, LO(AP_SLOT_RECV), HI(AP_SLOT_RECV),     /* lda slot_recv,x */
        0x8d, LO(AP_RECV), HI(AP_RECV),
        0xbd, LO(AP_SLOT_RECV + 1), HI(AP_SLOT_RECV + 1),
        0x8d, LO(AP_RECV + 1), HI(AP_RECV + 1),
        0x68, 0xaa,                                   /* pla, tax        */
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

    chain = rom->content[0xca1b] | (rom->content[0xca1c] << 8);
    start = find_free_space(rom->content, FIXED_FREE_START, 38);
    vpatch(rom, start, 38,
        0x20, LO(chain), HI(chain),                   /* jsr chain       */
        0xad, LO(AP_LOADED), HI(AP_LOADED),           /* lda loaded      */
        0xf0, 0x06,                                   /* beq new_game    */
        0xa9, 0x00,                                   /* lda #0          */
        0x8d, LO(AP_LOADED), HI(AP_LOADED),           /* sta loaded      */
        0x60,                                         /* rts: continue   */
        0xa9, 0x00,                                   /* new_game: lda #0 */
        0x8d, LO(AP_RECV), HI(AP_RECV),
        0x8d, LO(AP_RECV + 1), HI(AP_RECV + 1),
        0x8d, LO(AP_CHESTS), HI(AP_CHESTS),
        0x8d, LO(AP_CHESTS + 1), HI(AP_CHESTS + 1),
        0x8d, LO(AP_CHESTS + 2), HI(AP_CHESTS + 2),
        0x8d, LO(AP_CHESTS + 3), HI(AP_CHESTS + 3),
        0x8d, LO(AP_SEARCH), HI(AP_SEARCH),
        0x60                                          /* rts             */
    );
    vpatch(rom, 0xca1a, 3, 0x20, LO(start), HI(start));
}

/* "DWRAP", version, pointer to the slot name. */
static void ap_signature(dw_rom *rom)
{
    uint16_t name = find_free_space_in(rom->content, 0x7f0a, BANK_1_FREE_END,
            AP_NAME_LEN);
    uint8_t padded[AP_NAME_LEN] = { 0 };

    memcpy(padded, ap_slot_name, strlen(ap_slot_name));
    patch(rom, name, AP_NAME_LEN, padded);
    vpatch(rom, 0x7fe0, 16, 'D', 'W', 'R', 'A', 'P',
            AP_VERSION[0], AP_VERSION[1], AP_VERSION[2],
            LO(name), HI(name), 0, 0, 0, 0, 0, 0);
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
#define AP_MAIL_FLAG 0x682e
#define AP_MAIL_TEXT 0x6850

static void ap_mailbox(dw_rom *rom)
{
    uint16_t hook;

    if (rom->content[0xcdbf] != 0xa5 || rom->content[0xcdc0] != 0xe4
            || rom->content[0xcdc1] != 0x29 || rom->content[0xcdc2] != 0x04) {
        printf("AP mailbox: unexpected code at $CDBF, received items will have no message\n");
        return;
    }
    hook = find_free_space(rom->content, FIXED_FREE_START, 30);
    vpatch(rom, hook, 30,
        0xad, LO(AP_MAIL_FLAG), HI(AP_MAIL_FLAG),     /* lda flag        */
        0xd0, 0x05,                                   /* bne show        */
        0xa5, 0xe4,                                   /* lda $e4 (moved) */
        0x29, 0x04,                                   /* and #$04 (moved) */
        0x60,                                         /* rts             */
        0x68, 0x68,                                   /* show: pla, pla  */
        0xa9, 0x00,                                   /* lda #0          */
        0x8d, LO(AP_MAIL_FLAG), HI(AP_MAIL_FLAG),     /* sta flag        */
        0xa9, LO(AP_MAIL_TEXT), 0x85, 0xa3,           /* text pointer    */
        0xa9, HI(AP_MAIL_TEXT), 0x85, 0xa4,
        0xa9, 0xd9,                                   /* lda #$d9        */
        0x4c, 0x42, 0xd2                              /* jmp $d242       */
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

    ap_chest_and_search(rom);
    ap_save_and_load(rom);
    ap_mailbox(rom);
    ap_signature(rom);
}
