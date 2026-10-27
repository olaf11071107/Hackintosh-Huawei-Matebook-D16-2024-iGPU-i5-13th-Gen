# Technical notes

What was wrong on this machine and what each change does. NootedGreen changes are tagged in the source (`XE-…`);
all are in `patches/nootedgreen-huawei.diff`.

## Graphics memory (XE-GMS4, XE-GMS4-FB)

This BIOS encodes the stolen-memory size in GGC with the 4 MB-unit values `0xF0–0xFE` (here `0xFE` = 60 MB). Apple's
`IGMemoryManager::initSegments` and `AppleIntelBaseController::FBMemMgr_Init` decode the field as `GMS × 32 MB`, which
gave several GB and a panic. XE-GMS4 corrects the segment sizes on the raw-GGC path (used while `accel+0x128c` is 0);
XE-GMS4-FB replaces `shl eax,0x11; and eax,0xFE000000` in `FBMemMgr_Init` with the real size.

## Night Shift (XE-GAMMA)

Apple writes `GAMMA_MODE_A` (`0x4A480`) = `0xE0000001`. Bit 31 (pre-CSC degamma, with a table in Tiger Lake's format)
corrupts the image on display version 13; NootedGreen used to force the register to 0, which also killed Night Shift.
`ngreengammamask=0x7fffffff` keeps Apple's value minus bit 31; `ngreengamma=<value>` forces a value,
`ngreengamma=0xffffffff` passes Apple's value through for testing.

## Hardware cursor (XE-CURSHADOW)

The cursor showed 16-pixel dashes. Tests with XeShim (`tools/xe-curpat*.c`):

- the cursor plane displays a known pattern correctly from our own memory (xe-curpat), so the plane setup is fine;
- a copy of Apple's image in other pages displays cleanly; Apple's own live pages display dashed whatever is written to
  them through the aperture (xe-curpat2/3/5), while flushing the CPU cache over them changes nothing.

`hwSetCursorImage` writes the image with the CPU through `controller+0xC98`, an aperture mapping of the cursor range
starting at GGTT `controller+0xDC0` (two 256 KB buffers at `0x3C00000`/`0x3C40000`, backed by the
IOBufferMemoryDescriptor at `controller+0xCD0`). XE-CURSHADOW remaps that whole GGTT range to NootedGreen's own
write-combined buffer and points `controller+0xC98` at its direct kernel mapping, so Apple's code stores the image
straight into the pages the display reads. `-ngreennocurshadow` disables it.

## Audio

- macOS 26 has no AppleHDA; SimpleHDA loads the AppleHDA stack from OpenCore so AppleALC can patch it.
- AppleALC has no SN6140 profile; the CX8070 profile matches the node layout (see `applealc/`).
- The codec reports its headphone pin `0x16` as a vendor-defined widget (`wcaps 0x00F00000`) although its pin config
  (`0x00211040`), pin caps (`0x1001C`), connection list (`0x10, 0x11`) and pin control (`0xC0`) are a normal headphone
  pin. AppleHDA builds paths only through pins, so without a fix the layout produced no audio device at all.
  **XE-HPFIX** hooks `IOHDACodecDevice::executeVerb` and answers `0x00400581` (pin complex, stereo, unsolicited,
  connection list, power control) for that one query. `-ngreennohpfix` disables it.
- Headphones worked with this, then went silent with the same configuration. **XE-HPLOG** (in the current patch, not
  yet run) logs every setting AppleHDA writes and snapshots both output paths when the headphone/speaker pins change.
- Layout 21 differs from 15 only in pin settings: `0x1A` connection type unknown instead of digital, and pin control
  `0x24` on `0x19`/`0x1A` at init and wake. With 21 the built-in mic appeared, but the headphones were silent.
- Codec dump (via `alc-verb`, needs `alcverbs=1`, which must not be combined with XE-HPFIX):
  DACs `0x10`/`0x11`, beep `0x12`, ADC `0x13` ← `0x18 0x19 0x1A 0x15`, ADC `0x14` ← `0x1A 0x15`, vendor `0x15`,
  HP `0x16` (vendor-defined type), speaker `0x17` (`0x91170010`), `0x18` n/c, jack mic `0x19` (`0x00811030`),
  internal digital mic `0x1A` (`0x90A60020`), digital out `0x21` n/c.

## Fn keys (SSDT-HWFN)

The EC is `\_SB.PC00.LPCB.HWEC`. Brightness (`_Q0A`/`_Q0B`) notifies the display only when Windows enabled ACPI
brightness (`DSEN` bit 2), otherwise it goes through the IGD OpRegion. The other Fn keys store a Huawei WMI code in
`WMEN` and `Notify (WMI1, 0xA0)`: `0x284` mute (Fn+F4), `0x285`/`0x286` volume (Fn+F5/F6), `0x287` mic mute (Fn+F7),
`0x289` Wi-Fi (Fn+F9), `0x28A` Huawei M key (Fn+F10), `0x2BC`/`0x2BD` camera on/off. On Darwin, SSDT-HWFN sends PS/2
keys to VoodooPS2 instead (`e005`/`e006` brightness, `e020` mute, `e02e`/`e030` volume). Not mapped yet: mic mute,
Wi-Fi, M key.

## HDMI (XE-HDMI, XE-HDMIDIAG)

- With a monitor attached, `SDEISR` (`0xC4000`) bit 25 is set (TC2) and only GMBUS pin 10 (TC2) answers on DDC:
  the HDMI socket is the second Type-C port in legacy mode, DDI 4 in Apple's numbering (TC1–TC6 = DDI 3–8).
- Apple's port objects are configured straight from the platform connector table (`PortConfig` = connector entry:
  `index` = DDI, `busId` = GMBUS bus). `hwDoGMBusRequest` rejects bus numbers ≥ 8 and writes the bus number into the
  GMBUS0 pin field, so a TC pin (DDI + 6) cannot be named. `ngreenhdmi=<DDI>` turns connector 1 into HDMI on that DDI
  with placeholder bus 7, and NootedGreen's write filter redirects GMBUS0 pin 7 to the real pin.
- Booting with a screen attached freezes before the login window; hot-plugging after boot reaches no Apple code.
  XE-HDMIDIAG logs Apple's writes to power wells, PCODE mailbox, FIA, Dekel PHY, DDI/transcoder/clock, hotplug and
  GMBUS registers, plus enter/exit of `setPortMode`, `probePortState`, `enablePort` and `getDDCBlock` for DDI ≥ 3.
  Not yet run.

## Touchpad (SSDT-TPDX)

The DSDT declares the touchpad as `\_SB.PC00.TPD0`, directly under `PC00`; VoodooI2C only enumerates children of the
controller, so SSDT-TPDX adds `TPDX` under `I2C0` for Darwin (`BLTP7840`, address `0x2C`, HID descriptor register 1,
100 kHz). The GPIO interrupt never fired; polling works but logs "lost arbitration" (`TX_ABRT_SOURCE` bit 12).

## Wi-Fi

AirportItlwm cannot join hidden networks (macOS reports "network could not be found"; the driver never leaves
`SCAN`), and fails with WPA/WPA2 mixed mode (TKIP group cipher). WPA2-Personal AES with a broadcast SSID works.

## Camera and microphone permissions

With `amfi_get_out_of_my_way=1`, TCC never shows the permission prompt. Grant access in the user TCC database
(Terminal needs Full Disk Access):

```sh
DB=~/Library/Application\ Support/com.apple.TCC/TCC.db
grant() { for s in kTCCServiceCamera kTCCServiceMicrophone; do sqlite3 "$DB" "INSERT OR REPLACE INTO access (service, client, client_type, auth_value, auth_reason, auth_version, flags) VALUES ('$s', '$1', 0, 2, 3, 1, 0);"; done; }
grant com.google.Chrome; killall tccd
```
