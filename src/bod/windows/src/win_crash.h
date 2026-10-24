#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void bw_crash_install(const char* data_dir);
void bw_crash_test(void);
// The UI thread's heartbeat, once a frame: when it stops for 10 s the hang watchdog writes every thread's stack into
// the crash report and a minidump beside it (logs\hang-<pid>.dmp), once per hang.
void bw_crash_heartbeat(void);
#ifdef __cplusplus
}
#endif
