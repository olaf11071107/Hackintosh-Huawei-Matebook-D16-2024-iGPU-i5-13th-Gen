// xe-curdump: print the hardware cursor image as text, read through the GPU aperture (BAR2) the way the
// display engine sees it (GGTT-translated, uncached), next to what CUR_CTL/CUR_BASE say.
//   xe-curdump [rows] [cols] [stride_px]     defaults: 40 rows, 40 cols, stride from CUR_CTL (64/128/256)
// Build: clang -O2 -DXECTL_NO_MAIN -o tools/build/xe-curdump tools/xe-curdump.c tools/xectl.c -framework IOKit -framework CoreFoundation
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

int xe_open(void);
uint32_t xe_rd(uint32_t off);

int main(int argc, char **argv) {
    if (xe_open()) return 1;
    uint32_t ctl = xe_rd(0x70080), base = xe_rd(0x70084) & ~0xFFFu, pos = xe_rd(0x70088);
    int size = (ctl & 0x27) == 0x27 ? 64 : (ctl & 0x23) == 0x23 ? 256 : (ctl & 0x22) == 0x22 ? 128 : 64;
    int rows = argc > 1 ? atoi(argv[1]) : 40, cols = argc > 2 ? atoi(argv[2]) : 40;
    int stride = argc > 3 ? atoi(argv[3]) : size;
    printf("CUR_CTL=%08x CUR_BASE=%08x CUR_POS=%08x  mode=%dx%d  dump stride=%d px\n", ctl, base, pos, size, size, stride);

    io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("XeShim"));
    io_connect_t c = 0;
    if (!svc || IOServiceOpen(svc, mach_task_self(), 0, &c)) { fprintf(stderr, "open XeShim failed\n"); return 1; }
    IOObjectRelease(svc);
    mach_vm_address_t ap = 0; mach_vm_size_t apLen = 0;
    if (IOConnectMapMemory64(c, 1, mach_task_self(), &ap, &apLen, kIOMapAnywhere)) { fprintf(stderr, "map BAR2 failed\n"); return 1; }
    if ((uint64_t)base + (uint64_t)stride * 4 * rows > apLen) { fprintf(stderr, "cursor outside aperture (len %#llx)\n", (unsigned long long)apLen); return 1; }

    // xe-curdump rewrite: read both 256 KB cursor buffers through the aperture and write every dword back the
    // same way. Aperture writes go to RAM through the GGTT, not through the CPU cache, so if the display engine
    // was seeing stale RAM, the pointer becomes clean until the next cursor-image change.
    if (argc > 1 && argv[1][0] == 'r') {
        uint32_t start = 0x3c00000, len = 0x80000;
        if ((uint64_t)start + len > apLen) { fprintf(stderr, "cursor buffers outside aperture\n"); return 1; }
        for (uint32_t off = 0; off < len; off += 4) {
            volatile uint32_t *p = (volatile uint32_t *)(ap + start + off);
            *p = *p;
        }
        printf("rewrote %#x bytes at GGTT %#x through the aperture\n", len, start);
        return 0;
    }

    // '.' transparent, '#' dark opaque, 'o' light opaque, '+' partly transparent
    int opaque = 0;
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            uint32_t p = *(volatile uint32_t *)(ap + base + ((uint64_t)y * stride + x) * 4);
            uint32_t a = p >> 24, l = (((p >> 16) & 0xFF) + ((p >> 8) & 0xFF) + (p & 0xFF)) / 3;
            char ch = a < 0x20 ? '.' : a < 0xE0 ? '+' : l < 0x60 ? '#' : 'o';
            if (a >= 0x20) opaque++;
            putchar(ch);
        }
        putchar('\n');
    }
    printf("non-transparent pixels in dump: %d\n", opaque);
    return 0;
}
