# macOS Tahoe on the Huawei MCLG-XX (Intel UHD, Raptor Lake-H)

macOS 26.7.1 (25G241) with Intel graphics acceleration on a Huawei MCLG-XX laptop, built on
[dssoftx/Hackintosh-X1Carbon-G11-IrisXe-Tahoe](https://github.com/dssoftx/Hackintosh-X1Carbon-G11-IrisXe-Tahoe)
(NootedGreen running Apple's Tiger Lake driver on newer Intel graphics). This repository holds the changes needed for
this machine: a NootedGreen patch, three SSDTs, an AppleALC build script for its audio codec, and the diagnostic
tools used along the way.

Not affiliated with Huawei, Apple or Intel. Experimental: back up your data, and keep a working EFI on a second stick.

## Hardware

| Part | Model / ID |
|---|---|
| CPU | Intel Core i5-13420H (Raptor Lake-H) |
| Graphics | Intel UHD Graphics `8086:A7A8`, 48 EU, 6 L3 banks; panel 1920×1200 eDP |
| Audio | Synaptics/Conexant **SN6140** (`14F1:1F87`) on the Raptor Lake cAVS controller `8086:51CA` |
| Wi-Fi / Bluetooth | Intel AX201 (CNVi `8086:51F1`), Bluetooth `8087:0026` |
| Touchpad | I²C HID, `BLTP7840`, on I2C0 (`8086:51E8`) |
| Camera | USB UVC "HD Camera" (`3196:0244`) |
| Memory | 16 GB LPDDR |

## Status

| Feature | Status | How |
|---|---|---|
| Graphics acceleration (Metal) | ✅ works | NootedGreen + Apple's TGL drivers from the base repo, plus **XE-GMS4** fixes for this BIOS's 4 MB-unit stolen-memory encoding |
| Internal panel, brightness slider | ✅ works | `SSDT-PNLF` (under `GFX0`) + NootedGreen XE-BLR with `ngreenblmax=17741` |
| Night Shift | ✅ works | NootedGreen **XE-GAMMA**, `ngreengammamask=0x7fffffff` (drops the degamma stage this display reads wrongly) |
| Hardware cursor | ✅ works | NootedGreen **XE-CURSHADOW**: cursor moved into NootedGreen's own memory (Apple's aperture writes did not reach the display) |
| Keyboard, Fn keys | ✅ brightness Fn+F1/F2, mute/volume Fn+F4/F5/F6 | VoodooPS2 + `SSDT-HWFN` + 5 OpenCore ACPI renames (`_Q0A/_Q0B/_Q0D/_Q0E/_Q0F` → `XQ0x`) |
| Speakers | ✅ works | patched AppleALC (CX8070 profile retargeted to SN6140) + SimpleHDA (AppleHDA is gone in macOS 26) |
| Headphones | ⚠️ worked, then silent again | needs NootedGreen **XE-HPFIX** (the codec reports its headphone pin as vendor-defined). Silent since; **XE-HPLOG** codec log added to find out why |
| Microphone | ⚠️ partly | built-in + headset mic worked with layout 21 (which broke headphones); layout 15 + `v3` didn't create the built-in mic. Open |
| Camera | ✅ works | Apple's UVC driver; permissions granted in the TCC database (prompts never appear with `amfi_get_out_of_my_way=1`) |
| Touchpad | ⚠️ works, drops out | VoodooI2C + `SSDT-TPDX` (exposes the touchpad under I2C0), polling mode; "lost arbitration" errors cause drop-outs |
| Wi-Fi | ⚠️ mostly | AirportItlwm (Tahoe build): WPA2-AES works; **hidden networks** and WPA/WPA2 (TKIP) mixed mode don't |
| HDMI | ❌ in progress | the socket is **TC2** in legacy mode; NootedGreen **XE-HDMI** (`ngreenhdmi=4`) adds the connector; booting with a screen attached freezes; **XE-HDMIDIAG** logs the bring-up |
| Ethernet (USB adapter) | ✅ works | |
| Battery, sleep | not fully tested | |

## Installing

1. Install macOS with the base repository's installer EFI and follow its documentation (`docs/INSTALL.md` there),
   including `install.sh` for the Tiger Lake driver bundles.
2. Generate this machine's own SSDTs with [SSDTTime](https://github.com/corpnewt/SSDTTime) (EC, USBX, PLUG-ALT,
   RTCAWAC, XOSI) and map USB with USBToolBox. `scripts/windows/` has the PowerShell helpers used for that from Windows.
3. Build NootedGreen with this patch, in a clone of the base repository at the tested commit:
   ```sh
   git clone https://github.com/dssoftx/Hackintosh-X1Carbon-G11-IrisXe-Tahoe ~/xe && cd ~/xe
   git checkout c4cc862a8959741f360044a60a6022508a983511
   git apply /path/to/patches/nootedgreen-huawei.diff
   zsh build-nootedgreen.sh
   ```
4. Copy `acpi/*.aml` to `EFI/OC/ACPI/` and add them to `ACPI → Add`. `SSDT-HWFN` also needs the five `_Qxx → XQxx`
   renames (Find `_Q0A` = `X1EwQQ==`, Replace `XQ0A` = `WFEwQQ==`, and so on for 0B/0D/0E/0F; TableSignature `DSDT`).
5. Audio: build AppleALC with `applealc/patch-applealc-sn6140.py` from the official
   [AppleALC 1.9.6 release](https://github.com/acidanthera/AppleALC/releases/tag/1.9.6), add
   [SimpleHDA](https://github.com/laobamac/SimpleHDA) after AppleALC in `Kernel → Add`, and boot with `alcid=15`.
6. `efi/config.plist` (written by `scripts/collect-efi.sh`) is the working config with the SMBIOS blanked: generate
   your own serials with GenSMBIOS. `efi/MANIFEST.txt` lists every kext version and ACPI table of the working EFI.

### Boot arguments specific to this machine

| Argument | Purpose |
|---|---|
| `ngreen-eu=48 ngreen-l3=6` | real GPU topology for XE-TOPO |
| `ngreenblmax=17741` | duty value Apple treats as 100 % backlight |
| `ngreengammamask=0x7fffffff` | keep macOS's gamma setting minus the degamma bit (Night Shift) |
| `alcid=15` | AppleALC layout |
| `-vi2c-force-polling` | VoodooI2C polling (the touchpad's GPIO interrupt never fired) |
| `amfi_get_out_of_my_way=1` | needed by the patched Metal driver bundles; side effect: no camera/mic permission prompts |
| `ngreenhdmi=4` | optional, experimental: HDMI connector on TC2. **Freezes at boot with a screen attached** |

Never combine `alcverbs=1` with the XE-HPFIX build: both hook `IOHDACodecDevice::executeVerb`, and Lilu panics when
the earlier hook used a short jump.

## Repository layout

| Path | Contents |
|---|---|
| `patches/nootedgreen-huawei.diff` | all NootedGreen changes (against dssoftx `c4cc862`), see [docs/NOTES.md](docs/NOTES.md) |
| `acpi/` | `SSDT-TPDX`, `SSDT-PNLF`, `SSDT-HWFN` (source and compiled) |
| `applealc/` | reproducible SN6140 AppleALC build (`v1`, `v3`) |
| `tools/` | XeShim-based diagnostics (cursor plane, GMBUS/DDC probe); build against the base repo's `tools/xectl.c` and `xe.h` |
| `scripts/collect-efi.sh` | exports the running config without serials, plus a manifest |
| `scripts/windows/` | Windows helpers used for preparing the USB stick |

## Credits and license

NootedGreen by StezzaPilot, its Raptor Lake/X1 Carbon port by dssoftx (and the XPS 13 Plus work it builds on),
Lilu/AppleALC/WhateverGreen/VirtualSMC/BrightnessKeys by acidanthera, SimpleHDA by laobamac, AirportItlwm/itlwm by
OpenIntelWireless and laobamac, VoodooI2C/VoodooPS2 by their authors, USBToolBox by dhinakg, SSDTTime by CorpNewt.

The NootedGreen patch, tools and scripts derive from the base repository and are under the same
**Thou Shalt Not Profit License 1.0** ([LICENSE](LICENSE)). Third-party components keep their own licenses; see
[NOTICE.md](NOTICE.md).
