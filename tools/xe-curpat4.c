// xe-curpat4: is the display seeing different bytes than the CPU/aperture in Apple's live cursor buffer? Reads the
// live 64x64 image twice (changes while idle), prints it as text, then writes the same bytes back through the aperture.
// Build: clang -O2 -DXECTL_NO_MAIN -o tools/build/xe-curpat4 tools/xe-curpat4.c tools/xectl.c -framework IOKit -framework CoreFoundation
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include "xe.h"

#define DW (64 * 64)

static int diff(const uint32_t *a, const uint32_t *b) { int n = 0; for (int i = 0; i < DW; i++) n += a[i] != b[i]; return n; }

static void ascii(const uint32_t *p) {
    for (int y = 0; y < 40; y++) {
        for (int x = 0; x < 40; x++) {
            uint32_t v = p[y * 64 + x], a = v >> 24, l = (((v >> 16) & 0xFF) + ((v >> 8) & 0xFF) + (v & 0xFF)) / 3;
            putchar(a < 0x20 ? '.' : a < 0xE0 ? '+' : l < 0x60 ? '#' : 'o');
        }
        putchar('\n');
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

    static uint32_t a[DW], b[DW], c2[DW];
    for (int i = 0; i < DW; i++) a[i] = live[i];
    sleep(2);
    for (int i = 0; i < DW; i++) b[i] = live[i];
    printf("CUR_CTL=%08x CUR_BASE=%08x, changed dwords in 2 s (mouse still): %d\n", ctl, base, diff(a, b));
    ascii(b);

    printf("STEP 1: LOOK at the pointer now (6 s) - dashed?\n"); fflush(stdout);
    sleep(6);
    for (int i = 0; i < DW; i++) live[i] = b[i];
    for (int i = 0; i < DW; i++) c2[i] = live[i];
    printf("STEP 2: rewrote the live image through the aperture (readback differs: %d) - LOOK for 10 s, mouse still\n", diff(b, c2));
    fflush(stdout);
    sleep(10);
    printf("CUR_BASE now %08x. done\n", xe_rd(0x70084));
    return 0;
}
