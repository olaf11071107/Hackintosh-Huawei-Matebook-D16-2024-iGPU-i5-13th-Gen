// xe-curpat2: find out what is wrong with Apple's cursor buffer, by swapping one thing at a time against the setup
// that xe-curpat showed to work (our buffer, mapped at GGTT 0x4000000):
//   phase 1  copy of Apple's current cursor image, in our buffer, at 0x4000000   -> tests the image content
//   phase 2  Apple's own pages (its PTEs copied) at 0x4000000                    -> tests Apple's physical pages
//   phase 3  our buffer (with the copy) mapped at Apple's GGTT address           -> tests the GGTT address
// Everything (PTEs, CUR_BASE, CUR_CTL) is restored afterwards. Do not move the mouse while it runs.
// Build: clang -O2 -DXECTL_NO_MAIN -o tools/build/xe-curpat2 tools/xe-curpat2.c tools/xectl.c -framework IOKit -framework CoreFoundation
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include "xe.h"

#define PAGES 64                 // 256 KB, one Apple cursor buffer
#define TEST_IDX 16384           // GGTT 0x4000000 (64 MB), unused
#define TEST_ADDR (TEST_IDX * 4096u)

static void flushTlb(uint32_t idx) { (void)xe_gtt_rd(idx); xe_wr(0x101008, 1); }

static void mapPages(uint32_t idx, const uint64_t *ptes) {
    for (int i = 0; i < PAGES; i++) xe_gtt_wr(idx + i, ptes[i]);
    flushTlb(idx + PAGES - 1);
}

static void show(uint32_t ctl, uint32_t base, const char *what) {
    xe_wr(0x70080, ctl);
    xe_wr(0x70084, base);                        // writing CUR_BASE arms the update
    printf("%s: CUR_CTL=%08x CUR_BASE=%08x - LOOK at the pointer for 10 s\n", what, xe_rd(0x70080), xe_rd(0x70084));
    fflush(stdout);
    sleep(10);
}

int main(void) {
    if (xe_open()) return 1;
    uint32_t savedCtl = xe_rd(0x70080), savedBase = xe_rd(0x70084);
    uint32_t appleIdx = (savedBase & ~0xFFFu) / 4096;
    if (savedCtl != 0x27) { fprintf(stderr, "CUR_CTL=%08x: pointer not shown as 64x64 right now; move the mouse and rerun\n", savedCtl); return 1; }

    // read Apple's image through the aperture (GGTT-translated, like the display engine)
    io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("XeShim"));
    io_connect_t c = 0;
    if (!svc || IOServiceOpen(svc, mach_task_self(), 0, &c)) { fprintf(stderr, "open XeShim failed\n"); return 1; }
    IOObjectRelease(svc);
    mach_vm_address_t ap = 0; mach_vm_size_t apLen = 0;
    if (IOConnectMapMemory64(c, 1, mach_task_self(), &ap, &apLen, kIOMapAnywhere)) { fprintf(stderr, "map BAR2 failed\n"); return 1; }
    if ((uint64_t)(savedBase & ~0xFFFu) + PAGES * 4096 > apLen) { fprintf(stderr, "cursor outside aperture\n"); return 1; }

    struct xe_dma d = {0};
    if (xe_dma_alloc(PAGES * 4096, &d)) { fprintf(stderr, "xe_dma_alloc failed\n"); return 1; }
    volatile uint32_t *src = (volatile uint32_t *)(ap + (savedBase & ~0xFFFu)), *dst = (volatile uint32_t *)d.cpu;
    int opaque = 0;
    for (int i = 0; i < PAGES * 1024; i++) { uint32_t p = src[i]; dst[i] = p; if (i < 64 * 64 && (p >> 24) >= 0x20) opaque++; }
    __asm__ volatile("mfence" ::: "memory");
    for (uint64_t off = 0; off < d.size; off += 64) __asm__ volatile("clflush (%0)" :: "r"((uint8_t *)d.cpu + off) : "memory");
    __asm__ volatile("mfence" ::: "memory");

    uint64_t savedTest[PAGES], apple[PAGES], ours[PAGES];
    for (int i = 0; i < PAGES; i++) {
        savedTest[i] = xe_gtt_rd(TEST_IDX + i);
        apple[i] = xe_gtt_rd(appleIdx + i);
        ours[i] = ((d.bus + (uint64_t)i * 4096) & ~0xFFFull) | (apple[i] & 0xFFF);   // same flags as Apple's
    }
    printf("saved CUR_CTL=%08x CUR_BASE=%08x, Apple image: %d non-transparent px in 64x64\n", savedCtl, savedBase, opaque);
    printf("Apple PTEs [idx %u]: %#llx %#llx %#llx %#llx ... %#llx\n", appleIdx, (unsigned long long)apple[0],
           (unsigned long long)apple[1], (unsigned long long)apple[2], (unsigned long long)apple[3], (unsigned long long)apple[PAGES - 1]);
    printf("our buffer: bus %#llx\n", (unsigned long long)d.bus);

    mapPages(TEST_IDX, ours);
    show(savedCtl, TEST_ADDR, "phase 1 (copy of Apple's image, our pages, GGTT 64 MB)");
    mapPages(TEST_IDX, apple);
    show(savedCtl, TEST_ADDR, "phase 2 (Apple's pages, GGTT 64 MB)");
    mapPages(appleIdx, ours);
    show(savedCtl, savedBase, "phase 3 (our pages with the copy, at Apple's GGTT address)");

    mapPages(appleIdx, apple);
    mapPages(TEST_IDX, savedTest);
    xe_wr(0x70080, savedCtl);
    xe_wr(0x70084, savedBase);
    xe_dma_free(&d);
    printf("restored\n");
    return 0;
}
