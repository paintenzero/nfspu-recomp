#ifndef NFSPU_HUD_LAYOUT_H
#define NFSPU_HUD_LAYOUT_H

/* After 0041FB40 computes native HUD positions, place overlapping digital
 * speed/RPM readouts beside the analog speedometer. Guest machine lock held. */
void nfspu_hud_layout_apply(void);

#endif
