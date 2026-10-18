#include "prof.h"
#include "gxruntime/aurora_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_bw_prof_on = 0;
static void hostmode_report(void);

// Aurora's count of frames presented (a relaxed atomic read; the sampler polls it to time the shown frames).
unsigned long long aurora_get_shown_frames(void);
unsigned long long bluewake_host_retrace_count(void);

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

enum { MAX_DEPTH = 96, MAX_MODS = 1024 };
enum { PH_ACTOR_EXEC, PH_ACTOR_DRAW, PH_PAINTER, PH_AUDIO, PH_LOGIC, PH_OTHER, PH_COUNT };
static const char* const kPhaseName[PH_COUNT] = {"actor-exec", "actor-draw", "painter(gx)", "audio(guest)",
                                                  "game-logic", "host/other"};
enum { CAT_J3D, CAT_PARTICLE, CAT_CC, CAT_BG, CAT_AUDIO, CAT_JKR, CAT_ENV, CAT_SDK, CAT_UI, CAT_MATH, CAT_OTHER,
       CAT_COUNT };
static const char* const kCatName[CAT_COUNT] = {"j3d/anim/skin", "particles", "collision(cc)", "bg-collision",
                                                "audio(JAS/JAI)", "jkr/heap", "environment", "sdk(gx/os/pad)",
                                                "ui(j2d/dlist)", "math", "other"};

typedef struct {
    u32 addr, size;
    u16 mod;
    u8 cat;
    u8 phase;  // PH_COUNT = none
    const char* name;
} Sym;

static Sym* g_sym;
static u32 g_nsym;
static char* g_mod_name[MAX_MODS];
static u32 g_nmod;  // module 0 is "main"
static char* g_text;

static CPUState* g_cpu;
static HANDLE g_thread;
static volatile LONG g_stop;
static u32 g_hz = 500, g_secs = 10, g_top = 12;

// --- host phase marks (game thread) ---
static volatile long long g_host_ticks[BW_PROF_HOST_COUNT];
static LARGE_INTEGER g_qpf;

unsigned long long bluewake_prof_now(void) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (unsigned long long)t.QuadPart;
}

void bluewake_prof_add(int phase, unsigned long long t0) {
    if ((unsigned)phase >= BW_PROF_HOST_COUNT)
        return;
    g_host_ticks[phase] += (long long)(bluewake_prof_now() - t0);  // the game thread writes; the sampler reads
}

static u32 intern_mod(const char* name, size_t len) {
    for (u32 i = 0; i < g_nmod; ++i)
        if (strlen(g_mod_name[i]) == len && memcmp(g_mod_name[i], name, len) == 0)
            return i;
    if (g_nmod >= MAX_MODS)
        return 0;
    g_mod_name[g_nmod] = (char*)malloc(len + 1);
    memcpy(g_mod_name[g_nmod], name, len);
    g_mod_name[g_nmod][len] = 0;
    return g_nmod++;
}

static int starts(const char* s, const char* p) { return strncmp(s, p, strlen(p)) == 0; }

static u8 classify(const char* n) {
    if (starts(n, "J3D") || starts(n, "mDoExt_Mca") || starts(n, "mDoExt_Mtx") || starts(n, "mDoExt_model") ||
        starts(n, "mDoExt_J3D") || starts(n, "mDoExt_btk") || starts(n, "mDoExt_brk") || starts(n, "mDoExt_bck") ||
        starts(n, "mDoExt_bpk") || starts(n, "mDoExt_btp") || starts(n, "mDoExt_set") || starts(n, "mDoExt_change") ||
        starts(n, "calc__14mDoExt_McaMorf") || starts(n, "calc__15mDoExt_McaMorf2") || starts(n, "mDoExt_McaMorf"))
        return CAT_J3D;
    if (starts(n, "JPA") || starts(n, "dPa"))
        return CAT_PARTICLE;
    if (starts(n, "dCcS") || starts(n, "cCc") || starts(n, "dCcD") || starts(n, "dCcMassS"))
        return CAT_CC;
    if (starts(n, "dBgS") || starts(n, "cBgS") || starts(n, "cBgW") || starts(n, "dBgW") || starts(n, "dBgPc"))
        return CAT_BG;
    if (starts(n, "JAS") || starts(n, "JAI") || starts(n, "JAU") || starts(n, "mDoAud") || starts(n, "Z2"))
        return CAT_AUDIO;
    if (starts(n, "JKR") || starts(n, "JUT") || starts(n, "JSU") || starts(n, "JGadget"))
        return CAT_JKR;
    if (starts(n, "dKy") || starts(n, "dKankyo") || starts(n, "dEnv") || starts(n, "dKa_"))
        return CAT_ENV;
    if (starts(n, "GX") || starts(n, "OS") || starts(n, "VI") || starts(n, "PAD") || starts(n, "DVD") ||
        starts(n, "CARD") || starts(n, "__OS") || starts(n, "AI") || starts(n, "AR") || starts(n, "DSP"))
        return CAT_SDK;
    if (starts(n, "J2D") || starts(n, "dDlst") || starts(n, "dMeter") || starts(n, "dMsg"))
        return CAT_UI;
    if (starts(n, "cM3d") || starts(n, "cM_") || starts(n, "cXyz") || starts(n, "csXyz") || starts(n, "mDoMtx") ||
        starts(n, "PSMTX") || starts(n, "C_MTX") || starts(n, "MTX") || starts(n, "cMtx") || starts(n, "sqrt") ||
        starts(n, "atan") || starts(n, "sin") || starts(n, "cos") || starts(n, "cLib") || starts(n, "cAng") ||
        starts(n, "cM") || starts(n, "PSVEC") || starts(n, "VEC"))
        return CAT_MATH;
    return CAT_OTHER;
}

static u8 phase_marker(const char* name) {
    if (!strcmp(name, "fopAc_Execute__FPv")) return PH_ACTOR_EXEC;
    if (!strcmp(name, "fopAc_Draw__FPv")) return PH_ACTOR_DRAW;
    if (!strcmp(name, "mDoGph_Painter__Fv")) return PH_PAINTER;
    if (!strcmp(name, "mDoAud_Execute__Fv")) return PH_AUDIO;
    if (!strcmp(name, "audioproc__Q28JASystem12TAudioThreadFPv")) return PH_AUDIO;  // the guest audio thread
    if (!strcmp(name, "fapGm_Execute__Fv")) return PH_LOGIC;
    return PH_COUNT;
}

static int load_symbols(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    g_text = (char*)malloc((size_t)n + 1);
    if (!g_text || fread(g_text, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        return 0;
    }
    fclose(f);
    g_text[n] = 0;
    g_nmod = 0;
    intern_mod("main", 4);
    size_t cap = 0;
    for (char* p = g_text; *p; ++p)
        if (*p == '\n') ++cap;
    g_sym = (Sym*)calloc(cap + 2, sizeof(Sym));
    char* p = g_text;
    while (*p && g_nsym < cap) {
        char* eol = strchr(p, '\n');
        if (!eol) eol = p + strlen(p);
        char* q;
        u32 addr = (u32)strtoul(p, &q, 16);
        u32 size = (u32)strtoul(q, &q, 16);
        while (*q == ' ') ++q;
        char* ms = q;
        while (*q && *q != ' ' && q < eol) ++q;
        size_t ml = (size_t)(q - ms);
        while (*q == ' ') ++q;
        char* nm = q;
        char* e = eol;
        if (e > nm && e[-1] == '\r') --e;
        const char saved = *eol;
        *e = 0;
        if (ml && nm < e) {
            Sym* s = &g_sym[g_nsym++];
            s->addr = addr;
            s->size = size;
            s->mod = (u16)intern_mod(ms, ml);
            s->name = nm;
            s->cat = classify(nm);
            s->phase = phase_marker(nm);
        }
        if (saved == 0) break;
        p = eol + 1;
    }
    return 1;
}

static int find_sym(u32 pc) {
    int lo = 0, hi = (int)g_nsym - 1, best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (g_sym[mid].addr <= pc) { best = mid; lo = mid + 1; } else hi = mid - 1;
    }
    if (best < 0)
        return -1;
    const Sym* s = &g_sym[best];
    if (s->size != 0 && pc >= s->addr + s->size)
        return -1;
    if (s->size == 0 && pc - s->addr > 0x400)
        return -1;
    return best;
}

static int read32(u32 addr, u32* out) {
    if ((addr & 3u) != 0u)
        return 0;
    u32 a = addr & ~0x40000000u;
    if (a < 0x80000000u)
        return 0;
    u32 off = a - 0x80000000u;
    if (off + 4u > g_cpu->ram_size)
        return 0;
    *out = _byteswap_ulong(*(const volatile u32*)(g_cpu->ram + off));
    return 1;
}

// --- aggregation (sampler thread only) ---
static u32* g_self;       // by symbol
static u32* g_incl;
static u32* g_stamp;
static u32 g_mod_samples[MAX_MODS];       // innermost REL on the stack
static u32 g_phase[PH_COUNT];
static u32 g_cat[CAT_COUNT];
static u32 g_exec_cat[CAT_COUNT];
static u32 g_nsamples, g_nunnamed, g_serial;
static u32 g_unk_bucket[64];
enum { STACK_SLOTS = 8192, STACK_DEPTH = 5 };
typedef struct { int f[STACK_DEPTH]; u32 n; } StackRow;
static StackRow g_stacks[STACK_SLOTS];
static StackRow g_ostacks[STACK_SLOTS];   // samples with no phase marker on the stack: keyed by the stack's root end
static u32 g_shown_hist[256];       // shown-frame intervals, 1 ms buckets (255 = 255 ms and over)
static u32 g_shown_n;
static unsigned long long g_last_shown, g_last_shown_t, g_last_retrace, g_last_retrace_t;
static u32 g_retrace_hist[256];
static u32 g_retrace_n;

static int cmp_desc(const void* a, const void* b) {
    const u32* x = (const u32*)a;
    const u32* y = (const u32*)b;
    return (x[1] < y[1]) - (x[1] > y[1]);
}

static void reset_counts(void) {
    memset(g_self, 0, sizeof(u32) * (g_nsym + 1));
    memset(g_incl, 0, sizeof(u32) * (g_nsym + 1));
    memset(g_mod_samples, 0, sizeof g_mod_samples);
    memset(g_phase, 0, sizeof g_phase);
    memset(g_cat, 0, sizeof g_cat);
    memset(g_exec_cat, 0, sizeof g_exec_cat);
    memset(g_unk_bucket, 0, sizeof g_unk_bucket);
    memset(g_stacks, 0, sizeof g_stacks);
    memset(g_ostacks, 0, sizeof g_ostacks);
    memset(g_shown_hist, 0, sizeof g_shown_hist);
    memset(g_retrace_hist, 0, sizeof g_retrace_hist);
    g_shown_n = g_retrace_n = 0;
    g_nsamples = g_nunnamed = 0;
}

static void poll_frames(void) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    const unsigned long long shown = aurora_get_shown_frames();
    if (shown != g_last_shown) {
        if (g_last_shown_t != 0 && shown == g_last_shown + 1) {
            const u32 ms = (u32)((t.QuadPart - (LONGLONG)g_last_shown_t) * 1000 / g_qpf.QuadPart);
            ++g_shown_hist[ms > 255 ? 255 : ms];
            ++g_shown_n;
        }
        g_last_shown = shown;
        g_last_shown_t = (unsigned long long)t.QuadPart;
    }
    const unsigned long long rt = bluewake_host_retrace_count();
    if (rt != g_last_retrace) {
        if (g_last_retrace_t != 0 && rt == g_last_retrace + 1) {
            const u32 ms = (u32)((t.QuadPart - (LONGLONG)g_last_retrace_t) * 1000 / g_qpf.QuadPart);
            ++g_retrace_hist[ms > 255 ? 255 : ms];
            ++g_retrace_n;
        }
        g_last_retrace = rt;
        g_last_retrace_t = (unsigned long long)t.QuadPart;
    }
}

static u32 hist_pct(const u32* h, u32 n, double pct) {
    if (!n) return 0;
    u32 want = (u32)(n * pct / 100.0), acc = 0;
    for (u32 i = 0; i < 256; ++i) { acc += h[i]; if (acc > want) return i; }
    return 255;
}

static void sample_once(void) {
    poll_frames();
    const u32 pc = g_cpu->pc, lr = g_cpu->lr;
    u32 sp = g_cpu->gpr[1];
    int fr[MAX_DEPTH];
    int nf = 0;
    const int f0 = find_sym(pc);
    fr[nf++] = f0;
    const int fl = find_sym(lr);
    if (fl != f0) fr[nf++] = fl;
    for (int i = 0; i < MAX_DEPTH - 2 && nf < MAX_DEPTH; ++i) {
        u32 ra, back;
        if (!read32(sp + 4u, &ra)) break;
        const int f = find_sym(ra);
        if (f < 0 && !(ra >= 0x80000000u)) break;
        if (f != fr[nf - 1] || f < 0) fr[nf++] = f;
        if (!read32(sp, &back) || back <= sp || back - sp > 0x100000u) break;
        sp = back;
    }
    {
        int key[STACK_DEPTH];
        u32 h = 2166136261u;
        for (int i = 0; i < STACK_DEPTH; ++i) {
            key[i] = i < nf ? fr[i] : -2;
            h = (h ^ (u32)(key[i] + 3)) * 16777619u;
        }
        for (u32 probe = 0; probe < 16; ++probe) {
            StackRow* r = &g_stacks[(h + probe) & (STACK_SLOTS - 1)];
            if (r->n == 0) { memcpy(r->f, key, sizeof key); r->n = 1; break; }
            if (memcmp(r->f, key, sizeof key) == 0) { ++r->n; break; }
        }
    }
    ++g_nsamples;
    ++g_serial;
    if (f0 >= 0) ++g_self[f0]; else { ++g_nunnamed; ++g_unk_bucket[(pc >> 16) & 63]; }
    int phase = PH_COUNT, cat = -1, rel_mod = -1;
    for (int i = 0; i < nf; ++i) {
        const int f = fr[i];
        if (f < 0) continue;
        if (g_stamp[f] != g_serial) { g_stamp[f] = g_serial; ++g_incl[f]; }
        const Sym* s = &g_sym[f];
        if (phase == PH_COUNT && s->phase != PH_COUNT) phase = s->phase;
        if (cat < 0 && s->cat != CAT_OTHER && s->cat != CAT_MATH) cat = s->cat;
        if (rel_mod < 0 && s->mod != 0) rel_mod = s->mod;
    }
    if (cat < 0) cat = (f0 >= 0 && g_sym[f0].cat == CAT_MATH) ? CAT_MATH : CAT_OTHER;
    const int ph = phase == PH_COUNT ? PH_OTHER : phase;
    ++g_phase[ph];
    ++g_cat[cat];
    if (ph == PH_ACTOR_EXEC) ++g_exec_cat[cat];
    if (ph == PH_OTHER) {
        int key[STACK_DEPTH];
        u32 h = 2166136261u;
        for (int i = 0; i < STACK_DEPTH; ++i) {
            key[i] = (nf - 1 - i) >= 0 ? fr[nf - 1 - i] : -2;
            h = (h ^ (u32)(key[i] + 3)) * 16777619u;
        }
        for (u32 probe = 0; probe < 16; ++probe) {
            StackRow* r = &g_ostacks[(h + probe) & (STACK_SLOTS - 1)];
            if (r->n == 0) { memcpy(r->f, key, sizeof key); r->n = 1; break; }
            if (memcmp(r->f, key, sizeof key) == 0) { ++r->n; break; }
        }
    }
    if (rel_mod >= 0) ++g_mod_samples[rel_mod];
}

static void print_top_syms(const char* label, const u32* counts, u32 total, u32 top) {
    u32* rows = (u32*)malloc(sizeof(u32) * 2 * (g_nsym + 1));
    u32 n = 0;
    for (u32 i = 0; i < g_nsym; ++i)
        if (counts[i]) { rows[2 * n] = i; rows[2 * n + 1] = counts[i]; ++n; }
    qsort(rows, n, 8, cmp_desc);
    char line[3072];
    int len = snprintf(line, sizeof line, "[prof] %s:", label);
    for (u32 i = 0; i < n && i < top && len < (int)sizeof line - 200; ++i) {
        const Sym* s = &g_sym[rows[2 * i]];
        len += snprintf(line + len, sizeof line - (size_t)len, " %.1f%% %s%s%s;", 100.0 * rows[2 * i + 1] / total,
                        s->mod ? g_mod_name[s->mod] : "", s->mod ? ":" : "", s->name);
    }
    fprintf(stderr, "%s\n", line);
    free(rows);
}

static const char* sym_name(int f) {
    return f >= 0 ? g_sym[f].name : (f == -2 ? "" : "?");
}

static void print_top_stacks_of(const StackRow* table, const char* tag, u32 total, u32 top) {
    u32* rows = (u32*)malloc(sizeof(u32) * 2 * STACK_SLOTS);
    u32 n = 0;
    for (u32 i = 0; i < STACK_SLOTS; ++i)
        if (table[i].n) { rows[2 * n] = i; rows[2 * n + 1] = table[i].n; ++n; }
    qsort(rows, n, 8, cmp_desc);
    for (u32 i = 0; i < n && i < top; ++i) {
        const StackRow* r = &table[rows[2 * i]];
        char line[1024];
        int len = snprintf(line, sizeof line, "[prof] %s %.1f%%:", tag, 100.0 * r->n / total);
        for (int k = 0; k < STACK_DEPTH && r->f[k] != -2 && len < (int)sizeof line - 120; ++k)
            len += snprintf(line + len, sizeof line - (size_t)len, "%s%s", k ? " < " : " ", sym_name(r->f[k]));
        fprintf(stderr, "%s\n", line);
    }
    free(rows);
}

static void print_top_stacks(u32 total, u32 top) {
    print_top_stacks_of(g_stacks, "stack", total, top);
    print_top_stacks_of(g_ostacks, "unmarked(root first)", total, top);
}

static void print_top_mods(const char* label, const u32* counts, u32 total, u32 top) {
    u32 rows[2 * MAX_MODS];
    u32 n = 0;
    for (u32 i = 1; i < g_nmod; ++i)
        if (counts[i]) { rows[2 * n] = i; rows[2 * n + 1] = counts[i]; ++n; }
    qsort(rows, n, 8, cmp_desc);
    char line[1536];
    int len = snprintf(line, sizeof line, "[prof] %s:", label);
    for (u32 i = 0; i < n && i < top && len < (int)sizeof line - 80; ++i)
        len += snprintf(line + len, sizeof line - (size_t)len, " %s %.1f%%;", g_mod_name[rows[2 * i]],
                        100.0 * rows[2 * i + 1] / total);
    fprintf(stderr, "%s\n", line);
}

static void read_stage(char* stage, int* room) {
    memset(stage, 0, 9);
    const u32 base = 0x803C9D3Cu - 0x80000000u;
    if (base + 8u < g_cpu->ram_size)
        for (int i = 0; i < 8; ++i) {
            const char c = (char)g_cpu->ram[base + (u32)i];
            stage[i] = (c >= 32 && c < 127) ? c : 0;
        }
    const u32 r = 0x803F6A78u - 0x80000000u;
    *room = r < g_cpu->ram_size ? (int)(signed char)g_cpu->ram[r] : -1;
}

static void link_pos(int* x, int* y, int* z) {
    u32 player = 0;
    *x = *y = *z = 0;
    if (!read32(0x803CA74Cu, &player) || player < 0x80000000u) return;
    u32 v[3];
    if (!read32(player + 0x1F8u, &v[0]) || !read32(player + 0x1FCu, &v[1]) || !read32(player + 0x200u, &v[2])) return;
    float f[3];
    memcpy(f, v, sizeof f);
    *x = (int)f[0]; *y = (int)f[1]; *z = (int)f[2];
}

static DolAuroraFrameTiming g_timing0;

static void report(const char* why, double secs) {
    const u32 total = g_nsamples;
    if (total == 0) return;
    char stage[9];
    int room;
    read_stage(stage, &room);
    int px, py, pz;
    link_pos(&px, &py, &pz);
    fprintf(stderr, "[prof] %s stage=%s room=%d pos=%d,%d,%d samples=%u hz=%.0f over=%.1fs unnamed=%.1f%%\n", why,
            stage, room, px, py, pz, total, total / secs, secs, 100.0 * g_nunnamed / total);
    {
        DolAuroraFrameTiming t;
        dol_aurora_frame_timing(&t);
        const double dg = (double)(t.presents - g_timing0.presents);
        fprintf(stderr,
                "[prof] gpu-side/s: draws/frame=%.0f game-frames=%.1f shown=%.1f gx-worker-cpu=%.0f%% interp-cpu=%.0f%% "
                "render-cpu=%.0f%% present-ms=%.0f drain-ms=%.0f end-frame-ms=%.0f\n",
                dg > 0 ? (double)(t.draws - g_timing0.draws) / dg : 0.0, dg / secs,
                (double)(t.shown - g_timing0.shown) / secs,
                100.0 * (double)(t.gx_worker_cpu_us - g_timing0.gx_worker_cpu_us) / (secs * 1e6),
                100.0 * (double)(t.interp_helper_cpu_us - g_timing0.interp_helper_cpu_us) / (secs * 1e6),
                100.0 * (double)(t.render_worker_cpu_us - g_timing0.render_worker_cpu_us) / (secs * 1e6),
                (double)(t.present_us - g_timing0.present_us) / 1000.0 / secs,
                (double)(t.drain_us - g_timing0.drain_us) / 1000.0 / secs,
                (double)(t.end_frame_us - g_timing0.end_frame_us) / 1000.0 / secs);
        g_timing0 = t;
    }
    if (g_shown_n || g_retrace_n) {
        u32 over33 = 0, over50 = 0, rover20 = 0, rover34 = 0, rmax = 0, smax = 0;
        for (u32 i = 0; i < 256; ++i) {
            if (i >= 34) over33 += g_shown_hist[i];
            if (i >= 50) over50 += g_shown_hist[i];
            if (g_shown_hist[i]) smax = i;
            if (i >= 20) rover20 += g_retrace_hist[i];
            if (i >= 34) rover34 += g_retrace_hist[i];
            if (g_retrace_hist[i]) rmax = i;
        }
        fprintf(stderr,
                "[prof] frametime-ms (+-2): shown n=%u p50=%u p95=%u p99=%u max=%u >33ms=%u >50ms=%u | retrace n=%u p50=%u p95=%u p99=%u max=%u >20ms=%u >34ms=%u\n",
                g_shown_n, hist_pct(g_shown_hist, g_shown_n, 50), hist_pct(g_shown_hist, g_shown_n, 95),
                hist_pct(g_shown_hist, g_shown_n, 99), smax, over33, over50, g_retrace_n,
                hist_pct(g_retrace_hist, g_retrace_n, 50), hist_pct(g_retrace_hist, g_retrace_n, 95),
                hist_pct(g_retrace_hist, g_retrace_n, 99), rmax, rover20, rover34);
    }
    char line[1536];
    int len = snprintf(line, sizeof line, "[prof] phase:");
    for (int i = 0; i < PH_COUNT; ++i)
        len += snprintf(line + len, sizeof line - (size_t)len, " %s %.1f%%;", kPhaseName[i], 100.0 * g_phase[i] / total);
    fprintf(stderr, "%s\n", line);
    len = snprintf(line, sizeof line, "[prof] category:");
    for (int i = 0; i < CAT_COUNT; ++i)
        if (g_cat[i])
            len += snprintf(line + len, sizeof line - (size_t)len, " %s %.1f%%;", kCatName[i], 100.0 * g_cat[i] / total);
    fprintf(stderr, "%s\n", line);
    if (g_phase[PH_ACTOR_EXEC]) {
        len = snprintf(line, sizeof line, "[prof] in-actor-exec:");
        for (int i = 0; i < CAT_COUNT; ++i)
            if (g_exec_cat[i])
                len += snprintf(line + len, sizeof line - (size_t)len, " %s %.1f%%;", kCatName[i],
                                100.0 * g_exec_cat[i] / total);
        fprintf(stderr, "%s\n", line);
    }
    print_top_syms("self", g_self, total, g_top);
    print_top_syms("incl", g_incl, total, g_top);
    print_top_stacks(total, g_top / 2 + 2);
    print_top_mods("actors(incl)", g_mod_samples, total, g_top);
    LONGLONG h[BW_PROF_HOST_COUNT];
    for (int i = 0; i < BW_PROF_HOST_COUNT; ++i) { h[i] = g_host_ticks[i]; g_host_ticks[i] = 0; }
    const double ms = 1000.0 / (double)g_qpf.QuadPart;
    fprintf(stderr, "[prof] host-ms/s: audio %.0f gx %.0f present %.0f other %.0f\n", h[0] * ms / secs,
            h[1] * ms / secs, h[2] * ms / secs, h[3] * ms / secs);
    if (g_nunnamed) {
        u32 rows[128];
        u32 n = 0;
        for (u32 i = 0; i < 64; ++i)
            if (g_unk_bucket[i]) { rows[2 * n] = i; rows[2 * n + 1] = g_unk_bucket[i]; ++n; }
        qsort(rows, n, 8, cmp_desc);
        len = snprintf(line, sizeof line, "[prof] unnamed pc (pc>>16):");
        for (u32 i = 0; i < n && i < 4; ++i)
            len += snprintf(line + len, sizeof line - (size_t)len, " x%02X %.1f%%;", rows[2 * i],
                            100.0 * rows[2 * i + 1] / total);
        fprintf(stderr, "%s\n", line);
    }
    hostmode_report();
    fflush(stderr);
    reset_counts();
}


// --- host mode (BLUEWAKE_PROF_HOST=1, with BLUEWAKE_PROF=1): where the game thread's host code spends its time ---
// Suspends the game thread for a few microseconds at BLUEWAKE_PROF_HOST_HZ (default 200) to read its Rip. This one
// does cost the game thread (about 1% at 200 Hz): a diagnostic, never on by default. The Rip is bucketed by 16
// bytes; the summary resolves the top buckets through DbgHelp (the exe's PDB, found beside it or through
// BLUEWAKE_PROF_PDB) and names the module otherwise.
#include <psapi.h>
#include <dbghelp.h>
static HANDLE g_game_thread;
static int g_host_mode;
static u32 g_host_hz = 200;
enum { HOST_SLOTS = 16384 };
typedef struct { u64 bucket; u32 n; } HostRow;
static HostRow g_host_rows[HOST_SLOTS];
static u32 g_host_total;
typedef BOOL (WINAPI *SymInitializeFn)(HANDLE, PCSTR, BOOL);
typedef BOOL (WINAPI *SymFromAddrFn)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO);
typedef DWORD (WINAPI *SymSetOptionsFn)(DWORD);
static SymFromAddrFn g_sym_from_addr;

static void host_sym_init(void) {
    HMODULE dh = LoadLibraryA("dbghelp.dll");
    if (!dh) return;
    SymInitializeFn init = (SymInitializeFn)GetProcAddress(dh, "SymInitialize");
    SymSetOptionsFn opts = (SymSetOptionsFn)GetProcAddress(dh, "SymSetOptions");
    g_sym_from_addr = (SymFromAddrFn)GetProcAddress(dh, "SymFromAddr");
    if (!init || !g_sym_from_addr) { g_sym_from_addr = NULL; return; }
    if (opts) opts(0x00000002 /*SYMOPT_UNDNAME*/ | 0x00000010 /*DEFERRED_LOADS*/);
    char path[MAX_PATH * 2];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char* slash = strrchr(path, 92);
    if (slash) *slash = 0;
    const char* extra = getenv("BLUEWAKE_PROF_PDB");
    char search[MAX_PATH * 4];
    snprintf(search, sizeof search, "%s%s%s", path, extra ? ";" : "", extra ? extra : "");
    init(GetCurrentProcess(), search, TRUE);
}

static void hostmode_sample(void) {
    CONTEXT c;
    memset(&c, 0, sizeof c);
    c.ContextFlags = CONTEXT_CONTROL;
    if (SuspendThread(g_game_thread) == (DWORD)-1) return;
    const BOOL ok = GetThreadContext(g_game_thread, &c);
    ResumeThread(g_game_thread);
    if (!ok) return;
    const u64 bucket = (u64)c.Rip >> 4;
    ++g_host_total;
    u32 h = (u32)(bucket * 2654435761ull) & (HOST_SLOTS - 1);
    for (u32 k = 0; k < 64; ++k) {
        HostRow* r = &g_host_rows[(h + k) & (HOST_SLOTS - 1)];
        if (r->n == 0) { r->bucket = bucket; r->n = 1; return; }
        if (r->bucket == bucket) { ++r->n; return; }
    }
}

static int host_cmp(const void* a, const void* b) {
    const HostRow* x = (const HostRow*)a;
    const HostRow* y = (const HostRow*)b;
    return (x->n < y->n) - (x->n > y->n);
}

static void hostmode_report(void) {
    if (!g_host_mode || !g_host_total) return;
    HostRow* rows = (HostRow*)malloc(sizeof(HostRow) * HOST_SLOTS);
    u32 n = 0;
    for (u32 i = 0; i < HOST_SLOTS; ++i) if (g_host_rows[i].n) rows[n++] = g_host_rows[i];
    qsort(rows, n, sizeof(HostRow), host_cmp);
    // merge the top buckets by symbol / module
    struct { char name[160]; u32 n; } agg[96];
    u32 na = 0;
    u32 others = 0;
    for (u32 i = 0; i < n; ++i) {
        char name[160];
        const u64 rip = rows[i].bucket << 4;
        HMODULE mod = NULL;
        char modname[96] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)rip, &mod)) {
            char full[MAX_PATH];
            GetModuleFileNameA(mod, full, MAX_PATH);
            const char* base = strrchr(full, 92);
            snprintf(modname, sizeof modname, "%s", base ? base + 1 : full);
        }
        snprintf(name, sizeof name, "%s", modname);
        if (i < 400 && g_sym_from_addr) {
            char buf[sizeof(SYMBOL_INFO) + 256];
            PSYMBOL_INFO sym = (PSYMBOL_INFO)buf;
            memset(buf, 0, sizeof buf);
            sym->SizeOfStruct = sizeof(SYMBOL_INFO);
            sym->MaxNameLen = 255;
            DWORD64 disp = 0;
            if (g_sym_from_addr(GetCurrentProcess(), rip, &disp, sym))
                snprintf(name, sizeof name, "%s!%s", modname, sym->Name);
            else if (mod)
                snprintf(name, sizeof name, "%s+0x%llX", modname, (unsigned long long)(rip - (u64)mod) & ~0xFFFull);
        }
        u32 j = 0;
        while (j < na && strcmp(agg[j].name, name) != 0) ++j;
        if (j < na) agg[j].n += rows[i].n;
        else if (na < 96) { snprintf(agg[na].name, sizeof agg[na].name, "%s", name); agg[na++].n = rows[i].n; }
        else others += rows[i].n;
    }
    // sort agg
    for (u32 i = 0; i < na; ++i)
        for (u32 j = i + 1; j < na; ++j)
            if (agg[j].n > agg[i].n) { __typeof__(agg[0]) t = agg[i]; agg[i] = agg[j]; agg[j] = t; }
    fprintf(stderr, "[prof] host-rip samples=%u:", g_host_total);
    for (u32 i = 0; i < na && i < 16; ++i)
        fprintf(stderr, " %.1f%% %s;", 100.0 * agg[i].n / g_host_total, agg[i].name);
    fprintf(stderr, "\n");
    free(rows);
    memset(g_host_rows, 0, sizeof g_host_rows);
    g_host_total = 0;
}

static DWORD WINAPI sampler_main(LPVOID unused) {
    (void)unused;
    HANDLE timer = CreateWaitableTimerExW(NULL, NULL, 0x2 /*CREATE_WAITABLE_TIMER_HIGH_RESOLUTION*/, TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerW(NULL, FALSE, NULL);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG)(10000000ll / g_hz);
    LONG period_ms = (LONG)(1000 / g_hz);
    if (period_ms < 1) period_ms = 1;
    SetWaitableTimer(timer, &due, period_ms, NULL, NULL, FALSE);
    LARGE_INTEGER t0, now;
    QueryPerformanceCounter(&t0);
    char stage0[9] = {0}, stage[9];
    int room0 = -9, room;
    u32 since_check = 0, host_tick = 0;
    while (!g_stop) {
        WaitForSingleObject(timer, 100);
        sample_once();
        if (g_host_mode && (++host_tick % (g_hz / g_host_hz + 1u)) == 0u) hostmode_sample();
        if (++since_check < g_hz / 4 + 1) continue;
        since_check = 0;
        QueryPerformanceCounter(&now);
        const double secs = (double)(now.QuadPart - t0.QuadPart) / (double)g_qpf.QuadPart;
        read_stage(stage, &room);
        const int changed = stage[0] != 0 && (strcmp(stage, stage0) != 0 || room != room0);
        if (changed) {
            if (room0 != -9 && secs >= 2.0)
                report("stage-change", secs);
            else
                reset_counts();
            memcpy(stage0, stage, 9);
            room0 = room;
            t0 = now;
        } else if (secs >= (double)g_secs) {
            report("period", secs);
            t0 = now;
        }
    }
    CancelWaitableTimer(timer);
    CloseHandle(timer);
    return 0;
}

static int env_int(const char* name, int def) {
    const char* v = getenv(name);
    return (v && v[0]) ? atoi(v) : def;
}

void bluewake_prof_start(CPUState* cpu) {
    const char* on = getenv("BLUEWAKE_PROF");
    if (!on || !on[0] || !strcmp(on, "0"))
        return;
    g_cpu = cpu;
    QueryPerformanceFrequency(&g_qpf);
    g_hz = (u32)env_int("BLUEWAKE_PROF_HZ", 500);
    if (g_hz < 10) g_hz = 10;
    if (g_hz > 2000) g_hz = 2000;
    g_secs = (u32)env_int("BLUEWAKE_PROF_SECS", 10);
    g_top = (u32)env_int("BLUEWAKE_PROF_TOP", 12);
    char path[MAX_PATH * 2];
    const char* sym = getenv("BLUEWAKE_PROF_SYMBOLS");
    int loaded = 0;
    if (sym && sym[0]) loaded = load_symbols(sym);
    if (!loaded) {
        GetModuleFileNameA(NULL, path, MAX_PATH);
        char* slash = strrchr(path, '\\');
        if (slash) { strcpy(slash + 1, "guest_symbols.txt"); loaded = load_symbols(path); }
    }
    g_self = (u32*)calloc(g_nsym + 2, sizeof(u32));
    g_incl = (u32*)calloc(g_nsym + 2, sizeof(u32));
    g_stamp = (u32*)calloc(g_nsym + 2, sizeof(u32));
    fprintf(stderr, "[prof] on: %u Hz, summary every %u s, %u symbols%s\n", g_hz, g_secs, g_nsym,
            loaded ? "" : " (no guest_symbols.txt: functions unnamed)");
    if (env_int("BLUEWAKE_PROF_HOST", 0)) {
        if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_game_thread, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
            g_host_mode = 1;
            g_host_hz = (u32)env_int("BLUEWAKE_PROF_HOST_HZ", 200);
            if (g_host_hz < 10) g_host_hz = 10;
            host_sym_init();
            fprintf(stderr, "[prof] host mode: game thread suspended %u times a second to read its Rip\n", g_host_hz);
        }
    }
    g_bw_prof_on = 1;
    g_thread = CreateThread(NULL, 0, sampler_main, NULL, 0, NULL);
}

void bluewake_prof_stop(void) {
    if (!g_thread) return;
    InterlockedExchange(&g_stop, 1);
    WaitForSingleObject(g_thread, 2000);
    g_thread = NULL;
    g_bw_prof_on = 0;
}

#else
void bluewake_prof_start(CPUState* cpu) { (void)cpu; }
void bluewake_prof_stop(void) {}
unsigned long long bluewake_prof_now(void) { return 0; }
void bluewake_prof_add(int phase, unsigned long long t0) { (void)phase; (void)t0; }
#endif
