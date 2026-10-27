// xe-curpat5: writes the xe-curpat test pattern into Apple's LIVE cursor buffer through the aperture, once and then
// nonstop, and restores Apple's image afterwards. Showed that the live buffer pages displayed dashed whatever was in them.
// Build: clang -O2 -DXECTL_NO_MAIN -o tools/build/xe-curpat5 tools/xe-curpat5.c tools/xectl.c -framework IOKit -framework CoreFoundation
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include "xe.h"

#define DW (64 * 1024)   // 256 KB

static void pattern(uint32_t *px) {
    for (int i = 0; i < DW; i++) px[i] = 0;
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            uint32_t v = 0;
            if (x == y || y == 10) v = 0xFF000000;
            if (x == 10) v = 0xFFFF0000;
            if (x >= 50 && x < 54 && y >= 2 && y < 6) v = 0xFF000000;
            px[y * 64 + x] = v;
        }
}

int main(void) {
    if (xe_open()) return 1;
    uint32_t ctl = xe_rd(0x70080), base = xe_rd(0x70084) & ~0xFFFu;
    if (ctl != 0x27) { fprintf(stderr, "CUR_CTL=%08x: pointer not shown as 64x64 right now; move the mouse and rerun\n", ctl); return 1; }
    io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("XeShim"));
    io_connect_t c = 0;
    if (!svc || IOServiceOpen(svc, mach_task_self(), 0, &c)) { fprintf(stderr, "open XeShim failed\n"); return 1; }
    IOObjectRelease(svc);
    mach_vm_address_t ap = 0; mach_vm_size_t apLen = 0;
    if (IOConnectMapMemory64(c, 1, mach_task_self(), &ap, &apLen, kIOMapAnywhere)) { fprintf(stderr, "map BAR2 failed\n"); return 1; }
    if ((uint64_t)base + DW * 4 > apLen) { fprintf(stderr, "outside aperture\n"); return 1; }
    volatile uint32_t *live = (volatile uint32_t *)(ap + base);

    uint32_t *backup = malloc(DW * 4), *pat = malloc(DW * 4);
    for (int i = 0; i < DW; i++) backup[i] = live[i];
    pattern(pat);
    printf("CUR_CTL=%08x CUR_BASE=%08x\n", ctl, base);

    for (int i = 0; i < DW; i++) live[i] = pat[i];
    printf("phase 1: pattern written ONCE into the live buffer - LOOK for 8 s\n"); fflush(stdout);
    sleep(8);
    int changed = 0;
    for (int i = 0; i < DW; i++) changed += live[i] != pat[i];
    printf("  dwords no longer matching the pattern after 8 s: %d, CUR_BASE now %08x\n", changed, xe_rd(0x70084));

    printf("phase 2: pattern rewritten NONSTOP into the live buffer - LOOK for 8 s\n"); fflush(stdout);
    time_t end = time(NULL) + 8;
    long passes = 0;
    while (time(NULL) < end) { for (int i = 0; i < 64 * 64; i++) live[i] = pat[i]; passes++; }
    printf("  %ld rewrite passes\n", passes);

    for (int i = 0; i < DW; i++) live[i] = backup[i];
    printf("restored Apple's image. done\n");
    free(backup); free(pat);
    return 0;
}
