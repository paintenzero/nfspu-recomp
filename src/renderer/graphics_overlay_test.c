#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "graphics_overlay.h"
static unsigned batches,verts;
static uint8_t image[640*480*3];
static int emit(void *ctx,const nfspu_gpu_vertex *v,uint32_t nv,const uint32_t *i,uint32_t ni){
    (void)ctx;assert(nv&&ni&&nv%4==0&&ni==nv/4*6);batches++;verts=nv;
    for(uint32_t j=0;j<ni;j++)assert(i[j]<nv);
    for(uint32_t j=0;j<nv;j++)assert(isfinite(v[j].x)&&isfinite(v[j].y));
    for(uint32_t j=0;j<nv;j+=4){
        int x0=(int)ceilf(v[j].x),y0=(int)ceilf(v[j].y),x1=(int)ceilf(v[j+2].x),y1=(int)ceilf(v[j+2].y);
        if(x0<0)x0=0;if(y0<0)y0=0;if(x1>640)x1=640;if(y1>480)y1=480;
        unsigned a=v[j].color>>24,r=(v[j].color>>16)&255,g=(v[j].color>>8)&255,b=v[j].color&255;
        for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++){uint8_t*p=image+(y*640+x)*3;p[0]=(r*a+p[0]*(255-a))/255;p[1]=(g*a+p[1]*(255-a))/255;p[2]=(b*a+p[2]*(255-a))/255;}
    }return 1;
}
int main(void){
    nfspu_graphics_overlay o;nfspu_graphics_options current=nfspu_graphics_defaults();nfspu_overlay_init(&o,&current);
    nfspu_overlay_stats stats={"DIRECT3D12",640,480,1280,960,1920,1080,59.8f};
    assert(nfspu_overlay_render(&o,&stats,640,480,emit,NULL)&&batches==0);
    nfspu_overlay_open(&o,&current);nfspu_overlay_key(&o,81);nfspu_overlay_key(&o,79);assert(o.draft.fullscreen==1);
    assert(nfspu_overlay_key(&o,41)==NFSPU_OVERLAY_CANCEL&&!o.is_open&&!o.draft.fullscreen);
    nfspu_overlay_open(&o,&current);nfspu_overlay_key(&o,80);assert(o.draft.window_width==640);
    assert(nfspu_overlay_render(&o,&stats,640,480,emit,NULL)&&batches==1&&verts<98304);
    int x=(int)(o.x+420*o.scale),y=(int)(o.y+100*o.scale);
    assert(nfspu_overlay_pointer(&o,x,y,1,1,0)==0&&o.draft.window_width==960);
    for(int j=0;j<9;j++)nfspu_overlay_key(&o,81);
    assert(nfspu_overlay_key(&o,40)==NFSPU_OVERLAY_APPLY&&o.is_open);
    current=o.draft;nfspu_overlay_applied(&o,&current);assert(o.applied.window_width==960);
    nfspu_overlay_key(&o,21);assert(o.draft.vsync==1&&o.draft.render_scale==100);
    nfspu_overlay_close(&o);o.applied.show_stats=1;
    assert(nfspu_overlay_render(&o,&stats,640,480,emit,NULL)&&batches==2);
    nfspu_overlay_open(&o,&current);nfspu_overlay_status(&o,"APPLIED AND SAVED");
    for(int y=0;y<480;y++)for(int x=0;x<640;x++){uint8_t*p=image+(y*640+x)*3;p[0]=20+x/10;p[1]=30+y/8;p[2]=45;}
    assert(nfspu_overlay_render(&o,&stats,640,480,emit,NULL));
    FILE*f=fopen("overlay-ui.ppm","wb");assert(f);fprintf(f,"P6\n640 480\n255\n");fwrite(image,1,sizeof image,f);fclose(f);
    assert(nfspu_overlay_render(&o,&stats,320,240,emit,NULL));
    o.selected=4;o.draft.aspect=1;o.draft.wide_camera=1;
    nfspu_overlay_key(&o,79);
    assert(o.draft.aspect==2&&!o.draft.wide_camera);
    o.selected=8;nfspu_overlay_key(&o,79);assert(!o.draft.wide_camera);
    o.selected=4;nfspu_overlay_key(&o,79);assert(o.draft.aspect==0);
    nfspu_overlay_destroy(&o);puts("PASS overlay navigation, apply/cancel, mouse hit testing, batching and small-window layout");return 0;
}
