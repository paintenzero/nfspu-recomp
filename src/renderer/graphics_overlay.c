/* Small immediate-mode graphics panel. Text is authored 5x7 geometry so the
 * overlay remains readable without OS fonts, a second renderer, or textures. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "graphics_overlay.h"

#define ROWS 9
#define APPLY_ROW 9
#define RESET_ROW 10
#define CANCEL_ROW 11
#define LAST_ROW CANCEL_ROW
#define PANEL_W 548.0f
#define PANEL_H 468.0f
#define ROW_Y 91.0f
#define ROW_H 26.0f
#define BUTTON_Y 339.0f
#define MAX_VERTICES 98304u
#define MAX_INDICES (MAX_VERTICES/4u*6u)

typedef struct { char c; uint8_t row[7]; } glyph;
/* Rows are five bits, from the leftmost column to the rightmost. */
static const glyph font[]={
    {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
    {'C',{14,17,16,16,16,17,14}}, {'D',{30,17,17,17,17,17,30}},
    {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
    {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}},
    {'I',{14,4,4,4,4,4,14}}, {'J',{7,2,2,2,18,18,12}},
    {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}},
    {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,21,19,17,17,17}},
    {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}},
    {'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}},
    {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
    {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}},
    {'W',{17,17,17,21,21,21,10}}, {'X',{17,17,10,4,10,17,17}},
    {'Y',{17,17,10,4,4,4,4}}, {'Z',{31,1,2,4,8,16,31}},
    {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
    {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
    {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
    {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
    {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}},
    {'-',{0,0,0,31,0,0,0}}, {'+',{0,4,4,31,4,4,0}},
    {'.',{0,0,0,0,0,12,12}}, {',',{0,0,0,0,0,4,8}},
    {':',{0,12,12,0,12,12,0}}, {'/',{1,1,2,4,8,16,16}},
    {'<',{2,4,8,16,8,4,2}}, {'>',{8,4,2,1,2,4,8}},
    {'(',{2,4,8,8,8,4,2}}, {')',{8,4,2,2,2,4,8}},
    {'[',{14,8,8,8,8,8,14}}, {']',{14,2,2,2,2,2,14}},
    {'=',{0,0,31,0,31,0,0}}, {'!',{4,4,4,4,4,0,4}},
    {'?',{14,17,1,2,4,0,4}}, {'%',{17,2,4,8,16,0,17}},
    {'_', {0,0,0,0,0,0,31}}, {'*',{0,21,14,31,14,21,0}}
};

static const uint32_t window_sizes[][2]={
    {640,480},{960,720},{1280,960},{1280,720},{1600,900},{1920,1080}
};
static const uint32_t scales[]={100,150,200,300};

nfspu_graphics_options nfspu_graphics_defaults(void) {
    nfspu_graphics_options o={960,720,0,1,100,0,0,1,0,0};
    return o;
}
void nfspu_overlay_init(nfspu_graphics_overlay *o,const nfspu_graphics_options *options) {
    memset(o,0,sizeof *o);o->hover=-1;
    o->applied=options?*options:nfspu_graphics_defaults();o->draft=o->applied;
}
void nfspu_overlay_destroy(nfspu_graphics_overlay *o) {
    free(o->vertices);free(o->indices);memset(o,0,sizeof *o);
}
void nfspu_overlay_open(nfspu_graphics_overlay *o,const nfspu_graphics_options *options) {
    if(options)o->applied=*options;
    o->draft=o->applied;o->is_open=1;o->selected=0;o->hover=-1;o->status[0]=0;o->scale=0;
}
void nfspu_overlay_close(nfspu_graphics_overlay *o) {o->is_open=0;o->draft=o->applied;o->hover=-1;}
void nfspu_overlay_applied(nfspu_graphics_overlay *o,const nfspu_graphics_options *options) {
    int clean=memcmp(&o->draft,&o->applied,sizeof *options)==0;
    int applied_draft=memcmp(&o->draft,options,sizeof *options)==0;
    o->applied=*options;
    if(!o->is_open||clean||applied_draft)o->draft=*options;
}
void nfspu_overlay_status(nfspu_graphics_overlay *o,const char *message) {
    snprintf(o->status,sizeof o->status,"%s",message?message:"");
}
static uint32_t cycle(uint32_t value,uint32_t count,int direction) {
    return (value+count+(direction<0?-1:1))%count;
}
static void edit(nfspu_graphics_overlay *o,int direction) {
    nfspu_graphics_options *v=&o->draft;
    switch(o->selected) {
    case 0:{int at=-1;for(int i=0;i<6;i++)if(v->window_width==window_sizes[i][0]&&v->window_height==window_sizes[i][1])at=i;
        at=at<0?(direction<0?5:0):(int)cycle((uint32_t)at,6,direction);
        v->window_width=window_sizes[at][0];v->window_height=window_sizes[at][1];break;}
    case 1:v->fullscreen=!v->fullscreen;break;
    case 2:v->vsync=!v->vsync;break;
    case 3:{int at=0;for(int i=0;i<4;i++)if(v->render_scale==scales[i])at=i;v->render_scale=scales[cycle(at,4,direction)];break;}
    case 4:v->aspect=cycle(v->aspect,3,direction);if(v->aspect==2)v->wide_camera=0;break;
    case 5:v->texture_filter=cycle(v->texture_filter,3,direction);break;
    case 6:v->scaling_filter=cycle(v->scaling_filter,2,direction);break;
    case 7:v->show_stats=!v->show_stats;break;
    case 8:if(v->aspect!=2)v->wide_camera=!v->wide_camera;break;
    default:break;
    }
    o->status[0]=0;
}
static void reset(nfspu_graphics_overlay *o) {
    o->draft=nfspu_graphics_defaults();
    nfspu_overlay_status(o,"DEFAULTS SELECTED. APPLY TO USE THEM.");
}
static int activate(nfspu_graphics_overlay *o) {
    if(o->selected<ROWS){edit(o,1);return NFSPU_OVERLAY_NONE;}
    if(o->selected==APPLY_ROW)return NFSPU_OVERLAY_APPLY;
    if(o->selected==RESET_ROW){reset(o);return NFSPU_OVERLAY_NONE;}
    nfspu_overlay_close(o);return NFSPU_OVERLAY_CANCEL;
}
int nfspu_overlay_key(nfspu_graphics_overlay *o,uint32_t key) {
    if(!o->is_open)return NFSPU_OVERLAY_NONE;
    switch(key) {
    case 41:case 67:nfspu_overlay_close(o);return NFSPU_OVERLAY_CANCEL; /* Esc/F10 */
    case 82:o->selected=(o->selected+LAST_ROW)%(LAST_ROW+1);break;
    case 81:case 43:o->selected=(o->selected+1)%(LAST_ROW+1);break;
    case 80:case 79:
        if(o->selected<ROWS)edit(o,key==80?-1:1);
        else o->selected=APPLY_ROW+(int)cycle(o->selected-APPLY_ROW,3,key==80?-1:1);
        break;
    case 40:case 44:case 88:return activate(o); /* Enter/Space/keypad Enter */
    case 21:reset(o);break; /* R */
    default:break;
    }
    return NFSPU_OVERLAY_NONE;
}
static int hit(nfspu_graphics_overlay *o,float x,float y) {
    if(x<20||x>PANEL_W-20)return -1;
    if(y>=ROW_Y&&y<ROW_Y+ROWS*ROW_H)return (int)((y-ROW_Y)/ROW_H);
    if(y>=BUTTON_Y&&y<BUTTON_Y+29){
        if(x>=24&&x<182)return APPLY_ROW;
        if(x>=195&&x<353)return RESET_ROW;
        if(x>=366&&x<524)return CANCEL_ROW;
    }
    (void)o;return -1;
}
int nfspu_overlay_pointer(nfspu_graphics_overlay *o,int x,int y,uint32_t button,int down,int wheel) {
    if(!o->is_open||o->scale<=0)return NFSPU_OVERLAY_NONE;
    float px=(x-o->x)/o->scale,py=(y-o->y)/o->scale;
    int row=hit(o,px,py);o->hover=row;
    if(row<0)return NFSPU_OVERLAY_NONE;
    if(button==1&&down){o->selected=row;if(row<ROWS){edit(o,px<312?-1:1);return NFSPU_OVERLAY_NONE;}return activate(o);}
    if(wheel&&row<ROWS){o->selected=row;edit(o,wheel>0?1:-1);}
    return NFSPU_OVERLAY_NONE;
}

static void rect(nfspu_graphics_overlay *o,float x,float y,float w,float h,uint32_t color) {
    if(o->vertex_count+4>MAX_VERTICES||o->index_count+6>MAX_INDICES||w<=0||h<=0)return;
    uint32_t n=o->vertex_count;
    x=o->x+x*o->scale;y=o->y+y*o->scale;w*=o->scale;h*=o->scale;
    nfspu_gpu_vertex v[]={ {x,y,0,1,color,0,0,0},{x+w,y,0,1,color,0,0,0},
        {x+w,y+h,0,1,color,0,0,0},{x,y+h,0,1,color,0,0,0} };
    const uint32_t indices[]={n,n+1,n+2,n,n+2,n+3};
    memcpy(o->vertices+n,v,sizeof v);memcpy(o->indices+o->index_count,indices,sizeof indices);
    o->vertex_count+=4;o->index_count+=6;
}
static void text(nfspu_graphics_overlay *o,float x,float y,float pixel,uint32_t color,const char *message) {
    float start=x;
    for(const char *p=message;*p;p++) {
        unsigned char c=(unsigned char)*p;if(c>='a'&&c<='z')c-=32;
        if(c=='\n'){x=start;y+=9*pixel;continue;}
        if(c!=' ') {
            const uint8_t *rows=NULL;
            for(unsigned i=0;i<sizeof font/sizeof *font;i++)if(font[i].c==c){rows=font[i].row;break;}
            static const uint8_t unknown[]={14,17,1,2,4,0,4};
            if(!rows)rows=unknown;
            for(int row=0;row<7;row++)for(int col=0;col<5;col++)if(rows[row]&(1u<<(4-col)))rect(o,x+col*pixel,y+row*pixel,pixel,pixel,color);
        }
        x+=6*pixel;
    }
}
static void wrapped(nfspu_graphics_overlay *o,float x,float y,float pixel,uint32_t color,const char *s,int columns,int max_lines) {
    char line[128];
    for(int row=0;row<max_lines&&*s;row++) {
        int count=0,last_space=-1;
        while(s[count]&&s[count]!='\n'&&count<columns){if(s[count]==' ')last_space=count;count++;}
        if(s[count]&&s[count]!='\n'&&last_space>0)count=last_space;
        memcpy(line,s,(size_t)count);line[count]=0;
        text(o,x,y+row*9*pixel,pixel,color,line);s+=count;
        while(*s==' ')s++;
        if(*s=='\n')s++;
    }
}
static void value(const nfspu_graphics_options *v,int row,char *out,size_t size) {
    switch(row){
    case 0:snprintf(out,size,"%u X %u",v->window_width,v->window_height);break;
    case 1:snprintf(out,size,"%s",v->fullscreen?"BORDERLESS":"WINDOWED");break;
    case 2:snprintf(out,size,"%s",v->vsync?"ON":"OFF");break;
    case 3:snprintf(out,size,"%u%%",v->render_scale);break;
    case 4:snprintf(out,size,"%s",v->aspect==2?"PROPORTIONAL WIDE":v->aspect?"STRETCH TO FILL":"PRESERVE ASPECT");break;
    case 5:snprintf(out,size,"%s",v->texture_filter==1?"NEAREST":v->texture_filter==2?"LINEAR":"GAME CONTROLLED");break;
    case 6:snprintf(out,size,"%s",v->scaling_filter?"SMOOTH":"SHARP");break;
    case 7:snprintf(out,size,"%s",v->show_stats?"ON":"OFF");break;
    case 8:snprintf(out,size,"%s",v->aspect==2?"WIDE MODE":v->wide_camera?"ON":"OFF");break;
    default:out[0]=0;break;
    }
}
static const char *help(int selected) {
    static const char *const hints[]={
        "WINDOW SIZE CHANGES OUTPUT, NOT RENDER DETAIL. USE RENDER SCALE FOR SHARPER GEOMETRY.",
        "BORDERLESS FULLSCREEN. F11 OR ALT+ENTER ALSO TOGGLES THIS.",
        "VSYNC SYNCHRONIZES PRESENTATION WITH YOUR DISPLAY.",
        "CHANGES THE ACTUAL RENDER RESOLUTION. CANVAS IS THE GAME'S COORDINATE LAYOUT.",
        "PROPORTIONAL WIDE SHOWS MORE HORIZONTALLY. WINDOW SIZE AND WIDE MODE APPLY ON NEXT RACE. ORIGINAL MENUS KEEP 4:3.",
        "GAME CONTROLLED, CRISP NEAREST, OR SMOOTH LINEAR TEXTURE SAMPLING.",
        "FINAL IMAGE SCALING: SHARP PIXELS OR SMOOTH INTERPOLATION.",
        "SHOW FRAME RATE AND RENDER SIZE WHILE THE PANEL IS CLOSED.",
        "ORIGINAL GAME CAMERA OPTION. TAKES EFFECT IN RACES.",
        "APPLY THE SELECTED SETTINGS. THE PANEL STAYS OPEN.",
        "RESTORE DEFAULTS IN THIS PANEL, THEN APPLY TO USE THEM.",
        "CLOSE WITHOUT APPLYING CHANGES."
    };
    return hints[selected>=0&&selected<=LAST_ROW?selected:0];
}
int nfspu_overlay_render(nfspu_graphics_overlay *o,const nfspu_overlay_stats *stats,
    uint32_t width,uint32_t height,nfspu_overlay_emit_fn emit,void *context) {
    if(!width||!height||!emit||(!o->is_open&&!o->applied.show_stats))return 1;
    if(!o->vertices)o->vertices=malloc((size_t)MAX_VERTICES*sizeof *o->vertices);
    if(!o->indices)o->indices=malloc((size_t)MAX_INDICES*sizeof *o->indices);
    if(!o->vertices||!o->indices)return 0;
    o->vertex_count=o->index_count=0;
    if(!o->is_open){
        o->x=12;o->y=12;o->scale=width>=1280?2:1;
        char line[100];snprintf(line,sizeof line,"%.0F FPS  %u X %u  F10 GRAPHICS",stats?stats->fps:0,
            stats?stats->render_width:0,stats?stats->render_height:0);
        rect(o,0,0,6*(float)strlen(line)+12,17,0xC8101928);text(o,6,5,1,0xFFE9F8F7,line);
        return emit(context,o->vertices,o->vertex_count,o->indices,o->index_count);
    }
    float scale_x=(width>16?width-16:width)/PANEL_W,scale_y=(height>16?height-16:height)/PANEL_H;
    o->scale=scale_x<scale_y?scale_x:scale_y;if(o->scale>1.5f)o->scale=1.5f;
    if(o->scale<=0)return 1;
    o->x=((float)width-PANEL_W*o->scale)/2;o->y=((float)height-PANEL_H*o->scale)/2;
    /* A subtle full-window veil keeps game motion from obscuring the panel. */
    rect(o,-o->x/o->scale,-o->y/o->scale,width/o->scale,height/o->scale,0x65000000);
    rect(o,5,7,PANEL_W,PANEL_H,0x78000000);rect(o,0,0,PANEL_W,PANEL_H,0xFA101A29);
    rect(o,0,0,4,PANEL_H,0xFF63D3C7);
    text(o,23,18,2.5f,0xFFE7F3FA,"GRAPHICS");
    text(o,387,23,1.3f,0xFF8AA6BC,"F10 TO CLOSE");
    char line[160];
    snprintf(line,sizeof line,"%s  /  %.0F FPS",stats&&stats->backend?stats->backend:"SDL3 GPU",stats?stats->fps:0);
    text(o,24,46,1.25f,0xFF9DB3C7,line);
    snprintf(line,sizeof line,"CANVAS %u X %u  RENDER %u X %u  OUTPUT %u X %u",
        stats?stats->logical_width:0,stats?stats->logical_height:0,stats?stats->render_width:0,
        stats?stats->render_height:0,stats?stats->output_width:width,stats?stats->output_height:height);
    text(o,24,65,1.1f,0xFF9DB3C7,line);rect(o,24,82,500,1,0xFF304055);
    static const char *const labels[]={"WINDOW SIZE","DISPLAY MODE","VSYNC","RENDER SCALE","ASPECT","TEXTURE FILTER","IMAGE SCALING","FRAME STATISTICS","WIDE RACE CAMERA"};
    for(int row=0;row<ROWS;row++){
        float y=ROW_Y+row*ROW_H;int selected=o->selected==row;
        rect(o,20,y,PANEL_W-40,24,selected?0xFF254758:(row&1)?0xFF142235:0xFF18283C);
        if(selected)rect(o,20,y,3,24,0xFF63D3C7);
        text(o,31,y+6,1.6f,selected?0xFFFFFFFF:0xFFC6D4E3,labels[row]);
        value(&o->draft,row,line,sizeof line);
        if(row==3&&stats)snprintf(line,sizeof line,"%u%% (%u X %u)",o->draft.render_scale,
            (uint32_t)((uint64_t)stats->logical_width*o->draft.render_scale/100),
            (uint32_t)((uint64_t)stats->logical_height*o->draft.render_scale/100));
        text(o,307,y+6,1.6f,0xFFE1F1F6,line);
        text(o,286,y+6,1.6f,selected?0xFF63D3C7:0xFF607F98,"<");
        text(o,509,y+6,1.6f,selected?0xFF63D3C7:0xFF607F98,">");
    }
    static const char *const buttons[]={"APPLY","RESET","CANCEL"};
    for(int i=0;i<3;i++){
        float x=24+i*171;int selected=o->selected==APPLY_ROW+i;
        uint32_t fill=i==0?0xFF63D3C7:selected?0xFF35586D:0xFF26384D;
        rect(o,x,BUTTON_Y,158,29,fill);
        if(selected){rect(o,x,BUTTON_Y,158,2,0xFFFFFFFF);rect(o,x,BUTTON_Y+27,158,2,0xFFFFFFFF);}
        text(o,x+79-(float)strlen(buttons[i])*5.4f,BUTTON_Y+8,1.8f,i==0?0xFF0B2730:0xFFE7F3FA,buttons[i]);
    }
    wrapped(o,24,382,1.15f,0xFFA7BDCF,help(o->selected),70,2);
    if(o->status[0])wrapped(o,24,410,1.1f,0xFFFFD08A,o->status,72,2);
    else if(memcmp(&o->draft,&o->applied,sizeof o->draft))text(o,24,410,1.1f,0xFFFFD08A,"UNAPPLIED CHANGES");
    else text(o,24,410,1.1f,0xFF7D9BAC,"SETTINGS APPLY WHEN YOU CHOOSE APPLY.");
    text(o,24,445,1.0f,0xFF8AA6BC,"ARROWS CHANGE  ENTER SELECTS  R RESETS  ESC CANCELS");
    return emit(context,o->vertices,o->vertex_count,o->indices,o->index_count);
}
