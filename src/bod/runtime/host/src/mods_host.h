// The moment the disc's file table is readable (the host opened the image,
// before the game runs): the app's mods loader puts its files in the DVD
// overlay then, so the first scene already reads them.
#ifndef BLUEWAKE_MODS_HOST_H
#define BLUEWAKE_MODS_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

void bluewake_mods_set_disc_ready(void (*callback)(void));
// The host, once the disc is open.
void bluewake_mods_disc_ready(void);

#ifdef __cplusplus
}
#endif

#endif
