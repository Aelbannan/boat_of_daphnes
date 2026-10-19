// Boat of Daphnes for Linux: the player's disc, the first time and whenever it goes
// missing. See win_disc.h (this implements the same function).
//
// The app needs the player-generated module. The launcher asks for the player's own disc image (GameCube USA, GZLE01
// revision 0): an .iso or .gcm is used where it is (an .nkit.iso too: the importer reads the files through the image's
// own file table); a Dolphin image (.rvz, .wia, .gcz) or a .ciso is unpacked into a new private import folder by the
// app itself (runtime/host/src/disc_unpack.c; GameCube images are not encrypted).
// The disc is checked (its id, and main.dol's SHA-1 for revision 0) and prepared once, by the disc importer
// (runtime/host/src/disc_import.c): main.dol and the 415 RELs, into another fresh import folder. disc.txt remembers
// the disc and game.txt the completed preparation; prior folders are retained.
//
// Unlike the Windows launcher there is no progress window: the steps and their percentages go to the session log, and the
// file picker is SDL3's native one.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>

#include "disc_import.h"
#include "disc_unpack.h"
#include "win_disc.h"
#include "atomic_file.h"

#define BW_PATH_MAX 4096

static const char* g_exe;
static const char* g_data;

static int is_file(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int is_dir(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void box(const char* text, SDL_MessageBoxFlags flags) {
    if (getenv("BLUEWAKE_NO_DIALOG") != NULL)  // scripted runs: the log has it
        return;
    SDL_ShowSimpleMessageBox(flags, "boat-of-daphnes", text, NULL);
}

// A line of text from a file (the remembered disc), without its line end.
static int read_line(const char* path, char* out, size_t size) {
    out[0] = '\0';
    FILE* f = fopen(path, "rb");
    if (f == NULL)
        return 0;
    if (fgets(out, (int)size, f) == NULL)
        out[0] = '\0';
    fclose(f);
    size_t n = strlen(out);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r'))
        out[--n] = '\0';
    return n > 0;
}

static int write_line(const char* path, const char* line) {
    char* pending = bw_atomic_path(path);
    FILE* f = pending != NULL ? fopen(pending, "wb") : NULL;
    if (f == NULL) {
        free(pending);
        return 0;
    }
    const int written = fprintf(f, "%s\n", line) > 0;
    const int ok = pending != NULL && bw_atomic_finish(f, pending, path, written);
    free(pending);
    return ok;
}

// Never prepare/convert over the previous player's files. Failed attempts
// remain in their own directory, and the remembered selection changes only
// after the importer finishes. No source or converted image is deleted here.
static int fresh_import_dir(char* out, size_t size) {
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        int n = snprintf(out, size, "%sdisc-import-%ld-%llu-%u", g_data, (long)getpid(), (unsigned long long)time(NULL), attempt);
        if (n < 0 || (size_t)n >= size) return -1;
        if (mkdir(out, 0755) == 0) return 0;
        if (errno != EEXIST) return -1;
    }
    return -1;
}

static int prepared_ready(const char* dol, const char* rels) {
    if (!is_file(dol) || !is_dir(rels)) return 0;
    DIR* dir = opendir(rels);
    if (dir == NULL) return 0;
    unsigned count = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        const size_t n = strlen(entry->d_name);
        if (n > 4 && strcmp(entry->d_name + n - 4, ".rel") == 0) ++count;
    }
    closedir(dir);
    return count == 415;
}

// What the prepared files were made from: the disc's path, size and time, so
// a different or changed disc is prepared again.
static void disc_stamp(const char* disc, char* out, size_t size) {
    struct stat info;
    if (stat(disc, &info) != 0) {
        snprintf(out, size, "%s", disc);
        return;
    }
    snprintf(out, size, "%s|%llu|%llu", disc, (unsigned long long)info.st_size, (unsigned long long)info.st_mtim.tv_sec);
}

static const char* extension(const char* path) {
    const char* dot = strrchr(path, '.');
    const char* slash = strrchr(path, '/');
    return dot != NULL && (slash == NULL || dot > slash) ? dot : "";
}

// Dolphin's compressed and other formats, which are unpacked to an ISO: by the extension, or by the file's first bytes
// (a renamed file).
static int needs_unpacking(const char* path) {
    static const char* const kinds[] = {".rvz", ".wia", ".gcz", ".ciso", ".nfs", ".wbfs"};
    for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++)
        if (strcasecmp(extension(path), kinds[i]) == 0)
            return 1;
    return bluewake_disc_container(path) != BW_DISC_PLAIN;
}

// --- the work, with its progress in the log ----------------------------------

typedef struct Work {
    int (*run)(struct Work*);
    const char* disc;
    char out[BW_PATH_MAX];
    char error[1024];
    int last_percent;
    char stage[128];
    int result;
} Work;

static void note_progress(void* context, double fraction, const char* stage) {
    Work* work = (Work*)context;
    const int percent = (int)(fraction * 100.0);
    if (stage != NULL && strcmp(stage, work->stage) != 0) {  // the step the work is on
        snprintf(work->stage, sizeof work->stage, "%s", stage);
        fprintf(stderr, "[disc] %s\n", stage);
        work->last_percent = -1;
    }
    if (percent / 10 != work->last_percent / 10 || work->last_percent < 0) {
        work->last_percent = percent;
        fprintf(stderr, "[disc] %d%%\n", percent);
    }
}

static int with_progress(Work* work, const char* title) {
    fprintf(stderr, "[disc] %s\n", title);
    work->last_percent = -1;
    work->result = work->run(work);
    return work->result;
}

// --- the steps ---------------------------------------------------------------

static void unpack_progress(void* context, double fraction, const char* stage) {
    note_progress(context, fraction, stage);
}

// The app's own unpacker (disc_unpack.c): into a private folder of its own, nothing earlier is touched.
static int unpack(Work* work) {
    char pending[BW_PATH_MAX], folder[900];
    if (fresh_import_dir(folder, sizeof folder) != 0) {
        snprintf(work->error, sizeof work->error, "Could not create a private disc-import folder (error %d).", errno);
        return -1;
    }
    snprintf(pending, sizeof pending, "%s/GZLE01.iso", folder);
    char error[1024] = "";
    if (bluewake_disc_unpack(work->disc, pending, unpack_progress, work, error, sizeof error) == 0) {
        snprintf(work->out, sizeof work->out, "%s", pending);
        return 0;
    }
    snprintf(work->error, sizeof work->error, "%s", error);
    return -1;
}

// The disc importer's progress, scaled into the first part of the bar.
typedef struct Scaled {
    Work* work;
    double base, span;
} Scaled;

static void scaled_progress(void* context, double fraction, const char* stage) {
    const Scaled* scaled = (const Scaled*)context;
    char text[160];
    if (stage != NULL) {
        snprintf(text, sizeof text, "%s (once)...", stage);
        note_progress(scaled->work, scaled->base + scaled->span * fraction, text);
    } else {
        note_progress(scaled->work, scaled->base + scaled->span * fraction, NULL);
    }
}

// `text` quoted for /bin/sh.
static void shell_quote(const char* text, char* out, size_t size) {
    size_t used = 0;
    if (used + 1 < size) out[used++] = '\'';
    for (const char* c = text; *c != '\0' && used + 5 < size; c++) {
        if (*c == '\'') {
            memcpy(out + used, "'\\''", 4);
            used += 4;
        } else {
            out[used++] = *c;
        }
    }
    if (used + 2 < size) out[used++] = '\'';
    out[used] = '\0';
}

// The Python for rando/prepare_extras.py: the one the release carries (rando/python/bin/python3), else python3 on the PATH.
static void python_command(char* out, size_t size) {
    char bundled[BW_PATH_MAX];
    snprintf(bundled, sizeof bundled, "%srando/python/bin/python3", g_exe);
    if (is_file(bundled))
        shell_quote(bundled, out, size);
    else
        snprintf(out, size, "python3");
}

// The files the release does not carry because they are made from the player's disc: Swift Sail's textures,
// the Cosmetics Editor's Link data and the randomizer variant's RELs. rando/prepare_extras.py makes them in `game` beside
// main.dol and rels/; it prints "PROGRESS fraction text" lines for the log. A step that fails only costs its own feature
// (the log and game/extras.json say which); the game still starts. 1 when the tools are here and ran, 0 when they are not.
static int make_extras(Work* work, const char* disc, const char* game, double base, double span, int only_missing) {
    char script[BW_PATH_MAX], timing[BW_PATH_MAX];
    snprintf(script, sizeof script, "%srando/prepare_extras.py", g_exe);
    snprintf(timing, sizeof timing, "%sextras-seconds.txt", g_data);
    if (!is_file(script)) {
        fprintf(stderr, "[disc] rando/prepare_extras.py is missing: Swift Sail, Link's colors in the Cosmetics Editor and the "
                        "Randomizer cannot be made from the disc\n");
        return 0;
    }
    char python[BW_PATH_MAX], qscript[BW_PATH_MAX], qdisc[BW_PATH_MAX], qgame[BW_PATH_MAX], qtiming[BW_PATH_MAX];
    python_command(python, sizeof python);
    shell_quote(script, qscript, sizeof qscript);
    shell_quote(disc, qdisc, sizeof qdisc);
    shell_quote(game, qgame, sizeof qgame);
    shell_quote(timing, qtiming, sizeof qtiming);
    char command[BW_PATH_MAX * 6];
    snprintf(command, sizeof command, "%s %s --iso %s --out %s --timing %s%s 2>&1", python, qscript, qdisc, qgame, qtiming,
             only_missing ? " --only-missing" : "");
    FILE* pipe = popen(command, "r");
    if (pipe == NULL) {
        fprintf(stderr, "[disc] python3 could not start (error %d)\n", errno);
        return 0;
    }
    char line[1024];
    while (fgets(line, sizeof line, pipe) != NULL) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (strncmp(line, "PROGRESS ", 9) == 0) {
            char* end = NULL;
            const double fraction = strtod(line + 9, &end);
            if (end != NULL && *end == ' ')
                note_progress(work, base + span * fraction, end + 1);
        } else if (line[0] != '\0') {
            fprintf(stderr, "[disc] %s\n", line);
        }
    }
    const int status = pclose(pipe);
    if (status != 0)
        fprintf(stderr, "[disc] some of the files made from the disc were not made (exit code %d; %s/extras.json says "
                        "which): only the features that need them are affected\n", WIFEXITED(status) ? WEXITSTATUS(status) : -1, game);
    return 1;
}

static int prepare(Work* work) {
    Scaled scaled = {work, 0.0, 0.2};
    if (bluewake_disc_prepare(work->disc, work->out, scaled_progress, &scaled, work->error, sizeof work->error) != 0)
        return -1;
    make_extras(work, work->disc, work->out, 0.2, 0.8, 0);
    return 0;
}

// An earlier preparation of this disc that lacks some extras (made by an older app, or a step that failed): only the
// steps whose outputs are missing are made.
static int prepare_extras_only(Work* work) {
    make_extras(work, work->disc, work->out, 0.0, 1.0, 1);
    return 0;
}

// Whether prepare_extras.py --check finds a step whose outputs are missing (exit code 3). A quick run.
static int extras_missing(const char* disc, const char* game) {
    char script[BW_PATH_MAX];
    snprintf(script, sizeof script, "%srando/prepare_extras.py", g_exe);
    if (!is_file(script)) return 0;
    char python[BW_PATH_MAX], qscript[BW_PATH_MAX], qdisc[BW_PATH_MAX], qgame[BW_PATH_MAX];
    python_command(python, sizeof python);
    shell_quote(script, qscript, sizeof qscript);
    shell_quote(disc, qdisc, sizeof qdisc);
    shell_quote(game, qgame, sizeof qgame);
    char command[BW_PATH_MAX * 5];
    snprintf(command, sizeof command, "%s %s --iso %s --out %s --check >/dev/null 2>&1", python, qscript, qdisc, qgame);
    const int status = system(command);
    return WIFEXITED(status) && WEXITSTATUS(status) == 3;
}

// --- the file picker (SDL3's native dialog) ---------------------------------

typedef struct {
    char result[BW_PATH_MAX];
    char error[1024];
    bool failed;
    SDL_AtomicInt done;
} ChooseState;

static void SDLCALL choose_callback(void* userdata, const char* const* filelist, int filter) {
    (void)filter;
    ChooseState* state = (ChooseState*)userdata;
    if (filelist == NULL) {
        state->failed = true;
        snprintf(state->error, sizeof state->error, "%s", SDL_GetError());
    } else if (filelist[0] != NULL) {
        snprintf(state->result, sizeof state->result, "%s", filelist[0]);
    }
    SDL_SetAtomicInt(&state->done, 1);
}

// The explanation, then SDL's file picker. 1 and the chosen path, 0 when the player cancels, or -1 when no dialog backend
// is available.
static int choose(const char* why, char* out, size_t size) {
    const char* testing = getenv("BLUEWAKE_DISC_CHOICE");  // scripted tests: the picker's answer
    if (testing != NULL) {
        snprintf(out, size, "%s", testing);
        return testing[0] != '\0';
    }
    if (getenv("BLUEWAKE_NO_DIALOG") != NULL)
        return 0;
    if ((SDL_WasInit(SDL_INIT_EVENTS) & SDL_INIT_EVENTS) == 0 && !SDL_InitSubSystem(SDL_INIT_EVENTS)) {
        fprintf(stderr, "[disc] could not initialize the file chooser: %s\n", SDL_GetError());
        return -1;
    }
    char folder[BW_PATH_MAX];
    snprintf(folder, sizeof folder, "%s", g_data);
    const size_t length = strlen(folder);
    if (length > 1 && folder[length - 1] == '/')
        folder[length - 1] = '\0';
    char text[4096];
    snprintf(text, sizeof text,
             "%s"
             "boat-of-daphnes plays The Legend of Zelda: The Wind Waker from your own copy of the game: the "
             "GameCube disc for the USA (GZLE01).\n\n"
             "Choose your disc image next: an .iso or .gcm file (an .nkit.iso too), or a Dolphin .rvz, .wia or .gcz, or a "
             ".ciso. boat-of-daphnes checks it and prepares it once (a compressed image is unpacked to an ISO in %s), "
             "then remembers it.",
             why, folder);
    fprintf(stderr, "[disc] %s\n", text);
    static const SDL_DialogFileFilter filters[] = {
        {"GameCube disc images", "iso;gcm;rvz;wia;gcz;ciso;nfs"},
        {"All files", "*"},
    };
    ChooseState state;
    memset(&state, 0, sizeof state);
    SDL_ClearError();
    SDL_ShowOpenFileDialog(choose_callback, &state, NULL, filters, (int)(sizeof filters / sizeof filters[0]), NULL, false);
    // Pump until the callback fires (the dialog is asynchronous).
    while (!SDL_GetAtomicInt(&state.done)) {
        SDL_PumpEvents();
        SDL_Delay(10);
    }
    if (state.failed) {
        fprintf(stderr,
                "[disc] the file chooser could not open: %s\n"
                "[disc] Install an XDG desktop portal backend or Zenity, or start boat-of-daphnes with --disc FILE.\n",
                state.error[0] != '\0' ? state.error : "no supported Linux dialog backend");
        return -1;
    }
    if (state.result[0] == '\0')
        return 0;
    snprintf(out, size, "%s", state.result);
    return 1;
}

int bw_disc_setup(const char* exe_dir, const char* data_dir) {
    g_exe = exe_dir;
    g_data = data_dir;
    // Everything given already (the builder's training, scripted runs).
    const char* given_dol = getenv("BLUEWAKE_DOL");
    if (given_dol != NULL && given_dol[0] != '\0')
        return 0;
    char path[BW_PATH_MAX];
    const char* given = getenv("BLUEWAKE_DISC");
    const int explicit_disc = given != NULL && given[0] != '\0';
    // A folder the builder made from the player's disc has it all beside the app.
    char app_disc[BW_PATH_MAX], app_dol[BW_PATH_MAX], app_rels[BW_PATH_MAX];
    snprintf(app_disc, sizeof app_disc, "%sgame/GZLE01.iso", exe_dir);
    snprintf(app_dol, sizeof app_dol, "%sgame/main.dol", exe_dir);
    snprintf(app_rels, sizeof app_rels, "%sgame/rels", exe_dir);
    if (!explicit_disc && is_file(app_disc) && prepared_ready(app_dol, app_rels))
        return 0;

    char remembered[BW_PATH_MAX], game[900], game_record[BW_PATH_MAX], dol[BW_PATH_MAX], rels[BW_PATH_MAX], stamp_path[BW_PATH_MAX];
    snprintf(remembered, sizeof remembered, "%sdisc.txt", data_dir);
    snprintf(game_record, sizeof game_record, "%sgame.txt", data_dir);
    if (!read_line(game_record, game, sizeof game))
        snprintf(game, sizeof game, "%sgame", data_dir);
    snprintf(dol, sizeof dol, "%s/main.dol", game);
    snprintf(rels, sizeof rels, "%s/rels", game);
    snprintf(stamp_path, sizeof stamp_path, "%s/prepared.txt", game);
    char disc[BW_PATH_MAX] = "";
    if (explicit_disc)
        snprintf(disc, sizeof disc, "%s", given);
    else
        read_line(remembered, disc, sizeof disc);
    if (explicit_disc && !is_file(disc)) {
        char text[BW_PATH_MAX + 128];
        snprintf(text, sizeof text, "boat-of-daphnes cannot find the disc image it was given:\n\n%s", disc);
        fprintf(stderr, "[disc] %s\n", text);
        box(text, SDL_MESSAGEBOX_ERROR);
        return -1;
    }
    const char* why = disc[0] != '\0' && !is_file(disc) ? "The disc image boat-of-daphnes used before is gone.\n\n" : "";
    // A scripted choice is not asked again once it was refused.
    const int scripted = getenv("BLUEWAKE_DISC_CHOICE") != NULL;
    for (;;) {
        if (disc[0] == '\0' || !is_file(disc)) {
            const int chosen = choose(why, path, sizeof path);
            if (chosen < 0)
                return -1;
            if (chosen == 0) {
                fprintf(stderr, "[disc] no disc image chosen\n");
                return 1;
            }
            snprintf(disc, sizeof disc, "%s", path);
        }
        if (needs_unpacking(disc)) {
            Work work;
            memset(&work, 0, sizeof work);
            work.run = unpack;
            work.disc = disc;
            fprintf(stderr, "[disc] unpacking %s\n", disc);
            if (with_progress(&work, "Unpacking your disc image to an ISO (once)...") != 0) {
                fprintf(stderr, "[disc] %s\n", work.error);
                box(work.error, SDL_MESSAGEBOX_WARNING);
                if (explicit_disc || scripted)
                    return -1;
                disc[0] = '\0';
                why = "";
                continue;
            }
            snprintf(disc, sizeof disc, "%s", work.out);
        }
        char error[1024] = "";
        if (bluewake_disc_check(disc, error, sizeof error) != 0) {
            char text[2048];
            snprintf(text, sizeof text,
                     "%s\n\n%s\n\nChoose an image of the GameCube USA disc (GZLE01) instead: an .iso, .gcm, .nkit.iso, "
                     "or a Dolphin .rvz, .wia, .gcz or .ciso file.",
                     disc, error);
            fprintf(stderr, "[disc] %s: %s\n", disc, error);
            box(text, SDL_MESSAGEBOX_WARNING);
            if (explicit_disc || scripted)
                return -1;
            disc[0] = '\0';
            why = "";
            continue;
        }
        char stamp[BW_PATH_MAX + 64], prepared[BW_PATH_MAX + 64];
        disc_stamp(disc, stamp, sizeof stamp);
        read_line(stamp_path, prepared, sizeof prepared);
        if (strcmp(stamp, prepared) != 0 || !prepared_ready(dol, rels)) {
            if (fresh_import_dir(game, sizeof game) != 0) {
                box("boat-of-daphnes could not create a new preparation folder. Previous files are preserved.", SDL_MESSAGEBOX_ERROR);
                return -1;
            }
            snprintf(dol, sizeof dol, "%s/main.dol", game);
            snprintf(rels, sizeof rels, "%s/rels", game);
            snprintf(stamp_path, sizeof stamp_path, "%s/prepared.txt", game);
            Work work;
            memset(&work, 0, sizeof work);
            work.run = prepare;
            work.disc = disc;
            snprintf(work.out, sizeof work.out, "%s", game);
            fprintf(stderr, "[disc] preparing %s\n", disc);
            if (with_progress(&work, "Checking and preparing your disc (once)...") != 0) {
                char text[2048];
                snprintf(text, sizeof text, "%s\n\n%s\n\nboat-of-daphnes needs the USA disc, revision 0 (GZLE01).", disc, work.error);
                fprintf(stderr, "[disc] %s: %s\n", disc, work.error);
                box(text, SDL_MESSAGEBOX_WARNING);
                if (explicit_disc || scripted)
                    return -1;
                disc[0] = '\0';
                why = "";
                continue;
            }
            if (!write_line(stamp_path, stamp) || !write_line(game_record, game)) {
                box("boat-of-daphnes could not remember the prepared files. The source and prepared files are preserved; try again.",
                    SDL_MESSAGEBOX_ERROR);
                return -1;
            }
        } else {
            // Prepared by an earlier app: the files made from the disc may still be missing.
            char extras_record[BW_PATH_MAX];
            snprintf(extras_record, sizeof extras_record, "%s/extras.json", game);
            char tools[BW_PATH_MAX];
            snprintf(tools, sizeof tools, "%srando/prepare_extras.py", g_exe);
            if (is_file(tools) && (!is_file(extras_record) || extras_missing(disc, game))) {
                Work work;
                memset(&work, 0, sizeof work);
                work.run = prepare_extras_only;
                work.disc = disc;
                snprintf(work.out, sizeof work.out, "%s", game);
                fprintf(stderr, "[disc] some files that come from the disc are missing (%s): making them from %s\n",
                        is_file(extras_record) ? "an older install" : "none made yet", disc);
                with_progress(&work, "Making the files that come from your disc (once)...");
            }
        }
        break;
    }
    if (!explicit_disc && !write_line(remembered, disc)) {
        box("boat-of-daphnes could not remember this disc. The disc is preserved; try again.", SDL_MESSAGEBOX_ERROR);
        return -1;
    }
    setenv("BLUEWAKE_DISC", disc, 1);
    setenv("BLUEWAKE_DOL", dol, 1);
    setenv("BLUEWAKE_RELS_DIR", rels, 1);
    setenv("BLUEWAKE_EXTRAS_DIR", game, 1);
    fprintf(stderr, "[disc] %s (prepared in %s)\n", disc, game);
    return 0;
}
