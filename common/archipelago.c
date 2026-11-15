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
 *
 * The ROM signature at PRG $7FE0: "DWRAP", 3 version digits, then a little
 * endian pointer to the 16 byte slot name.
 *
 * Based on Serpikmin's ap_changes() for the Dragon Warrior APWorld, rewritten
 * for DWR's chest and search code.
 */

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

#define AP_VERSION    "001"
#define AP_NAME_LEN   16
/* The unused Tablet: its index in the item name list, and its 6 letters. */
#define TABLET_NAME   24
#define TABLET_TEXT   0x7b56

static BOOL ap_enabled = FALSE;
static char ap_slot_name[AP_NAME_LEN + 1] = "";

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

/*
 * Chest and search code. Both end in the same message, "Thou hast found the
 * APItem." The name buffer at $A3 holds item name indexes (index + 1, then
 * $FA). Index 24 is the unused Tablet, renamed to "APItem".
 */
static void ap_chest_and_search(dw_rom *rom)
{
    uint16_t bits, name, chest, show, search;

    /* Layout: bit table (8), name (2), chest (27), show (24), search (14). */
    bits = find_free_space(rom->content, FIXED_FREE_START, 8 + 2 + 27 + 24 + 14);
    name = bits + 8;
    chest = name + 2;
    show = chest + 27;
    search = show + 24;

    vpatch(rom, name, 2, TABLET_NAME + 1, 0xfa);
    set_text(rom, TABLET_TEXT, "APItem");

    vpatch(rom, bits, 8, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80);

    /* Hooked at $E221, where the chest at Y/4 was found. Falls into show. */
    vpatch(rom, chest, 27,
        0x98,                               /* tya                       */
        0x4a, 0x4a,                         /* lsr, lsr: chest index     */
        0x48,                               /* pha                       */
        0x29, 0x07,                         /* and #7                    */
        0xaa,                               /* tax                       */
        0xbd, LO(bits), HI(bits),           /* lda bits,x                */
        0x85, 0xde,                         /* sta $de (scratch)         */
        0x68,                               /* pla                       */
        0x4a, 0x4a, 0x4a,                   /* lsr x3: byte index        */
        0xaa,                               /* tax                       */
        0xa5, 0xde,                         /* lda $de                   */
        0x1d, LO(AP_CHESTS), HI(AP_CHESTS), /* ora AP_CHESTS,x           */
        0x9d, LO(AP_CHESTS), HI(AP_CHESTS), /* sta AP_CHESTS,x           */
        0xa9, 0x00                          /* lda #0: a chest           */
    );

    /* A = 0 for a chest, $ff for a search. $E355 marks the chest open and
     * plays the sound for a chest, then loads an item name, which is
     * replaced with the AP name. */
    vpatch(rom, show, 24,
        0x85, 0x0e,                         /* sta $0e                   */
        0xa9, 0x10,                         /* lda #$10: any real item   */
        0x85, 0xde,                         /* sta $de                   */
        0x20, 0x55, 0xe3,                   /* jsr $e355                 */
        0xa2, 0x01,                         /* ldx #1                    */
        0xbd, LO(name), HI(name),           /* - lda name,x              */
        0x95, 0xa3,                         /*   sta $a3,x               */
        0xca,                               /*   dex                     */
        0x10, 0xf8,                         /*   bpl -                   */
        0xa9, 0xd9,                         /* lda #$d9: found message   */
        0x4c, 0x42, 0xd2                    /* jmp $d242                 */
    );

    /* Hooked at $E12D, where search entry X matched. */
    vpatch(rom, search, 14,
        0xbd, LO(bits), HI(bits),           /* lda bits,x                */
        0x0d, LO(AP_SEARCH), HI(AP_SEARCH), /* ora AP_SEARCH             */
        0x8d, LO(AP_SEARCH), HI(AP_SEARCH), /* sta AP_SEARCH             */
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
 * The received counter goes into the save slot on save. LoadSavedData ($FB6B)
 * runs only on a continue, so it brings the counter back and sets AP_LOADED.
 * Game start ($CA1A) runs after it: without AP_LOADED it is a new game, which
 * clears the counter and the location bits. XP is not used, so a save made at
 * 0 XP continues correctly.
 */
static void ap_save_and_load(dw_rom *rom)
{
    uint16_t save, restore, start, chain;

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

    restore = find_free_space(rom->content, FIXED_FREE_START, 31);
    vpatch(rom, restore, 31,
        0x8a, 0x48,                                   /* txa, pha        */
        0xad, LO(SAVE_SLOT), HI(SAVE_SLOT),           /* lda slot        */
        0x0a, 0xaa,                                   /* asl, tax        */
        0xbd, LO(AP_SLOT_RECV), HI(AP_SLOT_RECV),     /* lda slot_recv,x */
        0x8d, LO(AP_RECV), HI(AP_RECV),
        0xbd, LO(AP_SLOT_RECV + 1), HI(AP_SLOT_RECV + 1),
        0x8d, LO(AP_RECV + 1), HI(AP_RECV + 1),
        0xa9, 0x01,                                   /* lda #1          */
        0x8d, LO(AP_LOADED), HI(AP_LOADED),           /* sta loaded      */
        0x68, 0xaa,                                   /* pla, tax        */
        0xa0, 0x00,                                   /* ldy #0 (moved)  */
        0xb1, 0x22,                                   /* lda ($22),y (moved) */
        0x60                                          /* rts             */
    );
    /* LoadSavedData at $FB6B was ldy #0, lda ($22),y. */
    vpatch(rom, 0xfb6b, 4, 0x20, LO(restore), HI(restore), 0xea);

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
    ap_signature(rom);
}
