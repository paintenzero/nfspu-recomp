/* mmap subset used by win32hle's guest memory and file views. */
#ifndef NFSPU_SYS_MMAN_H
#define NFSPU_SYS_MMAN_H
#include <stddef.h>
#include <sys/types.h>

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_NORESERVE 0x4000
#define MAP_FIXED_NOREPLACE 0x100000

#define MAP_FAILED ((void *)-1)

#define MADV_DONTNEED 4

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t length);
int madvise(void *addr, size_t length, int advice);
int getpagesize(void);
#endif
