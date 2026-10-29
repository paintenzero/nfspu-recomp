/* Native SDL3 GPU renderer, isolated behind the host-neutral Thrash backend ABI. */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "nfspu_gpu.h"
#include "nfspu_shaders.h"
#include "graphics_overlay.h"
#include <math.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define MAX_TEXTURES 8192
#define MAX_PIPELINES 512
#define VERTEX_BYTES (16u << 20)
#define INDEX_BYTES (4u << 20)
typedef struct { float position[4], color[4], uv[2], fog; } gpu_vertex;
typedef struct { SDL_GPUTexture *gpu; uint32_t w, h; } texture;
typedef struct { uint32_t depth_test,depth_write,blend,src,dst,cull,primitive,depth_func; } pipe_key;
static struct {
    SDL_Window *window;
    int subsystems_acquired;
    SDL_GPUDevice *device;
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUTexture *color, *depth;
    SDL_GPUTexture *presentation, *presentation_depth;
    SDL_GPUShader *vs,*ps;
    SDL_GPUSampler *samplers[4];
    SDL_GPUBuffer *vb,*ib;
    SDL_GPUTransferBuffer *transfer;
    uint32_t vertex_offset,index_offset,transfer_offset;
    struct { pipe_key key; SDL_GPUGraphicsPipeline *gpu; } pipes[MAX_PIPELINES];
    uint32_t npipes,w,h,render_w,render_h,present_w,present_h,white;
    nfspu_graphics_overlay overlay;
    uint32_t reset_input,options_event,camera_changed,camera_sync,hotkey_enter;
    uint64_t fps_started;
    unsigned fps_frames;
    float fps;
    texture textures[MAX_TEXTURES];
    char error[512];
} g;

static nfspu_graphics_options options,pending_options;
static int options_initialized,options_pending;
static char config_path[1200];
static int gpu_set_options(const nfspu_graphics_options *);
static int apply_pending_options(void);
static int gpu_draw_to(const nfspu_gpu_vertex *,uint32_t,const uint32_t *,uint32_t,const nfspu_gpu_state *,SDL_GPUTexture *,SDL_GPUTexture *,uint32_t,uint32_t,uint32_t,uint32_t,int);
static void gpu_set_overlay(int);
static void ensure_options(void){if(!options_initialized){options=nfspu_graphics_defaults();options_initialized=1;}}
static int valid_options(const nfspu_graphics_options *o){
    return o&&o->window_width>=320&&o->window_height>=240&&o->window_width<=7680&&o->window_height<=4320&&
        o->fullscreen<=1&&o->vsync<=1&&(o->render_scale==100||o->render_scale==150||o->render_scale==200||o->render_scale==300)&&
        o->aspect<=2&&o->texture_filter<=2&&o->scaling_filter<=1&&o->show_stats<=1&&o->wide_camera<=1&&!(o->aspect==2&&o->wide_camera);
}
static const nfspu_graphics_options *gpu_get_options(void){ensure_options();return &options;}
static int save_options(void){
    if(!config_path[0])return 1;
    FILE *f=fopen(config_path,"w");if(!f)return 0;
    int ok=fprintf(f,"version=1\nwindow_width=%u\nwindow_height=%u\nfullscreen=%u\nvsync=%u\nrender_scale=%u\naspect=%u\ntexture_filter=%u\nscaling_filter=%u\nshow_stats=%u\nwide_camera=%u\n",
        options.window_width,options.window_height,options.fullscreen,options.vsync,options.render_scale,options.aspect,
        options.texture_filter,options.scaling_filter,options.show_stats,options.wide_camera)>=0;
    return fclose(f)==0&&ok;
}
static int gpu_load_config(const char *path){
    ensure_options();if(!path||strlen(path)>=sizeof config_path)return 0;
    snprintf(config_path,sizeof config_path,"%s",path);FILE *f=fopen(path,"r");if(!f)return 1;
    nfspu_graphics_options loaded=nfspu_graphics_defaults();char line[160],key[80];unsigned value;
    while(fgets(line,sizeof line,f))if(sscanf(line,"%79[^=]=%u",key,&value)==2){
#define LOAD_FIELD(name) if(!strcmp(key,#name))loaded.name=value
        LOAD_FIELD(window_width);else LOAD_FIELD(window_height);else LOAD_FIELD(fullscreen);else LOAD_FIELD(vsync);
        else LOAD_FIELD(render_scale);else LOAD_FIELD(aspect);else LOAD_FIELD(texture_filter);else LOAD_FIELD(scaling_filter);
        else LOAD_FIELD(show_stats);else LOAD_FIELD(wide_camera);
#undef LOAD_FIELD
    }fclose(f);
    if(!valid_options(&loaded)){fprintf(stderr,"[gpu] invalid graphics settings; defaults retained\n");return 0;}
    return gpu_set_options(&loaded);
}

static void gpu_close(void);
static void gpu_clear(uint32_t color, float depth);
static uint32_t gpu_texture_create(uint32_t w,uint32_t h,const uint8_t *rgba);
static int gpu_texture_update(uint32_t id,const uint8_t *rgba);
static int fail(const char *operation) {
    snprintf(g.error,sizeof g.error,"%s: %s",operation,SDL_GetError());
    fprintf(stderr,"[gpu] %s\n",g.error);
    return 0;
}
static const char *gpu_error(void) { return g.error; }
static int command(void) {
    if (!g.device) return 0;
    if (!g.cmd) {
        g.cmd=SDL_AcquireGPUCommandBuffer(g.device);
        g.vertex_offset=g.index_offset=g.transfer_offset=0;
    }
    return g.cmd ? 1 : fail("acquire command buffer");
}
static int submit(void) {
    if (!g.cmd) return 1;
    SDL_GPUCommandBuffer *cmd=g.cmd; g.cmd=NULL;
    return SDL_SubmitGPUCommandBuffer(cmd) ? 1 : fail("submit");
}
static SDL_FColor argb(uint32_t c) {
    return (SDL_FColor){((c>>16)&255)/255.0f,((c>>8)&255)/255.0f,(c&255)/255.0f,(c>>24)/255.0f};
}
static SDL_GPUShader *shader(int fragment) {
    SDL_GPUShaderFormat formats=SDL_GetGPUShaderFormats(g.device);
    SDL_GPUShaderCreateInfo ci={0};
    ci.stage=fragment ? SDL_GPU_SHADERSTAGE_FRAGMENT : SDL_GPU_SHADERSTAGE_VERTEX;
    ci.num_uniform_buffers=1; ci.num_samplers=fragment ? 1:0;
    if(formats & SDL_GPU_SHADERFORMAT_DXIL) {
        ci.format=SDL_GPU_SHADERFORMAT_DXIL; ci.entrypoint="main";
        ci.code=fragment ? ps_dxil:vs_dxil;
        ci.code_size=(fragment ? sizeof ps_dxil:sizeof vs_dxil)-1;
    } else if(formats & SDL_GPU_SHADERFORMAT_SPIRV) {
        ci.format=SDL_GPU_SHADERFORMAT_SPIRV; ci.entrypoint="main";
        ci.code=fragment ? ps_spv:vs_spv;
        ci.code_size=(fragment ? sizeof ps_spv:sizeof vs_spv)-1;
    } else {
        ci.format=SDL_GPU_SHADERFORMAT_MSL;
        ci.entrypoint=fragment ? "ps_main":"vs_main";
        ci.code=msl; ci.code_size=sizeof msl-1;
    }
    return SDL_CreateGPUShader(g.device,&ci);
}
static int targets(uint32_t w,uint32_t h) {
    if(!w||!h||w>16384||h>16384) return 0;
    uint32_t rw=(uint32_t)((uint64_t)w*options.render_scale/100),rh=(uint32_t)((uint64_t)h*options.render_scale/100);
    if(!rw||!rh||rw>16384||rh>16384)return 0;
    if(!submit()) return 0;
    SDL_WaitForGPUIdle(g.device);
    SDL_GPUTextureCreateInfo ci={0};
    ci.type=SDL_GPU_TEXTURETYPE_2D; ci.width=rw;ci.height=rh;
    ci.layer_count_or_depth=1;ci.num_levels=1;ci.sample_count=SDL_GPU_SAMPLECOUNT_1;
    ci.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ci.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;
    SDL_GPUTexture *color=SDL_CreateGPUTexture(g.device,&ci);
    ci.format=SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    ci.usage=SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    SDL_GPUTexture *depth=SDL_CreateGPUTexture(g.device,&ci);
    if(!color||!depth) {
        if(color) SDL_ReleaseGPUTexture(g.device,color);
        if(depth) SDL_ReleaseGPUTexture(g.device,depth);
        return fail("create framebuffer");
    }
    SDL_GPUTexture *old_color=g.color,*old_depth=g.depth;uint32_t old_w=g.render_w,old_h=g.render_h;
    g.color=color;g.depth=depth;g.w=w;g.h=h;g.render_w=rw;g.render_h=rh;
    gpu_clear(0xFF000000,1.0f);
    if(old_color&&command()){
        SDL_GPUBlitInfo b={0};b.source.texture=old_color;b.source.w=old_w;b.source.h=old_h;
        b.destination.texture=g.color;b.destination.w=rw;b.destination.h=rh;b.load_op=SDL_GPU_LOADOP_LOAD;b.filter=SDL_GPU_FILTER_LINEAR;
        SDL_BlitGPUTexture(g.cmd,&b);
    }
    if(old_color)SDL_ReleaseGPUTexture(g.device,old_color);if(old_depth)SDL_ReleaseGPUTexture(g.device,old_depth);
    return submit();
}
static int gpu_open(uint32_t w,uint32_t h) {
    ensure_options();
    if(g.device) return targets(w,h);
    memset(&g,0,sizeof g);
    SDL_SetMainReady();
    if(!SDL_InitSubSystem(SDL_INIT_VIDEO|SDL_INIT_EVENTS)) return fail("SDL3 video initialization");
    g.subsystems_acquired=1;
    g.window=SDL_CreateWindow("Need for Speed: Porsche Unleashed — SDL3 GPU",(int)options.window_width,(int)options.window_height,SDL_WINDOW_RESIZABLE);
    if(!g.window) { fail("create window");gpu_close();return 0; }
    const char *driver=getenv("NFSPU_GPU_DRIVER");
    g.device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL|SDL_GPU_SHADERFORMAT_SPIRV|SDL_GPU_SHADERFORMAT_MSL,
        getenv("NFSPU_GPU_DEBUG")!=NULL,driver&&*driver ? driver:NULL);
    if(!g.device) { fail("create GPU device"); gpu_close(); return 0; }
    if(!SDL_ClaimWindowForGPUDevice(g.device,g.window)) { fail("claim window"); gpu_close();return 0; }
    SDL_GPUPresentMode mode=options.vsync?SDL_GPU_PRESENTMODE_VSYNC:SDL_GPU_PRESENTMODE_IMMEDIATE;
    if(!SDL_WindowSupportsGPUPresentMode(g.device,g.window,mode)) {mode=SDL_GPU_PRESENTMODE_VSYNC;options.vsync=1;}
    if(!SDL_SetGPUSwapchainParameters(g.device,g.window,SDL_GPU_SWAPCHAINCOMPOSITION_SDR,mode)){fail("set presentation mode");gpu_close();return 0;}
    g.vs=shader(0);g.ps=shader(1);
    if(!g.vs||!g.ps) { fail("create shaders");gpu_close();return 0; }
    for(unsigned i=0;i<4;i++) {
        SDL_GPUSamplerCreateInfo ci={0};
        ci.min_filter=ci.mag_filter=(i&1) ? SDL_GPU_FILTER_LINEAR:SDL_GPU_FILTER_NEAREST;
        ci.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        ci.address_mode_u=ci.address_mode_v=ci.address_mode_w=(i&2) ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT:SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        ci.max_lod=0;
        g.samplers[i]=SDL_CreateGPUSampler(g.device,&ci);
        if(!g.samplers[i]) { fail("create sampler");gpu_close();return 0; }
    }
    SDL_GPUBufferCreateInfo bi={SDL_GPU_BUFFERUSAGE_VERTEX,VERTEX_BYTES,0};
    g.vb=SDL_CreateGPUBuffer(g.device,&bi);
    bi.usage=SDL_GPU_BUFFERUSAGE_INDEX;bi.size=INDEX_BYTES;
    g.ib=SDL_CreateGPUBuffer(g.device,&bi);
    SDL_GPUTransferBufferCreateInfo ti={SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,VERTEX_BYTES+INDEX_BYTES,0};
    g.transfer=SDL_CreateGPUTransferBuffer(g.device,&ti);
    if(!g.vb||!g.ib||!g.transfer||!targets(w,h)) { fail("create draw buffers");gpu_close();return 0; }
    const uint8_t white[]={255,255,255,255};
    g.white=gpu_texture_create(1,1,white);
    if(!g.white||!submit()) { gpu_close();return 0; }
    nfspu_overlay_init(&g.overlay,&options);
    g.fps_started=SDL_GetTicksNS();
    if(options.fullscreen&&!SDL_SetWindowFullscreen(g.window,true))options.fullscreen=0;
    g.options_event=1;g.camera_changed=1;
    /* Explicitly show after creation: Windows launchers may supply SW_HIDE in
     * STARTUPINFO, which otherwise makes a successful render invisible. */
    SDL_HideWindow(g.window);
    SDL_SetWindowPosition(g.window,40,40);
    SDL_ShowWindow(g.window);
    SDL_RaiseWindow(g.window);
#ifdef _WIN32
    HWND hwnd=(HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(g.window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,NULL);
    if(hwnd){
        ShowWindow(hwnd,SW_RESTORE);
        SetWindowPos(hwnd,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);
        SetForegroundWindow(hwnd);
        SetWindowPos(hwnd,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
        RECT rect;GetWindowRect(hwnd,&rect);
        fprintf(stderr,"[gpu] native window visible=%d rect=%ld,%ld-%ld,%ld\n",IsWindowVisible(hwnd),rect.left,rect.top,rect.right,rect.bottom);
    }
#endif
    fprintf(stderr,"[gpu] window id=%u flags=0x%llx\n",SDL_GetWindowID(g.window),(unsigned long long)SDL_GetWindowFlags(g.window));
    fprintf(stderr,"[gpu] SDL3 %d, backend %s, %ux%u\n",SDL_GetVersion(),SDL_GetGPUDeviceDriver(g.device),w,h);
    return 1;
}
static void gpu_close(void) {
    nfspu_overlay_destroy(&g.overlay);
    if(g.device) {
        submit();SDL_WaitForGPUIdle(g.device);
        for(unsigned i=0;i<MAX_TEXTURES;i++) if(g.textures[i].gpu) SDL_ReleaseGPUTexture(g.device,g.textures[i].gpu);
        for(unsigned i=0;i<g.npipes;i++) SDL_ReleaseGPUGraphicsPipeline(g.device,g.pipes[i].gpu);
        for(unsigned i=0;i<4;i++) if(g.samplers[i]) SDL_ReleaseGPUSampler(g.device,g.samplers[i]);
        if(g.vb) SDL_ReleaseGPUBuffer(g.device,g.vb);
        if(g.ib) SDL_ReleaseGPUBuffer(g.device,g.ib);
        if(g.transfer) SDL_ReleaseGPUTransferBuffer(g.device,g.transfer);
        if(g.color) SDL_ReleaseGPUTexture(g.device,g.color);
        if(g.depth) SDL_ReleaseGPUTexture(g.device,g.depth);
        if(g.presentation)SDL_ReleaseGPUTexture(g.device,g.presentation);
        if(g.presentation_depth)SDL_ReleaseGPUTexture(g.device,g.presentation_depth);
        if(g.vs) SDL_ReleaseGPUShader(g.device,g.vs);
        if(g.ps) SDL_ReleaseGPUShader(g.device,g.ps);
        if(g.window) SDL_ReleaseWindowFromGPUDevice(g.device,g.window);
        SDL_DestroyGPUDevice(g.device);
    }
    if(g.window) SDL_DestroyWindow(g.window);
    if(g.subsystems_acquired) SDL_QuitSubSystem(SDL_INIT_VIDEO|SDL_INIT_EVENTS);
    char error[sizeof g.error];memcpy(error,g.error,sizeof error);
    memset(&g,0,sizeof g);memcpy(g.error,error,sizeof error);
}
static int gpu_resize(uint32_t w,uint32_t h) {
    if(!g.device) return gpu_open(w,h);
    if(g.w==w&&g.h==h) return 1;
    /* Game mode changes affect logical rendering. Window size belongs to the
     * host overlay and must not jump back to 640x480 when a menu opens. */
    return targets(w,h);
}
static int gpu_set_options(const nfspu_graphics_options *next){
    ensure_options();if(!valid_options(next)){SDL_SetError("invalid graphics options");return fail("graphics settings");}
    if(!g.device){options=*next;return 1;}
    pending_options=*next;options_pending=1;return 1;
}
static void window_options(const nfspu_graphics_options *o){
    SDL_SetWindowFullscreen(g.window,o->fullscreen!=0);
    if(!o->fullscreen)SDL_SetWindowSize(g.window,(int)o->window_width,(int)o->window_height);
    SDL_SetGPUSwapchainParameters(g.device,g.window,SDL_GPU_SWAPCHAINCOMPOSITION_SDR,o->vsync?SDL_GPU_PRESENTMODE_VSYNC:SDL_GPU_PRESENTMODE_IMMEDIATE);
}
static int apply_pending_options(void){
    if(!options_pending)return 1;
    options_pending=0;nfspu_graphics_options before=options,next=pending_options;
    SDL_GPUPresentMode mode=next.vsync?SDL_GPU_PRESENTMODE_VSYNC:SDL_GPU_PRESENTMODE_IMMEDIATE;
    if(!SDL_WindowSupportsGPUPresentMode(g.device,g.window,mode)){
        nfspu_overlay_status(&g.overlay,"PRESENT MODE NOT SUPPORTED; SETTINGS KEPT");return 1;
    }
    if(!submit()||!SDL_WaitForGPUIdle(g.device))return fail("wait before graphics change");
    if(!SDL_SetWindowFullscreen(g.window,next.fullscreen!=0)||
       (!next.fullscreen&&!SDL_SetWindowSize(g.window,(int)next.window_width,(int)next.window_height))||
       !SDL_SetGPUSwapchainParameters(g.device,g.window,SDL_GPU_SWAPCHAINCOMPOSITION_SDR,mode)){
        window_options(&before);nfspu_overlay_status(&g.overlay,"WINDOW CHANGE FAILED; SETTINGS KEPT");return 1;
    }
    options=next;
    if(before.render_scale!=next.render_scale&&!targets(g.w,g.h)){
        options=before;window_options(&before);nfspu_overlay_status(&g.overlay,"RENDER SIZE FAILED; SETTINGS KEPT");return 1;
    }
    nfspu_overlay_applied(&g.overlay,&options);
    nfspu_overlay_status(&g.overlay,save_options()?"APPLIED AND SAVED":"APPLIED; COULD NOT SAVE SETTINGS");
    g.options_event=1;g.camera_changed|=before.wide_camera!=next.wide_camera;g.reset_input=1;
    fprintf(stderr,"[gpu] settings window=%ux%u fullscreen=%u vsync=%u scale=%u aspect=%u filter=%u\n",options.window_width,options.window_height,options.fullscreen,options.vsync,options.render_scale,options.aspect,options.texture_filter);
    return 1;
}
static void gpu_set_overlay(int open){
    if(!g.device)return;
    if(open){nfspu_overlay_open(&g.overlay,&options);g.camera_sync=1;}else nfspu_overlay_close(&g.overlay);
    g.reset_input=1;
}
static int gpu_overlay_open(void){return g.overlay.is_open;}
static void gpu_sync_camera(uint32_t enabled){
    options.wide_camera=options.aspect==2?0:enabled!=0;
    if(options_pending)pending_options.wide_camera=options.wide_camera;
    nfspu_overlay_applied(&g.overlay,&options);
}
static void overlay_action(int action){
    if(action==NFSPU_OVERLAY_APPLY){
        if(gpu_set_options(&g.overlay.draft))nfspu_overlay_status(&g.overlay,"APPLYING...");
        else nfspu_overlay_status(&g.overlay,"INVALID SETTINGS; NOTHING CHANGED");
    }else if(action==NFSPU_OVERLAY_CANCEL)gpu_set_overlay(0);
}
static void gpu_clear(uint32_t color,float depth) {
    if(!command()) return;
    SDL_GPUColorTargetInfo c={0};
    c.texture=g.color;c.clear_color=argb(color);c.load_op=SDL_GPU_LOADOP_CLEAR;c.store_op=SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo d={0};
    d.texture=g.depth;d.clear_depth=depth;d.load_op=SDL_GPU_LOADOP_CLEAR;d.store_op=SDL_GPU_STOREOP_STORE;
    d.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;d.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(g.cmd,&c,1,&d);
    if(pass) SDL_EndGPURenderPass(pass); else fail("clear framebuffer");
}
static SDL_Rect content_rect(uint32_t w,uint32_t h){
    SDL_Rect r={0,0,(int)w,(int)h};
    if(options.aspect!=1&&g.w&&g.h){
        if((uint64_t)w*g.h>(uint64_t)h*g.w){r.w=(int)((uint64_t)h*g.w/g.h);r.x=((int)w-r.w)/2;}
        else{r.h=(int)((uint64_t)w*g.h/g.w);r.y=((int)h-r.h)/2;}
    }return r;
}
static int presentation_targets(uint32_t w,uint32_t h){
    if(g.presentation&&g.present_w==w&&g.present_h==h)return 1;
    if(!submit())return 0;
    SDL_GPUTextureCreateInfo ci={0};ci.type=SDL_GPU_TEXTURETYPE_2D;ci.width=w;ci.height=h;ci.layer_count_or_depth=1;ci.num_levels=1;
    ci.sample_count=SDL_GPU_SAMPLECOUNT_1;ci.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;ci.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;
    SDL_GPUTexture *c=SDL_CreateGPUTexture(g.device,&ci);ci.format=SDL_GPU_TEXTUREFORMAT_D32_FLOAT;ci.usage=SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    SDL_GPUTexture *d=SDL_CreateGPUTexture(g.device,&ci);
    if(!c||!d){if(c)SDL_ReleaseGPUTexture(g.device,c);if(d)SDL_ReleaseGPUTexture(g.device,d);return fail("create presentation targets");}
    if(g.presentation)SDL_ReleaseGPUTexture(g.device,g.presentation);if(g.presentation_depth)SDL_ReleaseGPUTexture(g.device,g.presentation_depth);
    g.presentation=c;g.presentation_depth=d;g.present_w=w;g.present_h=h;return 1;
}
static int overlay_emit(void *unused,const nfspu_gpu_vertex *v,uint32_t n,const uint32_t *i,uint32_t ni){
    (void)unused;nfspu_gpu_state s={0};s.blend_enable=1;s.src_blend=5;s.dst_blend=6;s.cull=1;
    return gpu_draw_to(v,n,i,ni,&s,g.presentation,g.presentation_depth,g.present_w,g.present_h,g.present_w,g.present_h,0);
}
static int gpu_present(void) {
    if(!apply_pending_options())return 0;
    /* Keep overlay uploads in their own arena/command buffer. In particular,
     * never submit an acquired swapchain texture midway through an arena wrap. */
    if(!submit())return 0;
    int pixel_w=0,pixel_h=0;SDL_GetWindowSizeInPixels(g.window,&pixel_w,&pixel_h);
    if(pixel_w<=0||pixel_h<=0)return submit();
    if(!presentation_targets((uint32_t)pixel_w,(uint32_t)pixel_h)||!command())return 0;
    SDL_GPUTexture *swap=NULL;Uint32 w=0,h=0;
    if(!SDL_WaitAndAcquireGPUSwapchainTexture(g.cmd,g.window,&swap,&w,&h)) return fail("acquire swapchain");
    if(swap) {
        /* A swapchain resize can complete between the size query and acquire.
         * Present this acquisition untouched and resize before the next one. */
        if(w!=g.present_w||h!=g.present_h)return submit();
        SDL_GPUColorTargetInfo c={0};c.texture=g.presentation;c.clear_color=(SDL_FColor){0,0,0,1};c.load_op=SDL_GPU_LOADOP_CLEAR;c.store_op=SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo d={0};d.texture=g.presentation_depth;d.clear_depth=1;d.load_op=SDL_GPU_LOADOP_CLEAR;d.store_op=SDL_GPU_STOREOP_STORE;
        d.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;d.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
        SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(g.cmd,&c,1,&d);if(!pass)return fail("clear presentation");SDL_EndGPURenderPass(pass);
        SDL_Rect rect=content_rect(w,h);
        SDL_GPUBlitInfo b={0};
        b.source.texture=g.color;b.source.w=g.render_w;b.source.h=g.render_h;
        b.destination.texture=g.presentation;b.destination.x=rect.x;b.destination.y=rect.y;b.destination.w=rect.w;b.destination.h=rect.h;
        b.load_op=SDL_GPU_LOADOP_LOAD;b.filter=options.scaling_filter?SDL_GPU_FILTER_LINEAR:SDL_GPU_FILTER_NEAREST;
        SDL_BlitGPUTexture(g.cmd,&b);
        uint64_t now=SDL_GetTicksNS();g.fps_frames++;if(now-g.fps_started>=1000000000ull){g.fps=(float)((double)g.fps_frames*1e9/(double)(now-g.fps_started));g.fps_frames=0;g.fps_started=now;}
        nfspu_overlay_stats stats={SDL_GetGPUDeviceDriver(g.device),g.w,g.h,g.render_w,g.render_h,w,h,g.fps};
        if(!nfspu_overlay_render(&g.overlay,&stats,w,h,overlay_emit,NULL))return 0;
        memset(&b,0,sizeof b);b.source.texture=g.presentation;b.source.w=w;b.source.h=h;b.destination.texture=swap;b.destination.w=w;b.destination.h=h;
        b.load_op=SDL_GPU_LOADOP_DONT_CARE;b.filter=SDL_GPU_FILTER_NEAREST;SDL_BlitGPUTexture(g.cmd,&b);
    }
    return submit();
}
static int gpu_texture_update(uint32_t id,const uint8_t *rgba) {
    if(!id||id>=MAX_TEXTURES||!g.textures[id].gpu||!rgba) return 0;
    texture *t=&g.textures[id];
    uint32_t stride=(t->w*4+255)&~255u;
    if((uint64_t)stride*t->h>UINT32_MAX) return 0;
    SDL_GPUTransferBufferCreateInfo ci={SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,stride*t->h,0};
    SDL_GPUTransferBuffer *transfer=SDL_CreateGPUTransferBuffer(g.device,&ci);
    if(!transfer) return fail("create texture upload");
    uint8_t *dst=SDL_MapGPUTransferBuffer(g.device,transfer,false);
    if(!dst) { SDL_ReleaseGPUTransferBuffer(g.device,transfer);return fail("map texture upload"); }
    for(uint32_t y=0;y<t->h;y++) memcpy(dst+y*stride,rgba+(size_t)y*t->w*4,t->w*4);
    SDL_UnmapGPUTransferBuffer(g.device,transfer);
    if(!command()) { SDL_ReleaseGPUTransferBuffer(g.device,transfer);return 0; }
    SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(g.cmd);
    SDL_GPUTextureTransferInfo src={transfer,0,stride/4,t->h};
    SDL_GPUTextureRegion region={0};region.texture=t->gpu;region.w=t->w;region.h=t->h;region.d=1;
    SDL_UploadToGPUTexture(copy,&src,&region,true);
    SDL_EndGPUCopyPass(copy);
    /* SDL defers physical destruction until commands using this buffer finish. */
    SDL_ReleaseGPUTransferBuffer(g.device,transfer);
    return 1;
}
static uint32_t gpu_texture_create(uint32_t w,uint32_t h,const uint8_t *rgba) {
    if(!g.device||!w||!h||w>16384||h>16384) return 0;
    uint32_t id=1;while(id<MAX_TEXTURES&&g.textures[id].gpu) id++;
    if(id==MAX_TEXTURES) return 0;
    SDL_GPUTextureCreateInfo ci={0};
    ci.type=SDL_GPU_TEXTURETYPE_2D;ci.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ci.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;ci.width=w;ci.height=h;ci.layer_count_or_depth=1;ci.num_levels=1;ci.sample_count=SDL_GPU_SAMPLECOUNT_1;
    g.textures[id]=(texture){SDL_CreateGPUTexture(g.device,&ci),w,h};
    if(!g.textures[id].gpu) { fail("create texture");return 0; }
    if(rgba&&!gpu_texture_update(id,rgba)) {
        SDL_ReleaseGPUTexture(g.device,g.textures[id].gpu);g.textures[id].gpu=NULL;return 0;
    }
    return id;
}
static void gpu_texture_destroy(uint32_t id) {
    if(id&&id<MAX_TEXTURES&&g.textures[id].gpu&&id!=g.white) {
        SDL_ReleaseGPUTexture(g.device,g.textures[id].gpu);memset(&g.textures[id],0,sizeof(texture));
    }
}
static SDL_GPUBlendFactor blend(uint32_t v) {
    static const SDL_GPUBlendFactor values[]={SDL_GPU_BLENDFACTOR_ONE,SDL_GPU_BLENDFACTOR_ZERO,SDL_GPU_BLENDFACTOR_ONE,
        SDL_GPU_BLENDFACTOR_SRC_COLOR,SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR,SDL_GPU_BLENDFACTOR_SRC_ALPHA,
        SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,SDL_GPU_BLENDFACTOR_DST_ALPHA,SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA,
        SDL_GPU_BLENDFACTOR_DST_COLOR,SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR,SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE};
    return v<sizeof values/sizeof *values ? values[v]:SDL_GPU_BLENDFACTOR_ONE;
}
static SDL_GPUGraphicsPipeline *pipeline(const nfspu_gpu_state *s) {
    pipe_key key={s->depth_test,s->depth_write,s->blend_enable,s->src_blend,s->dst_blend,s->cull,s->primitive,s->depth_func};
    for(unsigned i=0;i<g.npipes;i++) if(!memcmp(&key,&g.pipes[i].key,sizeof key)) return g.pipes[i].gpu;
    if(g.npipes==MAX_PIPELINES) { SDL_SetError("pipeline cache full");fail("create pipeline");return NULL; }
    SDL_GPUVertexBufferDescription vb={0,sizeof(gpu_vertex),SDL_GPU_VERTEXINPUTRATE_VERTEX,0};
    SDL_GPUVertexAttribute attrs[]={
        {0,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,offsetof(gpu_vertex,position)},
        {1,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,offsetof(gpu_vertex,color)},
        {2,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,offsetof(gpu_vertex,uv)},
        {3,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,offsetof(gpu_vertex,fog)}};
    SDL_GPUColorTargetDescription color={0};color.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    color.blend_state.enable_blend=s->blend_enable!=0;
    color.blend_state.src_color_blendfactor=blend(s->src_blend);
    color.blend_state.dst_color_blendfactor=blend(s->dst_blend);
    color.blend_state.src_alpha_blendfactor=blend(s->src_blend);
    color.blend_state.dst_alpha_blendfactor=blend(s->dst_blend);
    color.blend_state.color_blend_op=color.blend_state.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
    SDL_GPUGraphicsPipelineCreateInfo ci={0};ci.vertex_shader=g.vs;ci.fragment_shader=g.ps;
    ci.vertex_input_state=(SDL_GPUVertexInputState){&vb,1,attrs,4};
    ci.primitive_type=s->primitive==1 ? SDL_GPU_PRIMITIVETYPE_LINELIST:s->primitive==2 ? SDL_GPU_PRIMITIVETYPE_POINTLIST:SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    ci.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;
    ci.rasterizer_state.cull_mode=s->cull==2 ? SDL_GPU_CULLMODE_FRONT:s->cull==3 ? SDL_GPU_CULLMODE_BACK:SDL_GPU_CULLMODE_NONE;
    ci.rasterizer_state.front_face=SDL_GPU_FRONTFACE_CLOCKWISE;ci.rasterizer_state.enable_depth_clip=true;
    ci.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;
    ci.depth_stencil_state.enable_depth_test=s->depth_test!=0;ci.depth_stencil_state.enable_depth_write=s->depth_write!=0;
    static const SDL_GPUCompareOp cmp[]={SDL_GPU_COMPAREOP_LESS_OR_EQUAL,SDL_GPU_COMPAREOP_NEVER,
        SDL_GPU_COMPAREOP_LESS,SDL_GPU_COMPAREOP_EQUAL,SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        SDL_GPU_COMPAREOP_GREATER,SDL_GPU_COMPAREOP_NOT_EQUAL,SDL_GPU_COMPAREOP_GREATER_OR_EQUAL,SDL_GPU_COMPAREOP_ALWAYS};
    ci.depth_stencil_state.compare_op=cmp[s->depth_func<=8 ? s->depth_func:0];
    ci.target_info.color_target_descriptions=&color;ci.target_info.num_color_targets=1;
    ci.target_info.has_depth_stencil_target=true;ci.target_info.depth_stencil_format=SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    SDL_GPUGraphicsPipeline *p=SDL_CreateGPUGraphicsPipeline(g.device,&ci);
    if(p) { g.pipes[g.npipes].key=key;g.pipes[g.npipes++].gpu=p; } else fail("create pipeline");
    return p;
}
static int gpu_draw_to(const nfspu_gpu_vertex *vertices,uint32_t nv,const uint32_t *indices,uint32_t ni,const nfspu_gpu_state *s,
                      SDL_GPUTexture *target,SDL_GPUTexture *depth_target,uint32_t logical_w,uint32_t logical_h,uint32_t target_w,uint32_t target_h,int is_game) {
    if(!g.device||!s||!vertices||!nv||nv>VERTEX_BYTES/sizeof(gpu_vertex)||ni>INDEX_BYTES/4||(!indices&&ni)) return 0;
    for(uint32_t i=0;i<ni;i++) if(indices[i]>=nv) return 0;
    SDL_GPUGraphicsPipeline *p=pipeline(s);if(!p||!command()) return 0;
    uint32_t bytes=nv*sizeof(gpu_vertex);
    uint32_t index_bytes=ni*4;
    /* Append into one frame-sized upload allocation. Cycling a 20 MB buffer
     * for each tiny Thrash draw would otherwise allocate gigabytes per frame. */
    if(g.vertex_offset+bytes>VERTEX_BYTES||g.index_offset+index_bytes>INDEX_BYTES||
       g.transfer_offset+bytes+index_bytes>VERTEX_BYTES+INDEX_BYTES) {
        if(!submit()||!command()) return 0;
    }
    uint8_t *mapped=SDL_MapGPUTransferBuffer(g.device,g.transfer,g.transfer_offset==0);
    gpu_vertex *data=(gpu_vertex *)(mapped ? mapped+g.transfer_offset:NULL);
    if(!data) return fail("map vertices");
    for(uint32_t i=0;i<nv;i++) {
        const nfspu_gpu_vertex *v=&vertices[i];gpu_vertex *d=&data[i];
        d->position[0]=v->x;d->position[1]=v->y;d->position[2]=v->z;d->position[3]=v->rhw;
        SDL_FColor c=argb(v->color);d->color[0]=c.r;d->color[1]=c.g;d->color[2]=c.b;d->color[3]=c.a;
        d->uv[0]=v->u;d->uv[1]=v->v;d->fog=(v->specular>>24)/255.0f;
    }
    if(ni) memcpy((uint8_t *)data+bytes,indices,index_bytes);
    SDL_UnmapGPUTransferBuffer(g.device,g.transfer);
    SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(g.cmd);
    SDL_GPUTransferBufferLocation src={g.transfer,g.transfer_offset};SDL_GPUBufferRegion dst={g.vb,g.vertex_offset,bytes};
    SDL_UploadToGPUBuffer(copy,&src,&dst,g.vertex_offset==0);
    if(ni) { src.offset+=bytes;dst.buffer=g.ib;dst.offset=g.index_offset;dst.size=index_bytes;SDL_UploadToGPUBuffer(copy,&src,&dst,g.index_offset==0); }
    SDL_EndGPUCopyPass(copy);
    SDL_GPUColorTargetInfo color={0};color.texture=target;color.load_op=SDL_GPU_LOADOP_LOAD;color.store_op=SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth={0};depth.texture=depth_target;depth.load_op=SDL_GPU_LOADOP_LOAD;depth.store_op=SDL_GPU_STOREOP_STORE;
    depth.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;depth.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
    float view[4]={(float)logical_w,(float)logical_h,0,0};SDL_FColor fog=argb(s->fog_color);
    float effects[12]={fog.r,fog.g,fog.b,fog.a,(float)s->alpha_test,s->alpha_ref/255.0f,(float)s->fog_enable,
        (s->alpha_modulate||!s->texture) ? 1.0f:0.0f,(float)s->fog_mode,s->fog_density,s->fog_near,s->fog_far};
    SDL_PushGPUVertexUniformData(g.cmd,0,view,sizeof view);SDL_PushGPUFragmentUniformData(g.cmd,0,effects,sizeof effects);
    SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(g.cmd,&color,1,&depth);
    if(!pass) return fail("begin draw");
    SDL_BindGPUGraphicsPipeline(pass,p);
    SDL_GPUViewport vp={0,0,(float)target_w,(float)target_h,0,1};SDL_SetGPUViewport(pass,&vp);
    if(s->scissor_enable) {
        SDL_Rect rect={(int)((uint64_t)s->scissor_x*target_w/logical_w),(int)((uint64_t)s->scissor_y*target_h/logical_h),
            (int)((uint64_t)s->scissor_w*target_w/logical_w),(int)((uint64_t)s->scissor_h*target_h/logical_h)};
        SDL_SetGPUScissor(pass,&rect);
    }
    SDL_GPUBufferBinding vb={g.vb,g.vertex_offset};SDL_BindGPUVertexBuffers(pass,0,&vb,1);
    uint32_t tid=s->texture;if(!tid||tid>=MAX_TEXTURES||!g.textures[tid].gpu) tid=g.white;
    int linear=s->filter_linear!=0;if(is_game&&options.texture_filter)linear=options.texture_filter==2;
    SDL_GPUTextureSamplerBinding texture={g.textures[tid].gpu,g.samplers[(linear?1:0)|(s->address_wrap?2:0)]};
    SDL_BindGPUFragmentSamplers(pass,0,&texture,1);
    if(ni) { SDL_GPUBufferBinding ib={g.ib,g.index_offset};SDL_BindGPUIndexBuffer(pass,&ib,SDL_GPU_INDEXELEMENTSIZE_32BIT);SDL_DrawGPUIndexedPrimitives(pass,ni,1,0,0,0); }
    else SDL_DrawGPUPrimitives(pass,nv,1,0,0);
    SDL_EndGPURenderPass(pass);
    g.vertex_offset+=bytes;g.index_offset+=index_bytes;g.transfer_offset+=bytes+index_bytes;
    return 1;
}
static int gpu_draw(const nfspu_gpu_vertex *v,uint32_t n,const uint32_t *i,uint32_t ni,const nfspu_gpu_state *s){
    return gpu_draw_to(v,n,i,ni,s,g.color,g.depth,g.w,g.h,g.render_w,g.render_h,1);
}
static int gpu_poll(nfspu_gpu_event *event) {
    if(!event||!g.window) return 0;
    SDL_Event e;
notifications:
    if(g.reset_input){g.reset_input=0;memset(event,0,sizeof *event);event->type=6;return 1;}
    if(g.options_event){g.options_event=0;memset(event,0,sizeof *event);event->type=7;event->scancode=options.wide_camera;event->down=g.camera_changed;g.camera_changed=0;
        SDL_GetWindowSizeInPixels(g.window,&event->x,&event->y);return 1;}
    if(g.camera_sync){g.camera_sync=0;memset(event,0,sizeof *event);event->type=8;return 1;}
    while(SDL_PollEvent(&e)) {
        memset(event,0,sizeof *event);
        int ww=1,wh=1;SDL_GetWindowSize(g.window,&ww,&wh);
        if(ww<=0||wh<=0)continue;
        SDL_Rect rect=content_rect((uint32_t)ww,(uint32_t)wh);
        float sx=rect.w>0?(float)g.w/rect.w:1,sy=rect.h>0?(float)g.h/rect.h:1;
        if(e.type==SDL_EVENT_KEY_DOWN||e.type==SDL_EVENT_KEY_UP){
            SDL_Scancode sc=e.key.scancode;
            if(sc==SDL_SCANCODE_F10){if(e.key.down&&!e.key.repeat)gpu_set_overlay(!g.overlay.is_open);goto notifications;}
            if(sc==SDL_SCANCODE_RETURN&&!e.key.down&&g.hotkey_enter){g.hotkey_enter=0;goto notifications;}
            if(sc==SDL_SCANCODE_F11||(sc==SDL_SCANCODE_RETURN&&(e.key.mod&SDL_KMOD_ALT))){
                if(e.key.down&&!e.key.repeat){nfspu_graphics_options next=options;next.fullscreen=!next.fullscreen;gpu_set_options(&next);g.reset_input=1;if(sc==SDL_SCANCODE_RETURN)g.hotkey_enter=1;}
                goto notifications;
            }
            if(g.overlay.is_open){if(e.key.down)overlay_action(nfspu_overlay_key(&g.overlay,sc));goto notifications;}
        }
        if(g.overlay.is_open&&(e.type==SDL_EVENT_MOUSE_MOTION||e.type==SDL_EVENT_MOUSE_BUTTON_DOWN||e.type==SDL_EVENT_MOUSE_BUTTON_UP||e.type==SDL_EVENT_MOUSE_WHEEL)){
            float mx=0,my=0;SDL_GetMouseState(&mx,&my);
            if(e.type==SDL_EVENT_MOUSE_MOTION){mx=e.motion.x;my=e.motion.y;}
            if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN||e.type==SDL_EVENT_MOUSE_BUTTON_UP){mx=e.button.x;my=e.button.y;}
            int pw=0,ph=0;SDL_GetWindowSizeInPixels(g.window,&pw,&ph);
            uint32_t button=(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN||e.type==SDL_EVENT_MOUSE_BUTTON_UP)?e.button.button:0;
            int down=e.type==SDL_EVENT_MOUSE_BUTTON_DOWN,wheel=e.type==SDL_EVENT_MOUSE_WHEEL?(int)e.wheel.y:0;
            overlay_action(nfspu_overlay_pointer(&g.overlay,(int)(mx*pw/ww),(int)(my*ph/wh),button,down,wheel));goto notifications;
        }
        switch(e.type) {
        case SDL_EVENT_QUIT: case SDL_EVENT_WINDOW_CLOSE_REQUESTED:event->type=1;return 1;
        case SDL_EVENT_WINDOW_FOCUS_LOST:g.reset_input=1;goto notifications;
        case SDL_EVENT_KEY_DOWN:case SDL_EVENT_KEY_UP:event->type=2;event->scancode=e.key.scancode;event->down=e.key.down;return 1;
        case SDL_EVENT_MOUSE_MOTION:event->type=3;event->x=(int32_t)((e.motion.x-rect.x)*sx);event->y=(int32_t)((e.motion.y-rect.y)*sy);event->dx=(int32_t)(e.motion.xrel*sx);event->dy=(int32_t)(e.motion.yrel*sy);return 1;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:case SDL_EVENT_MOUSE_BUTTON_UP:
            if(e.button.down&&(e.button.x<rect.x||e.button.y<rect.y||e.button.x>=rect.x+rect.w||e.button.y>=rect.y+rect.h))continue;
            event->type=4;event->button=e.button.button;event->down=e.button.down;event->x=(int32_t)((e.button.x-rect.x)*sx);event->y=(int32_t)((e.button.y-rect.y)*sy);return 1;
        case SDL_EVENT_MOUSE_WHEEL:event->type=5;event->dx=(int32_t)e.wheel.x;event->dy=(int32_t)e.wheel.y;return 1;
        default:break;
        }
    }
    return 0;
}
static int gpu_read_pixels(uint8_t *rgba,uint32_t w,uint32_t h) {
    if(!rgba||w!=g.w||h!=g.h||!command()) return 0;
    uint32_t stride=(w*4+255)&~255u;
    SDL_GPUTransferBufferCreateInfo ci={SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,stride*h,0};
    SDL_GPUTransferBuffer *download=SDL_CreateGPUTransferBuffer(g.device,&ci);if(!download)return fail("create download");
    SDL_GPUTexture *read_texture=g.color,*downsample=NULL;
    if(w!=g.render_w||h!=g.render_h){
        SDL_GPUTextureCreateInfo ci={0};ci.type=SDL_GPU_TEXTURETYPE_2D;ci.width=w;ci.height=h;ci.layer_count_or_depth=1;ci.num_levels=1;
        ci.sample_count=SDL_GPU_SAMPLECOUNT_1;ci.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;ci.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;
        downsample=SDL_CreateGPUTexture(g.device,&ci);if(!downsample){SDL_ReleaseGPUTransferBuffer(g.device,download);return fail("create logical readback");}
        SDL_GPUBlitInfo b={0};b.source.texture=g.color;b.source.w=g.render_w;b.source.h=g.render_h;b.destination.texture=downsample;b.destination.w=w;b.destination.h=h;
        b.load_op=SDL_GPU_LOADOP_DONT_CARE;b.filter=SDL_GPU_FILTER_LINEAR;SDL_BlitGPUTexture(g.cmd,&b);read_texture=downsample;
    }
    SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(g.cmd);
    SDL_GPUTextureRegion src={0};src.texture=read_texture;src.w=w;src.h=h;src.d=1;
    SDL_GPUTextureTransferInfo dst={download,0,stride/4,h};SDL_DownloadFromGPUTexture(copy,&src,&dst);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(g.cmd);g.cmd=NULL;
    if(!fence) { if(downsample)SDL_ReleaseGPUTexture(g.device,downsample);SDL_ReleaseGPUTransferBuffer(g.device,download);return fail("submit readback"); }
    int ok=SDL_WaitForGPUFences(g.device,true,&fence,1);
    uint8_t *data=ok ? SDL_MapGPUTransferBuffer(g.device,download,false):NULL;
    if(data) {
        for(uint32_t y=0;y<h;y++) memcpy(rgba+(size_t)y*w*4,data+y*stride,w*4);
        SDL_UnmapGPUTransferBuffer(g.device,download);
    } else ok=fail("read framebuffer");
    SDL_ReleaseGPUFence(g.device,fence);SDL_ReleaseGPUTransferBuffer(g.device,download);
    if(downsample)SDL_ReleaseGPUTexture(g.device,downsample);
    return ok;
}
static const nfspu_gpu_api api={NFSPU_GPU_ABI,gpu_open,gpu_close,gpu_resize,gpu_clear,gpu_present,gpu_texture_create,gpu_texture_update,gpu_texture_destroy,gpu_draw,gpu_poll,gpu_read_pixels,gpu_error,gpu_get_options,gpu_set_options,gpu_set_overlay,gpu_overlay_open,gpu_load_config,gpu_sync_camera};
NFSPU_GPU_EXPORT const nfspu_gpu_api *nfspu_gpu_get_api(void) { return &api; }
