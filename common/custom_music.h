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

#endif
