#ifndef NFSPU_GRAPHICS_OVERLAY_H
#define NFSPU_GRAPHICS_OVERLAY_H
#include <stdint.h>
#include "nfspu_gpu.h"
#include "graphics_options.h"

typedef struct nfspu_overlay_stats {
    const char *backend;
    uint32_t logical_width, logical_height;
    uint32_t render_width, render_height;
    uint32_t output_width, output_height;
    float fps;
} nfspu_overlay_stats;

enum nfspu_overlay_action {
    NFSPU_OVERLAY_NONE=0,
    NFSPU_OVERLAY_APPLY=1,
    NFSPU_OVERLAY_CANCEL=2
};

typedef int (*nfspu_overlay_emit_fn)(void *context,
    const nfspu_gpu_vertex *vertices, uint32_t vertex_count,
    const uint32_t *indices, uint32_t index_count);

typedef struct nfspu_graphics_overlay {
    int is_open, selected, hover;
    nfspu_graphics_options applied, draft;
    char status[160];
    float x,y,scale;
    nfspu_gpu_vertex *vertices;
    uint32_t *indices;
    uint32_t vertex_count,index_count;
} nfspu_graphics_overlay;

nfspu_graphics_options nfspu_graphics_defaults(void);
void nfspu_overlay_init(nfspu_graphics_overlay *,const nfspu_graphics_options *);
void nfspu_overlay_destroy(nfspu_graphics_overlay *);
void nfspu_overlay_open(nfspu_graphics_overlay *,const nfspu_graphics_options *);
void nfspu_overlay_close(nfspu_graphics_overlay *);
/* Call after an apply succeeds, or to synchronize external F11 changes.
 * An open draft is preserved unless this call follows its Apply action. */
void nfspu_overlay_applied(nfspu_graphics_overlay *,const nfspu_graphics_options *);
void nfspu_overlay_status(nfspu_graphics_overlay *,const char *);
/* SDL physical scancode, key-down only. Returns an action; Apply leaves the
 * panel open until the caller commits, reports success/error, or closes it. */
int nfspu_overlay_key(nfspu_graphics_overlay *,uint32_t scancode);
/* Output-pixel coordinates. Motion: button=0/down=0; click: button=1/down=1;
 * wheel carries signed vertical wheel steps and may be called with button=0. */
int nfspu_overlay_pointer(nfspu_graphics_overlay *,int x,int y,
                         uint32_t button,int down,int wheel);
/* One triangle batch. It is intended for a presentation-only color target,
 * after game rendering; do not draw this into the guest framebuffer. */
int nfspu_overlay_render(nfspu_graphics_overlay *,const nfspu_overlay_stats *,
                        uint32_t output_width,uint32_t output_height,
                        nfspu_overlay_emit_fn,void *context);
#endif
