/* Asset-free regression for the race-only projection and mode callbacks.
 * Link with widescreen.c and libm, without the game or HLE runtime. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"
#include "widescreen.h"

ptrdiff_t g_mem_base;
void (*recomp_projection_hook)(uint32_t camera);
void (*recomp_mode_hook)(uint32_t caller,uint32_t entry_esp);
static int failures;
#define CHECK(c,m) do { int ok = !!(c); printf("%s %s\n",ok?"PASS":"FAIL",m); if(!ok)failures++; } while(0)
static int close_to(double a, double b) { return fabs(a-b) < 0.00001; }

enum { WRAPPER=0x00700000, WINDOW=0x00700100, CAMERA=0x00700400,
       OTHER=0x00700800, STACK=0x00701000 };
static void rebuild(uint32_t camera, float width, float height) {
    memset((void *)ADDR(camera), 0, 0x224);
    MEMF(camera+0xD4)=90.0f;
    MEMF(camera+0xD8)=4.0f/3.0f;
    MEMF(camera+0x148)=1.0f;
    MEMF(camera+0x15C)=-4.0f/3.0f;
    MEMF(camera+0x1D8)=width;
    MEMF(camera+0x1DC)=height;
    /* Nonzero origins distinguish viewport dimensions from the clip origin. */
    MEMF(camera+0x1E0)=17;
    MEMF(camera+0x1E4)=31;
    MEMF(camera+0xF8)=1;
    MEMF(camera+0xFC)=0.70710678f;
    MEMF(camera+0x100)=1.7320508f;
    MEMF(camera+0x104)=0.5f;
}
static void mode_args(uint32_t width, uint32_t height, uint32_t depth) {
    MEM32(STACK)=RECOMP_RETADDR;
    MEM32(STACK+4)=width;
    MEM32(STACK+8)=height;
    MEM32(STACK+12)=depth;
    MEM32(STACK+16)=2;
}
int main(void) {
    void *memory=calloc(1,0x00800000);
    if(!memory)return 2;
    g_mem_base=(ptrdiff_t)(uintptr_t)memory;
    MEM32(WRAPPER+4)=WINDOW;
    MEM32(WINDOW+0xC)=CAMERA;
    MEM32(0x657A48)=640;MEM32(0x657A4C)=480;
    nfspu_widescreen_configure(1,1600,900);
    CHECK(recomp_projection_hook==nfspu_widescreen_projection&&recomp_mode_hook==nfspu_widescreen_mode,"configuration installs runtime adapters");
    rebuild(CAMERA,1600,900);MEM32(0x628140)=WRAPPER;
    nfspu_widescreen_projection(CAMERA);
    CHECK(MEMF(CAMERA+0x148)==1,"request waits for race mode before enabling projection");
    MEM32(0x628140)=0;mode_args(640,480,32);MEM32(0x657D5C)=1;
    nfspu_widescreen_mode(0x4B6A50,STACK);
    CHECK(MEM32(STACK+4)==1600&&MEM32(STACK+8)==900&&MEM32(STACK+12)==32&&MEM32(STACK+16)==2,"race initialization selects target dimensions and preserves format/buffers");
    CHECK(MEM32(0x657A48)==1600&&MEM32(0x657A4C)==900&&MEM32(0x657D5C)==0,"race mode updates desired dimensions and disables legacy crop");
    MEM32(0x628140)=WRAPPER;

    rebuild(CAMERA,640,480);
    uint8_t original[0x224];memcpy(original,(void *)ADDR(CAMERA),sizeof original);
    nfspu_widescreen_projection(CAMERA);
    CHECK(memcmp(original,(void *)ADDR(CAMERA),sizeof original)==0,"4:3 viewport remains unchanged");

    rebuild(CAMERA,1600,900);
    uint32_t base_fov=MEM32(CAMERA+0xD4),base_aspect=MEM32(CAMERA+0xD8),base_y=MEM32(CAMERA+0x15C);
    nfspu_widescreen_projection(CAMERA);
    CHECK(close_to(MEMF(CAMERA+0x148),0.75),"16:9 widens horizontal projection by 4/3");
    CHECK(MEM32(CAMERA+0x15C)==base_y,"vertical projection stays unchanged");
    CHECK(MEM32(CAMERA+0xD4)==base_fov&&MEM32(CAMERA+0xD8)==base_aspect,"cached base FOV and aspect stay unchanged");
    CHECK(close_to(MEMF(CAMERA+0xF8),4.0/3.0)&&close_to(MEMF(CAMERA+0xFC),0.6),"horizontal culling includes the wider view");
    CHECK(close_to(MEMF(CAMERA+0x100),0.75)&&close_to(MEMF(CAMERA+0x104),0.8),"vertical culling matches the preserved vertical view");
    rebuild(CAMERA,1600,900);nfspu_widescreen_projection(CAMERA);
    CHECK(close_to(MEMF(CAMERA+0x148),0.75),"fresh constructor rebuild does not compound correction");

    rebuild(OTHER,1600,900);memcpy(original,(void *)ADDR(OTHER),sizeof original);
    nfspu_widescreen_projection(OTHER);
    CHECK(memcmp(original,(void *)ADDR(OTHER),sizeof original)==0,"non-race camera remains unchanged");
    rebuild(CAMERA,1600,900);MEM32(CAMERA+0x220)=1;
    nfspu_widescreen_projection(CAMERA);
    CHECK(MEMF(CAMERA+0x148)==1,"orthographic HUD remains unchanged");

    MEM32(0x628140)=0;
    mode_args(640,480,16);nfspu_widescreen_mode(0x123456,STACK);
    CHECK(MEM32(STACK+4)==640&&MEM32(STACK+8)==480,"unrelated mode call before race remains unchanged");
    MEM32(0x628140)=WRAPPER;nfspu_widescreen_mode(0x123456,STACK);
    CHECK(MEM32(STACK+4)==1600&&MEM32(STACK+8)==900,"active race mode change retains target dimensions");
    const uint32_t excluded[]={0x4D3420,0x4DD600,0x4D3440,0x4DD61F,0x4DD71F};
    int preserved=1;
    for(unsigned i=0;i<sizeof excluded/sizeof excluded[0];i++){
        mode_args(640,480,16);nfspu_widescreen_mode(excluded[i],STACK);
        preserved&=MEM32(STACK+4)==640&&MEM32(STACK+8)==480;
    }
    CHECK(preserved,"explicit menu and movie modes remain 640x480 during a race");
    nfspu_widescreen_configure(0,1600,900);rebuild(CAMERA,1600,900);
    CHECK(MEM32(0x657A48)==640&&MEM32(0x657A4C)==480,"disabling restores the original desired dimensions");
    nfspu_widescreen_projection(CAMERA);
    CHECK(close_to(MEMF(CAMERA+0x148),0.75),"disabling preserves correct projection in the running wide race");
    mode_args(640,480,16);nfspu_widescreen_mode(0x4DD600,STACK);
    rebuild(CAMERA,1600,900);nfspu_widescreen_projection(CAMERA);
    CHECK(close_to(MEMF(CAMERA+0x148),0.75),"movie mode does not clear the active race projection latch");
    mode_args(640,480,16);nfspu_widescreen_mode(0x123456,STACK);
    CHECK(MEM32(STACK+4)==1600&&MEM32(STACK+8)==900&&MEM32(0x657A48)==640&&MEM32(0x657A4C)==480,"running race mode retains its size without overwriting restored preferences");
    mode_args(640,480,16);nfspu_widescreen_mode(0x4152E0,STACK);
    rebuild(CAMERA,1600,900);nfspu_widescreen_projection(CAMERA);
    CHECK(MEMF(CAMERA+0x148)==1&&MEM32(STACK+4)==640,"next race disables the projection and leaves its requested mode unchanged");

    nfspu_widescreen_configure(1,1600,900);mode_args(640,480,16);
    nfspu_widescreen_mode(0x4152E0,STACK);
    MEM32(0x657A48)=1280;MEM32(0x657A4C)=960;
    nfspu_widescreen_configure(0,1600,900);
    CHECK(MEM32(0x657A48)==1280&&MEM32(0x657A4C)==960,"disabling preserves dimensions changed by the user");
    nfspu_widescreen_configure(1,1600,900);mode_args(1280,960,16);
    nfspu_widescreen_mode(0x4152E0,STACK);nfspu_widescreen_configure(0,1600,900);
    CHECK(MEM32(0x657A48)==1280&&MEM32(0x657A4C)==960,"a later override captures and restores the current user dimensions");
    free(memory);
    return failures?1:0;
}
