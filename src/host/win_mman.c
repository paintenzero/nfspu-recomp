/* Windows stand-in for the mmap/realpath calls win32hle makes.
 * The guest's 4 GB is one VirtualAlloc reservation. MAP_FIXED commits or
 * decommits a subrange. A file-backed view is read into that range (private);
 * shared write-back is not required for this title's first boot. */
#define WIN32_LEAN_AND_MEAN
#include <stdint.h>
#include <windows.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include "wincompat/sys/mman.h"

int getpagesize(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwPageSize;
}

static DWORD protect_of(int prot) {
    if (!(prot & (PROT_READ | PROT_WRITE))) return PAGE_NOACCESS;
    if (prot & PROT_WRITE) return PAGE_READWRITE;
    return PAGE_READONLY;
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    if (!length) { errno = EINVAL; return MAP_FAILED; }
    DWORD protect = protect_of(prot);

    if (prot == PROT_NONE && !(flags & MAP_FIXED)) {
        void *p = VirtualAlloc(addr, length, MEM_RESERVE, PAGE_NOACCESS);
        if (!p && addr && !(flags & MAP_FIXED_NOREPLACE))
            p = VirtualAlloc(NULL, length, MEM_RESERVE, PAGE_NOACCESS);
        if (!p) { errno = ENOMEM; return MAP_FAILED; }
        return p;
    }

    if (flags & MAP_FIXED) {
        if (prot == PROT_NONE) {
            /* Drop committed pages. The reservation (if any) stays. */
            VirtualFree(addr, length, MEM_DECOMMIT);
            return addr;
        }
        void *p = VirtualAlloc(addr, length, MEM_COMMIT, protect);
        if (!p) { errno = ENOMEM; return MAP_FAILED; }
        if (fd >= 0) {
            HANDLE h = (HANDLE)_get_osfhandle(fd);
            if (h == INVALID_HANDLE_VALUE) { errno = EBADF; return MAP_FAILED; }
            LARGE_INTEGER dist;
            dist.QuadPart = (LONGLONG)offset;
            if (!SetFilePointerEx(h, dist, NULL, FILE_BEGIN)) { errno = EINVAL; return MAP_FAILED; }
            uint8_t *dst = (uint8_t *)p;
            size_t left = length;
            while (left) {
                DWORD chunk = left > 1 << 20 ? 1 << 20 : (DWORD)left;
                DWORD got = 0;
                if (!ReadFile(h, dst, chunk, &got, NULL)) { errno = EIO; return MAP_FAILED; }
                if (!got) break;
                dst += got;
                left -= got;
            }
        }
        return p;
    }

    void *p = VirtualAlloc(addr, length, MEM_RESERVE | MEM_COMMIT, protect);
    if (!p) p = VirtualAlloc(NULL, length, MEM_RESERVE | MEM_COMMIT, protect);
    if (!p) { errno = ENOMEM; return MAP_FAILED; }
    return p;
}

int munmap(void *addr, size_t length) {
    (void)length;
    if (!addr || addr == MAP_FAILED) return -1;
    /* Subrange release is MEM_DECOMMIT; MEM_RELEASE only works on the base. */
    if (!VirtualFree(addr, length, MEM_DECOMMIT))
        VirtualFree(addr, 0, MEM_RELEASE);
    return 0;
}

int madvise(void *addr, size_t length, int advice) {
    (void)advice;
    VirtualFree(addr, length, MEM_DECOMMIT);
    if (!VirtualAlloc(addr, length, MEM_COMMIT, PAGE_READWRITE)) return -1;
    return 0;
}

char *realpath(const char *path, char *resolved) {
    char tmp[1024];
    if (!path || !_fullpath(tmp, path, sizeof tmp)) return NULL;
    for (char *p = tmp; *p; p++) if (*p == '\\') *p = '/';
    size_t n = strlen(tmp);
    if (n > 3 && tmp[n - 1] == '/') tmp[--n] = 0;
    if (tmp[1] == ':') tmp[0] = (char)toupper((unsigned char)tmp[0]);
    if (!resolved) {
        resolved = (char *)malloc(n + 1);
        if (!resolved) return NULL;
    }
    memcpy(resolved, tmp, n + 1);
    return resolved;
}
