#ifndef NFSPU_GRAPHICS_OPTIONS_H
#define NFSPU_GRAPHICS_OPTIONS_H
#include <stdint.h>
/* Stored settings are host-side values, never guest pointers. */
typedef struct nfspu_graphics_options {
    uint32_t window_width, window_height;
    uint32_t fullscreen, vsync;
    uint32_t render_scale;       /* percent: 100, 150, 200, 300 */
    uint32_t aspect;             /* 0 preserve, 1 stretch, 2 proportional wide race */
    uint32_t texture_filter;     /* 0 game, 1 nearest, 2 linear */
    uint32_t scaling_filter;     /* 0 nearest, 1 linear */
    uint32_t show_stats;
    uint32_t wide_camera;        /* original race camera option */
} nfspu_graphics_options;
#endif
