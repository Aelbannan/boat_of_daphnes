// See mods_host.h.
#include "mods_host.h"

#include <stddef.h>

static void (*g_disc_ready)(void);

void bluewake_mods_set_disc_ready(void (*callback)(void)) { g_disc_ready = callback; }

void bluewake_mods_disc_ready(void) {
    if (g_disc_ready != NULL)
        g_disc_ready();
}
