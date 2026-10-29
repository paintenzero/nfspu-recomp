/* Force-included on Windows. Declares the few POSIX bits win32hle calls
 * that mingw-w64 does not provide, and points <sys/mman.h> users at our shim. */
#ifndef NFSPU_WIN_POSIX_H
#define NFSPU_WIN_POSIX_H
#ifdef _WIN32
#include <sys/stat.h>
#include <time.h>
#ifndef lstat
#define lstat stat
#endif
char *realpath(const char *path, char *resolved);
#ifndef timegm
static inline time_t timegm(struct tm *t) { return _mkgmtime(t); }
#endif
#ifndef gmtime_r
static inline struct tm *gmtime_r(const time_t *t, struct tm *out) {
    return gmtime_s(out, t) ? NULL : out;
}
#endif
#ifndef localtime_r
static inline struct tm *localtime_r(const time_t *t, struct tm *out) {
    return localtime_s(out, t) ? NULL : out;
}
#endif
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0x40
#endif
#ifndef F_GETFL
#define F_GETFL 3
#define F_SETFL 4
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 04000
#endif
int fcntl(int fd, int cmd, ...);
#endif
#endif
