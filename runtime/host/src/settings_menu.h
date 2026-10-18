#ifndef BLUEWAKE_SETTINGS_MENU_H
#define BLUEWAKE_SETTINGS_MENU_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// The settings menu is the Windows app's (windows/src/win_settings.cpp). The
// host calls these at fixed points; loading and installing are done by the
// Windows entry shim, so they are empty here.
static inline void bluewake_settings_load(void) {}
static inline void bluewake_settings_menu_install(void) {}
// Every SDL event: true when the menu took it (its keys, or anything while it
// is open), so nothing else acts on it.
bool bluewake_settings_menu_event(const void* sdl_event);

#ifdef __cplusplus
}
#endif

#endif
