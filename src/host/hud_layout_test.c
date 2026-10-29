/* Asset-free checks for the requested instrument arrangement. Link only
 * hud_layout.c and this file, without HLE or a fixed RECOMP_MEM_BASE. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"
#include "hud_layout.h"

ptrdiff_t g_mem_base;
static int failures;
#define CHECK(c,m) do { int ok = !!(c); printf("%s %s\n",ok?"PASS":"FAIL",m); if(!ok)failures++; } while(0)
enum { BASE_OBJECT=0x700000, BASE_ITEMS=0x700100, BASE=0x700200,
       DELTA_OBJECT=0x700500, DELTA_ITEMS=0x700600, DELTA=0x700700 };

static void native_layout(int width, int height) {
    MEM32(0x60C900)=width;MEM32(0x60C81C)=height;
    MEM32(0x60C860)=width-248;MEM32(0x60C864)=height-128;
    MEM32(0x60C830)=width-203;MEM32(0x60C834)=height-72;
    MEM32(0x60C888)=width-203;MEM32(0x60C88C)=height-36;
}

int main(void) {
    void *memory=calloc(1,0x800000);
    if(!memory)return 2;
    g_mem_base=(ptrdiff_t)(uintptr_t)memory;
    MEM32(0x60C818)=BASE_OBJECT;MEM32(BASE_OBJECT)=BASE_ITEMS;
    MEM32(BASE_ITEMS)=BASE;MEM32(0x60C904)=DELTA_OBJECT;
    MEM32(DELTA_OBJECT)=DELTA_ITEMS;MEM32(DELTA_ITEMS)=DELTA;
    MEM32(0x60C95C)=0;
    MEM32(BASE+0x9C)=73;MEM32(BASE+0xA0)=35;
    MEM32(DELTA+0x14)=73;MEM32(DELTA+0x18)=35;
    MEM32(BASE+0xD4)=128;MEM32(BASE+0xD8)=128;
    MEM32(DELTA+0x30)=2;MEM32(DELTA+0x38)=2;
    MEM32(BASE+0x28)=0xFFD70A0F;MEMF(BASE+0x1C0)=0.6125f;
    uint8_t base_before[480],delta_before[104];
    memcpy(base_before,(void *)ADDR(BASE),sizeof base_before);
    memcpy(delta_before,(void *)ADDR(DELTA),sizeof delta_before);

    const int modes[][2]={{640,480},{1600,900},{1280,720}};
    for(unsigned i=0;i<sizeof modes/sizeof modes[0];i++) {
        int width=modes[i][0],height=modes[i][1];native_layout(width,height);
        uint8_t caches[0xA0];memcpy(caches,(void *)ADDR(0x60C800),sizeof caches);
        nfspu_hud_layout_apply();
        CHECK(MEM32(0x60C830)==(uint32_t)(width-333)&&MEM32(0x60C834)==(uint32_t)(height-85)&&
              MEM32(0x60C888)==(uint32_t)(width-333)&&MEM32(0x60C88C)==(uint32_t)(height-43),
              "both digital rows move left of the analog dial at the current mode");
        memcpy(caches+0x30,(void *)ADDR(0x60C830),8);
        memcpy(caches+0x88,(void *)ADDR(0x60C888),8);
        CHECK(memcmp(caches,(void *)ADDR(0x60C800),sizeof caches)==0,
              "analog instruments and unrelated HUD pixel caches remain unchanged");
        nfspu_hud_layout_apply();
        CHECK(MEM32(0x60C830)==(uint32_t)(width-333)&&MEM32(0x60C88C)==(uint32_t)(height-43),
              "reapplying the arrangement does not accumulate offsets");
    }
    CHECK(memcmp(base_before,(void *)ADDR(BASE),sizeof base_before)==0&&
          memcmp(delta_before,(void *)ADDR(DELTA),sizeof delta_before)==0,
          "profile arrays, normalized anchors, visibility flags, and colors stay unchanged");

    native_layout(1600,900);MEM32(DELTA+0x30)=1;MEM32(DELTA+0x38)=1;
    nfspu_hud_layout_apply();
    CHECK(MEM32(0x60C830)==1397&&MEM32(0x60C834)==828&&MEM32(0x60C888)==1397&&MEM32(0x60C88C)==864,
          "standalone digital instrument layout stays unchanged");
    MEM32(DELTA+0x30)=2;native_layout(1600,900);nfspu_hud_layout_apply();
    CHECK(MEM32(0x60C830)==1267&&MEM32(0x60C888)==1397&&MEM32(0x60C88C)==864,
          "a standalone digital RPM preference is preserved alongside the analog speedometer");
    MEM32(DELTA+0x38)=2;native_layout(1600,900);
    MEM32(0x60C830)=400;MEM32(0x60C834)=500;MEM32(0x60C888)=500;MEM32(0x60C88C)=600;
    nfspu_hud_layout_apply();
    CHECK(MEM32(0x60C830)==400&&MEM32(0x60C834)==500&&MEM32(0x60C888)==500&&MEM32(0x60C88C)==600,
          "custom readouts outside the dial cluster keep their positions");

    native_layout(1280,720);MEM32(0x60C860)=10;
    MEM32(0x60C830)=20;MEM32(0x60C888)=20;nfspu_hud_layout_apply();
    CHECK(MEM32(0x60C830)==0&&MEM32(0x60C888)==0&&MEM32(0x60C834)+35<=720&&MEM32(0x60C88C)+35<=720,
          "a dial near the left edge keeps readouts inside the viewport");
    native_layout(1600,900);MEM32(BASE+0x9C)=0;nfspu_hud_layout_apply();
    CHECK(MEM32(0x60C830)==1397&&MEM32(0x60C888)==1397,
          "invalid widget dimensions leave the native layout intact");
    free(memory);
    return failures?1:0;
}
