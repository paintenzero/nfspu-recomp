/* Hor+ adaptation for the race camera in the USA 3.5 lift. These callbacks
 * use memory and native math only: they do not enter guest code or touch its
 * register, flags, or x87 shadow state. */
#include <math.h>
#include <stdio.h>
#include "recomp_types.h"
#include "widescreen.h"

#define RACE_WRAPPER 0x00628140u
#define DESIRED_WIDTH 0x00657A48u
#define DESIRED_HEIGHT 0x00657A4Cu
#define LEGACY_WIDESCREEN 0x00657D5Cu

static int requested_widescreen, active_widescreen;
static uint32_t target_width, target_height;
static uint32_t active_width, active_height;
static uint32_t saved_width, saved_height, forced_width, forced_height;
static int saved_dimensions;
static int projection_logged;
static unsigned mode_calls;

void nfspu_widescreen_configure(int enabled, uint32_t width, uint32_t height) {
    /* Explicit runtime registration avoids COFF weak/selectany choosing a
     * zero initializer from another translation unit. */
    recomp_projection_hook=nfspu_widescreen_projection;
    recomp_mode_hook=nfspu_widescreen_mode;
    target_width = width;
    target_height = height;
    requested_widescreen = enabled && width >= 640 && height >= 480 &&
                         width <= 8192 && height <= 8192 &&
                         (double)width / height > 4.0 / 3.0 + 0.001;
    if (!requested_widescreen && saved_dimensions) {
        /* Restore only values still owned by the override. A resolution
         * selected by the user after our last write belongs to the user. */
        if (MEM32(DESIRED_WIDTH) == forced_width &&
            MEM32(DESIRED_HEIGHT) == forced_height) {
            MEM32(DESIRED_WIDTH) = saved_width;
            MEM32(DESIRED_HEIGHT) = saved_height;
        }
        saved_dimensions = 0;
    }
}

void nfspu_widescreen_projection(uint32_t camera) {
    uint32_t wrapper, window;
    double width, height, reference_aspect, aspect, factor, half_fov, tx, ty;
    if (!active_widescreen || !camera) return;
    wrapper = MEM32(RACE_WRAPPER);
    if (!wrapper || !(window = MEM32(wrapper + 4)) ||
        MEM32(window + 0xC) != camera || MEM32(camera + 0x220) != 0) return;

    /* 00538DA0 stores viewport size at 1D8/1DC and its origin at 1E0/1E4. */
    width = MEMF(camera + 0x1D8);
    height = MEMF(camera + 0x1DC);
    reference_aspect = MEMF(camera + 0xD8);
    half_fov = MEMF(camera + 0xD4) * (3.14159265358979323846 / 360.0);
    if (!isfinite(width) || !isfinite(height) || width <= 0 || height <= 0 ||
        !isfinite(reference_aspect) || reference_aspect <= 0 ||
        !isfinite(half_fov) || half_fov <= 0 || half_fov >= 1.5707963267948966)
        return;
    aspect = width / height;
    factor = aspect / reference_aspect;
    if (!isfinite(factor) || factor <= 1.001) return;

    tx = tan(half_fov);
    ty = tx / reference_aspect;
    tx *= factor;
    /* The original constructor rebuilds 148 afresh on every call. Preserve
     * its vertical projection and cached base FOV/aspect, so a subsequent
     * 005390D0 rebuild cannot compound the horizontal adjustment. */
    MEMF(camera + 0x148) = (float)(MEMF(camera + 0x148) / factor);
    MEMF(camera + 0xF8) = (float)tx;
    MEMF(camera + 0xFC) = (float)(1.0 / sqrt(1.0 + tx * tx));
    /* Original vertical culling multiplies the half angle by aspect, which
     * can cross 90 degrees at widescreen FOVs. Match the actual projection. */
    MEMF(camera + 0x100) = (float)ty;
    MEMF(camera + 0x104) = (float)(1.0 / sqrt(1.0 + ty * ty));
    if(!projection_logged){
        fprintf(stderr,"[wide] race camera viewport %.0fx%.0f base-aspect=%.6f horizontal-factor=%.6f\n",width,height,reference_aspect,factor);
        projection_logged=1;
    }
}

void nfspu_widescreen_mode(uint32_t caller, uint32_t entry_esp) {
    uint32_t depth;
    if (!entry_esp) return;
    if(requested_widescreen&&mode_calls++<8)
        fprintf(stderr,"[wide] mode selector caller=%08X request=%ux%u race=%08X\n",caller,MEM32(entry_esp+4),MEM32(entry_esp+8),MEM32(RACE_WRAPPER));
    /* Menus and movies deliberately request 640x480 even while a race
     * wrapper exists. g_cur_func identifies these containing functions. */
    if (caller == 0x004D3420u || caller == 0x004DD600u ||
        caller == 0x004D3440u || caller == 0x004DD61Fu || caller == 0x004DD71Fu)
        return;
    /* Settings take effect when the next race chooses its native mode.
     * Keeping the projection latch preserves a running wide race after the
     * user disables the next-race request in the overlay. */
    if (caller == 0x004B6A50u || caller == 0x004152E0u) {
        active_widescreen = requested_widescreen;
        active_width = target_width;
        active_height = target_height;
        projection_logged = 0;
    } else if (!MEM32(RACE_WRAPPER)) return;
    if (!active_widescreen) return;
    MEM32(entry_esp + 4) = active_width;
    MEM32(entry_esp + 8) = active_height;
    depth = MEM32(entry_esp + 12);
    if (depth != 16 && depth != 32) MEM32(entry_esp + 12) = 16;
    if (requested_widescreen) {
        if (!saved_dimensions || MEM32(DESIRED_WIDTH) != forced_width ||
            MEM32(DESIRED_HEIGHT) != forced_height) {
            saved_width = MEM32(DESIRED_WIDTH);
            saved_height = MEM32(DESIRED_HEIGHT);
            saved_dimensions = 1;
        }
        MEM32(DESIRED_WIDTH) = forced_width = active_width;
        MEM32(DESIRED_HEIGHT) = forced_height = active_height;
    }
    MEM32(LEGACY_WIDESCREEN) = 0;
    fprintf(stderr,"[wide] race mode %ux%u (caller %08X)\n",active_width,active_height,caller);
}
