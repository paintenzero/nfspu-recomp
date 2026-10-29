#ifndef NFSPU_GPU_H
#define NFSPU_GPU_H
#include <stdint.h>
#include "graphics_options.h"
#define NFSPU_GPU_ABI 3u
#ifdef _WIN32
#define NFSPU_GPU_EXPORT __declspec(dllexport)
#else
#define NFSPU_GPU_EXPORT __attribute__((visibility("default")))
#endif
typedef struct nfspu_gpu_vertex {
    float x, y, z, rhw;
    uint32_t color, specular;
    float u, v;
} nfspu_gpu_vertex;
typedef struct nfspu_gpu_state {
    uint32_t texture;
    uint32_t depth_test, depth_write;
    uint32_t blend_enable, src_blend, dst_blend;
    uint32_t filter_linear, address_wrap, cull;
    uint32_t alpha_test, alpha_ref;
    uint32_t fog_enable, fog_color;
    float fog_near, fog_far;
    uint32_t primitive; /* 0 triangles, 1 lines, 2 points */
    uint32_t depth_func; /* D3DCMPFUNC 1..8; zero defaults to LEQUAL */
    uint32_t scissor_enable, scissor_x, scissor_y, scissor_w, scissor_h;
    uint32_t alpha_modulate; /* textured: 0 texture alpha, 1 texture*diffuse alpha */
    uint32_t fog_mode; /* 0 vertex visibility, 1 linear, 2 exponential, 3 exponential-squared */
    float fog_density; /* table fog uses eye depth reconstructed from RHW */
} nfspu_gpu_state;
typedef struct nfspu_gpu_event {
    uint32_t type; /* 1 quit, 2 key, 3 motion, 4 button, 5 wheel, 6 reset, 7 applied, 8 camera sync request */
    uint32_t scancode, down, button;
    int32_t x, y, dx, dy; /* type 7: x/y are actual output pixel dimensions */
} nfspu_gpu_event;
typedef struct nfspu_gpu_api {
    uint32_t abi;
    int (*open)(uint32_t width, uint32_t height);
    void (*close)(void);
    int (*resize)(uint32_t width, uint32_t height);
    void (*clear)(uint32_t argb, float depth);
    int (*present)(void);
    uint32_t (*texture_create)(uint32_t width, uint32_t height, const uint8_t *rgba);
    int (*texture_update)(uint32_t texture, const uint8_t *rgba);
    void (*texture_destroy)(uint32_t texture);
    int (*draw)(const nfspu_gpu_vertex *vertices, uint32_t vertex_count,
                const uint32_t *indices, uint32_t index_count,
                const nfspu_gpu_state *state);
    int (*poll)(nfspu_gpu_event *event);
    int (*read_pixels)(uint8_t *rgba, uint32_t width, uint32_t height);
    const char *(*error)(void);
    const nfspu_graphics_options *(*get_options)(void);
    int (*set_options)(const nfspu_graphics_options *);
    void (*set_overlay)(int open);
    int (*overlay_open)(void);
    int (*load_config)(const char *path);
    void (*sync_camera)(uint32_t enabled);
} nfspu_gpu_api;
typedef const nfspu_gpu_api *(*nfspu_gpu_get_api_fn)(void);
NFSPU_GPU_EXPORT const nfspu_gpu_api *nfspu_gpu_get_api(void);
#endif
