/* DirectInput guest callback, descriptor, filter and stack ABI regression. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL_scancode.h>
#include "win32hle.h"
void nfspu_register_dinput(void);
static uint8_t portable_keys[512];
static unsigned pump_calls;
void nfspu_thrash_pump(void) { pump_calls++; }
int nfspu_thrash_input(uint8_t *keys, int *x, int *y, uint32_t *buttons) {
    if(keys)memcpy(keys,portable_keys,sizeof portable_keys);
    if(x)*x=0;if(y)*y=0;if(buttons)*buttons=0;return 1;
}
static unsigned devices, objects, stop_after;
static uint32_t expected_kind, expected_filter, guids[2][4];
static void device_callback(void) {
    uint32_t p = A32(0);
    assert(A32(1) == 0x1234 && MEM32(p) == 580);
    uint32_t type = MEM32(p + 36) & 0xFF;
    assert(type == 2 || type == 3);
    if (expected_kind) assert(type == expected_kind);
    memcpy(guids[type == 2], hle_ptr(p + 4), 16);
    assert(MEM8(p + 40) && MEM8(p + 300));
    devices++;
    RET(stop_after && devices >= stop_after ? 0 : 1, 2);
}
static void object_callback(void) {
    uint32_t p = A32(0), type = MEM32(p + 24), offset = MEM32(p + 20);
    assert(A32(1) == 0x5678 && MEM32(p) == 316 && MEM8(p + 32));
    if (expected_filter) assert(type & expected_filter & 0xFF);
    if (expected_kind == 3) {
        assert(MEM32(p + 4) == 0x55728220 && offset == objects && (type >> 8) == objects);
    } else if (type & 1) assert(offset < 12 && !(offset % 4));
    else assert(offset >= 12 && offset <= 14);
    objects++;
    RET(stop_after && objects >= stop_after ? 0 : 1, 2);
}
const recomp_dispatch_entry_t recomp_dispatch_table[] = {
    { 0x401000, device_callback }, { 0x401100, object_callback }
};
const uint32_t recomp_dispatch_count = 2;
static uint32_t call(const char *name, unsigned n, const uint32_t *args) {
    mach_enter(); uint32_t before = g_esp;
    for (unsigned i = n; i; i--) PUSH32(g_esp, args[i - 1]);
    PUSH32(g_esp, RECOMP_RETADDR);
    recomp_func_t fn = recomp_lookup_import(hle_resolve(name)); assert(fn); fn();
    assert(g_esp == before); uint32_t result = g_eax; mach_leave(); return result;
}
static uint32_t slot(uint32_t dev,unsigned index,unsigned n,const uint32_t *args) {
    mach_enter();uint32_t before=g_esp;
    for(unsigned i=n;i;i--)PUSH32(g_esp,args[i-1]);PUSH32(g_esp,RECOMP_RETADDR);
    uint32_t va=MEM32(MEM32(dev)+index*4);recomp_func_t fn=recomp_lookup_import(va);assert(fn);fn();
    assert(g_esp==before);uint32_t result=g_eax;mach_leave();return result;
}
static uint32_t iid(uint32_t first,uint32_t second) {
    uint32_t p=hle_va(hle_alloc(16));MEM32(p)=first;MEM32(p+4)=second;MEM32(p+8)=0x4544C7BF;MEM32(p+12)=0x5453;return p;
}
int main(void) {
    nfspu_register_dinput();
    uint32_t out = hle_va(hle_alloc(4));
    uint32_t create[] = {0, 0x700, out, 0};
    assert(call("DirectInputCreateA", 4, create) == 0); uint32_t di = MEM32(out);
    uint32_t unknown=iid(0xDEADBEEF,0x11CFAA8A),qi[]={di,unknown,out};MEM32(out)=0xDEADBEEF;
    assert(slot(di,0,3,qi)==0x80004002u&&MEM32(out)==0);
    uint32_t enumerate[] = {di, 0, 0x401000, 0x1234, 0};
    assert(call("IDirectInputA::EnumDevices", 5, enumerate) == 0 && devices == 2);
    for (unsigned kind = 2; kind <= 4; kind++) {
        devices = 0; expected_kind = kind; enumerate[1] = kind;
        assert(call("IDirectInputA::EnumDevices", 5, enumerate) == 0);
        assert(devices == (kind == 4 ? 0u : 1u));
    }
    expected_kind = 0; devices = 0; stop_after = 1; enumerate[1] = 0;
    assert(call("IDirectInputA::EnumDevices", 5, enumerate) == 0 && devices == 1);
    stop_after = 0;
    for (unsigned mouse = 0; mouse < 2; mouse++) {
        uint32_t guid = hle_va(hle_alloc(16)); memcpy(hle_ptr(guid), guids[mouse], 16);
        uint32_t make_device[] = {di, guid, out, 0};
        assert(call("IDirectInputA::CreateDevice", 4, make_device) == 0);
        uint32_t dev = MEM32(out), caps = hle_va(hle_alloc(48));
        qi[0]=dev;qi[1]=iid(0x5944E682,0x11CFC92E);qi[2]=out;
        uint32_t references=MEM32(dev+4);
        assert(slot(dev,0,3,qi)==0&&MEM32(out)==dev&&MEM32(dev+4)==references+1);
        assert(MEM32(MEM32(dev)+25*4)==hle_resolve("IDirectInputDevice2A::Poll"));
        qi[1]=unknown;MEM32(out)=0xDEADBEEF;
        assert(slot(dev,0,3,qi)==0x80004002u&&MEM32(out)==0);
        uint32_t self[]={dev};assert(slot(dev,25,1,self)==0x8007000Cu);
        uint32_t state=hle_va(hle_alloc(mouse?16:256)),read[]={dev,mouse?16u:256u,state};
        assert(slot(dev,9,3,read)==0x8007000Cu);
        assert(slot(dev,7,1,self)==0);references=MEM32(dev+4);
        for(unsigned i=0;i<200;i++)assert(slot(dev,25,1,self)==0);
        assert(MEM32(dev+4)==references&&pump_calls>=200);
        uint32_t format=hle_va(hle_alloc(24)),count=mouse?7:256,formats=hle_va(hle_alloc(count*16));
        MEM32(format)=24;MEM32(format+4)=16;MEM32(format+8)=2;MEM32(format+12)=mouse?16:256;
        MEM32(format+16)=count;MEM32(format+20)=formats;
        for(uint32_t i=0;i<count;i++){
            MEM32(formats+i*16+4)=mouse?(i<3?i*4:12+i-3):i;
            MEM32(formats+i*16+8)=mouse?(i<3?0x00FFFF03u:0x80FFFF0Cu):(0x8000000Cu|i<<8);
        }
        uint32_t format_args[]={dev,format};assert(slot(dev,11,2,format_args)==0);
        MEM32(format+12)=mouse?20:255;assert(slot(dev,11,2,format_args)==0x80070057u);MEM32(format+12)=mouse?16:256;
        if(!mouse){
            portable_keys[SDL_SCANCODE_UP]=portable_keys[SDL_SCANCODE_RALT]=portable_keys[SDL_SCANCODE_KP_8]=portable_keys[SDL_SCANCODE_LEFTBRACKET]=1;
            assert(slot(dev,9,3,read)==0&&MEM8(state+0xC8)==0x80&&MEM8(state+0xB8)==0x80&&MEM8(state+0x48)==0x80&&MEM8(state+0x1A)==0x80);
            memset(portable_keys,0,sizeof portable_keys);assert(slot(dev,9,3,read)==0&&MEM8(state+0xC8)==0&&MEM8(state+0xB8)==0&&MEM8(state+0x48)==0);
        }
        assert(slot(dev,8,1,self)==0&&slot(dev,25,1,self)==0x8007000Cu);
        assert(slot(dev,7,1,self)==0&&slot(dev,9,3,read)==0);
        uint32_t effect[]={dev,0,0,out,0};MEM32(out)=0xDEADBEEF;
        assert(slot(dev,18,5,effect)==0x80004001u&&MEM32(out)==0);
        for (unsigned size = 24; size <= 44; size += 20) {
            MEM32(caps) = size; MEM32(caps + size) = 0xDEADBEEF;
            uint32_t args[] = {dev, caps}; assert(call("IDirectInputDeviceA::GetCapabilities", 2, args) == 0);
            assert(MEM32(caps + size) == 0xDEADBEEF && MEM32(caps + 4) & 1);
            assert(MEM32(caps + 12) == (mouse ? 3u : 0u));
            assert(MEM32(caps + 16) == (mouse ? 3u : 256u));
        }
        uint32_t info = hle_va(hle_alloc(584)), info_args[] = {dev, info};
        MEM32(info) = 560; MEM32(info + 560) = 0xDEADBEEF;
        assert(call("IDirectInputDeviceA::GetDeviceInfo", 2, info_args) == 0);
        assert(MEM32(info) == 560 && MEM32(info + 560) == 0xDEADBEEF);
        expected_kind = mouse ? 2 : 3; objects = 0; expected_filter = 0;
        uint32_t enum_objects[] = {dev, 0x401100, 0x5678, 0};
        assert(call("IDirectInputDeviceA::EnumObjects", 4, enum_objects) == 0);
        assert(objects == (mouse ? 6u : 256u));
        objects = 0; stop_after = 1;
        assert(call("IDirectInputDeviceA::EnumObjects", 4, enum_objects) == 0 && objects == 1);
        stop_after = 0;
        if (mouse) for (unsigned filter = 3; filter <= 12; filter += 9) {
            objects = 0; expected_filter = filter; enum_objects[3] = filter;
            assert(call("IDirectInputDeviceA::EnumObjects", 4, enum_objects) == 0 && objects == 3);
        }
    }
    puts("PASS: DirectInput enumeration, Device2 QI/Poll vtable, acquire/reacquire, immediate key state, data format, bounds and stack ABI");
    return 0;
}
