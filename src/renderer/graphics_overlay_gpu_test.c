/* Hardware regression for host graphics controls. No game image/assets. */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nfspu_gpu.h"

#define W 320
#define H 240
static const nfspu_gpu_api *gpu;
static uint8_t pixels[W*H*4];
static int failures;
static void check(int ok,const char *name) {
    printf("%s %s\n",ok?"PASS":"FAIL",name);
    if(!ok) { failures++;fprintf(stderr,"FAIL %s; SDL=%s GPU=%s\n",name,SDL_GetError(),gpu?gpu->error():""); }
}
static int pixel(int x,int y,int r,int green,int b) {
    const uint8_t *p=pixels+(y*W+x)*4;
    return abs((int)p[0]-r)<=3&&abs((int)p[1]-green)<=3&&abs((int)p[2]-b)<=3;
}
static int quad(float x,float y,float right,float bottom,uint32_t color) {
    nfspu_gpu_vertex v[]={{x,y,.5f,1,color,0,0,0},{right,y,.5f,1,color,0,1,0},
        {right,bottom,.5f,1,color,0,1,1},{x,bottom,.5f,1,color,0,0,1}};
    const uint32_t index[]={0,1,2,0,2,3};
    nfspu_gpu_state state={0};state.cull=1;state.src_blend=2;state.dst_blend=1;
    return gpu->draw(v,4,index,6,&state);
}
static void drain(void) { nfspu_gpu_event e;while(gpu->poll(&e)) {} }
static void key(SDL_WindowID window,SDL_Scancode sc) {
    SDL_Event e={0};e.type=SDL_EVENT_KEY_DOWN;e.key.windowID=window;e.key.scancode=sc;e.key.down=true;
    check(SDL_PushEvent(&e),"inject keyboard event");
}
static void motion(SDL_WindowID window,float x,float y) {
    SDL_Event e={0};e.type=SDL_EVENT_MOUSE_MOTION;e.motion.windowID=window;e.motion.x=x;e.motion.y=y;
    check(SDL_PushEvent(&e),"inject mouse motion");
}
static void button(SDL_WindowID window,float x,float y,int down) {
    SDL_Event e={0};e.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.windowID=window;e.button.button=SDL_BUTTON_LEFT;e.button.down=down!=0;e.button.x=x;e.button.y=y;
    check(SDL_PushEvent(&e),"inject mouse button");
}
int main(void) {
    gpu=nfspu_gpu_get_api();
    if(!gpu||gpu->abi!=NFSPU_GPU_ABI)return 2;
    /* The test owns this fixture in its isolated build/test working folder. */
    FILE *f=fopen("overlay-settings-test.ini","w");
    if(!f)return 2;
    fprintf(f,"version=1\nwindow_width=960\nwindow_height=540\nfullscreen=0\nvsync=1\nrender_scale=100\naspect=0\ntexture_filter=0\nscaling_filter=1\nshow_stats=0\nwide_camera=0\n");fclose(f);
    check(gpu->load_config("overlay-settings-test.ini"),"load isolated graphics settings");
    if(!gpu->open(W,H)) {fprintf(stderr,"Cannot open GPU: %s\n",gpu->error());return 2;}
    int window_count=0;SDL_Window **windows=SDL_GetWindows(&window_count);
    SDL_Window *window=window_count?windows[0]:NULL;SDL_free(windows);
    if(!window) {gpu->close();return 2;}
    SDL_WindowID id=SDL_GetWindowID(window);drain();

    nfspu_graphics_options next=*gpu->get_options();next.render_scale=200;
    check(gpu->set_options(&next),"queue supersampling change");
    check(gpu->get_options()->render_scale==100,"settings wait for frame boundary");
    check(gpu->present(),"apply settings during presentation");
    check(gpu->get_options()->render_scale==200,"render scale committed");
    gpu->clear(0xFF000000,1);
    check(quad(10.5f,10.5f,110.5f,110.5f,0xFFFF0000),"draw at subpixel edge in supersampled target");
    check(gpu->read_pixels(pixels,W,H),"supersampled framebuffer reads back at logical size");
    check(pixel(50,50,255,0,0)&&pixel(150,150,0,0,0),"supersampling keeps logical geometry positions");
    unsigned edge=pixels[(50*W+10)*4];
    check(edge>80&&edge<180,"200 percent target supplies multiple samples per logical edge");
    f=fopen("overlay-settings-test.ini","r");char saved[2048]={0};if(f){fread(saved,1,sizeof saved-1,f);fclose(f);}
    check(strstr(saved,"render_scale=200")!=NULL,"applied graphics options persist to owned file");
    nfspu_gpu_event applied;int unexpected_camera=0;
    while(gpu->poll(&applied))if(applied.type==7)unexpected_camera|=applied.down!=0;
    check(!unexpected_camera,"ordinary graphics changes do not change game camera");
    next=*gpu->get_options();next.wide_camera=1;check(gpu->set_options(&next)&&gpu->present(),"apply explicit race camera choice");
    int camera_notified=0;while(gpu->poll(&applied))camera_notified|=applied.type==7&&applied.scancode==1&&applied.down;
    check(camera_notified,"only explicit camera changes notify guest camera state");

    gpu->clear(0xFF123456,1);gpu->set_overlay(1);
    check(gpu->overlay_open(),"open graphics panel");
    check(gpu->present(),"render graphics overlay");
    check(gpu->read_pixels(pixels,W,H)&&pixel(10,10,18,52,86)&&pixel(160,120,18,52,86),"overlay does not contaminate guest framebuffer");
    gpu->set_overlay(0);drain();

    key(id,SDL_SCANCODE_F10);
    nfspu_gpu_event event;int reset=0,leaked=0;
    while(gpu->poll(&event)){reset|=event.type==6;leaked|=event.type==2&&event.scancode==SDL_SCANCODE_F10;}
    check(gpu->overlay_open()&&reset&&!leaked,"F10 opens overlay and resets game input without leaking key");
    key(id,SDL_SCANCODE_A);motion(id,10,10);button(id,10,10,1);button(id,10,10,0);
    leaked=0;while(gpu->poll(&event))leaked|=event.type==2||event.type==3||event.type==4||event.type==5;
    check(!leaked,"open overlay absorbs game keyboard and pointer events");
    key(id,SDL_SCANCODE_ESCAPE);reset=0;leaked=0;
    while(gpu->poll(&event)){reset|=event.type==6;leaked|=event.type==2&&event.scancode==SDL_SCANCODE_ESCAPE;}
    check(!gpu->overlay_open()&&reset&&!leaked,"Escape closes overlay and resets input without reaching game");
    key(id,SDL_SCANCODE_B);int got_key=0;while(gpu->poll(&event))got_key|=event.type==2&&event.scancode==SDL_SCANCODE_B;
    check(got_key,"game keyboard input resumes after overlay closes");

    int ww=0,wh=0;SDL_GetWindowSize(window,&ww,&wh);
    float left=((float)ww-(float)wh*4.0f/3.0f)/2.0f;
    motion(id,left,(float)wh/2);int got_motion=0;
    while(gpu->poll(&event))if(event.type==3){got_motion=abs(event.x)<=1&&abs(event.y-H/2)<=1;}
    check(got_motion,"fit-aspect pointer mapping removes pillarbox offset");
    button(id,1,wh/2.0f,1);int got_button=0;while(gpu->poll(&event))got_button|=event.type==4&&event.down;
    check(!got_button,"letterbox-bar clicks do not reach game");

    next=*gpu->get_options();next.aspect=2;next.wide_camera=0;
    check(gpu->set_options(&next)&&gpu->present(),"proportional widescreen settings accepted");
    int dimensions_notified=0;
    while(gpu->poll(&event))if(event.type==7)dimensions_notified=event.x>0&&event.y>0;
    check(dimensions_notified,"graphics event carries actual output size for race mode selection");
    gpu->sync_camera(1);check(!gpu->get_options()->wide_camera,"proportional mode excludes legacy cinematic crop");
    motion(id,left,(float)wh/2);got_motion=0;
    while(gpu->poll(&event))if(event.type==3)got_motion=abs(event.x)<=1&&abs(event.y-H/2)<=1;
    check(got_motion,"proportional wide menus retain fitted pointer mapping");
    next=*gpu->get_options();next.wide_camera=1;
    check(!gpu->set_options(&next),"conflicting cinematic crop rejected in proportional mode");

    next=*gpu->get_options();next.render_scale=123;
    check(!gpu->set_options(&next)&&gpu->get_options()->render_scale==200,"invalid options preserve active settings");
    gpu->close();printf("Graphics overlay GPU regression: %d failures\n",failures);return failures?1:0;
}
