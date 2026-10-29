/* Real SDL3 -> GPU event pump -> Win32 messages/Device2 immediate state.
 * No game image, saved bindings, or physical key injection is required. */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "win32hle.h"
#include "host.h"
#include "thrash.h"
#include "nfspu_gpu.h"
void nfspu_register_dinput(void);
const recomp_dispatch_entry_t recomp_dispatch_table[]={{0,NULL}};
const uint32_t recomp_dispatch_count=0;
static int failures;
#define CHECK(c,m) do{int ok=(c)!=0;printf("%s %s\n",ok?"PASS":"FAIL",m);if(!ok)failures++;}while(0)
static uint32_t invoke(uint32_t va,unsigned n,const uint32_t *args){
    mach_enter();uint32_t before=g_esp;
    for(unsigned i=n;i;i--)PUSH32(g_esp,args[i-1]);PUSH32(g_esp,RECOMP_RETADDR);
    recomp_func_t f=recomp_lookup_import(va);if(!f){fprintf(stderr,"missing method %08X\n",va);exit(2);}f();
    if(g_esp!=before){fprintf(stderr,"FAIL stack cleanup at %08X\n",va);failures++;}g_esp=before;uint32_t result=g_eax;mach_leave();return result;
}
static uint32_t named(const char *name,unsigned n,const uint32_t *a){return invoke(hle_resolve(name),n,a);}
static uint32_t device_call(uint32_t dev,unsigned slot,unsigned extra,const uint32_t *a){
    uint32_t args[5]={dev};for(unsigned i=0;i<extra;i++)args[i+1]=a[i];
    return invoke(MEM32(MEM32(dev)+slot*4),extra+1,args);
}
static void push_key(SDL_WindowID id,SDL_Scancode sc,int down){
    SDL_Event e={0};e.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;e.key.windowID=id;e.key.scancode=sc;e.key.down=down!=0;
    if(!SDL_PushEvent(&e))exit(2);
}
static void clear_messages(void){uint32_t m,w,l;while(hle_msg_pop(&m,&w,&l)){} }
static int key_message(uint32_t msg,uint32_t vk,uint32_t scan,int extended){
    uint32_t m,w,l;int found=0;
    while(hle_msg_pop(&m,&w,&l))if(m==msg&&w==vk&&((l>>16)&255)==scan&&!!(l&(1u<<24))==extended)found=1;
    return found;
}
int main(int argc,char **argv){
    (void)argc;recomp_host_init();nfspu_register_dinput();nfspu_thrash_register(argv[0]);
    uint32_t winargs[]={0,hle_guest_strdup("STATIC"),hle_guest_strdup("Keyboard test"),0x10000000,0,0,640,480,0,0,0x400000,0};
    uint32_t hwnd=named("CreateWindowExA",12,winargs);uint32_t show[]={hwnd,1};named("ShowWindow",2,show);named("SetFocus",1,&hwnd);
    CHECK(hwnd&&named("GetFocus",0,NULL)==hwnd,"guest window keyboard focus");
    uint32_t mode[]={1,2,1};CHECK(named("_THRASH_setvideomode@12",3,mode)==1,"open real SDL3 input bridge");
    int nwin=0;SDL_Window **windows=SDL_GetWindows(&nwin);SDL_WindowID id=nwin?SDL_GetWindowID(windows[0]):0;SDL_free(windows);
    if(!id)return 2;nfspu_thrash_pump();clear_messages();
    uint32_t out=hle_va(hle_alloc(4)),create[]={0,0x700,out,0};CHECK(named("DirectInputCreateA",4,create)==0,"create input interface");
    uint32_t di=MEM32(out),*guid=hle_alloc(16);guid[0]=0x6F1D2B61;guid[1]=0x11CFD5A0;guid[2]=0x4544C7BF;guid[3]=0x5453;
    uint32_t make[]={di,hle_va(guid),out,0};CHECK(named("IDirectInputA::CreateDevice",4,make)==0,"create keyboard");uint32_t dev=MEM32(out);
    uint32_t *iid=hle_alloc(16);iid[0]=0x5944E682;iid[1]=0x11CFC92E;iid[2]=0x4544C7BF;iid[3]=0x5453;
    uint32_t qi[]={hle_va(iid),out};CHECK(device_call(dev,0,2,qi)==0&&MEM32(out)==dev,"query Device2 interface used by race");
    uint32_t *format=hle_alloc(24),*objects=hle_alloc(256*16);format[0]=24;format[1]=16;format[2]=2;format[3]=256;format[4]=256;format[5]=hle_va(objects);
    for(uint32_t i=0;i<256;i++){objects[i*4+1]=i;objects[i*4+2]=0x8000000Cu|(i<<8);}
    uint32_t fmt=hle_va(format);CHECK(device_call(dev,11,1,&fmt)==0,"accept original keyboard data format");
    CHECK(device_call(dev,25,0,NULL)==0x8007000Cu,"unacquired race Poll reports reacquire requirement");
    CHECK(device_call(dev,7,0,NULL)==0,"acquire keyboard");
    uint32_t data=hle_va(hle_alloc(256)),read[]={256,data},refs=MEM32(dev+4);
    push_key(id,SDL_SCANCODE_UP,1);CHECK(device_call(dev,25,0,NULL)==0,"race vtable Poll succeeds");
    CHECK(device_call(dev,9,2,read)==0&&MEM8(data+0xC8)==0x80,"SDL Up reaches race DIK_C8 byte");
    CHECK(key_message(0x100,0x26,0x48,1),"Up Win32 message carries make code 48 and E0 extension");
    CHECK(device_call(dev,9,2,read)==0&&MEM8(data+0xC8)==0x80,"held key survives repeated state reads");
    for(int i=0;i<10;i++)device_call(dev,25,0,NULL);
    CHECK(MEM32(dev+4)==refs,"Poll never acts as adjacent AddRef");
    push_key(id,SDL_SCANCODE_UP,0);device_call(dev,25,0,NULL);device_call(dev,9,2,read);
    CHECK(MEM8(data+0xC8)==0,"key release reaches race state");
    CHECK(key_message(0x101,0x26,0x48,1),"Up key-up message retains E0 scan encoding");
    push_key(id,SDL_SCANCODE_W,1);push_key(id,SDL_SCANCODE_KP_8,1);device_call(dev,25,0,NULL);device_call(dev,9,2,read);
    CHECK(MEM8(data+0x11)==0x80&&MEM8(data+0x48)==0x80&&!MEM8(data+0xC8),"W and keypad 8 remain distinct from extended Up");
    CHECK(key_message(0x100,0x57,0x11,0),"W Win32 message addresses the game's W scan slot");
    push_key(id,SDL_SCANCODE_W,0);push_key(id,SDL_SCANCODE_KP_8,0);device_call(dev,25,0,NULL);device_call(dev,9,2,read);
    push_key(id,SDL_SCANCODE_A,1);push_key(id,SDL_SCANCODE_A,0);device_call(dev,25,0,NULL);device_call(dev,9,2,read);
    CHECK(MEM8(data+0x1E)==0x80,"short key tap is seen for one device sample");device_call(dev,9,2,read);CHECK(!MEM8(data+0x1E),"short tap does not become stuck");
    push_key(id,SDL_SCANCODE_LSHIFT,1);push_key(id,SDL_SCANCODE_RSHIFT,1);device_call(dev,25,0,NULL);push_key(id,SDL_SCANCODE_LSHIFT,0);device_call(dev,25,0,NULL);
    uint32_t shift=0x10;CHECK(named("GetAsyncKeyState",1,&shift)&0x8000,"releasing left Shift preserves held right Shift");
    push_key(id,SDL_SCANCODE_RSHIFT,0);device_call(dev,25,0,NULL);CHECK(!(named("GetAsyncKeyState",1,&shift)&0x8000),"generic Shift releases after both sides");
    push_key(id,SDL_SCANCODE_UP,1);device_call(dev,25,0,NULL);device_call(dev,9,2,read);
    const nfspu_gpu_api *gpu=nfspu_gpu_get_api();gpu->set_overlay(1);device_call(dev,25,0,NULL);device_call(dev,9,2,read);CHECK(!MEM8(data+0xC8),"overlay opening clears held driving input");
    push_key(id,SDL_SCANCODE_W,1);device_call(dev,25,0,NULL);device_call(dev,9,2,read);CHECK(!MEM8(data+0x11),"overlay keyboard does not reach race device");
    gpu->set_overlay(0);device_call(dev,25,0,NULL);push_key(id,SDL_SCANCODE_UP,1);device_call(dev,25,0,NULL);device_call(dev,9,2,read);CHECK(MEM8(data+0xC8)==0x80,"driving input resumes after overlay closes");
    SDL_Event lost={0};lost.type=SDL_EVENT_WINDOW_FOCUS_LOST;lost.window.windowID=id;SDL_PushEvent(&lost);device_call(dev,25,0,NULL);device_call(dev,9,2,read);CHECK(!MEM8(data+0xC8),"focus loss releases held input");
    nfspu_thrash_shutdown();printf("Keyboard integration: %d failures\n",failures);return failures?1:0;
}
