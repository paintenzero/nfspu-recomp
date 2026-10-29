/* Standalone regression for native thread guest-memory lifetime and flags. */
/* The assertions include the worker calls and must also execute in Release. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include "win32hle.h"

static uint32_t first_stack, first_scratch;
static int iteration;
static void guest_probe(void) {
    uint32_t stack = MEM32(g_fs_base + 8);
    uint32_t scratch = hle_scratch(32);
    if (!iteration) { first_stack = stack; first_scratch = scratch; }
    else { assert(stack == first_stack); assert(scratch == first_scratch); }
    RET(123, 0);
}
const recomp_dispatch_entry_t recomp_dispatch_table[] = { { 0x401000u, guest_probe } };
const uint32_t recomp_dispatch_count = 1;
static void *worker(void *unused) {
    (void)unused;
    assert(hle_call_guest(0x401000u, 0, NULL) == 123);
    return NULL;
}
static void *flag_worker(void *unused) {
    (void)unused;
    mach_enter();
    assert(g_flag_k == 0 && g_flag_a == 0 && g_flag_b == 0 && g_flag_cf == 0);
    g_flag_k = 21; g_flag_a = 22; g_flag_b = 23; g_flag_cf = 24;
    mach_leave();
    return NULL;
}
int main(void) {
    pthread_t thread;
    /* Before cleanup this consumes > 2 GB of guest stacks and aborts. */
    for (iteration = 0; iteration < 600; iteration++) {
        assert(pthread_create(&thread, NULL, worker, NULL) == 0);
        assert(pthread_join(thread, NULL) == 0);
    }
    mach_enter();
    g_flag_k = 11; g_flag_a = 12; g_flag_b = 13; g_flag_cf = 14;
    hle_block_begin();
    assert(pthread_create(&thread, NULL, flag_worker, NULL) == 0);
    assert(pthread_join(thread, NULL) == 0);
    hle_block_end();
    assert(g_flag_k == 11 && g_flag_a == 12 && g_flag_b == 13 && g_flag_cf == 14);
    mach_leave();
    assert(hle_vm_reserve(0, 0x80000000u) == 0); /* bounded address-space exhaustion */
    puts("PASS: 600 native threads reused guest stack/scratch; flags survive a thread switch");
    return 0;
}
