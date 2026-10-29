/* Hardware-backed renderer checks: no guest/game data required. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "nfspu_gpu.h"
#define W 320
#define H 240
static const nfspu_gpu_api *gpu;
static uint8_t pixels[W*H*4];
static int check(int x,int y,int r,int g,int b,const char *label) {
    uint8_t *p=&pixels[(y*W+x)*4];
    if(abs((int)p[0]-r)>3||abs((int)p[1]-g)>3||abs((int)p[2]-b)>3) {
        fprintf(stderr,"FAIL %s: pixel %d,%d=%u,%u,%u expected %d,%d,%d\n",label,x,y,p[0],p[1],p[2],r,g,b);return 0;
    }
    printf("PASS %s\n",label);return 1;
}
static int quad(int x0,int y0,int x1,int y1,float z,uint32_t color,nfspu_gpu_state *state) {
    nfspu_gpu_vertex v[]={
        {(float)x0,(float)y0,z,1,color,0xFF000000,0,0},
        {(float)x1,(float)y0,z,1,color,0xFF000000,1,0},
        {(float)x1,(float)y1,z,1,color,0xFF000000,1,1},
        {(float)x0,(float)y1,z,1,color,0xFF000000,0,1}};
    const uint32_t i[]={0,1,2,0,2,3};
    return gpu->draw(v,4,i,6,state);
}
static int fog_rect(int x0,int x1,float left_depth,float right_depth,uint32_t color,nfspu_gpu_state *state) {
    nfspu_gpu_vertex v[]={
        {(float)x0,0,.5f,1.0f/left_depth,color,0,0,0},
        {(float)x1,0,.5f,1.0f/right_depth,color,0,1,0},
        {(float)x1,H,.5f,1.0f/right_depth,color,0,1,1},
        {(float)x0,H,.5f,1.0f/left_depth,color,0,0,1}};
    const uint32_t i[]={0,1,2,0,2,3};
    return gpu->draw(v,4,i,6,state);
}
int main(int argc,char **argv) {
    int hold=argc>1 ? atoi(argv[1]):0;
    gpu=nfspu_gpu_get_api();
    if(!gpu||gpu->abi!=NFSPU_GPU_ABI||!gpu->open(W,H)) {
        fprintf(stderr,"GPU initialization failed: %s\n",gpu?gpu->error():"ABI mismatch");return 1;
    }
    int ok=1;
    uint8_t green[]={0,255,0,255};
    uint32_t tex=gpu->texture_create(1,1,green);
    nfspu_gpu_state state={0};state.texture=tex;state.depth_test=1;state.depth_write=1;state.src_blend=2;state.dst_blend=1;
    gpu->clear(0xFF102030,1);
    ok &= quad(40,40,280,200,0.3f,0xFFFFFFFF,&state);
    state.texture=0;
    ok &= quad(40,40,280,200,0.8f,0xFFFF0000,&state);
    ok &= gpu->read_pixels(pixels,W,H);
    ok &= check(10,10,16,32,48,"clear color");
    ok &= check(160,120,0,255,0,"texture sampling and depth rejection");

    state.depth_test=state.depth_write=0;state.blend_enable=1;state.src_blend=5;state.dst_blend=6;
    ok &= quad(40,40,280,200,0.2f,0x80FF0000,&state);
    ok &= gpu->read_pixels(pixels,W,H);
    ok &= check(160,120,128,127,0,"alpha blending");

    gpu->clear(0xFF000000,1);state.blend_enable=0;state.alpha_test=1;state.alpha_ref=128;
    ok &= quad(40,40,280,200,0.2f,0x40FFFFFF,&state);
    ok &= gpu->read_pixels(pixels,W,H);
    ok &= check(160,120,0,0,0,"alpha-test discard");

    gpu->clear(0xFF000000,1);state.alpha_test=0;state.texture=tex;
    uint8_t red[]={255,0,0,255},blue[]={0,0,255,255};
    ok &= gpu->texture_update(tex,red);ok &= quad(0,0,160,H,0.2f,0xFFFFFFFF,&state);
    ok &= gpu->texture_update(tex,blue);ok &= quad(160,0,W,H,0.2f,0xFFFFFFFF,&state);
    ok &= gpu->read_pixels(pixels,W,H);
    ok &= check(80,120,255,0,0,"texture update preserves earlier draw");
    ok &= check(240,120,0,0,255,"texture update reaches later draw");

    /* The game draws glyphs with diffuse 0x00FFFFFF: stage-0 SELECTARG1
     * must preserve texture alpha independently from RGB modulation. */
    const uint8_t glyph[]={255,0,0,255, 0,255,0,0, 0,0,255,255, 255,255,255,255};
    uint32_t glyphtex=gpu->texture_create(2,2,glyph);
    gpu->clear(0xFF000000,1);state.texture=glyphtex;state.blend_enable=1;
    state.src_blend=5;state.dst_blend=6;state.alpha_test=1;state.alpha_ref=128;state.alpha_modulate=0;
    ok &= quad(0,0,W,H,0.2f,0x00FFFFFF,&state);
    ok &= gpu->read_pixels(pixels,W,H);
    ok &= check(80,60,255,0,0,"texture-only alpha keeps zero-alpha diffuse glyph visible");
    ok &= check(240,60,0,0,0,"texture-only alpha preserves transparent texel");
    ok &= check(80,180,0,0,255,"2x2 UV bottom-left orientation");
    ok &= check(240,180,255,255,255,"2x2 UV bottom-right orientation");
    gpu->clear(0xFF000000,1);state.alpha_modulate=1;
    ok &= quad(0,0,W,H,0.2f,0x00FFFFFF,&state);
    ok &= gpu->read_pixels(pixels,W,H);
    ok &= check(80,60,0,0,0,"modulated alpha respects zero diffuse alpha");
    gpu->texture_destroy(glyphtex);
    state.alpha_modulate=state.alpha_test=state.blend_enable=0;

    gpu->clear(0xFF000000,1);state.texture=0;state.scissor_enable=1;
    state.scissor_x=100;state.scissor_y=80;state.scissor_w=100;state.scissor_h=80;
    ok &= quad(0,0,W,H,0.2f,0xFFFFFFFF,&state);
    ok &= gpu->read_pixels(pixels,W,H);
    ok &= check(120,100,255,255,255,"scissor interior");
    ok &= check(80,100,0,0,0,"scissor exterior");
    state.scissor_enable=0;

    gpu->clear(0xFF000000,1);state.fog_enable=1;state.fog_color=0xFFC0D0E0;
    nfspu_gpu_vertex fogquad[]={
        {0,0,0.2f,1,0xFFFF0000,0,0,0},{W,0,0.2f,1,0xFFFF0000,0,1,0},
        {W,H,0.2f,1,0xFFFF0000,0,1,1},{0,H,0.2f,1,0xFFFF0000,0,0,1}};
    const uint32_t qi[]={0,1,2,0,2,3};
    ok &= gpu->draw(fogquad,4,qi,6,&state);ok &= gpu->read_pixels(pixels,W,H);
    ok &= check(160,120,192,208,224,"per-vertex fog");

    /* Race geometry has zero caller specular alpha when hardware table fog
     * is selected. Its visibility comes from RHW depth, not that alpha. */
    state.fog_color=0xFF000000;state.fog_mode=2;state.fog_density=.01f;
    gpu->clear(0xFF000000,1);
    ok &= fog_rect(0,160,10,10,0xFFFF0000,&state);
    ok &= fog_rect(160,W,100,100,0xFFFF0000,&state);
    ok &= gpu->read_pixels(pixels,W,H);
    ok &= check(80,120,(int)lroundf(255*expf(-.1f)),0,0,"exponential fog keeps near zero-specular geometry visible");
    ok &= check(240,120,(int)lroundf(255*expf(-1)),0,0,"exponential fog attenuates distant geometry");

    state.fog_mode=3;gpu->clear(0xFF000000,1);
    ok &= fog_rect(0,160,10,10,0xFFFF0000,&state);
    ok &= fog_rect(160,W,100,100,0xFFFF0000,&state);
    ok &= gpu->read_pixels(pixels,W,H);
    ok &= check(80,120,(int)lroundf(255*expf(-.01f)),0,0,"exponential-squared near fog");
    ok &= check(240,120,(int)lroundf(255*expf(-1)),0,0,"exponential-squared distant fog");

    state.fog_mode=1;state.fog_near=0;state.fog_far=100;gpu->clear(0xFF000000,1);
    ok &= fog_rect(0,160,10,10,0xFFFF0000,&state);
    ok &= fog_rect(160,W,100,100,0xFFFF0000,&state);
    ok &= gpu->read_pixels(pixels,W,H);
    ok &= check(80,120,230,0,0,"linear eye-relative fog");
    ok &= check(240,120,0,0,0,"linear fog reaches endpoint");

    state.fog_mode=2;gpu->clear(0xFF000000,1);
    ok &= fog_rect(0,W,10,100,0xFFFF0000,&state);
    ok &= gpu->read_pixels(pixels,W,H);
    float screen_t=160.5f/W,eye_depth=1.0f/((1-screen_t)/10+screen_t/100);
    ok &= check(160,120,(int)lroundf(255*expf(-.01f*eye_depth)),0,0,"table fog depth interpolates perspective-correctly");

    state.fog_color=0xFFF8848C;state.fog_density=.000535663f;gpu->clear(0xFF000000,1);
    ok &= fog_rect(0,W,10,10,0xFF00FF00,&state);ok &= gpu->read_pixels(pixels,W,H);
    float race_visibility=expf(-state.fog_density*10);
    ok &= check(160,120,(int)lroundf(248*(1-race_visibility)),(int)lroundf(255*race_visibility+132*(1-race_visibility)),
        (int)lroundf(140*(1-race_visibility)),"race-density pink fog does not hide near geometry");
    state.fog_enable=0;gpu->clear(0xFF000000,1);
    ok &= fog_rect(0,W,100,100,0xFFFF0000,&state);ok &= gpu->read_pixels(pixels,W,H);
    ok &= check(160,120,255,0,0,"fog-disabled HUD ignores selected table mode");
    state.fog_mode=0;

    gpu->clear(0xFF000000,0);state.depth_test=state.depth_write=1;state.depth_func=5;
    ok &= quad(0,0,W,H,0.4f,0xFFFF0000,&state);
    ok &= quad(0,0,W,H,0.2f,0xFF0000FF,&state);
    ok &= gpu->read_pixels(pixels,W,H);
    ok &= check(160,120,255,0,0,"GREATER depth comparison");
    state.depth_test=state.depth_write=state.depth_func=0;

    /* D3DCULL names the screen-space winding to discard. The ordinary
     * top-left, top-right, bottom-right quad is clockwise with screen Y down.
     * Testing both directions catches a reversed front-face convention as
     * well as a swapped CW/CCW state mapping. */
    gpu->clear(0xFF000000,1);state.cull=2;
    ok &= quad(40,40,280,200,0.5f,0xFFFF0000,&state);
    ok &= gpu->read_pixels(pixels,W,H);
    ok &= check(160,120,0,0,0,"CW culling rejects clockwise screen geometry");
    gpu->clear(0xFF000000,1);state.cull=3;
    ok &= quad(40,40,280,200,0.5f,0xFFFF0000,&state);
    ok &= gpu->read_pixels(pixels,W,H);
    ok &= check(160,120,255,0,0,"CCW culling preserves clockwise screen geometry");
    const nfspu_gpu_vertex reversequad[]={
        {40,40,0.5f,1,0xFF00FF00,0,0,0},{280,40,0.5f,1,0xFF00FF00,0,1,0},
        {280,200,0.5f,1,0xFF00FF00,0,1,1},{40,200,0.5f,1,0xFF00FF00,0,0,1}};
    const uint32_t reverse_indices[]={0,2,1,0,3,2};
    gpu->clear(0xFF000000,1);
    ok &= gpu->draw(reversequad,4,reverse_indices,6,&state);
    ok &= gpu->read_pixels(pixels,W,H);
    ok &= check(160,120,0,0,0,"CCW culling rejects counterclockwise screen geometry");
    gpu->clear(0xFF000000,1);state.cull=2;
    ok &= gpu->draw(reversequad,4,reverse_indices,6,&state);
    ok &= gpu->read_pixels(pixels,W,H);
    ok &= check(160,120,0,255,0,"CW culling preserves counterclockwise screen geometry");
    state.cull=0;

    gpu->clear(0xFF102030,1);state.texture=0;
    nfspu_gpu_vertex tri[]={
        {160,20,0.5f,1,0xFFFF4040,0xFF000000,0.5f,0},
        {30,210,0.5f,1,0xFF40FF40,0xFF000000,0,1},
        {290,210,0.5f,1,0xFF4040FF,0xFF000000,1,1}};
    ok &= gpu->draw(tri,3,NULL,0,&state);
    ok &= gpu->read_pixels(pixels,W,H);
    FILE *f=fopen("gpu-smoke.ppm","wb");
    if(f) { fprintf(f,"P6\n%d %d\n255\n",W,H);for(size_t i=0;i<W*H;i++) fwrite(pixels+i*4,1,3,f);fclose(f); }
    ok &= gpu->present();
    time_t until=time(NULL)+hold;
    while(time(NULL)<until) { nfspu_gpu_event event;while(gpu->poll(&event)) if(event.type==1) until=0; }
    gpu->texture_destroy(tex);gpu->close();
    printf("SDL3 GPU smoke: %s\n",ok?"PASS":"FAIL");return ok?0:2;
}
