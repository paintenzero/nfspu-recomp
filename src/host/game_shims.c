/* Title-specific imports that win32hle does not answer.
 * mrbupd is EA's online updater. These exports are cdecl (undecorated) and
 * this portable build has nothing to update, so they report "not needed". */
#include <stdio.h>
#include <string.h>
#include "win32hle.h"

static void mrb_no(void) { RET(0, 0); }          /* cdecl: caller pops arguments */
static void mrb_init(void) {
    fprintf(stderr, "[mrbupd] update check skipped (portable build)\n");
    RET(0, 0);
}

/* dsound.dll ordinal 6 on the DX7 import set is DirectSoundCaptureEnumerateW.
 * No capture device; returning DS_OK with an empty enumeration is enough. */
static void ds_capture_enum(void) { RET(0, 2); }

/* Linked by Porsche.exe, not in the stock wsock32 table. Single-player boot
 * does not need them; a real call returns "no sockets ready" / an error. */
static void w_select(void) { RET(0, 5); }
static void w_accept(void) { RET(0xFFFFFFFFu, 3); }
static void w_shutdown(void) { RET(0, 2); }
static void w_fdisset(void) { RET(0, 2); }

static void s_GetShortPathNameA(void) {
    const char *in = ASTR(0);
    char *out = ASTR(1);
    uint32_t cap = A32(2);
    uint32_t n = in ? (uint32_t)strlen(in) : 0;
    if (out && cap > n && in) { memcpy(out, in, n + 1); RET(n, 3); }
    RET(n + 1, 3);
}
static void s_DebugBreak(void) { RETV(0); }
static void s_SetEnvironmentVariableW(void) { RET(1, 2); }
static void s_GetCommProperties(void) { RET(0, 2); }
static void s_TerminateThread(void) { RET(1, 2); }
static void s_DisableThreadLibraryCalls(void) { RET(1, 1); }
static void s_OpenSemaphoreA(void) { RET(0, 3); }
static void s_OpenFileMappingA(void) { RET(0, 3); }
static void s_RegFlushKey(void) { RET(0, 1); }
static void s_GetWindowDC(void) { RET(1, 1); }
static void s_keybd_event(void) { RETV(4); }
static void s_GetKeyboardType(void) { RET(4, 1); }
static void s_UnregisterClassA(void) { RET(1, 2); }
static void s_IsIconic(void) { RET(0, 1); }
static void s_SendNotifyMessageA(void) { RET(1, 4); }
static void s_SystemParametersInfoA(void) { RET(1, 4); }

const win32hle_shim nfspu_game_shims[] = {
    { "mrbInitUpdate", mrb_init },
    { "mrbIsRepairNeeded", mrb_no },
    { "mrbIsUpdateAvailable", mrb_no },
    { "mrbIsPrepareNeeded", mrb_no },
    { "mrbTermUpdate", mrb_no },
    { "dsound.dll#6", ds_capture_enum },
    { "select", w_select },
    { "accept", w_accept },
    { "shutdown", w_shutdown },
    { "__WSAFDIsSet", w_fdisset },
    { "GetShortPathNameA", s_GetShortPathNameA },
    { "DebugBreak", s_DebugBreak },
    { "SetEnvironmentVariableW", s_SetEnvironmentVariableW },
    { "GetCommProperties", s_GetCommProperties },
    { "TerminateThread", s_TerminateThread },
    { "DisableThreadLibraryCalls", s_DisableThreadLibraryCalls },
    { "OpenSemaphoreA", s_OpenSemaphoreA },
    { "OpenFileMappingA", s_OpenFileMappingA },
    { "RegFlushKey", s_RegFlushKey },
    { "GetWindowDC", s_GetWindowDC },
    { "keybd_event", s_keybd_event },
    { "GetKeyboardType", s_GetKeyboardType },
    { "UnregisterClassA", s_UnregisterClassA },
    { "IsIconic", s_IsIconic },
    { "SendNotifyMessageA", s_SendNotifyMessageA },
    { "SystemParametersInfoA", s_SystemParametersInfoA },
    { 0, 0 }
};
