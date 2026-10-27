// xe-curpat: show a known 64x64 test pattern on the hardware cursor plane, from our own buffer, to see how the
// display engine actually reads cursor memory. Pattern (ARGB, 64-px rows): black diagonal (x == y), black
// horizontal line at row 10, red vertical line at column 10, a small black 4x4 block at (50,2); rest transparent.
// Phase 1 maps the buffer with Apple's cursor PTE flags (0x8f), phase 2 with stolen-memory flags (0x8b).
// Everything (PTEs at GGTT 64 MB, CUR_BASE, CUR_CTL) is restored afterwards.
// Build: clang -O2 -DXECTL_NO_MAIN -o tools/build/xe-curpat tools/xe-curpat.c tools/xectl.c -framework IOKit -framework CoreFoundation
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include "xe.h"

int xe_open(void);
uint32_t xe_rd(uint32_t off);
void xe_wr(uint32_t off, uint32_t v);
uint64_t xe_gtt_rd(uint32_t idx);
void xe_gtt_wr(uint32_t idx, uint64_t pte);

#define PAGES 64                 // 256 KB
#define GTT_IDX 16384            // GGTT 0x4000000 (64 MB): unused, maps the scratch page
#define GTT_ADDR (GTT_IDX * 4096u)

static void show(uint64_t bus, uint64_t flags, uint32_t ctl, const char *what) {
    for (int i = 0; i < PAGES; i++) xe_gtt_wr(GTT_IDX + i, ((bus + (uint64_t)i * 4096) & ~0xFFFull) | flags);
    (void)xe_gtt_rd(GTT_IDX + PAGES - 1);
    xe_wr(0x101008, 1);                          // GFX_FLSH_CNTL: flush GGTT TLB
    xe_wr(0x70080, ctl);
    xe_wr(0x70084, GTT_ADDR);                    // writing CUR_BASE arms the update
    printf("%s: PTE flags 0x%llx, CUR_CTL=%08x CUR_BASE=%08x - LOOK at the pointer for 10 s\n",
           what, (unsigned long long)flags, xe_rd(0x70080), xe_rd(0x70084));
    fflush(stdout);
    sleep(10);
}

int main(void) {
    if (xe_open()) return 1;
    struct xe_dma d = {0};
    if (xe_dma_alloc(PAGES * 4096, &d)) { fprintf(stderr, "xe_dma_alloc failed\n"); return 1; }
    printf("buffer: bus %#llx size %#llx\n", (unsigned long long)d.bus, (unsigned long long)d.size);

    volatile uint32_t *px = (volatile uint32_t *)d.cpu;
    for (uint64_t i = 0; i < d.size / 4; i++) px[i] = 0;              // transparent
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            uint32_t v = 0;
            if (x == y || y == 10) v = 0xFF000000;                       // black
            if (x == 10) v = 0xFFFF0000;                                 // red
            if (x >= 50 && x < 54 && y >= 2 && y < 6) v = 0xFF000000;   // block top-right
            px[y * 64 + x] = v;
        }
    __asm__ volatile("mfence" ::: "memory");
    for (uint64_t off = 0; off < d.size; off += 64) __asm__ volatile("clflush (%0)" :: "r"((uint8_t *)d.cpu + off) : "memory");
    __asm__ volatile("mfence" ::: "memory");

    uint64_t savedPte[PAGES];
    for (int i = 0; i < PAGES; i++) savedPte[i] = xe_gtt_rd(GTT_IDX + i);
    uint32_t savedCtl = xe_rd(0x70080), savedBase = xe_rd(0x70084);
    printf("saved CUR_CTL=%08x CUR_BASE=%08x PTE[%d]=%#llx\n", savedCtl, savedBase, GTT_IDX, (unsigned long long)savedPte[0]);

    show(d.bus, 0x8f, 0x27, "phase 1 (Apple cursor flags)");
    show(d.bus, 0x8b, 0x27, "phase 2 (stolen-memory flags)");

    xe_wr(0x70080, savedCtl);
    xe_wr(0x70084, savedBase);
    for (int i = 0; i < PAGES; i++) xe_gtt_wr(GTT_IDX + i, savedPte[i]);
    (void)xe_gtt_rd(GTT_IDX);
    xe_wr(0x101008, 1);
    xe_dma_free(&d);
    printf("restored\n");
    return 0;
}
