/* Stub: kernel32_ext includes this and does not call statvfs. */
#ifndef NFSPU_SYS_STATVFS_H
#define NFSPU_SYS_STATVFS_H
struct statvfs {
    unsigned long f_bsize, f_frsize, f_blocks, f_bfree, f_bavail, f_files, f_ffree, f_favail, f_fsid, f_flag, f_namemax;
};
static inline int statvfs(const char *p, struct statvfs *s) { (void)p; if (s) *s = (struct statvfs){0}; return -1; }
#endif
