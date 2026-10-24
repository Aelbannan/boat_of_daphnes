// The Linux release is built against glibc 2.31 (Steam Runtime 3, tools/build-linux.sh) so it runs on
// older systems, but Aurora's prebuilt Dawn library was compiled against a newer glibc and refers to
// a few names 2.31 does not have. They are defined here, in the release build only
// (SA2_GLIBC_COMPAT): a newer system's own come first everywhere else.
#if defined(SA2_GLIBC_COMPAT) && defined(__linux__)

#include <cstdlib>

extern "C" {
// glibc 2.38's C23 conversions: the same as the old ones but for a "0b" prefix (base 0 or 2), which
// Dawn's uses (numbers in shader source and in command-line style options) never have.
long __isoc23_strtol(const char* s, char** end, int base) { return std::strtol(s, end, base); }
long long __isoc23_strtoll(const char* s, char** end, int base) { return std::strtoll(s, end, base); }
unsigned long __isoc23_strtoul(const char* s, char** end, int base) { return std::strtoul(s, end, base); }
unsigned long long __isoc23_strtoull(const char* s, char** end, int base) { return std::strtoull(s, end, base); }

// glibc 2.32: nonzero while the process has one thread, letting the C++ library skip atomic
// operations. 0 is always correct (it only costs the atomics), and the game has many threads anyway.
char __libc_single_threaded = 0;
}

#endif
