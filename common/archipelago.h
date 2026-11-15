
#ifndef _ARCHIPELAGO_H_
#define _ARCHIPELAGO_H_

#include "dwr.h"

/* Turns Archipelago mode on for the next build. slot_name may be NULL. */
void dwr_set_ap(BOOL enabled, const char *slot_name);
BOOL dwr_ap_enabled(void);

/* Applies the Archipelago ROM changes. Does nothing when AP mode is off. */
void ap_changes(dw_rom *rom);

#endif
