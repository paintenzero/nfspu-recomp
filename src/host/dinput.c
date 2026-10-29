/* DirectInputCreateA and a keyboard/mouse device, on SDL.
 * Porsche Unleashed links only DirectInputCreateA; the rest is COM. */
#include <SDL3/SDL.h>
#include <string.h>
#include "win32hle.h"
#include "thrash.h"
#include "input_keys.h"

#define DI_OK 0u
#define DIERR_INVALIDPARAM 0x80070057u
#define DIERR_NOTACQUIRED  0x8007000Cu
#define DIERR_DEVICENOTREG 0x80040154u
#define DIERR_UNSUPPORTED 0x80004001u
#define E_NOINTERFACE 0x80004002u
#define E_POINTER 0x80004003u

/* Fixed-width guest layouts from dinput.h; the host may have 64-bit pointers.
 * DX3 structures are prefixes of the DX5+ structures used for enumeration. */
typedef struct {
    uint32_t size, instance[4], product[4], type;
    char instance_name[260], product_name[260];
    uint32_t ff_driver[4];
    uint16_t usage_page, usage;
} device_instance;
typedef struct {
    uint32_t size, guid[4], offset, type, flags;
    char name[260];
    uint32_t ff_max, ff_resolution;
    uint16_t collection, designator, usage_page, usage;
    uint32_t dimension;
    uint16_t exponent, report;
} object_instance;
_Static_assert(sizeof(device_instance) == 580, "DIDEVICEINSTANCEA layout");
_Static_assert(sizeof(object_instance) == 316, "DIDEVICEOBJECTINSTANCEA layout");

static void put_guid(uint32_t guid[4], uint32_t d1, uint16_t d2) {
    guid[0] = d1; guid[1] = 0x11CF0000u | d2;
    guid[2] = 0x4544C7BFu; guid[3] = 0x00005453u;
}
static uint32_t device_type(int mouse) { return mouse ? 0x202u : 0x403u; }
static void fill_device(device_instance *d, int mouse) {
    memset(d, 0, sizeof *d); d->size = sizeof *d;
    put_guid(d->instance, mouse ? 0x6F1D2B60u : 0x6F1D2B61u, 0xD5A0);
    memcpy(d->product, d->instance, sizeof d->product);
    d->type = device_type(mouse);
    snprintf(d->instance_name, sizeof d->instance_name, "System %s", mouse ? "Mouse" : "Keyboard");
    memcpy(d->product_name, d->instance_name, sizeof d->product_name);
    d->usage_page = 1; d->usage = mouse ? 2 : 6;
}
static void fill_object(object_instance *o, int mouse, unsigned index) {
    memset(o, 0, sizeof *o); o->size = sizeof *o;
    if (!mouse) {
        put_guid(o->guid, 0x55728220u, 0xD33C);
        o->offset = index; o->type = 4u | index << 8;
        snprintf(o->name, sizeof o->name, "Key %02X", index);
        o->usage_page = 7;
    } else if (index < 3) {
        put_guid(o->guid, 0xA36D02E0u + index, 0xC9F3);
        o->offset = index * 4; o->type = 1u | index << 8;
        o->flags = 0x100u; /* DIDOI_ASPECTPOSITION */
        snprintf(o->name, sizeof o->name, "%c Axis", "XYZ"[index]);
        o->usage_page = 1; o->usage = (uint16_t)(0x30 + index);
    } else {
        unsigned button = index - 3;
        put_guid(o->guid, 0xA36D02F0u, 0xC9F3);
        o->offset = 12 + button; o->type = 4u | button << 8;
        snprintf(o->name, sizeof o->name, "Button %u", button + 1);
        o->usage_page = 9; o->usage = (uint16_t)(button + 1);
    }
}

static uint32_t g_vt_di, g_vt_dev;
static int g_ready;
static int g_mx, g_my;

static int guid_is(uint32_t g, uint32_t d1) {
    return g && MEM32(g) == d1 && MEM16(g + 4) == 0xD5A0 && MEM16(g + 6) == 0x11CF;
}
static int iid_is(uint32_t va, uint32_t d1, uint16_t d2) {
    return va && MEM32(va)==d1 && MEM32(va+4)==(0x11CF0000u|d2) &&
        MEM32(va+8)==0x4544C7BFu && MEM32(va+12)==0x00005453u;
}
static int iid_unknown(uint32_t va) {
    return va && !MEM32(va) && !MEM32(va+4) && MEM32(va+8)==0xC0u && MEM32(va+12)==0x46000000u;
}
static void di_QueryInterface(void) {
    uint32_t self=A32(0),iid=A32(1),out=A32(2);
    if(!out)RET(E_POINTER,3);
    MEM32(out)=0;
    if(!iid_unknown(iid)&&!iid_is(iid,0x89521360u,0xAA8A))RET(E_NOINTERFACE,3);
    MEM32(out)=self;MEM32(self+4)++;RET(DI_OK,3);
}
static void dev_QueryInterface(void) {
    uint32_t self=A32(0),iid=A32(1),out=A32(2);
    if(!out)RET(E_POINTER,3);
    MEM32(out)=0;
    /* The shared object exposes the complete A/Device2A prefix. Returning
     * success for an arbitrary IID can make a caller read beyond its table. */
    if(!iid_unknown(iid)&&!iid_is(iid,0x5944E680u,0xC92E)&&!iid_is(iid,0x5944E682u,0xC92E))RET(E_NOINTERFACE,3);
    MEM32(out)=self;MEM32(self+4)++;RET(DI_OK,3);
}

static void ensure(void) {
    if (g_ready) return;
    static const win32hle_shim di[] = {
        { "IDirectInputA::QueryInterface", hle_com_QueryInterface },
        { "IDirectInputA::AddRef", hle_com_AddRef },
        { "IDirectInputA::Release", hle_com_Release },
        { "IDirectInputA::CreateDevice", 0 },
        { "IDirectInputA::EnumDevices", 0 },
        { "IDirectInputA::GetDeviceStatus", 0 },
        { "IDirectInputA::RunControlPanel", 0 },
        { "IDirectInputA::Initialize", 0 },
        { 0, 0 }
    };
    /* filled below after the functions exist — see di_init() */
    (void)di;
    g_ready = 1;
}

static void dev_ok1(void) { RET(DI_OK, 1); }
static void dev_ok2(void) { RET(DI_OK, 2); }
static void dev_ok3(void) { RET(DI_OK, 3); }
static void dev_Acquire(void) { MEM32(A32(0)+12)=1; RET(DI_OK, 1); }
static void dev_Unacquire(void) { MEM32(A32(0)+12)=0; RET(DI_OK, 1); }
static void dev_Poll(void) {
    nfspu_thrash_pump();
    RET(MEM32(A32(0)+12) ? DI_OK : DIERR_NOTACQUIRED,1);
}
static void dev_GetDeviceState(void) {
    uint32_t cb = A32(1);
    uint8_t *dst = (uint8_t *)APTR(2);
    int mouse=MEM32(A32(0)+8)!=0;
    if (!MEM32(A32(0)+12)) RET(DIERR_NOTACQUIRED, 3);
    if (!dst || cb != (mouse ? 16u : 256u)) RET(DIERR_INVALIDPARAM, 3);
    memset(dst, 0, cb);
    if (!mouse && cb >= 256) {
        int n = 0;
        uint8_t portable_keys[512];
        int portable=nfspu_thrash_input(portable_keys,NULL,NULL,NULL);
        const bool *keys = SDL_GetKeyboardState(&n);
        if(portable)n=512;
        for (int i = 0; i < n; i++) {
            int dik = nfspu_dik_from_scancode(i);
            if (dik >= 0 && (uint32_t)dik < cb && (portable?portable_keys[i]:keys[i])) dst[dik] = 0x80;
        }
    } else if (mouse && cb >= 16) {
        int x=0,y=0;uint32_t buttons=0;
        if(!nfspu_thrash_input(NULL,&x,&y,&buttons)) {
            float fx=0,fy=0;buttons=SDL_GetRelativeMouseState(&fx,&fy);x=(int)fx;y=(int)fy;
        }
        int32_t *d = (int32_t *)dst;
        d[0] = x; d[1] = y; d[2] = 0;
        if (buttons & SDL_BUTTON_MASK(1)) dst[12] = 0x80;
        if (buttons & SDL_BUTTON_MASK(3)) dst[13] = 0x80;
        if (buttons & SDL_BUTTON_MASK(2)) dst[14] = 0x80;
        (void)g_mx; (void)g_my;
    }
    RET(DI_OK, 3);
}

static void di_EnumDevices(void) {
    uint32_t type = A32(1), callback = A32(2), context = A32(3), flags = A32(4);
    if (!callback) RET(DIERR_INVALIDPARAM, 5);
    /* Neither the keyboard nor mouse is a force-feedback device. */
    if (flags & 0x100u) RET(DI_OK, 5);
    uint32_t mark = hle_scratch_mark(), desc = hle_scratch(sizeof(device_instance));
    for (int mouse = 0; mouse <= 1; mouse++) {
        if (type && type != (mouse ? 2u : 3u)) continue;
        fill_device((device_instance *)hle_ptr(desc), mouse);
        uint32_t args[2] = { desc, context };
        if (!hle_call_guest(callback, 2, args)) break; /* DIENUM_STOP */
    }
    hle_scratch_release(mark);
    RET(DI_OK, 5);
}
static void di_GetDeviceStatus(void) {
    RET(guid_is(A32(1), 0x6F1D2B60u) || guid_is(A32(1), 0x6F1D2B61u) ? DI_OK : DIERR_DEVICENOTREG, 2);
}
static void di_RunControlPanel(void) { RET(DI_OK, 3); }
static void di_Initialize(void) { RET(DI_OK, 3); }
static void dev_GetCapabilities(void) {
    uint32_t out = A32(1), size = out ? MEM32(out) : 0;
    if (size != 24 && size != 44) RET(DIERR_INVALIDPARAM, 2);
    int mouse = MEM32(A32(0) + 8) != 0;
    memset(hle_ptr(out), 0, size); MEM32(out) = size;
    MEM32(out + 4) = 5; /* DIDC_ATTACHED | DIDC_EMULATED */
    MEM32(out + 8) = device_type(mouse);
    MEM32(out + 12) = mouse ? 3 : 0;
    MEM32(out + 16) = mouse ? 3 : 256;
    RET(DI_OK, 2);
}
static void dev_GetDeviceInfo(void) {
    uint32_t out = A32(1), size = out ? MEM32(out) : 0;
    if (size != 560 && size != 580) RET(DIERR_INVALIDPARAM, 2);
    device_instance desc; fill_device(&desc, MEM32(A32(0) + 8) != 0);
    memcpy(hle_ptr(out), &desc, size); MEM32(out) = size;
    RET(DI_OK, 2);
}
static void dev_EnumObjects(void) {
    uint32_t callback = A32(1), context = A32(2), filter = A32(3);
    if (!callback) RET(DIERR_INVALIDPARAM, 4);
    if (filter & (0x01000000u | 0x10000000u)) RET(DI_OK, 4); /* no FF actuators/output */
    int mouse = MEM32(A32(0) + 8) != 0;
    uint32_t mark = hle_scratch_mark(), desc = hle_scratch(sizeof(object_instance));
    for (unsigned i = 0; i < (mouse ? 6u : 256u); i++) {
        object_instance *o = (object_instance *)hle_ptr(desc); fill_object(o, mouse, i);
        if ((filter & 0xFFu) && !(filter & o->type & 0xFFu)) continue;
        uint32_t instance = filter & 0x00FFFF00u;
        if (instance && instance != 0x00FFFF00u && instance != (o->type & 0x00FFFF00u)) continue;
        uint32_t args[2] = { desc, context };
        if (!hle_call_guest(callback, 2, args)) break;
    }
    hle_scratch_release(mark);
    RET(DI_OK, 4);
}
static void dev_GetObjectInfo(void) {
    uint32_t out = A32(1), obj = A32(2), how = A32(3), size = out ? MEM32(out) : 0;
    if ((size != 292 && size != 316) || (how != 1 && how != 2)) RET(DIERR_INVALIDPARAM, 4);
    int mouse = MEM32(A32(0) + 8) != 0;
    for (unsigned i = 0; i < (mouse ? 6u : 256u); i++) {
        object_instance desc; fill_object(&desc, mouse, i);
        if ((how == 1 && obj == desc.offset) || (how == 2 && obj == desc.type)) {
            memcpy(hle_ptr(out), &desc, size); MEM32(out) = size;
            RET(DI_OK, 4);
        }
    }
    RET(0x80070002u, 4); /* DIERR_OBJECTNOTFOUND */
}
static void dev_SetProperty(void) { RET(DI_OK, 3); }
static void dev_GetProperty(void) { RET(DIERR_INVALIDPARAM, 3); }
static void dev_GetDeviceData(void) { if(A32(3))MEM32(A32(3))=0;RET(DI_OK,5); }
static void dev_Initialize(void) { RET(DI_OK,4); }
static void dev_SetDataFormat(void) {
    uint32_t f=A32(1),mouse=MEM32(A32(0)+8)!=0;
    if(!f||MEM32(f)!=24||MEM32(f+4)!=16)RET(DIERR_INVALIDPARAM,2);
    uint32_t size=MEM32(f+12),count=MEM32(f+16),objects=MEM32(f+20);
    if(size!=(mouse?16u:256u)||count>(mouse?7u:256u)||(count&&!objects))RET(DIERR_INVALIDPARAM,2);
    for(uint32_t i=0;i<count;i++){
        uint32_t o=objects+i*16,offset=MEM32(o+4),type=MEM32(o+8),low=type&0xFFu;
        uint32_t bytes=(low&3u)?4u:1u;
        if(offset>=size||bytes>size-offset)RET(DIERR_INVALIDPARAM,2);
        if(!mouse){
            if(!(low&0xCu)||((type>>8)&0xFFFFu)!=offset)RET(DIERR_INVALIDPARAM,2);
        }else if((low&3u)?(offset>8||offset%4):(offset<12||offset>15))RET(DIERR_INVALIDPARAM,2);
    }
    RET(DI_OK,2);
}
static void dev_CreateEffect(void) { if(A32(3))MEM32(A32(3))=0;RET(DIERR_UNSUPPORTED,5); }
static void dev_EnumEffects(void) { RET(DI_OK,4); }
static void dev_GetEffectInfo(void) { RET(DIERR_UNSUPPORTED,3); }
static void dev_GetForceFeedbackState(void) { if(A32(1))MEM32(A32(1))=0;RET(DIERR_UNSUPPORTED,2); }
static void dev_Unsupported2(void) { RET(DIERR_UNSUPPORTED,2); }
static void dev_EnumCreatedEffects(void) { RET(DI_OK,4); }
static void dev_SendDeviceData(void) { if(A32(3))MEM32(A32(3))=0;RET(DIERR_UNSUPPORTED,5); }

static void di_init(void) {
    if (g_vt_di) return;
    static const win32hle_shim di[] = {
        { "IDirectInputA::QueryInterface", hle_com_QueryInterface },
        { "IDirectInputA::AddRef", hle_com_AddRef },
        { "IDirectInputA::Release", hle_com_Release },
        { "IDirectInputA::CreateDevice", 0 }, /* patched after */
        { "IDirectInputA::EnumDevices", di_EnumDevices },
        { "IDirectInputA::GetDeviceStatus", di_GetDeviceStatus },
        { "IDirectInputA::RunControlPanel", di_RunControlPanel },
        { "IDirectInputA::Initialize", di_Initialize },
        { 0, 0 }
    };
    static const win32hle_shim dev[] = {
        { "IDirectInputDeviceA::QueryInterface", dev_QueryInterface },
        { "IDirectInputDeviceA::AddRef", hle_com_AddRef },
        { "IDirectInputDeviceA::Release", hle_com_Release },
        { "IDirectInputDeviceA::GetCapabilities", dev_GetCapabilities },
        { "IDirectInputDeviceA::EnumObjects", dev_EnumObjects },
        { "IDirectInputDeviceA::GetProperty", dev_GetProperty },
        { "IDirectInputDeviceA::SetProperty", dev_SetProperty },
        { "IDirectInputDeviceA::Acquire", dev_Acquire },
        { "IDirectInputDeviceA::Unacquire", dev_Unacquire },
        { "IDirectInputDeviceA::GetDeviceState", dev_GetDeviceState },
        { "IDirectInputDeviceA::GetDeviceData", dev_GetDeviceData },
        { "IDirectInputDeviceA::SetDataFormat", dev_SetDataFormat },
        { "IDirectInputDeviceA::SetEventNotification", dev_ok2 },
        { "IDirectInputDeviceA::SetCooperativeLevel", dev_ok3 },
        { "IDirectInputDeviceA::GetObjectInfo", dev_GetObjectInfo },
        { "IDirectInputDeviceA::GetDeviceInfo", dev_GetDeviceInfo },
        { "IDirectInputDeviceA::RunControlPanel", dev_ok3 },
        { "IDirectInputDeviceA::Initialize", dev_Initialize },
        { "IDirectInputDevice2A::CreateEffect", dev_CreateEffect },
        { "IDirectInputDevice2A::EnumEffects", dev_EnumEffects },
        { "IDirectInputDevice2A::GetEffectInfo", dev_GetEffectInfo },
        { "IDirectInputDevice2A::GetForceFeedbackState", dev_GetForceFeedbackState },
        { "IDirectInputDevice2A::SendForceFeedbackCommand", dev_Unsupported2 },
        { "IDirectInputDevice2A::EnumCreatedEffectObjects", dev_EnumCreatedEffects },
        { "IDirectInputDevice2A::Escape", dev_Unsupported2 },
        { "IDirectInputDevice2A::Poll", dev_Poll },
        { "IDirectInputDevice2A::SendDeviceData", dev_SendDeviceData },
        { 0, 0 }
    };
    /* CreateDevice is filled by a second registration; the slot must exist first. */
    g_vt_dev = hle_com_vtable(dev);
    (void)di;
}

static void di_CreateDevice(void); /* defined below, used in the vtable via a thunk */

static void di_CreateDevice(void) {
    di_init();
    uint32_t guid = A32(1), out = A32(2);
    if (!out) RET(DIERR_INVALIDPARAM, 4);
    if (!guid_is(guid, 0x6F1D2B60u) && !guid_is(guid, 0x6F1D2B61u)) {
        MEM32(out) = 0; RET(DIERR_DEVICENOTREG, 4);
    }
    int mouse = guid_is(guid, 0x6F1D2B60u);
    uint32_t obj = hle_com_new(g_vt_dev, 16);
    MEM32(obj + 8) = mouse;
    MEM32(out) = obj;
    fprintf(stderr, "[dinput] CreateDevice %s\n", mouse ? "mouse" : "keyboard");
    RET(DI_OK, 4);
}

static void di_build(void) {
    if (g_vt_di) return;
    di_init();
    static const win32hle_shim di[] = {
        { "IDirectInputA::QueryInterface", di_QueryInterface },
        { "IDirectInputA::AddRef", hle_com_AddRef },
        { "IDirectInputA::Release", hle_com_Release },
        { "IDirectInputA::CreateDevice", di_CreateDevice },
        { "IDirectInputA::EnumDevices", di_EnumDevices },
        { "IDirectInputA::GetDeviceStatus", di_GetDeviceStatus },
        { "IDirectInputA::RunControlPanel", di_RunControlPanel },
        { "IDirectInputA::Initialize", di_Initialize },
        { 0, 0 }
    };
    g_vt_di = hle_com_vtable(di);
}

static void k_DirectInputCreateA(void) {
    di_build();
    uint32_t out = A32(2);
    if (!out) RET(DIERR_INVALIDPARAM, 4);
    uint32_t obj = hle_com_new(g_vt_di, 8);
    MEM32(out) = obj;
    fprintf(stderr, "[dinput] DirectInputCreateA -> 0x%08X\n", obj);
    RET(DI_OK, 4);
}

const win32hle_shim nfspu_dinput[] = {
    { "DirectInputCreateA", k_DirectInputCreateA },
    { "dinput.dll#1", k_DirectInputCreateA },
    { 0, 0 }
};
void nfspu_register_dinput(void) { win32hle_register(nfspu_dinput); }
