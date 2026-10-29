/* Exercise the installation/media aliases through the real guest API. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "win32hle.h"
#include "host.h"
const recomp_dispatch_entry_t recomp_dispatch_table[]={{0,NULL}};
const uint32_t recomp_dispatch_count=0;
static int failures;
#define CHECK(c,m) do {if(!(c)){fprintf(stderr,"FAIL %s\n",m);failures++;}else puts("PASS " m);}while(0)
static uint32_t call(const char *name,uint32_t arg){
    mach_enter();uint32_t before=g_esp;
    PUSH32(g_esp,arg);PUSH32(g_esp,RECOMP_RETADDR);
    recomp_lookup_import(hle_resolve(name))();
    CHECK(g_esp==before,"media query preserves stdcall stack");
    uint32_t result=g_eax;mach_leave();return result;
}
int main(void){
    char cwd[1024],image[1200],guest[1200],host[1200];
    if(!getcwd(cwd,sizeof cwd))return 2;
    /* CTest owns this isolated directory; never overwrite a real game image. */
    if(!strstr(cwd,"nfspu-media-test"))return 2;
    FILE *f=fopen("Porsche.exe","wb");if(!f)return 2;
    fwrite("fixture",1,7,f);fclose(f);
    snprintf(image,sizeof image,"%s/Porsche.exe",cwd);
    recomp_host_init();hle_set_drive('C',cwd);hle_set_drive('D',cwd);hle_set_cd_drive('D');
    hle_guest_path(image,guest,sizeof guest);
    CHECK(!strcmp(guest,"C:\\Porsche.exe"),"installed executable keeps C: when D: aliases its files");
    CHECK(call("GetDriveTypeA",hle_guest_strdup("C:\\"))==3,"installation drive remains fixed storage");
    CHECK(call("GetDriveTypeA",hle_guest_strdup("D:\\"))==5,"local media alias is a CD drive");
    CHECK(call("GetDriveTypeA",hle_guest_strdup("E:\\"))==1,"unmapped drive is unavailable");
    int found=0;
    for(char letter='D';letter<='Z';letter++){
        char root[]={letter,':','\\',0};
        if(call("GetDriveTypeA",hle_guest_strdup(root))!=5)continue;
        char file[]={letter,':','\\','P','o','r','s','c','h','e','.','e','x','e',0};
        if(call("GetFileAttributesA",hle_guest_strdup(file))!=0xFFFFFFFFu){found=letter;break;}
    }
    CHECK(found=='D',"original CD scan finds the locally installed game file");
    CHECK(hle_host_path("d:\\pOrScHe.ExE",host,sizeof host),"disc data paths resolve without case sensitivity");
    snprintf(image,sizeof image,"%s/nested",cwd);hle_set_drive('E',image);
    hle_guest_path(image,guest,sizeof guest);
    CHECK(!strcmp(guest,"E:\\"),"more specific drive mapping still takes precedence");
    return failures?1:0;
}
