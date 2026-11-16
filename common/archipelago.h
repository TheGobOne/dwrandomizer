
#ifndef _ARCHIPELAGO_H_
#define _ARCHIPELAGO_H_

#include "dwr.h"

/* Locations with a found message: 31 chests, then the 3 search spots. Each name
 * is up to AP_TEXT_LEN letters plus an end byte. */
#define AP_TEXT_LOCS  (CHEST_COUNT + 3)
#define AP_TEXT_LEN   16

/* In AP mode the top of expansion bank 3 holds the found-message data: a low
 * and a high pointer table (one byte per location each), then the names in
 * fixed slots, ending at $BFFF. Custom music stops its bank 3 songs below it. */
#define AP_BANK3_NAMES (0xc000 - AP_TEXT_LOCS * (AP_TEXT_LEN + 1 + 2))

/* Turns Archipelago mode on for the next build. slot_name may be NULL. */
void dwr_set_ap(BOOL enabled, const char *slot_name);
BOOL dwr_ap_enabled(void);

/* Reads a .apdwr file from the APWorld: turns AP mode on, sets the slot name
 * and the item text for each chest and search spot. FALSE if it can't. */
BOOL dwr_load_ap_patch(const char *path);

/* What the loaded .apdwr file asks for: the flag string (NULL if none), the
 * seed (FALSE if none) and Free Charlock (1 on, 0 off, -1 not given). */
const char *dwr_ap_patch_flags(void);
BOOL dwr_ap_patch_seed(uint64_t *seed);
int dwr_ap_patch_free_charlock(void);

/* Applies the Archipelago ROM changes. Does nothing when AP mode is off. */
void ap_changes(dw_rom *rom);

#endif
