// Linux entry shim (the counterpart of win_entry.c).
//
// It fills in default paths beside the executable, starts the session log,
// then runs the host (runtime/host/src/main.c, compiled with main renamed to
// bluewake_host_main).
//
// Everything lives in the executable's folder, as on Windows, when that folder is writable. From a read-only one (an
// AppImage, /usr/bin) the player files go to $XDG_DATA_HOME/boat-of-daphnes (~/.local/share/boat-of-daphnes) instead:
//   boat-of-daphnes, gGZLE01_recomp.so         the host and the translated game module
//   game/GZLE01.iso, game/main.dol, game/rels/ the disc image and its code
//   dsp/dsp_rom.bin, dsp/dsp_coef.bin          only for BLUEWAKE_DSP_MODE=lle
//   initial_pipeline_cache.db                  Aurora's bundled pipeline seed
// Player files, never touched by a rebuild:
//   boat-of-daphnes.json, imgui.ini            settings (CVars) and window layout
//   Save/GZLE01.card, Save/sram.bin            the memory card (saves) and the console's settings
//   Save/states/                               save states
//   mods/textures/                             texture packs (Dolphin format)
//   Randomizer/                                randomizer seeds and spoiler logs
//   logs/session-*.log, logs/crash-*.log       the newest sessions, crashes
//   cache/                                     shader and pipeline caches, and what is made from the picked disc on
//                                              first launch (linux_disc.c)
// BLUEWAKE_DATA_DIR moves the player files elsewhere (tests, scripted runs).
// Every default is only a default: an environment variable that is already
// set (BLUEWAKE_DISC, BLUEWAKE_CARD_PATH, ...) wins.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <SDL3/SDL.h>
#include <aurora/aurora.h>

#include "win_settings.h"
#include "win_disc.h"
#include "win_crash.h"
#include "launch_marker.h"

#define BW_PATH_MAX 4096

int bluewake_host_main(int argc, char** argv);

static char g_exe_dir[BW_PATH_MAX];
static char g_data_dir[BW_PATH_MAX];
static char g_cache_dir[BW_PATH_MAX];
static char g_log_path[BW_PATH_MAX];

static int file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int dir_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void bw_default(const char* name, const char* value) {
    const char* existing = getenv(name);
    if (existing != NULL && existing[0] != '\0')
        return;
    setenv(name, value, 1);
}

// The GameCube adapter (USB 057E:0337) is read by the app itself (runtime/host/src/gc_adapter.c). SDL's gamepad layer
// must not also claim it, so it is told to ignore that device, before SDL starts. The list keeps any entries the
// player set. BLUEWAKE_GCADAPTER=0 turns the app's reader off and leaves the adapter to SDL.
static void bw_ignore_gc_adapter_in_sdl(void) {
    const char* off = getenv("BLUEWAKE_GCADAPTER");
    if (off != NULL && off[0] == '0')
        return;
    const char* entry = "0x057e/0x0337";
    const char* existing = getenv("SDL_GAMECONTROLLER_IGNORE_DEVICES");
    char list[1024];
    if (existing != NULL && existing[0] != '\0') {
        if (strstr(existing, entry) != NULL || strlen(existing) + strlen(entry) + 2 > sizeof list)
            return;
        snprintf(list, sizeof list, "%s,%s", existing, entry);
    } else {
        snprintf(list, sizeof list, "%s", entry);
    }
    setenv("SDL_GAMECONTROLLER_IGNORE_DEVICES", list, 1);
}

static void bw_default_path(const char* name, const char* dir, const char* relative) {
    char path[BW_PATH_MAX];
    snprintf(path, sizeof path, "%s%s", dir, relative);
    bw_default(name, path);
}

static void make_dir(const char* path) {
    if (mkdir(path, 0755) != 0 && errno != EEXIST)
        fprintf(stderr, "[linux] cannot create %s: %s\n", path, strerror(errno));
}

static void make_dirs(const char* path) {
    char tmp[BW_PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char* p = tmp + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static void resolve_dirs(void) {
    ssize_t n = readlink("/proc/self/exe", g_exe_dir, sizeof g_exe_dir - 2);
    if (n > 0) {
        g_exe_dir[n] = '\0';
        char* slash = strrchr(g_exe_dir, '/');
        if (slash != NULL)
            slash[1] = '\0';
    } else {
        snprintf(g_exe_dir, sizeof g_exe_dir, "./");
    }
    // Player files beside the executable when it can be written to, else in the player's data folder.
    const char* override = getenv("BLUEWAKE_DATA_DIR");
    if (override != NULL && override[0] != '\0') {
        snprintf(g_data_dir, sizeof g_data_dir, "%s/", override);
    } else if (access(g_exe_dir, W_OK) == 0) {
        snprintf(g_data_dir, sizeof g_data_dir, "%s", g_exe_dir);
    } else {
        const char* xdg = getenv("XDG_DATA_HOME");
        const char* home = getenv("HOME");
        if (xdg != NULL && xdg[0] == '/')
            snprintf(g_data_dir, sizeof g_data_dir, "%s/boat-of-daphnes/", xdg);
        else if (home != NULL && home[0] != '\0')
            snprintf(g_data_dir, sizeof g_data_dir, "%s/.local/share/boat-of-daphnes/", home);
        else
            snprintf(g_data_dir, sizeof g_data_dir, "%s", g_exe_dir);
    }
    make_dirs(g_data_dir);
    static const char* const folders[] = {"Save", "Save/states", "mods", "mods/textures", "Randomizer", "logs", "cache"};
    for (size_t i = 0; i < sizeof folders / sizeof folders[0]; i++) {
        char folder[BW_PATH_MAX];
        snprintf(folder, sizeof folder, "%s%s", g_data_dir, folders[i]);
        make_dir(folder);
    }
    snprintf(g_cache_dir, sizeof g_cache_dir, "%scache/", g_data_dir);
}

// Session log. Everything the host writes to stdout and stderr goes to
// logs/session-YYYYMMDD-HHMMSS-<pid>.log in the data folder, each line stamped
// with the local time, and still to the terminal it was started from, if any.
static int g_console = -1;  // the original standard error, a copy
static FILE* g_log_file;
static pthread_t g_log_thread;
static int g_log_thread_started;

static void* log_pump(void* arg) {
    const int read_fd = (int)(intptr_t)arg;
    char buffer[16384];
    char line[8192];
    size_t line_len = 0;
    for (;;) {
        const ssize_t n = read(read_fd, buffer, sizeof buffer);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        for (ssize_t i = 0; i < n; i++) {
            if (line_len < sizeof line - 1)
                line[line_len++] = buffer[i];
            if (buffer[i] != '\n')
                continue;
            line[line_len] = '\0';
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            struct tm local;
            localtime_r(&ts.tv_sec, &local);
            fprintf(g_log_file, "%02d:%02d:%02d.%03ld %s", local.tm_hour, local.tm_min, local.tm_sec, ts.tv_nsec / 1000000L,
                    line);
            fflush(g_log_file);
            line_len = 0;
        }
        if (g_console >= 0) {
            ssize_t off = 0;
            while (off < n) {
                const ssize_t w = write(g_console, buffer + off, (size_t)(n - off));
                if (w <= 0)
                    break;
                off += w;
            }
        }
    }
    return NULL;
}

static int compare_names(const void* a, const void* b) { return strcmp((const char*)a, (const char*)b); }

static void prune_logs(const char* dir, int keep) {
    char names[256][64];
    int count = 0;
    DIR* d = opendir(dir);
    if (d == NULL)
        return;
    struct dirent* entry;
    while ((entry = readdir(d)) != NULL) {
        if (strncmp(entry->d_name, "session-", 8) != 0 || strlen(entry->d_name) >= sizeof names[0])
            continue;
        if (count < 256)
            snprintf(names[count++], sizeof names[0], "%s", entry->d_name);
    }
    closedir(d);
    // The names sort by time; remove all but the newest `keep`.
    qsort(names, (size_t)count, sizeof names[0], compare_names);
    for (int i = 0; i + keep < count; i++) {
        char path[BW_PATH_MAX];
        snprintf(path, sizeof path, "%s%s", dir, names[i]);
        unlink(path);
    }
}

// At exit, however the host ends: close the pipe's write ends so the pump
// reads to the end, and wait for it, or the last lines (usually the ones that
// explain the exit) would be lost with the process.
static void finish_session_log(void) {
    if (!g_log_thread_started)
        return;
    fflush(stdout);
    fflush(stderr);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    g_log_thread_started = 0;
    pthread_join(g_log_thread, NULL);
    fclose(g_log_file);
}

static void start_session_log(void) {
    char logs[BW_PATH_MAX];
    snprintf(logs, sizeof logs, "%slogs/", g_data_dir);
    make_dir(logs);
    prune_logs(logs, 7);
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    char stamp[48];
    strftime(stamp, sizeof stamp, "session-%Y%m%d-%H%M%S", &local);
    snprintf(g_log_path, sizeof g_log_path, "%s%s-%ld.log", logs, stamp, (long)getpid());
    g_log_file = fopen(g_log_path, "w");
    int fds[2];
    if (g_log_file == NULL || pipe2(fds, O_CLOEXEC) != 0) {
        g_log_path[0] = '\0';
        return;
    }
    // Echo to where the parent pointed stderr (a terminal, a file or a pipe it redirected to).
    g_console = fcntl(STDERR_FILENO, F_DUPFD_CLOEXEC, 3);
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    if (pthread_create(&g_log_thread, NULL, log_pump, (void*)(intptr_t)fds[0]) == 0) {
        g_log_thread_started = 1;
        atexit(finish_session_log);
    }
}

static void usage(void) {
    fprintf(stderr,
            "usage: " BLUEWAKE_APP_NAME " [options]\n"
            "  --aspect W:H       force the picture's shape (16:9, 21:9, 4:3, ...); black bars\n"
            "                     fill the rest of the window. By default the picture fills it\n"
            "  --betterww         Better Wind Waker's settings, at their defaults\n"
            "  --options LIST     change them: name,-name,... (mods/betterww/options.txt)\n"
            "  --fullscreen       start in fullscreen (F11 toggles it while playing)\n"
            "  --window WxH       the window's size\n"
            "  --scale N          render at N x 480 lines (0: the window's own pixels)\n"
            "  --fps              show the frame rate\n"
            "  --safe-mode        Recover startup with HLE audio and mods off\n"
            "  --hle-audio        Fast audio for this session\n"
            "  --lle-audio        run the DSP's own microcode instead of the HLE ucode\n"
            "  --mods LIST        mods compiled into the module, by name\n"
            "  --disc FILE        the disc image to read (default: the one you chose on first launch)\n"
            "  --module FILE      the translated game module (default gGZLE01_recomp.so)\n"
            "Keyboard: arrows D-pad, J A, K B, U X, I Y, W/A/S/D stick,\n"
            "H/F/T/G C-stick, E/R L/R, Q Z, Return START. Game controllers work too.\n"
            "F1 or Esc settings, F11 or Alt+Enter fullscreen, F9 frame rate.\n"
            "The settings menu saves to boat-of-daphnes.json in the player files folder; options given here win\n"
            "for the session.\n");
}

static void fatal_box(const char* message) {
    // A terminal already shows the message; scripted runs set
    // BLUEWAKE_NO_DIALOG so nothing waits for a click.
    if (getenv("BLUEWAKE_NO_DIALOG") != NULL || (g_console >= 0 && isatty(g_console)))
        return;
    char text[4096];
    if (g_log_path[0] != '\0')
        snprintf(text, sizeof text, "%s\n\nSession log:\n%s", message, g_log_path);
    else
        snprintf(text, sizeof text, "%s", message);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, BLUEWAKE_APP_NAME, text, NULL);
}

// The game's translated code recurses on the host stack as the guest does on the GameCube's; Linux gives the main
// thread 8 MB, which grows on demand up to the soft limit, so the limit is raised (macOS gives 8 MB and is enough there).
static void raise_stack_limit(void) {
    struct rlimit limit;
    const rlim_t wanted = 64ull * 1024 * 1024;
    if (getrlimit(RLIMIT_STACK, &limit) != 0 || limit.rlim_cur == RLIM_INFINITY || limit.rlim_cur >= wanted)
        return;
    limit.rlim_cur = limit.rlim_max == RLIM_INFINITY || limit.rlim_max >= wanted ? wanted : limit.rlim_max;
    setrlimit(RLIMIT_STACK, &limit);
}

int main(int argc, char** argv) {
    raise_stack_limit();
    bw_settings_capture_environment();
    resolve_dirs();
    if (getenv("BLUEWAKE_SESSION_LOG") == NULL || strcmp(getenv("BLUEWAKE_SESSION_LOG"), "0") != 0)
        start_session_log();

    // Options become the host's own settings (scripts/mac/run_host.sh lists
    // them): the host turns BLUEWAKE_ASPECT into the widescreen mod and the
    // frame buffer's shape, and BLUEWAKE_MODS=betterww into Better Wind
    // Waker's options.
    const char* module_arg = NULL;
    char mods[256] = "";
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        const int more = i + 1 < argc;
        if (strcmp(a, "--disc") == 0 && more) {
            setenv("BLUEWAKE_DISC", argv[++i], 1);
        } else if (strcmp(a, "--module") == 0 && more) {
            module_arg = argv[++i];
        } else if (strcmp(a, "--mods") == 0 && more) {
            snprintf(mods + strlen(mods), sizeof mods - strlen(mods), "%s%s", mods[0] ? "," : "", argv[++i]);
        } else if (strcmp(a, "--betterww") == 0) {
            snprintf(mods + strlen(mods), sizeof mods - strlen(mods), "%sbetterww", mods[0] ? "," : "");
        } else if (strcmp(a, "--options") == 0 && more) {
            setenv("BLUEWAKE_OPTIONS", argv[++i], 1);
        } else if (strcmp(a, "--aspect") == 0 && more) {
            // W:H (or a plain ratio such as 2.333) as the renderer's forced aspect.
            const char* text = argv[++i];
            double w = 0.0, h = 0.0;
            char ratio[32];
            if (sscanf(text, "%lf:%lf", &w, &h) == 2 && w > 0.0 && h > 0.0)
                snprintf(ratio, sizeof ratio, "%.6f", w / h);
            else
                snprintf(ratio, sizeof ratio, "%s", text);
            setenv("DOL_AURORA_ASPECT_RATIO", ratio, 1);
        } else if (strcmp(a, "--fullscreen") == 0) {
            setenv("DOL_AURORA_FULLSCREEN", "1", 1);
        } else if (strcmp(a, "--window") == 0 && more) {
            setenv("DOL_AURORA_WINDOW", argv[++i], 1);
        } else if (strcmp(a, "--scale") == 0 && more) {
            setenv("DOL_AURORA_RENDER_SCALE", argv[++i], 1);
        } else if (strcmp(a, "--fps") == 0) {
            // Aurora reads DOL_AURORA_SHOW_FPS in a static initializer, before
            // main: set it through its API too.
            setenv("DOL_AURORA_SHOW_FPS", "1", 1);
            aurora_set_fps_overlay(true);
        } else if (strcmp(a, "--safe-mode") == 0) {
            setenv("BLUEWAKE_SAFE_MODE", "1", 1);
        } else if (strcmp(a, "--hle-audio") == 0) {
            setenv("BLUEWAKE_DSP_MODE", "hle", 1);
        } else if (strcmp(a, "--lle-audio") == 0) {
            setenv("BLUEWAKE_DSP_MODE", "lle", 1);
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage();
            return 0;
        } else {
            fprintf(stderr, BLUEWAKE_APP_NAME ": unknown option %s\n", a);
            usage();
            return 2;
        }
    }
    if (mods[0] != '\0')
        setenv("BLUEWAKE_MODS", mods, 1);

    // The player's settings (boat-of-daphnes.json, the menu), where the command
    // line did not already choose.
    bw_settings_load(g_data_dir);
    bw_settings_apply_launch();

    // The same play configuration as the iOS and Windows apps: one perf line
    // a second in the log, wall-clock pacing, Dolphin's HLE Zelda ucode, the
    // real clock for save dates, and the console's SRAM (stereo) kept with the
    // saves.
    // The children and the app's own code that look for the player files by this variable (the randomizer's seeds, Anchor's
    // downloads): the data folder, which an AppImage's read-only mount is not.
    bw_default("BLUEWAKE_DATA_DIR", g_data_dir);
    bw_default("BLUEWAKE_PERF_LOG", "1");
    bw_default("BLUEWAKE_WALL_PACE", "1");
    bw_default("DOL_AUDIO_NO_THROTTLE", "1");
    bw_default("BLUEWAKE_RENDERER", "aurora");
    bw_default("BLUEWAKE_CYCLE_CAP", "16384");
    bw_default("BLUEWAKE_MAX_BLOCKS", "100000000000");
    bw_default("BLUEWAKE_DSP_MODE", "hle");
    bw_default("BLUEWAKE_CLOCK", "now");
    bw_default_path("BLUEWAKE_SRAM", g_data_dir, "Save/sram.bin");
    bw_default_path("BLUEWAKE_CARD_PATH", g_data_dir, "Save/GZLE01.card");
    bw_default_path("BLUEWAKE_STATE_DIR", g_data_dir, "Save/states");
    // Aurora's files: imgui.ini in the data folder, caches in cache/.
    bw_default("DOL_AURORA_USER_DIR", g_data_dir);
    bw_default("DOL_AURORA_CACHE_DIR", g_cache_dir);
    char module[BW_PATH_MAX];
    const char* module_env = getenv("BLUEWAKE_COMPOSITE");
    if (module_arg != NULL)
        snprintf(module, sizeof module, "%s", module_arg);
    else if (module_env != NULL && module_env[0] != '\0')
        snprintf(module, sizeof module, "%s", module_env);
    else
        snprintf(module, sizeof module, "%sgGZLE01_recomp.so", g_exe_dir);
    // A packaged app (an AppImage) carries no module: the player's own, built from their disc, is in the data folder.
    if (module_arg == NULL && !file_exists(module)) {
        char kept[BW_PATH_MAX];
        snprintf(kept, sizeof kept, "%sgGZLE01_recomp.so", g_data_dir);
        if (file_exists(kept))
            snprintf(module, sizeof module, "%s", kept);
    }

    if (!file_exists(module)) {
        const char* text = "The player-generated game module is missing. Build your own copy from your USA revision-0 disc "
                           "with python3 scripts/linux/build.py YOUR_DISC.iso --install-module (it puts gGZLE01_recomp.so in "
                           "the data folder), or keep the personal build folder it makes. See docs/linux.md.";
        fprintf(stderr, "[linux] %s\n", text);
        fatal_box(text);
        return 1;
    }
    const int disc_status = bw_disc_setup(g_exe_dir, g_cache_dir);
    if (disc_status != 0) return disc_status < 0 ? 1 : 0;
    bw_default_path("BLUEWAKE_DOL", g_exe_dir, "game/main.dol");
    bw_default_path("BLUEWAKE_RELS_DIR", g_exe_dir, "game/rels");
    bw_default_path("BLUEWAKE_DISC", g_exe_dir, "game/GZLE01.iso");
    bw_default_path("BLUEWAKE_DSP_IROM", g_exe_dir, "dsp/dsp_rom.bin");
    bw_default_path("BLUEWAKE_DSP_COEF", g_exe_dir, "dsp/dsp_coef.bin");

    // Say plainly what is missing instead of failing somewhere in the host.
    const char* missing = NULL;
    const char* missing_path = NULL;
    if (!file_exists(module)) {
        missing = "the translated game module";
        missing_path = module;
    } else if (!file_exists(getenv("BLUEWAKE_DOL"))) {
        missing = "the prepared game executable";
        missing_path = getenv("BLUEWAKE_DOL");
    } else if (!dir_exists(getenv("BLUEWAKE_RELS_DIR"))) {
        missing = "the prepared game modules";
        missing_path = getenv("BLUEWAKE_RELS_DIR");
    } else if (!file_exists(getenv("BLUEWAKE_DISC"))) {
        missing = "the disc image";
        missing_path = getenv("BLUEWAKE_DISC");
    }
    if (missing != NULL) {
        char message[2048];
        snprintf(message, sizeof message,
                 BLUEWAKE_APP_NAME " cannot start: %s is missing.\n\n%s\n\n"
                 "Build your own copy from your disc with\n"
                 "python scripts/linux/build.py YOUR_DISC.iso",
                 missing, missing_path);
        fprintf(stderr, "[linux] %s\n", message);
        fatal_box(message);
        return 1;
    }

    fprintf(stderr, "[linux] app=%s data=%s module=%s disc=%s\n", g_exe_dir, g_data_dir, module, getenv("BLUEWAKE_DISC"));
    bw_crash_install(g_data_dir);
    // The name the volume mixer shows for the game's audio.
    SDL_SetAppMetadata(BLUEWAKE_APP_NAME, "0.1", "dev.boat-of-daphnes");
    bw_ignore_gc_adapter_in_sdl();
    bw_settings_install();
    char* host_argv[3] = {argv[0], module, NULL};
    char launch_marker[BW_PATH_MAX];
    snprintf(launch_marker, sizeof launch_marker, "%slaunch.pending", g_data_dir);
    if (!bw_launch_begin(launch_marker)) {
        fatal_box(BLUEWAKE_APP_NAME " could not create its launch recovery marker. Check that its folder is writable.");
        return 1;
    }
    bw_crash_test();
    const int status = bluewake_host_main(2, host_argv);
    if (status == 0 && !bw_launch_clear(launch_marker))
        fprintf(stderr, "[safe-mode] could not clear launch marker on clean exit\n");
    fflush(stdout);
    fflush(stderr);
    if (status != 0) {
        char message[256];
        snprintf(message, sizeof message, BLUEWAKE_APP_NAME " stopped with an error (status %d).", status);
        fatal_box(message);
    }
    if (status == 0 && bw_settings_relaunch() != 0)
        return 1;
    return status;
}
