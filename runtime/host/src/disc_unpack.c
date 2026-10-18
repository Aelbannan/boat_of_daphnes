// See disc_unpack.h.
#include "disc_unpack.h"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bzlib.h>
#include <lzma.h>
#include <zlib.h>
#include <zstd.h>

#if defined(_WIN32)
#define BW_SEEK(f, off) _fseeki64((f), (__int64)(off), SEEK_SET)
#else
#include <sys/types.h>
#define BW_SEEK(f, off) fseeko((f), (off_t)(off), SEEK_SET)
#endif

#define BW_GC_MAGIC 0xC2339F3Du
// Every GameCube disc image, unpacked, is this long.
#define BW_GC_DISC_SIZE 1459978240ull
#define BW_IO_STEP (1u << 20)

static int fail(char* error, size_t size, const char* fmt, ...) {
    if (error != NULL && size > 0) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(error, size, fmt, ap);
        va_end(ap);
    }
    return -1;
}

// What to tell the player when an image is one we do not unpack.
static int unsupported(char* error, size_t size, const char* what) {
    fail(error, size,
         "%s boat-of-daphnes cannot unpack that itself. Convert the image to ISO in Dolphin (right-click the game in "
         "Dolphin's list, Convert File, Format: ISO) and choose the .iso instead.",
         what);
    return 1;
}

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static uint64_t be64(const uint8_t* p) { return ((uint64_t)be32(p) << 32) | be32(p + 4); }
static uint32_t le32(const uint8_t* p) {
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[0];
}
static uint64_t le64(const uint8_t* p) { return ((uint64_t)le32(p + 4) << 32) | le32(p); }

static int read_at(FILE* f, uint64_t offset, void* out, size_t size) {
    if (size == 0)
        return 1;
    return BW_SEEK(f, offset) == 0 && fread(out, 1, size, f) == size;
}

BluewakeDiscContainer bluewake_disc_container(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f == NULL)
        return BW_DISC_PLAIN;
    uint8_t h[8] = {0};
    const size_t got = fread(h, 1, sizeof h, f);
    fclose(f);
    if (got < 4)
        return BW_DISC_PLAIN;
    if (memcmp(h, "WIA\x01", 4) == 0)
        return BW_DISC_WIA;
    if (memcmp(h, "RVZ\x01", 4) == 0)
        return BW_DISC_RVZ;
    if (memcmp(h, "CISO", 4) == 0)
        return BW_DISC_CISO;
    if (memcmp(h, "WBFS", 4) == 0)
        return BW_DISC_WBFS;
    if (le32(h) == 0xB10BC001u)
        return BW_DISC_GCZ;
    return BW_DISC_PLAIN;
}

// --- the output ------------------------------------------------------------------------------------------------------

typedef struct {
    FILE* f;
    uint64_t written, total;
    uint64_t reported;
    BlueWakeImportProgress progress;
    void* context;
} Sink;

static int sink_write(Sink* s, const uint8_t* data, size_t size) {
    if (fwrite(data, 1, size, s->f) != size)
        return 0;
    s->written += size;
    if (s->progress != NULL && s->total > 0 && s->written - s->reported >= (8u << 20)) {
        s->reported = s->written;
        s->progress(s->context, (double)s->written / (double)s->total, "Unpacking your disc image");
    }
    return 1;
}

static int sink_zeros(Sink* s, uint64_t size) {
    static const uint8_t zeros[BW_IO_STEP];
    while (size > 0) {
        const size_t n = size < sizeof zeros ? (size_t)size : sizeof zeros;
        if (!sink_write(s, zeros, n))
            return 0;
        size -= n;
    }
    return 1;
}

// --- junk data: the lagged Fibonacci generator (j = 32, k = 521) the discs' padding is made with --------------------------

#define LFG_K 521u
#define LFG_J 32u
#define LFG_SEED 17u

typedef struct {
    uint32_t buf[LFG_K];
    size_t pos;  // in bytes
} Lfg;

static void lfg_step(Lfg* l) {
    for (uint32_t i = 0; i < LFG_J; ++i)
        l->buf[i] ^= l->buf[i + LFG_K - LFG_J];
    for (uint32_t i = LFG_J; i < LFG_K; ++i)
        l->buf[i] ^= l->buf[i - LFG_J];
}

// 68 bytes of seed, big endian words. The output takes bits 24-31, 18-25, 8-15 and 0-7 of each word (18, not 16): the
// words are kept already arranged that way (the steps are XORs, which the arrangement commutes with).
static void lfg_seed(Lfg* l, const uint8_t* seed) {
    for (uint32_t i = 0; i < LFG_SEED; ++i)
        l->buf[i] = be32(seed + 4u * i);
    for (uint32_t i = LFG_SEED; i < LFG_K; ++i)
        l->buf[i] = (l->buf[i - 17] << 23) ^ (l->buf[i - 16] >> 9) ^ l->buf[i - 1];
    for (uint32_t i = 0; i < LFG_K; ++i)
        l->buf[i] = (l->buf[i] & 0xFF00FFFFu) | ((l->buf[i] >> 2) & 0x00FF0000u);
    for (int i = 0; i < 4; ++i)
        lfg_step(l);
    l->pos = 0;
}

static void lfg_forward(Lfg* l, size_t count) {
    l->pos += count;
    while (l->pos >= LFG_K * 4u) {
        lfg_step(l);
        l->pos -= LFG_K * 4u;
    }
}

static void lfg_bytes(Lfg* l, size_t count, uint8_t* out) {
    while (count > 0) {
        const uint32_t word = l->buf[l->pos >> 2];
        *out++ = (uint8_t)(word >> (24 - 8 * (l->pos & 3u)));
        --count;
        if (++l->pos == LFG_K * 4u) {
            lfg_step(l);
            l->pos = 0;
        }
    }
}

// RVZ's packing: records of a big endian size (bit 31: junk, with a 68 byte seed; else that many bytes follow).
static int rvz_unpack(const uint8_t* in, size_t in_size, uint8_t* out, size_t out_size, uint64_t data_offset) {
    static Lfg lfg;  // 2 KB; the unpacking runs on one thread
    size_t ip = 0, op = 0;
    while (op < out_size) {
        if (ip + 4 > in_size)
            return 0;
        uint32_t size = be32(in + ip);
        ip += 4;
        const int junk = (size & 0x80000000u) != 0;
        size &= 0x7FFFFFFFu;
        if (size > out_size - op)
            return 0;
        if (junk) {
            if (ip + LFG_SEED * 4u > in_size)
                return 0;
            lfg_seed(&lfg, in + ip);
            ip += LFG_SEED * 4u;
            lfg_forward(&lfg, (size_t)(data_offset % 0x8000u));
            lfg_bytes(&lfg, size, out + op);
        } else {
            if (size > in_size - ip)
                return 0;
            memcpy(out + op, in + ip, size);
            ip += size;
        }
        op += size;
        data_offset += size;
    }
    return 1;
}

// --- WIA and RVZ ---------------------------------------------------------------------------------------------------------

enum { WIA_NONE = 0, WIA_PURGE = 1, WIA_BZIP2 = 2, WIA_LZMA = 3, WIA_LZMA2 = 4, WIA_ZSTD = 5 };

typedef struct {
    uint64_t offset, size;
    uint32_t group_index, n_groups;
} RawEntry;

typedef struct {
    FILE* f;
    uint64_t file_size, iso_size;
    int rvz;
    uint32_t compression, chunk_size;
    uint8_t dhead[0x80];
    uint8_t cdata[7];
    uint8_t cdata_size;
    RawEntry* raw;
    uint32_t n_raw;
    uint8_t* groups;  // as stored (big endian): 8 bytes each (WIA) or 12 (RVZ)
    uint32_t n_groups;
    // the group last decoded
    uint8_t* chunk;
    uint64_t chunk_group;  // total group index + 1; 0: none
    size_t chunk_len;
    uint8_t* packed;
    size_t packed_cap;
    uint8_t* stored;
    size_t stored_cap;
} Wia;

static int ensure(uint8_t** buf, size_t* cap, size_t need) {
    if (*cap >= need)
        return 1;
    uint8_t* grown = (uint8_t*)realloc(*buf, need);
    if (grown == NULL)
        return 0;
    *buf = grown;
    *cap = need;
    return 1;
}

// PURGE: segments (offset, size, bytes) in ascending order over zeros, then the SHA-1 of the stream before it.
static int purge_decode(const uint8_t* in, size_t in_size, uint8_t* out, size_t out_size) {
    if (in_size < 20)
        return 0;
    const size_t end = in_size - 20;
    size_t ip = 0;
    memset(out, 0, out_size);
    while (ip < end) {
        if (ip + 8 > end)
            return 0;
        const uint32_t offset = be32(in + ip), size = be32(in + ip + 4);
        ip += 8;
        if (size > end - ip || (uint64_t)offset + size > out_size)
            return 0;
        memcpy(out + offset, in + ip, size);
        ip += size;
    }
    return 1;
}

static int lzma_decode(const Wia* w, int lzma2, const uint8_t* in, size_t in_size, uint8_t* out, size_t out_size) {
    lzma_options_lzma options;
    memset(&options, 0, sizeof options);
    if (!lzma2 && w->cdata_size == 5) {
        const uint8_t d = w->cdata[0];
        if (d >= 9 * 5 * 5)
            return 0;
        options.dict_size = le32(w->cdata + 1);
        options.lc = d % 9u;
        options.pb = (d / 9u) / 5u;
        options.lp = (d / 9u) % 5u;
    } else if (lzma2 && w->cdata_size == 1) {
        const uint8_t p = w->cdata[0];
        if (p > 40)
            return 0;
        options.dict_size = p == 40 ? 0xFFFFFFFFu : ((uint32_t)2 | (p & 1u)) << (p / 2 + 11);
    } else {
        return 0;
    }
    lzma_filter filters[2] = {{lzma2 ? LZMA_FILTER_LZMA2 : LZMA_FILTER_LZMA1, &options}, {LZMA_VLI_UNKNOWN, NULL}};
    lzma_stream stream = LZMA_STREAM_INIT;
    if (lzma_raw_decoder(&stream, filters) != LZMA_OK)
        return 0;
    stream.next_in = in;
    stream.avail_in = in_size;
    stream.next_out = out;
    stream.avail_out = out_size;
    lzma_ret r;
    do {
        r = lzma_code(&stream, LZMA_FINISH);
    } while (r == LZMA_OK && stream.avail_out > 0);
    const size_t produced = out_size - stream.avail_out;
    lzma_end(&stream);
    // LZMA with a known size has no end marker: the output being full is the end.
    return produced == out_size && (r == LZMA_OK || r == LZMA_STREAM_END || r == LZMA_BUF_ERROR);
}

// Decompresses `in` (compressed with `type`) to exactly out_size bytes.
static int decompress(const Wia* w, uint32_t type, const uint8_t* in, size_t in_size, uint8_t* out, size_t out_size) {
    switch (type) {
    case WIA_NONE:
        if (in_size < out_size)
            return 0;
        memcpy(out, in, out_size);
        return 1;
    case WIA_PURGE:
        return purge_decode(in, in_size, out, out_size);
    case WIA_BZIP2: {
        unsigned int length = (unsigned int)out_size;
        return BZ2_bzBuffToBuffDecompress((char*)out, &length, (char*)in, (unsigned int)in_size, 0, 0) == BZ_OK &&
               length == out_size;
    }
    case WIA_LZMA:
    case WIA_LZMA2:
        return lzma_decode(w, type == WIA_LZMA2, in, in_size, out, out_size);
    case WIA_ZSTD: {
        const size_t n = ZSTD_decompress(out, out_size, in, in_size);
        return !ZSTD_isError(n) && n == out_size;
    }
    default:
        return 0;
    }
}

// A table (raw data entries, groups) stored compressed with the file's method.
static uint8_t* wia_table(Wia* w, uint64_t offset, uint32_t stored_size, size_t length) {
    uint8_t* out = (uint8_t*)malloc(length > 0 ? length : 1);
    if (out == NULL)
        return NULL;
    if (stored_size == 0 || offset + stored_size > w->file_size || !ensure(&w->stored, &w->stored_cap, stored_size) ||
        !read_at(w->f, offset, w->stored, stored_size) ||
        !decompress(w, w->compression, w->stored, stored_size, out, length)) {
        free(out);
        return NULL;
    }
    return out;
}

static void wia_close(Wia* w) {
    if (w->f != NULL)
        fclose(w->f);
    free(w->raw);
    free(w->groups);
    free(w->chunk);
    free(w->packed);
    free(w->stored);
}

// Opens and reads the headers. 0 ok, 1 not handled (message set), -1 failure.
static int wia_open(Wia* w, const char* path, char* error, size_t error_size) {
    memset(w, 0, sizeof *w);
    w->f = fopen(path, "rb");
    if (w->f == NULL)
        return fail(error, error_size, "The disc image could not be opened.");
    uint8_t h1[0x48], h2[0xDC];
    if (BW_SEEK(w->f, 0) != 0 || fseek(w->f, 0, SEEK_END) != 0)
        return fail(error, error_size, "The disc image could not be read.");
    w->file_size = (uint64_t)
#if defined(_WIN32)
        _ftelli64(w->f);
#else
        ftello(w->f);
#endif
    if (!read_at(w->f, 0, h1, sizeof h1))
        return fail(error, error_size, "The disc image is too small to be a WIA or RVZ file.");
    w->rvz = memcmp(h1, "RVZ\x01", 4) == 0;
    const uint32_t h2_size = be32(h1 + 12);
    w->iso_size = be64(h1 + 0x24);
    if (h2_size < 0xD4 || h2_size > sizeof h2)
        return fail(error, error_size, "The disc image's header is damaged.");
    memset(h2, 0, sizeof h2);
    if (!read_at(w->f, sizeof h1, h2, h2_size))
        return fail(error, error_size, "The disc image's header is damaged.");
    const uint32_t disc_type = be32(h2), compression = be32(h2 + 4);
    w->compression = compression;
    w->chunk_size = be32(h2 + 12);
    memcpy(w->dhead, h2 + 16, sizeof w->dhead);
    // Layout of header 2 (big endian): +0 disc type, +4 compression, +8 level, +12 chunk size, +16 disc header (0x80),
    // +0x90 partition count, +0x94 partition entry size, +0x98 partitions offset, +0xA0 their SHA-1, +0xB4 raw data count,
    // +0xB8 raw offset, +0xC0 raw stored size, +0xC4 group count, +0xC8 group offset, +0xD0 group stored size, +0xD4 length
    // of the compressor data, +0xD5 its 7 bytes.
    const uint32_t partitions = be32(h2 + 0x90);
    w->n_raw = be32(h2 + 0xB4);
    const uint64_t raw_off = be64(h2 + 0xB8);
    const uint32_t raw_stored = be32(h2 + 0xC0);
    w->n_groups = be32(h2 + 0xC4);
    const uint64_t group_off = be64(h2 + 0xC8);
    const uint32_t group_stored = be32(h2 + 0xD0);
    w->cdata_size = h2[0xD4];
    memcpy(w->cdata, h2 + 0xD5, 7);

    if (disc_type == 2 || partitions != 0 || be32(w->dhead + 0x18) == 0x5D1C9EA3u)
        return unsupported(error, error_size, "This is a Wii disc image (its partitions are encrypted).");
    if (be32(w->dhead + 0x1C) != BW_GC_MAGIC)
        return fail(error, error_size, "This file is not a GameCube disc image. boat-of-daphnes needs a dump of your disc.");
    if (compression > WIA_ZSTD || (compression == WIA_ZSTD && !w->rvz) || (compression == WIA_PURGE && w->rvz))
        return unsupported(error, error_size, "This image uses a compression this version does not know.");
    if (w->chunk_size < 0x8000u || w->chunk_size > (256u << 20) || w->n_raw == 0 || w->n_raw > 4096 || w->n_groups == 0 ||
        w->n_groups > (1u << 24) || w->iso_size == 0 || w->iso_size > (2ull << 30))
        return fail(error, error_size, "The disc image's header is damaged.");
    uint8_t* raw = wia_table(w, raw_off, raw_stored, (size_t)w->n_raw * 0x18u);
    const size_t group_len = w->rvz ? 12u : 8u;
    w->groups = wia_table(w, group_off, group_stored, (size_t)w->n_groups * group_len);
    if (raw == NULL || w->groups == NULL) {
        free(raw);
        return fail(error, error_size, "The disc image's tables could not be read; the file is damaged or cut short.");
    }
    w->raw = (RawEntry*)calloc(w->n_raw, sizeof(RawEntry));
    w->chunk = (uint8_t*)malloc(w->chunk_size);
    if (w->raw == NULL || w->chunk == NULL) {
        free(raw);
        return fail(error, error_size, "Out of memory.");
    }
    for (uint32_t i = 0; i < w->n_raw; ++i) {
        w->raw[i].offset = be64(raw + 0x18u * i);
        w->raw[i].size = be64(raw + 0x18u * i + 8);
        w->raw[i].group_index = be32(raw + 0x18u * i + 16);
        w->raw[i].n_groups = be32(raw + 0x18u * i + 20);
    }
    free(raw);
    return 0;
}

// The decoded bytes of group `index` (chunk_len of them) into w->chunk. `data_offset` is the group's offset in its data (the
// junk generator needs it).
static int wia_group(Wia* w, uint64_t index, size_t chunk_len, uint64_t data_offset) {
    if (w->chunk_group == index + 1 && w->chunk_len == chunk_len)
        return 1;
    w->chunk_group = 0;
    if (index >= w->n_groups)
        return 0;
    const uint8_t* g = w->groups + index * (w->rvz ? 12u : 8u);
    const uint64_t offset = (uint64_t)be32(g) << 2;
    uint32_t stored = be32(g + 4);
    uint32_t type = w->compression;
    uint32_t packed_size = 0;
    if (w->rvz) {
        if ((stored & 0x80000000u) == 0)
            type = WIA_NONE;
        stored &= 0x7FFFFFFFu;
        packed_size = be32(g + 8);
    }
    uint8_t* chunk = w->chunk;
    if (stored == 0) {
        memset(chunk, 0, chunk_len);
    } else {
        if (offset + stored > w->file_size || !ensure(&w->stored, &w->stored_cap, stored) ||
            !read_at(w->f, offset, w->stored, stored))
            return 0;
        if (packed_size != 0) {
            if (!ensure(&w->packed, &w->packed_cap, packed_size) ||
                !decompress(w, type, w->stored, stored, w->packed, packed_size) ||
                !rvz_unpack(w->packed, packed_size, chunk, chunk_len, data_offset))
                return 0;
        } else if (!decompress(w, type, w->stored, stored, chunk, chunk_len)) {
            return 0;
        }
    }
    w->chunk_group = index + 1;
    w->chunk_len = chunk_len;
    return 1;
}

// Disc bytes [offset, offset + size) into out; 0 if the file does not hold them.
static int wia_read(Wia* w, uint64_t offset, uint64_t size, uint8_t* out) {
    if (offset < sizeof w->dhead) {
        const uint64_t n = size < sizeof w->dhead - offset ? size : sizeof w->dhead - offset;
        memcpy(out, w->dhead + offset, (size_t)n);
        offset += n;
        size -= n;
        out += n;
    }
    while (size > 0) {
        const RawEntry* r = NULL;
        for (uint32_t i = 0; i < w->n_raw; ++i)
            if (w->raw[i].size != 0 && offset >= w->raw[i].offset && offset < w->raw[i].offset + w->raw[i].size) {
                r = &w->raw[i];
                break;
            }
        if (r == NULL)
            return 0;
        // The data starts at the 32 KiB boundary before its offset: the groups cover it from there.
        const uint64_t skipped = r->offset % 0x8000u;
        const uint64_t start = r->offset - skipped, length = r->size + skipped;
        for (uint64_t i = (offset - start) / w->chunk_size; i < r->n_groups && size > 0; ++i) {
            const uint64_t in_data = i * w->chunk_size;
            if (in_data >= length)
                return 0;
            const uint64_t group_len = length - in_data < w->chunk_size ? length - in_data : w->chunk_size;
            const uint64_t in_group = offset - start - in_data;
            if (in_group >= group_len)
                return 0;
            const uint64_t n = group_len - in_group < size ? group_len - in_group : size;
            if (!wia_group(w, (uint64_t)r->group_index + i, (size_t)group_len, in_data))
                return 0;
            memcpy(out, w->chunk + in_group, (size_t)n);
            offset += n;
            size -= n;
            out += n;
        }
    }
    return 1;
}

static int unpack_wia(const char* in_path, Sink* sink, char* error, size_t error_size) {
    Wia w;
    int rc = wia_open(&w, in_path, error, error_size);
    if (rc != 0) {
        wia_close(&w);
        return rc;
    }
    sink->total = w.iso_size;
    uint8_t* block = (uint8_t*)malloc(w.chunk_size);
    if (block == NULL) {
        wia_close(&w);
        return fail(error, error_size, "Out of memory.");
    }
    for (uint64_t offset = 0; offset < w.iso_size;) {
        uint64_t n = w.chunk_size - offset % w.chunk_size;
        if (n > w.iso_size - offset)
            n = w.iso_size - offset;
        if (!wia_read(&w, offset, n, block)) {
            rc = fail(error, error_size,
                      "The disc image could not be read at %llu MB; the file is damaged, cut short, or uses a variant "
                      "this version does not know.", (unsigned long long)(offset >> 20));
            break;
        }
        if (!sink_write(sink, block, (size_t)n)) {
            rc = fail(error, error_size, "The ISO could not be written (%s). Is there room on the disk?", strerror(errno));
            break;
        }
        offset += n;
    }
    free(block);
    wia_close(&w);
    return rc;
}

// --- GCZ -------------------------------------------------------------------------------------------------------------------

static int unpack_gcz(const char* in_path, Sink* sink, char* error, size_t error_size) {
    FILE* f = fopen(in_path, "rb");
    if (f == NULL)
        return fail(error, error_size, "The disc image could not be opened.");
    int rc = -1;
    uint8_t h[32];
    uint8_t *pointers = NULL, *stored = NULL, *block = NULL;
    if (!read_at(f, 0, h, sizeof h)) {
        fail(error, error_size, "The disc image is too small to be a GCZ file.");
        goto done;
    }
    const uint64_t compressed_size = le64(h + 8), data_size = le64(h + 16);
    const uint32_t block_size = le32(h + 24), n_blocks = le32(h + 28);
    if (block_size < 0x800 || block_size > (64u << 20) || n_blocks == 0 || n_blocks > (1u << 24) ||
        data_size > (uint64_t)n_blocks * block_size || data_size <= (uint64_t)(n_blocks - 1) * block_size) {
        fail(error, error_size, "The disc image's header is damaged.");
        goto done;
    }
    pointers = (uint8_t*)malloc((size_t)n_blocks * 8u);
    stored = (uint8_t*)malloc(block_size + 64u);
    block = (uint8_t*)malloc(block_size);
    if (pointers == NULL || stored == NULL || block == NULL) {
        fail(error, error_size, "Out of memory.");
        goto done;
    }
    if (!read_at(f, sizeof h, pointers, (size_t)n_blocks * 8u)) {
        fail(error, error_size, "The disc image's block table could not be read.");
        goto done;
    }
    {
        const uint64_t base = sizeof h + (uint64_t)n_blocks * 12u;
        const uint64_t top = 1ull << 63;
        sink->total = data_size;
        for (uint32_t b = 0; b < n_blocks; ++b) {
            const uint64_t raw = le64(pointers + 8u * b);
            const uint64_t start = raw & ~top;
            const uint64_t end = b + 1 < n_blocks ? (le64(pointers + 8u * (b + 1)) & ~top) : compressed_size;
            if (end < start || end - start > block_size + 64u) {
                fail(error, error_size, "The disc image's block table is damaged.");
                goto done;
            }
            const size_t length = (size_t)(end - start);
            if (!read_at(f, base + start, stored, length)) {
                fail(error, error_size, "The disc image is cut short (block %u of %u).", b, n_blocks);
                goto done;
            }
            const size_t want = b + 1 < n_blocks ? block_size : (size_t)(data_size - (uint64_t)b * block_size);
            if (raw & top) {
                if (length < want) {
                    fail(error, error_size, "The disc image is damaged (block %u).", b);
                    goto done;
                }
                memcpy(block, stored, want);
            } else {
                uLongf out_len = block_size;
                if (uncompress(block, &out_len, stored, (uLong)length) != Z_OK || out_len < want) {
                    fail(error, error_size, "The disc image is damaged (block %u does not unpack).", b);
                    goto done;
                }
            }
            if (!sink_write(sink, block, want)) {
                fail(error, error_size, "The ISO could not be written (%s). Is there room on the disk?", strerror(errno));
                goto done;
            }
        }
    }
    rc = 0;
done:
    free(pointers);
    free(stored);
    free(block);
    fclose(f);
    return rc;
}

// --- CISO ------------------------------------------------------------------------------------------------------------------

static int unpack_ciso(const char* in_path, Sink* sink, char* error, size_t error_size) {
    enum { HEADER = 0x8000, MAP = HEADER - 8 };
    FILE* f = fopen(in_path, "rb");
    if (f == NULL)
        return fail(error, error_size, "The disc image could not be opened.");
    int rc = -1;
    uint8_t* header = (uint8_t*)malloc(HEADER);
    uint8_t* block = NULL;
    if (header == NULL || !read_at(f, 0, header, HEADER)) {
        fail(error, error_size, "The disc image is too small to be a CISO file.");
        goto done;
    }
    const uint32_t block_size = le32(header + 4);
    if (block_size < 0x800 || block_size > (64u << 20)) {
        fail(error, error_size, "The disc image's header is damaged.");
        goto done;
    }
    block = (uint8_t*)calloc(block_size, 1);
    if (block == NULL) {
        fail(error, error_size, "Out of memory.");
        goto done;
    }
    {
        // The disc is a GameCube disc (checked from its first block), whose image is always the same length.
        const uint64_t disc_size = BW_GC_DISC_SIZE;
        uint32_t used = 0;
        sink->total = disc_size;
        for (uint64_t offset = 0; offset < disc_size;) {
            const uint64_t index = offset / block_size;
            const uint64_t in_block = offset % block_size;
            uint64_t n = block_size - in_block;
            if (n > disc_size - offset)
                n = disc_size - offset;
            if (index < MAP && header[8 + index] == 1) {
                // The blocks are stored one after another, in the order of the map.
                uint64_t before = 0;
                if (index == 0 || used == 0) {
                    for (uint64_t i = 0; i < index; ++i)
                        before += header[8 + i] == 1;
                    used = (uint32_t)before;
                } else {
                    before = used;
                }
                memset(block, 0, block_size);
                const uint64_t file_offset = HEADER + before * block_size;
                BW_SEEK(f, file_offset);
                const size_t got = fread(block, 1, block_size, f);  // the last block may be short
                if (got == 0 && offset == 0) {
                    fail(error, error_size, "The disc image is cut short.");
                    goto done;
                }
                if (offset == 0 && be32(block + 0x1C) != BW_GC_MAGIC) {
                    rc = unsupported(error, error_size, "This CISO is not a GameCube disc image (a Wii disc is encrypted).");
                    goto done;
                }
                if (!sink_write(sink, block + in_block, (size_t)n)) {
                    fail(error, error_size, "The ISO could not be written (%s). Is there room on the disk?", strerror(errno));
                    goto done;
                }
                if (in_block + n == block_size)
                    used = (uint32_t)before + 1;
            } else {
                if (offset == 0) {
                    fail(error, error_size, "This CISO has no data at the start; it is not a disc image.");
                    goto done;
                }
                if (!sink_zeros(sink, n)) {
                    fail(error, error_size, "The ISO could not be written (%s). Is there room on the disk?", strerror(errno));
                    goto done;
                }
                used = 0;  // recount at the next used block
            }
            offset += n;
        }
    }
    rc = 0;
done:
    free(header);
    free(block);
    fclose(f);
    return rc;
}

// --- the entry --------------------------------------------------------------------------------------------------------------

int bluewake_disc_unpack(const char* in_path, const char* out_path, BlueWakeImportProgress progress, void* context,
                         char* error, size_t error_size) {
    const BluewakeDiscContainer kind = bluewake_disc_container(in_path);
    if (kind == BW_DISC_PLAIN)
        return fail(error, error_size, "This file is not a compressed disc image I know.");
    if (kind == BW_DISC_WBFS)
        return unsupported(error, error_size, "WBFS is a Wii format.");
    Sink sink = {0};
    sink.f = fopen(out_path, "wb");
    sink.progress = progress;
    sink.context = context;
    if (sink.f == NULL)
        return fail(error, error_size, "The ISO could not be created (%s).", strerror(errno));
    int rc;
    switch (kind) {
    case BW_DISC_WIA:
    case BW_DISC_RVZ:
        rc = unpack_wia(in_path, &sink, error, error_size);
        break;
    case BW_DISC_GCZ:
        rc = unpack_gcz(in_path, &sink, error, error_size);
        break;
    default:
        rc = unpack_ciso(in_path, &sink, error, error_size);
        break;
    }
    if (fclose(sink.f) != 0 && rc == 0)
        rc = fail(error, error_size, "The ISO could not be written (%s).", strerror(errno));
    if (rc == 0 && progress != NULL)
        progress(context, 1.0, "Unpacking your disc image");
    if (rc != 0)
        remove(out_path);
    return rc;
}
