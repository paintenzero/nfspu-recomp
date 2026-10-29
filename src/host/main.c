/* Portable host for the lifted USA 3.5 Porsche.exe.
 * Same sources build on Windows, Linux and macOS against pcrecomp win32hle. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "win32hle.h"
#include "host.h"
#include "thrash.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

void nfspu_register_dinput(void);
extern const win32hle_shim nfspu_game_shims[];

#ifdef _WIN32
static LONG WINAPI log_host_exception(EXCEPTION_POINTERS *p) {
    EXCEPTION_RECORD *e=p->ExceptionRecord;
    fprintf(stderr,"[crash] code=0x%08lX native=%p offset=0x%llX guest=0x%08X import=%s\n",
        e->ExceptionCode,e->ExceptionAddress,
        (unsigned long long)((uintptr_t)e->ExceptionAddress-(uintptr_t)GetModuleHandleA(NULL)),
        g_cur_func,g_cur_import?g_cur_import:"");
    if(e->NumberParameters>=2)fprintf(stderr,"[crash] access=%llu address=0x%llX\n",(unsigned long long)e->ExceptionInformation[0],(unsigned long long)e->ExceptionInformation[1]);
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

static void hold_on_exit(int code) {
    char msg[2200];
    const char *last = hle_screen_last_notice();
    snprintf(msg, sizeof msg, "%s\n\nThe game stopped (code %d). This window stays up so you can read it.",
             last && last[0] ? last : "The game stopped.", code);
    hle_screen_notice(msg);
    hle_screen_hold(20);
}

static void note(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static void fail_visible(const char *msg) {
    note("%s\n", msg);
#ifdef _WIN32
    MessageBoxA(NULL, msg, "Porsche Unleashed", MB_OK | MB_ICONERROR);
#endif
}

static void dirname_of(const char *path, char *out, size_t n) {
    snprintf(out, n, "%s", path);
    char *slash = strrchr(out, '/');
    char *bslash = strrchr(out, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
    if (slash) *slash = 0;
    else snprintf(out, n, ".");
}

static int to_abs(const char *in, char *out, size_t n) {
#ifdef _WIN32
    DWORD nch = GetFullPathNameA(in, (DWORD)n, out, NULL);
    return nch > 0 && nch < n;
#else
    char *r = realpath(in, NULL);
    if (!r) return 0;
    snprintf(out, n, "%s", r);
    free(r);
    return 1;
#endif
}

static int file_exists(const char *p) {
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static void open_log(const char *host_exe) {
    char dir[1024], path[1100], abs_exe[1024];
    const char *exe = host_exe ? host_exe : "PorscheUnleashed.exe";
    if (to_abs(exe, abs_exe, sizeof abs_exe)) exe = abs_exe;
    dirname_of(exe, dir, sizeof dir);
    snprintf(path, sizeof path, "%s/PorscheUnleashed.log", dir);
    /* The loader and HLE write directly to stderr. Capture that stream too,
     * otherwise the advertised log loses the actual reason boot failed. */
    if (freopen(path, "w", stderr)) {
        setvbuf(stderr, NULL, _IONBF, 0);
        note("[nfspu] log %s\n", path);
    }
}

int main(int argc, char **argv) {
#ifdef _WIN32
    /* Diagnostics go to our log; do not leave OS crash dialogs behind when a
     * bring-up assertion aborts during an automated run. */
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(log_host_exception);
    typedef unsigned int (__cdecl *abort_behavior_fn)(unsigned int, unsigned int);
    abort_behavior_fn abort_behavior=(abort_behavior_fn)(uintptr_t)GetProcAddress(GetModuleHandleA("msvcrt.dll"),"_set_abort_behavior");
    if(abort_behavior)abort_behavior(0,3);
#endif
    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    open_log(argc > 0 ? argv[0] : "PorscheUnleashed.exe");
    char host_image[1024];
    if (!to_abs(argc > 0 ? argv[0] : "PorscheUnleashed", host_image, sizeof host_image))
        snprintf(host_image, sizeof host_image, "%s", argc > 0 ? argv[0] : "PorscheUnleashed");

    char image[1024];
    image[0] = 0;
    if (argc > 1 && to_abs(argv[1], image, sizeof image) && file_exists(image)) {
        /* absolute path, captured before chdir */
    } else {
        char host_abs[1024], host_dir[1024], cand[1100], abs[1024];
        const char *exe = argc > 0 ? argv[0] : ".";
        if (!to_abs(exe, host_abs, sizeof host_abs)) snprintf(host_abs, sizeof host_abs, "%s", exe);
        dirname_of(host_abs, host_dir, sizeof host_dir);
        const char *rels[] = { "game/Porsche.exe", "../game/Porsche.exe", "../../game/Porsche.exe", "Porsche.exe", NULL };
        for (int i = 0; rels[i] && !image[0]; i++) {
            snprintf(cand, sizeof cand, "%s/%s", host_dir, rels[i]);
            if (to_abs(cand, abs, sizeof abs) && file_exists(abs)) snprintf(image, sizeof image, "%s", abs);
            else if (to_abs(rels[i], abs, sizeof abs) && file_exists(abs)) snprintf(image, sizeof image, "%s", abs);
        }
    }
    if (!image[0] || !file_exists(image)) {
        fail_visible("Could not find Porsche.exe.\n\nFrom the project folder run:\n  build\\windows\\PorscheUnleashed.exe game\\Porsche.exe");
        return 64;
    }

    char game_dir[1024];
    dirname_of(image, game_dir, sizeof game_dir);
    note("[nfspu] image %s\n", image);
    if (chdir(game_dir) != 0) {
        fail_visible("Could not enter the game directory.");
        return 65;
    }
    note("[nfspu] game directory %s\n", game_dir);

    recomp_host_init();
    win32hle_trace = getenv("NFSPU_TRACE") != NULL;
    /* Native Options stores display/device settings in the emulated registry.
     * Keep its backing path alive for main's lifetime and independent of the
     * game-directory chdir and the platform's installed Windows registry. */
    char host_dir[1024], registry_path[1100];
    dirname_of(host_image, host_dir, sizeof host_dir);
    snprintf(registry_path, sizeof registry_path, "%s/PorscheUnleashed.registry.txt", host_dir);
    hle_reg_load(registry_path);
    note("[nfspu] native settings registry %s\n", registry_path);
    win32hle_register(nfspu_game_shims);
    nfspu_register_dinput();
    nfspu_thrash_register(host_image);
    nfspu_thrash_load_graphics();
    hle_set_drive('C', game_dir);
    /* 0059D650 scans D:..Z: for a CD containing Porsche.exe, even with
     * all assets installed. Present that same local data as its media root. */
    hle_set_drive('D', game_dir);
    hle_set_cd_drive('D');
    char guest_image[1024], guest_command[1100];
    hle_guest_path(image, guest_image, sizeof guest_image);
    snprintf(guest_command, sizeof guest_command, "\"%s\"", guest_image);
    hle_set_command_line(guest_command);

    if (hle_screen_open("Need for Speed: Porsche Unleashed", 0, 0) != 0)
        note("[nfspu] SDL window did not open\n");
    else {
        hle_screen_notice("Need for Speed: Porsche Unleashed\n\nStarting. This window stays in front.\nIf the Start menu is open, close it.");
        hle_screen_pump();
        note("[nfspu] window open\n");
    }
    hle_on_exit = hold_on_exit;

    int rc = recomp_host_boot_permissive(image);
    if (rc) {
        fail_visible("The recompiled game failed to boot. See PorscheUnleashed.log next to this program.");
        return rc;
    }
    note("[nfspu] entry 0x%08X, running\n", recomp_host_entry());
    hle_screen_title("Need for Speed: Porsche Unleashed");
    int result=(int)recomp_host_run();
    nfspu_thrash_shutdown();
    return result;
}
