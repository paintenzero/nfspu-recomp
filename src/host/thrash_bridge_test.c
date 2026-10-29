/* Exercises the real 32-bit guest ABI and hardware-backed pixel path. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "win32hle.h"
#include "host.h"
#include "thrash.h"
#include "nfspu_gpu.h"
#include "widescreen.h"
const recomp_dispatch_entry_t recomp_dispatch_table[]={{0,NULL}};
const uint32_t recomp_dispatch_count=0;
static int failures;
#define CHECK(test,msg) do {if(!(test)){fprintf(stderr,"FAIL %s\n",msg);failures++;}else printf("PASS %s\n",msg);}while(0)
/* External ABI regression: the stock 0044E780 UI predicate uses the advertised
 * memory budget and 40-byte mode records. Previously a zero budget rejected
 * every row even though the isolated renderer could set a mode successfully. */
static int stock_mode_visible(uint32_t desc,uint32_t index) {
    uint32_t m=MEM32(desc+0x40)+index*40,w=MEM32(m),h=MEM32(m+4),bpp=MEM32(m+8);
    int64_t remaining=(int64_t)MEM32(desc+0x70)-(int64_t)w*h*((bpp+7)/8)*3;
    uint32_t reserve=bpp>16?4000000:w>640?2000000:1500000;
    return MEM32(m+0x18)>=2&&w>=640&&h>=480&&remaining>reserve;
}
static uint32_t call(const char *name,int n,const uint32_t *args) {
    mach_enter();uint32_t before=g_esp;
    for(int i=n-1;i>=0;i--)PUSH32(g_esp,args[i]);PUSH32(g_esp,RECOMP_RETADDR);
    recomp_func_t fn=recomp_lookup_import(hle_resolve(name));
    if(!fn){fprintf(stderr,"Missing %s\n",name);exit(2);}fn();
    if(g_esp!=before){fprintf(stderr,"FAIL stack cleanup %s: %08X != %08X\n",name,g_esp,before);failures++;g_esp=before;}
    uint32_t result=g_eax;mach_leave();return result;
}
int main(int argc,char **argv) {
    recomp_host_init();nfspu_thrash_register(argv[0]);
    nfspu_widescreen_configure(0,1600,900);
    CHECK(recomp_mode_hook==nfspu_widescreen_mode&&recomp_projection_hook==nfspu_widescreen_projection,"runtime resolves installed widescreen adapters across translation units");
    CPUID_impl(0,0,0,0);CHECK(g_eax==1&&g_ebx==0x756E6547,"portable guest CPU vendor");
    CPUID_impl(1,0,0,0);CHECK(g_edx==0x111&&g_ecx==0,"unsupported SIMD paths disabled");
    uint32_t library=hle_guest_strdup(".\\drivers\\dxgistryz");
    uint32_t module=call("LoadLibraryA",1,&library);
    uint32_t procargs[]={module,hle_guest_strdup("_THRASH_about@0")};
    CHECK(module&&call("GetProcAddress",2,procargs)==hle_resolve("_THRASH_about@0"),"virtual renderer module and export routing");
    procargs[1]=hle_guest_strdup("missing_export");CHECK(call("GetProcAddress",2,procargs)==0,"unknown replacement export fails safely");
    CHECK(call("_THRASH_init@0",0,NULL)==1,"renderer initialization");
    uint32_t desc=call("_THRASH_about@0",0,NULL);
    CHECK(desc&&MEM32(desc+8)==107&&MEM32(desc+60)>1,"guest descriptor/version/modes");
    uint32_t mode_count=MEM32(desc+0x3C),mode_table=MEM32(desc+0x40),visible16=0,visible32=0,last_valid=0;
    int layout_ok=MEM32(mode_table)==0&&mode_count>1;
    for(uint32_t i=1;i<=mode_count;i++){
        uint32_t row=mode_table+i*40,bpp=MEM32(row+8);
        layout_ok&=MEM32(row+0x10)==1&&MEM32(row+0x0C)==(bpp==32?6:4)&&(bpp==16||bpp==32);
        if(stock_mode_visible(desc,i)){if(bpp==16)visible16++;else visible32++;last_valid=i;}
    }
    CHECK(layout_ok,"inclusive 1-based mode table has RGB565/ARGB8888 layouts");
    CHECK(visible16>=7&&visible32>=7&&last_valid<mode_count,"stock resolution filter admits 16/32-bit modes including its final visible row");
    CHECK(MEM32(desc+0x70)>=128u*1024*1024,"graphics mode budgeting memory is advertised");
    uint32_t invalid_mode[]={mode_count+1,2,1};CHECK(call("_THRASH_setvideomode@12",3,invalid_mode)==0,"mode above inclusive maximum is rejected");
    invalid_mode[0]=0;CHECK(call("_THRASH_setvideomode@12",3,invalid_mode)==0,"unused mode zero is rejected");
    uint32_t mode32[]={mode_count/2+1,2,2};CHECK(call("_THRASH_setvideomode@12",3,mode32)==1,"32-bit graphics mode can be selected");
    uint32_t info_key[]={29},info32=call("_THRASH_getstate@4",1,info_key);
    CHECK(info32&&MEM32(info32+8)==6&&MEM32(info32+4)==640*4,"32-bit mode surface format and pitch");
    uint32_t mode[]={1,2,1};CHECK(call("_THRASH_setvideomode@12",3,mode)==1,"video mode through guest ABI");
    uint32_t key[]={29};uint32_t info=call("_THRASH_getstate@4",1,key);
    CHECK(info&&MEM32(info)==0&&MEM32(info+8)==4&&MEM32(info+12)==640,"persistent surface info (startup crash regression)");
    uint32_t unsupported[]={37,0};CHECK(call("_THRASH_setstate@8",2,unsupported)==0,"unsupported timer feature stays disabled");
    unsupported[0]=451;CHECK(call("_THRASH_setstate@8",2,unsupported)==0,"unsupported raster capability stays disabled");
    uint32_t alloc[]={2,2,4,0,0};uint32_t tex=call("_THRASH_talloc@20",5,alloc);
    CHECK(tex!=0,"RGB565 texture allocation");
    uint16_t *pixels=hle_alloc(8);for(int i=0;i<4;i++)pixels[i]=0x07E0;
    uint32_t update[]={tex,hle_va(pixels),0};CHECK(call("_THRASH_tupdate@12",3,update)==tex,"RGB565 texture upload");
    CHECK(call("_THRASH_settexture@4",1,&tex)==1,"texture binding");
    call("_THRASH_clearwindow@0",0,NULL);
    nfspu_gpu_vertex *v=hle_alloc(4*sizeof *v);
    nfspu_gpu_vertex verts[]={ {10,10,.5f,1,0xFFFFFFFF,0,0,0},{110,10,.5f,1,0xFFFFFFFF,0,1,0},{110,110,.5f,1,0xFFFFFFFF,0,1,1},{10,110,.5f,1,0xFFFFFFFF,0,0,1}};
    memcpy(v,verts,sizeof verts);uint32_t *indices=hle_alloc(16);for(int i=0;i<4;i++)indices[i]=i;
    uint32_t draw[]={1,hle_va(v),hle_va(indices)};
    CHECK(call("_THRASH_drawquadmesh@12",3,draw)==1,"indexed quad count/vertices/indices ABI");
    uint32_t lock=call("_THRASH_lockwindow@0",0,NULL);
    CHECK(lock&&MEM32(lock+8)==4&&MEM32(lock+12)==640&&MEM32(lock+16)==480,"lockwindow layout");
    if(lock)CHECK(MEM16(MEM32(lock)+(50*640+50)*2)==0x07E0,"guest RGB565 framebuffer readback");
    CHECK(MEM32(info)==0,"locking does not mutate persistent surface info");
    uint32_t geometry_state[]={2,1};
    call("_THRASH_setstate@8",2,geometry_state);
    call("_THRASH_clearwindow@0",0,NULL);call("_THRASH_drawquadmesh@12",3,draw);
    lock=call("_THRASH_lockwindow@0",0,NULL);
    CHECK(lock&&MEM16(MEM32(lock)+(50*640+50)*2)==0x07E0,"Thrash cull 1 preserves clockwise car faces");
    geometry_state[1]=2;call("_THRASH_setstate@8",2,geometry_state);
    call("_THRASH_clearwindow@0",0,NULL);call("_THRASH_drawquadmesh@12",3,draw);
    lock=call("_THRASH_lockwindow@0",0,NULL);
    CHECK(lock&&MEM16(MEM32(lock)+(50*640+50)*2)==0,"Thrash cull 2 rejects clockwise car faces");
    geometry_state[1]=0;call("_THRASH_setstate@8",2,geometry_state);
    struct vertex40 {nfspu_gpu_vertex v;float u2,v2;} *v40=hle_alloc(4*40);
    for(int i=0;i<4;i++){v40[i].v=verts[i];v40[i].u2=1234;v40[i].v2=-5678;}
    geometry_state[0]=42;geometry_state[1]=1;call("_THRASH_setstate@8",2,geometry_state);
    draw[1]=hle_va(v40);call("_THRASH_clearwindow@0",0,NULL);call("_THRASH_drawquadmesh@12",3,draw);
    lock=call("_THRASH_lockwindow@0",0,NULL);
    CHECK(lock&&MEM16(MEM32(lock)+(50*640+50)*2)==0x07E0,"40-byte indexed car vertices retain positions and UVs");
    geometry_state[1]=0;call("_THRASH_setstate@8",2,geometry_state);draw[1]=hle_va(v);
    uint32_t free_tex=tex;call("_THRASH_tfree@4",1,&free_tex);
    alloc[2]=3;tex=call("_THRASH_talloc@20",5,alloc);
    for(int i=0;i<4;i++)v[i].color=0x00FFFFFF; /* The game's actual font convention. */
    pixels[0]=pixels[2]=0xFC00;pixels[1]=pixels[3]=0x7C00;
    update[0]=tex;call("_THRASH_tupdate@12",3,update);call("_THRASH_settexture@4",1,&tex);
    uint32_t setting[]={10,2};call("_THRASH_setstate@8",2,setting);
    setting[0]=65;setting[1]=0;call("_THRASH_setstate@8",2,setting);
    setting[0]=3;setting[1]=0xFF0000FF;call("_THRASH_setstate@8",2,setting);
    call("_THRASH_clearwindow@0",0,NULL);call("_THRASH_drawquadmesh@12",3,draw);
    lock=call("_THRASH_lockwindow@0",0,NULL);
    CHECK(lock&&MEM16(MEM32(lock)+(50*640+25)*2)==0xF800,"ARGB1555 opaque glyph texels");
    CHECK(lock&&MEM16(MEM32(lock)+(50*640+90)*2)==0x001F,"ARGB1555 transparent glyph background");
    call("_THRASH_tfree@4",1,&tex);alloc[2]=1;alloc[3]=16;tex=call("_THRASH_talloc@20",5,alloc);
    uint8_t *indexed=hle_alloc(2);indexed[0]=indexed[1]=0x21;
    uint32_t *palette=hle_alloc(16*4);palette[1]=0x0000FF00;palette[2]=0x00FF0000;
    uint32_t pal_update[]={tex,hle_va(indexed),0};call("_THRASH_tupdate@12",3,pal_update);
    pal_update[1]=0;pal_update[2]=hle_va(palette);CHECK(call("_THRASH_tupdate@12",3,pal_update)==tex,"palette-only texture update");
    call("_THRASH_settexture@4",1,&tex);call("_THRASH_drawquadmesh@12",3,draw);lock=call("_THRASH_lockwindow@0",0,NULL);
    CHECK(lock&&MEM16(MEM32(lock)+(50*640+25)*2)==0x07E0,"P4 low-nibble palette sampling");
    indexed[0]=indexed[1]=0x12;pal_update[1]=hle_va(indexed);pal_update[2]=0;call("_THRASH_tupdate@12",3,pal_update);
    call("_THRASH_drawquadmesh@12",3,draw);lock=call("_THRASH_lockwindow@0",0,NULL);
    CHECK(lock&&MEM16(MEM32(lock)+(50*640+25)*2)==0xF800,"pixel-only upload preserves palette");
    uint32_t no_texture=0;call("_THRASH_settexture@4",1,&no_texture);
    for(int i=0;i<4;i++){v[i]=verts[i];v[i].color=0xFF00FF00;v[i].specular=0;v[i].rhw=0.01f;}
    setting[0]=15;setting[1]=0x000000FF;call("_THRASH_setstate@8",2,setting);
    float density=0.001f;setting[0]=14;memcpy(&setting[1],&density,4);call("_THRASH_setstate@8",2,setting);
    setting[0]=21;setting[1]=4;call("_THRASH_setstate@8",2,setting);
    setting[1]=1;call("_THRASH_setstate@8",2,setting); /* Enable must retain EXP, not reset to vertex fog. */
    call("_THRASH_clearwindow@0",0,NULL);call("_THRASH_drawquadmesh@12",3,draw);lock=call("_THRASH_lockwindow@0",0,NULL);
    uint16_t fogged=lock?MEM16(MEM32(lock)+(50*640+50)*2):0;
    CHECK(lock&&((fogged>>5)&63)>52&&(fogged&31)<6,"race EXP fog retains nearby geometry with zero specular alpha");
    for(int i=0;i<4;i++)v[i].rhw=1.0f/3000.0f;
    call("_THRASH_clearwindow@0",0,NULL);call("_THRASH_drawquadmesh@12",3,draw);lock=call("_THRASH_lockwindow@0",0,NULL);
    fogged=lock?MEM16(MEM32(lock)+(50*640+50)*2):0;
    CHECK(lock&&((fogged>>5)&63)<6&&(fogged&31)>27,"race EXP fog approaches fog color at distance");
    uint8_t *fog_table=hle_alloc(64);for(int i=0;i<64;i++)fog_table[i]=(uint8_t)(i*255/63);
    setting[0]=105;setting[1]=hle_va(fog_table);call("_THRASH_setstate@8",2,setting);
    setting[0]=21;setting[1]=1;call("_THRASH_setstate@8",2,setting);
    for(int i=0;i<4;i++){v[i].rhw=1;v[i].z=0;}
    call("_THRASH_clearwindow@0",0,NULL);call("_THRASH_drawquadmesh@12",3,draw);lock=call("_THRASH_lockwindow@0",0,NULL);
    CHECK(lock&&MEM16(MEM32(lock)+(50*640+50)*2)==0x07E0,"custom fog alias 105 supplies near visibility from vertex depth");
    for(int i=0;i<4;i++)v[i].z=1;
    call("_THRASH_clearwindow@0",0,NULL);call("_THRASH_drawquadmesh@12",3,draw);lock=call("_THRASH_lockwindow@0",0,NULL);
    CHECK(lock&&MEM16(MEM32(lock)+(50*640+50)*2)==0x001F,"custom fog table supplies far visibility");
    CHECK(v[0].specular==0&&v[0].z==1,"fog preparation does not modify guest vertices");
    setting[0]=21;setting[1]=0;call("_THRASH_setstate@8",2,setting);
    call("_THRASH_clearwindow@0",0,NULL);call("_THRASH_drawquadmesh@12",3,draw);lock=call("_THRASH_lockwindow@0",0,NULL);
    CHECK(lock&&MEM16(MEM32(lock)+(50*640+50)*2)==0x07E0,"disabling fog preserves HUD colors");
    setting[0]=4;setting[1]=1;call("_THRASH_setstate@8",2,setting);
    setting[0]=40;setting[1]=1;call("_THRASH_setstate@8",2,setting);
    for(int i=0;i<4;i++){v[i].z=.5f;v[i].color=0xFFFF0000;}
    call("_THRASH_clearwindow@0",0,NULL);call("_THRASH_drawquadmesh@12",3,draw);
    setting[0]=102;setting[1]=0xFFFFFFFFu;call("_THRASH_setstate@8",2,setting);
    for(int i=0;i<4;i++)v[i].color=0xFF00FF00;
    call("_THRASH_drawquadmesh@12",3,draw);lock=call("_THRASH_lockwindow@0",0,NULL);
    CHECK(lock&&MEM16(MEM32(lock)+(50*640+50)*2)==0x07E0&&v[0].z==.5f,"signed vertex depth bias separates coplanar geometry without changing inputs");
    key[0]=451;CHECK(call("_THRASH_getstate@4",1,key)==0xFFFFFFFFu,"unknown state is unsupported");
    call("_THRASH_tfree@4",1,&tex);nfspu_thrash_shutdown();
    printf("Thrash bridge: %d failures\n",failures);return failures?1:0;
}
