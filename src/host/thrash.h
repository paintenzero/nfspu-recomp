#ifndef NFSPU_THRASH_H
#define NFSPU_THRASH_H
#include <stdint.h>
void nfspu_thrash_register(const char *host_exe);
void nfspu_thrash_load_graphics(void);
void nfspu_thrash_pump(void);
int nfspu_thrash_input(uint8_t keys[512], int *dx, int *dy, uint32_t *buttons);
void nfspu_thrash_shutdown(void);
#endif
