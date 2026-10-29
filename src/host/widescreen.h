#ifndef NFSPU_WIDESCREEN_H
#define NFSPU_WIDESCREEN_H

#include <stdint.h>

/* Called while the guest machine lock is held. All addresses are guest VAs.
 * Requests apply on the next race initialization; a running race retains its
 * projection. Disabling restores desired dimensions still owned by the hook. */
void nfspu_widescreen_configure(int enabled, uint32_t target_width,
                              uint32_t target_height);
/* Run once after the original 005391B0 perspective constructor returns. */
void nfspu_widescreen_projection(uint32_t camera);
/* Run before 00467FC0; entry_esp addresses [return, width, height, depth, buffers]. */
void nfspu_widescreen_mode(uint32_t caller, uint32_t entry_esp);

#endif
