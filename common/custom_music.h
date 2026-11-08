#ifndef DWRANDOMIZER_CUSTOM_MUSIC_H
#define DWRANDOMIZER_CUSTOM_MUSIC_H

#include "dwr_types.h"

/**
 * Loads a JSON5 music pack (the format used by ZORA and Z2R).
 *
 * @param path The pack file
 * @return TRUE if the pack has at least one usable song
 */
BOOL custom_music_load(const char *path);

/** Forget any loaded packs. */
void custom_music_clear(void);

/**
 * Picks songs from the loaded pack and installs the driver. Does nothing if
 * no pack is loaded. Call after setup_expansion().
 *
 * @param rom The rom struct
 */
void custom_music_apply(dw_rom *rom);
BOOL custom_music_has_credits(void);
/* True when a music pack is loaded. */
BOOL custom_music_has_pack(void);

/** Pick songs the way the site does with every slot on Random, for the command line. */
void custom_music_choose_like_site(void);

#endif
