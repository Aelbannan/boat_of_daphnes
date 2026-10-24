/* libarchive's build configuration for BlueWake on Windows (x86-64, clang with the MSVC ABI).
 *
 * Adapted from the config.h libarchive 3.8.9's CMake generates for that target (clang 20.1.8, the
 * Windows SDK), with its comments and unset macros removed. Differences from a stock build:
 *   - read support only is compiled (see CMakeLists.txt); no tar/cpio/cat/unzip tools
 *   - zlib (deflate in .zip and .7z) and liblzma (LZMA/LZMA2 in .7z, .zip and .xz) are linked; bzip2,
 *     zstd, lz4, OpenSSL, libxml2, expat and iconv are not (names are converted with the Win32 API)
 *   - encrypted-entry crypto, MD5/SHA digests and HMAC use Windows' CNG (bcrypt)
 *   - no ACL or extended attribute code
 * PPMd, BCJ and Delta (7z) and the RAR and RAR5 decoders are libarchive's own code. */
#define __LIBARCHIVE_CONFIG_H_INCLUDED 1

/* Linked libraries (BlueWake's vendored zlib-ng in zlib-compatible mode, and liblzma). */
#define HAVE_ZLIB_H 1
#define HAVE_LIBZ 1
#define HAVE_LZMA_H 1
#define HAVE_LIBLZMA 1
#define HAVE_STDCKDINT_H 1
#define HAVE_INT16_T
#define HAVE_INT32_T
#define HAVE_INT64_T
#define HAVE_INTMAX_T
#define HAVE_UINT8_T
#define HAVE_UINT16_T
#define HAVE_UINT32_T
#define HAVE_UINT64_T
#define HAVE_UINTMAX_T
#define HAVE___INT64
#define HAVE_UNSIGNED___INT64
#define SIZEOF_SHORT 2
#define SIZEOF_INT 4
#define SIZEOF_LONG 4
#define SIZEOF_LONG_LONG 8
#define SIZEOF_UNSIGNED_SHORT 2
#define SIZEOF_UNSIGNED 4
#define SIZEOF_UNSIGNED_LONG 4
#define SIZEOF_UNSIGNED_LONG_LONG 8
#if !defined(HAVE_INT64_T) && defined(HAVE___INT64)
typedef __int64 int64_t;
#define HAVE_INT64_T
#endif
#if !defined(HAVE_INT64_T) && SIZEOF_INT == 8
typedef int int64_t;
#define HAVE_INT64_T
#endif
#if !defined(HAVE_INT64_T) && SIZEOF_LONG == 8
typedef long int64_t;
#define HAVE_INT64_T
#endif
#if !defined(HAVE_INT64_T) && SIZEOF_LONG_LONG == 8
typedef long long int64_t;
#define HAVE_INT64_T
#endif
#if !defined(HAVE_INT64_T)
#error No 64-bit integer type was found.
#endif
#if !defined(HAVE_INT32_T) && SIZEOF_INT == 4
typedef int int32_t;
#define HAVE_INT32_T
#endif
#if !defined(HAVE_INT32_T) && SIZEOF_LONG == 4
typedef long int32_t;
#define HAVE_INT32_T
#endif
#if !defined(HAVE_INT32_T)
#error No 32-bit integer type was found.
#endif
#if !defined(HAVE_INT16_T) && SIZEOF_INT == 2
typedef int int16_t;
#define HAVE_INT16_T
#endif
#if !defined(HAVE_INT16_T) && SIZEOF_SHORT == 2
typedef short int16_t;
#define HAVE_INT16_T
#endif
#if !defined(HAVE_INT16_T)
#error No 16-bit integer type was found.
#endif
#if !defined(HAVE_UINT64_T) && defined(HAVE_UNSIGNED___INT64)
typedef unsigned __int64 uint64_t;
#define HAVE_UINT64_T
#endif
#if !defined(HAVE_UINT64_T) && SIZEOF_UNSIGNED == 8
typedef unsigned uint64_t;
#define HAVE_UINT64_T
#endif
#if !defined(HAVE_UINT64_T) && SIZEOF_UNSIGNED_LONG == 8
typedef unsigned long uint64_t;
#define HAVE_UINT64_T
#endif
#if !defined(HAVE_UINT64_T) && SIZEOF_UNSIGNED_LONG_LONG == 8
typedef unsigned long long uint64_t;
#define HAVE_UINT64_T
#endif
#if !defined(HAVE_UINT64_T)
#error No 64-bit unsigned integer type was found.
#endif
#if !defined(HAVE_UINT32_T) && SIZEOF_UNSIGNED == 4
typedef unsigned uint32_t;
#define HAVE_UINT32_T
#endif
#if !defined(HAVE_UINT32_T) && SIZEOF_UNSIGNED_LONG == 4
typedef unsigned long uint32_t;
#define HAVE_UINT32_T
#endif
#if !defined(HAVE_UINT32_T)
#error No 32-bit unsigned integer type was found.
#endif
#if !defined(HAVE_UINT16_T) && SIZEOF_UNSIGNED == 2
typedef unsigned uint16_t;
#define HAVE_UINT16_T
#endif
#if !defined(HAVE_UINT16_T) && SIZEOF_UNSIGNED_SHORT == 2
typedef unsigned short uint16_t;
#define HAVE_UINT16_T
#endif
#if !defined(HAVE_UINT16_T)
#error No 16-bit unsigned integer type was found.
#endif
#if !defined(HAVE_UINT8_T)
typedef unsigned char uint8_t;
#define HAVE_UINT8_T
#endif
#if !defined(HAVE_UINT8_T)
#error No 8-bit unsigned integer type was found.
#endif
#if !defined(HAVE_INTMAX_T)
typedef int64_t intmax_t;
#endif
#if !defined(HAVE_UINTMAX_T)
typedef uint64_t uintmax_t;
#endif
#define ARCHIVE_CRYPTO_MD5_WIN 1
#define ARCHIVE_CRYPTO_SHA1_WIN 1
#define ARCHIVE_CRYPTO_SHA256_WIN 1
#define ARCHIVE_CRYPTO_SHA384_WIN 1
#define ARCHIVE_CRYPTO_SHA512_WIN 1
#define HAVE_BCRYPT_H 1
#define HAVE_CTYPE_H 1
#define HAVE_DECL_INT32_MAX 1
#define HAVE_DECL_INT32_MIN 1
#define HAVE_DECL_INT64_MAX 1
#define HAVE_DECL_INT64_MIN 1
#define HAVE_DECL_INTMAX_MAX 1
#define HAVE_DECL_INTMAX_MIN 1
#define HAVE_DECL_SIZE_MAX 1
#define HAVE_DECL_UINT32_MAX 1
#define HAVE_DECL_UINT64_MAX 1
#define HAVE_DECL_UINTMAX_MAX 1
#define HAVE_DIRECT_H 1
#define HAVE_EILSEQ 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_FSTAT 1
#define HAVE_GETPID 1
#define HAVE_INTSAFE_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_IO_H 1
#define HAVE_LIMITS_H 1
#define HAVE_LOCALE_H 1
#define HAVE_MBRTOWC 1
#define HAVE_MEMMOVE 1
#define HAVE_MEMORY_H 1
#define HAVE_MKDIR 1
#define HAVE_PROCESS_H 1
#define HAVE_SETLOCALE 1
#define HAVE_SIGNAL_H 1
#define HAVE_STDARG_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRCHR 1
#define HAVE_STRNLEN 1
#define HAVE_STRDUP 1
#define HAVE_STRERROR 1
#define HAVE_STRFTIME 1
#define HAVE_STRING_H 1
#define HAVE_STRRCHR 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_UTIME_H 1
#define HAVE_TIME_H 1
#define HAVE_TZSET 1
#define HAVE_UTIME 1
#define HAVE_WCHAR_H 1
#define HAVE_WCHAR_T 1
#define HAVE_WCRTOMB 1
#define HAVE_WCSCMP 1
#define HAVE_WCSCPY 1
#define HAVE_WCSLEN 1
#define HAVE_WCTOMB 1
#define HAVE_WCTYPE_H 1
#define HAVE_WINDOWS_H 1
#define HAVE__CrtSetReportMode 1
#define HAVE_CTIME_S 1
#define HAVE__FSEEKI64 1
#define HAVE__GET_TIMEZONE 1
#define HAVE_GMTIME_S 1
#define HAVE_LOCALTIME_S 1
#define HAVE__MKGMTIME 1
#define ICONV_CONST
#define LIBARCHIVE_VERSION_NUMBER "3008009"
#define LIBARCHIVE_VERSION_STRING "3.8.9"
#define SIZEOF_WCHAR_T 2
#define SAFE_TO_DEFINE_EXTENSIONS 1
#ifdef SAFE_TO_DEFINE_EXTENSIONS
#ifndef _ALL_SOURCE
# define _ALL_SOURCE 1
#endif
#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif
#ifndef _POSIX_PTHREAD_SEMANTICS
# define _POSIX_PTHREAD_SEMANTICS 1
#endif
#ifndef _TANDEM_SOURCE
# define _TANDEM_SOURCE 1
#endif
#ifndef __EXTENSIONS__
# define __EXTENSIONS__ 1
#endif
#endif
#define VERSION "3.8.9"
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000000
#endif // NTDDI_VERSION
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif // _WIN32_WINNT
#ifndef WINVER
#define WINVER 0x0A00
#endif // WINVER
#define gid_t short
#define id_t short
#define mode_t unsigned short
#define pid_t int
#define ssize_t int64_t
#define uid_t short
