// xe-curpat3: is it Apple's cursor pages themselves, or how data gets into them? Writes the xe-curpat test pattern
// through the aperture (GPU path) into
//   phase 1  our pages, shown at GGTT 0x4000000
//   phase 2  Apple's spare cursor buffer (the one CUR_BASE does not point at), shown at GGTT 0x4000000
// and restores the spare buffer's contents, the PTEs, CUR_BASE and CUR_CTL afterwards. Do not move the mouse.
// Build: clang -O2 -DXECTL_NO_MAIN -o tools/build/xe-curpat3 tools/xe-curpat3.c tools/xectl.c -framework IOKit -framework CoreFoundation
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include "xe.h"

#define PAGES 64
#define TEST_IDX 16384
#define TEST_ADDR (TEST_IDX * 4096u)

static void mapPages(uint32_t idx, const uint64_t *ptes) {
    for (int i = 0; i < PAGES; i++) xe_gtt_wr(idx + i, ptes[i]);
    (void)xe_gtt_rd(idx + PAGES - 1);
    xe_wr(0x101008, 1);
}

static void pattern(volatile uint32_t *px) {
    for (int i = 0; i < PAGES * 1024; i++) px[i] = 0;
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            uint32_t v = 0;
            if (x == y || y == 10) v = 0xFF000000;
            if (x == 10) v = 0xFFFF0000;
            if (x >= 50 && x < 54 && y >= 2 && y < 6) v = 0xFF000000;
            px[y * 64 + x] = v;
        }
}

static void show(uint32_t ctl, const char *what) {
    xe_wr(0x70080, ctl);
    xe_wr(0x70084, TEST_ADDR);
    printf("%s: CUR_CTL=%08x CUR_BASE=%08x - LOOK at the pointer for 10 s\n", what, xe_rd(0x70080), xe_rd(0x70084));
    fflush(stdout);
    sleep(10);
}

int main(void) {
    if (xe_open()) return 1;
    uint32_t savedCtl = xe_rd(0x70080), savedBase = xe_rd(0x70084);
    if (savedCtl != 0x27) { fprintf(stderr, "CUR_CTL=%08x: pointer not shown as 64x64 right now; move the mouse and rerun\n", savedCtl); return 1; }
    uint32_t spare = (savedBase & ~0xFFFu) ^ 0x40000u, spareIdx = spare / 4096;

    io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("XeShim"));
    io_connect_t c = 0;
    if (!svc || IOServiceOpen(svc, mach_task_self(), 0, &c)) { fprintf(stderr, "open XeShim failed\n"); return 1; }
    IOObjectRelease(svc);
    mach_vm_address_t ap = 0; mach_vm_size_t apLen = 0;
    if (IOConnectMapMemory64(c, 1, mach_task_self(), &ap, &apLen, kIOMapAnywhere)) { fprintf(stderr, "map BAR2 failed\n"); return 1; }
    if ((uint64_t)TEST_ADDR + PAGES * 4096 > apLen || (uint64_t)spare + PAGES * 4096 > apLen) { fprintf(stderr, "outside aperture\n"); return 1; }

    struct xe_dma d = {0};
    if (xe_dma_alloc(PAGES * 4096, &d)) { fprintf(stderr, "xe_dma_alloc failed\n"); return 1; }

    uint64_t savedTest[PAGES], apple[PAGES], ours[PAGES];
    for (int i = 0; i < PAGES; i++) {
        savedTest[i] = xe_gtt_rd(TEST_IDX + i);
        apple[i] = xe_gtt_rd(spareIdx + i);
        ours[i] = ((d.bus + (uint64_t)i * 4096) & ~0xFFFull) | (apple[i] & 0xFFF);
    }
    uint32_t *backup = malloc(PAGES * 4096);
    volatile uint32_t *sp = (volatile uint32_t *)(ap + spare);
    for (int i = 0; i < PAGES * 1024; i++) backup[i] = sp[i];
    printf("CUR_BASE=%08x spare=%08x spare PTE0=%#llx our bus=%#llx\n", savedBase, spare,
           (unsigned long long)apple[0], (unsigned long long)d.bus);

    volatile uint32_t *tw = (volatile uint32_t *)(ap + TEST_ADDR);
    mapPages(TEST_IDX, ours);
    pattern(tw);                                  // through the aperture into our pages
    show(savedCtl, "phase 1 (pattern via aperture, our pages)");

    mapPages(TEST_IDX, apple);
    pattern(tw);                                  // through the aperture into Apple's spare buffer
    show(savedCtl, "phase 2 (pattern via aperture, Apple's spare pages)");

    for (int i = 0; i < PAGES * 1024; i++) tw[i] = backup[i];
    xe_wr(0x70080, savedCtl);
    xe_wr(0x70084, savedBase);
    mapPages(TEST_IDX, savedTest);
    xe_dma_free(&d);
    free(backup);
    printf("restored\n");
    return 0;
}
