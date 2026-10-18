#include "host_input.h"

#include "gxruntime/aurora_backend.h"
#include "settings_menu.h"

#include <stddef.h>

static void observe(const void* sdl_event, void* user) {
    (void)user;
    // The settings menu opens and closes on its keys, and while it is open it
    // has the keyboard, mouse and controller to itself.
    (void)bluewake_settings_menu_event(sdl_event);
}

void bluewake_host_input_install(void) {
    dol_aurora_set_event_observer(observe, NULL);
}
