/* Read-only access to a mod archive (.zip, .rar including RAR5, .7z) for the Windows app.
 *
 * A thin C wrapper over the vendored libarchive (runtime/third_party/libarchive); no libarchive
 * type appears here. An archive is opened once and its list of files read; entries are then found
 * by name and read whole into memory. RAR and 7z are sequential formats, so reading an entry that
 * lies before the one read last re-opens the archive and skips forward; reading in ascending index
 * order streams through the archive once.
 *
 * Indices count regular files only, in the archive's own order; folders, links and device entries
 * are not listed. Names are UTF-8 with forward slashes, with any leading "./" or "/" removed.
 * Not thread safe: use one ModArchive from one thread at a time. */
#ifndef BLUEWAKE_MOD_ARCHIVE_H
#define BLUEWAKE_MOD_ARCHIVE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ModArchive ModArchive;

/* Opens the archive at `path_utf8` (a UTF-8 path; on Windows it is converted to UTF-16 so any
 * path works) and reads its entry list. NULL if the file is not a readable .zip/.rar/.7z, or its
 * headers are encrypted or damaged. Entry data that is encrypted fails later, in
 * mod_archive_read. */
ModArchive* mod_archive_open(const char* path_utf8);

/* The number of files, and the UTF-8 name and uncompressed size of file `i` (NULL / 0 when `i` is
 * out of range). The name stays valid until mod_archive_close. */
unsigned mod_archive_count(const ModArchive* archive);
const char* mod_archive_name(const ModArchive* archive, unsigned i);
unsigned long long mod_archive_size(const ModArchive* archive, unsigned i);

/* The index of the first file whose name equals `name_ci` ignoring ASCII case (and treating
 * '\\' as '/'), or -1. */
int mod_archive_find(const ModArchive* archive, const char* name_ci);

/* Reads file `i` whole: a malloc'd buffer (free() it) with a terminating zero byte past the end
 * that is not counted in *size, or NULL when the entry is missing, encrypted, damaged or does not
 * fit in memory. */
unsigned char* mod_archive_read(ModArchive* archive, unsigned i, unsigned long long* size);

/* Closes the archive; NULL is accepted. */
void mod_archive_close(ModArchive* archive);

#ifdef __cplusplus
}
#endif

#endif
