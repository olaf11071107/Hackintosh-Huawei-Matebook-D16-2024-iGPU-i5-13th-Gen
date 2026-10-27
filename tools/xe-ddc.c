// xe-ddc: find which display port an external monitor is wired to, by reading the first 8 bytes of its EDID
// (DDC, I2C address 0x50) through the display engine's GMBUS controller on every port's pin. Read-only.
// Pins (i915 gmbus_pins_icp, ADL-P/ADP PCH): 1 = DDI A, 2 = DDI B, 3 = DDI C, 9..14 = TC1..TC6.
// Build: clang -O2 -DXECTL_NO_MAIN -o tools/build/xe-ddc tools/xe-ddc.c tools/xectl.c -framework IOKit -framework CoreFoundation
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include "xe.h"

#define GMBUS0 0xC5100   // pin select / rate
#define GMBUS1 0xC5104   // command
#define GMBUS2 0xC5108   // status
#define GMBUS3 0xC510C   // data
#define SW_CLR_INT  (1u << 31)
#define SW_RDY      (1u << 30)
#define CYCLE_STOP  (1u << 27)
#define CYCLE_WAIT  (1u << 25)
#define ST_ACTIVE   (1u << 9)
#define ST_NAK      (1u << 10)
#define ST_HW_RDY   (1u << 11)

static void reset(void) {
    xe_wr(GMBUS1, SW_CLR_INT);
    xe_wr(GMBUS1, 0);
    xe_wr(GMBUS0, 0);
}

// 0 = EDID bytes read, 1 = NAK (nothing there), 2 = timeout
static int probe(uint32_t pin, uint8_t out[8]) {
    reset();
    xe_wr(GMBUS0, pin);                                       // 100 kHz
    xe_wr(GMBUS1, SW_RDY | CYCLE_WAIT | (8u << 16) | (0x50u << 1) | 1u);
    int got = 0, rc = 2;
    for (int t = 0; t < 2000 && got < 8; t++) {
        uint32_t st = xe_rd(GMBUS2);
        if (st & ST_NAK) { rc = 1; break; }
        if (st & ST_HW_RDY) {
            uint32_t v = xe_rd(GMBUS3);
            for (int i = 0; i < 4 && got < 8; i++) out[got++] = (v >> (8 * i)) & 0xFF;
            continue;
        }
        usleep(500);
    }
    if (got == 8) rc = 0;
    xe_wr(GMBUS1, SW_RDY | CYCLE_STOP);
    for (int t = 0; t < 200 && (xe_rd(GMBUS2) & ST_ACTIVE); t++) usleep(500);
    reset();
    return rc;
}

int main(void) {
    if (xe_open()) return 1;
    static const struct { uint32_t pin; const char *name; } pins[] = {
        {1, "DDI A"}, {2, "DDI B"}, {3, "DDI C"},
        {9, "TC1"}, {10, "TC2"}, {11, "TC3"}, {12, "TC4"}, {13, "TC5"}, {14, "TC6"},
    };
    for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        uint8_t b[8] = {0};
        int rc = probe(pins[i].pin, b);
        printf("pin %2u (%-5s): ", pins[i].pin, pins[i].name);
        if (rc == 0) {
            int edid = b[0] == 0 && b[1] == 0xFF && b[6] == 0xFF && b[7] == 0;
            printf("%02x %02x %02x %02x %02x %02x %02x %02x%s\n", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
                   edid ? "  <-- EDID: monitor here" : "");
        } else {
            printf("%s\n", rc == 1 ? "no answer" : "timeout");
        }
    }
    return 0;
}
