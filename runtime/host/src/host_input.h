#ifndef BLUEWAKE_HOST_INPUT_H
#define BLUEWAKE_HOST_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

// The host's one SDL event observer (dol_aurora_set_event_observer). The
// settings menu sees every event first and keeps the ones it takes; the game
// reads its controllers through Aurora's PAD, not through these events.
// Once, after the Aurora window exists.
void bluewake_host_input_install(void);

#ifdef __cplusplus
}
#endif

#endif
