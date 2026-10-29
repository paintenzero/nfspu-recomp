/* Invoke write and read in separate processes against a disposable test path.
 * This checks the guest registry ABI, on-disk updates and startup restoration
 * without loading a game image or touching a player's profile or bindings. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "win32hle.h"
#include "host.h"

const recomp_dispatch_entry_t recomp_dispatch_table[]={{0,NULL}};
const uint32_t recomp_dispatch_count=0;
static int failures;
#define CHECK(c,m) do { if(!(c)){fprintf(stderr,"FAIL %s\n",m);failures++;}else printf("PASS %s\n",m); } while(0)

static uint32_t call(const char *name,unsigned count,const uint32_t *args) {
    mach_enter();uint32_t before=g_esp;
    for(unsigned i=count;i;i--)PUSH32(g_esp,args[i-1]);
    PUSH32(g_esp,RECOMP_RETADDR);
    recomp_func_t fn=recomp_lookup_import(hle_resolve(name));
    if(!fn){fprintf(stderr,"Missing import %s\n",name);exit(2);}fn();
    CHECK(g_esp==before,"registry stdcall stack cleanup");
    g_esp=before;uint32_t result=g_eax;mach_leave();return result;
}
static uint32_t open_key(int create) {
    uint32_t out=hle_va(hle_alloc(4));
    uint32_t args[]={0x80000002u,hle_guest_strdup("SOFTWARE\\NFSPU Registry Persistence Test\\Native Options"),out};
    CHECK(call(create?"RegCreateKeyA":"RegOpenKeyA",3,args)==0,"open native settings key");
    return MEM32(out);
}
static void set(uint32_t key,const char *name,uint32_t type,const void *bytes,uint32_t size) {
    uint32_t data=hle_va(hle_alloc(size?size:1));
    if(size)memcpy(hle_ptr(data),bytes,size);
    uint32_t args[]={key,hle_guest_strdup(name),0,type,data,size};
    CHECK(call("RegSetValueExA",6,args)==0,"write native setting");
}
static void query(uint32_t key,const char *name,uint32_t type,const void *bytes,uint32_t size) {
    uint32_t kind=hle_va(hle_alloc(4)),length=hle_va(hle_alloc(4));
    uint32_t args[]={key,hle_guest_strdup(name),0,kind,0,length};
    CHECK(call("RegQueryValueExA",6,args)==0&&MEM32(kind)==type&&MEM32(length)==size,"restored type and byte length");
    uint32_t data=hle_va(hle_alloc(size?size:1));args[4]=data;
    MEM32(length)=size?size-1:0;
    if(size)CHECK(call("RegQueryValueExA",6,args)==234&&MEM32(length)==size,"undersized query reports required length");
    MEM32(length)=size;
    CHECK(call("RegQueryValueExA",6,args)==0&&(!size||memcmp(hle_ptr(data),bytes,size)==0),"restored exact setting bytes");
}
int main(int argc,char **argv) {
    if(argc!=3||(strcmp(argv[1],"write")&&strcmp(argv[1],"read"))){
        fprintf(stderr,"Usage: registry_test write|read disposable.registry-test.txt\n");return 2;
    }
    /* Reject accidental use of a real game registry/profile as test output. */
    const char *suffix=".registry-test.txt";size_t n=strlen(argv[2]),s=strlen(suffix);
    if(n<s||strcmp(argv[2]+n-s,suffix)){fprintf(stderr,"Test path must end in %s\n",suffix);return 2;}
    int writing=!strcmp(argv[1],"write");
    if(writing){FILE *old=fopen(argv[2],"r");if(old){fclose(old);if(remove(argv[2]))return 2;}}
    recomp_host_init();hle_reg_load(argv[2]);
    uint32_t key=open_key(writing);
    const char resolution[]="1600x900x16";
    const unsigned char binary[]={0,255,13,10,61,58,92,0,128,42};
    uint32_t initial=0,enabled=1;
    if(writing){
        set(key,"Thrash Resolution",1,resolution,sizeof resolution);
        set(key,"Triple Buffer",4,&initial,4);
        set(key,"Triple Buffer",4,&enabled,4);
        set(key,"Binary Settings",3,binary,sizeof binary);
        set(key,"Temporary",4,&enabled,4);
        uint32_t del[]={key,hle_guest_strdup("Temporary")};
        CHECK(call("RegDeleteValueA",2,del)==0,"persist setting deletion");
        FILE *saved=fopen(argv[2],"r");CHECK(saved!=NULL,"native settings backing file exists");if(saved)fclose(saved);
    }else{
        query(key,"thrash resolution",1,resolution,sizeof resolution);
        query(key,"TRIPLE BUFFER",4,&enabled,4);
        query(key,"Binary Settings",3,binary,sizeof binary);
        uint32_t length=hle_va(hle_alloc(4)),args[]={key,hle_guest_strdup("Temporary"),0,0,0,length};
        CHECK(call("RegQueryValueExA",6,args)==2,"deleted setting stays deleted after restart");
    }
    CHECK(call("RegCloseKey",1,&key)==0,"close settings key");
    return failures?1:0;
}
