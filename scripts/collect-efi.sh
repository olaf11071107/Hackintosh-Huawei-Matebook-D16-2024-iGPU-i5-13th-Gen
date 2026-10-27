#!/bin/bash
# Copy the running OpenCore setup from the USB stick into this repository, without anything that identifies the
# machine: config.plist with the SMBIOS serial, MLB, system UUID and ROM blanked, plus a manifest of the kexts
# (bundle id, version, sha256 of the executable) and ACPI tables. Kext binaries are not copied (their own licenses).
# Usage: scripts/collect-efi.sh [/Volumes/OPENCORE]
set -euo pipefail
VOL=${1:-/Volumes/OPENCORE}
OC=$VOL/EFI/OC
REPO=$(cd "$(dirname "$0")/.." && pwd)
OUT=$REPO/efi
PB=/usr/libexec/PlistBuddy
[ -f "$OC/config.plist" ] || { echo "no $OC/config.plist"; exit 1; }
mkdir -p "$OUT"

cp "$OC/config.plist" "$OUT/config.plist"
for k in SystemSerialNumber MLB SystemUUID; do
  $PB -c "Set :PlatformInfo:Generic:$k " "$OUT/config.plist" 2>/dev/null || true
done
plutil -replace PlatformInfo.Generic.ROM -data AAAAAAAA "$OUT/config.plist"
plutil -lint "$OUT/config.plist"
SERIAL=$(ioreg -rd1 -c IOPlatformExpertDevice | awk -F'"' '/IOPlatformSerialNumber/{print $4}')
if [ -n "$SERIAL" ] && grep -qF "$SERIAL" "$OUT/config.plist"; then
  echo "serial number still present in efi/config.plist - not safe to publish"; exit 1
fi

{
  echo "# Kexts in $OC/Kexts (config order, enabled flag, bundle id, version, sha256 of executable)"
  i=0
  while $PB -c "Print :Kernel:Add:$i" "$OC/config.plist" >/dev/null 2>&1; do
    b=$($PB -c "Print :Kernel:Add:$i:BundlePath" "$OC/config.plist" 2>/dev/null || true)
    e=$($PB -c "Print :Kernel:Add:$i:Enabled" "$OC/config.plist" 2>/dev/null || true)
    x=$($PB -c "Print :Kernel:Add:$i:ExecutablePath" "$OC/config.plist" 2>/dev/null || true)
    id=$($PB -c "Print :CFBundleIdentifier" "$OC/Kexts/$b/Contents/Info.plist" 2>/dev/null || echo "?")
    v=$($PB -c "Print :CFBundleShortVersionString" "$OC/Kexts/$b/Contents/Info.plist" 2>/dev/null || echo "?")
    h=$([ -n "$x" ] && [ -f "$OC/Kexts/$b/$x" ] && shasum -a 256 "$OC/Kexts/$b/$x" | cut -c1-16 || echo "-")
    printf "%2d %-5s %-62s %-45s %-10s %s\n" "$i" "$e" "$b" "$id" "$v" "$h"
    i=$((i + 1))
  done
  echo
  echo "# ACPI tables in $OC/ACPI (md5)"
  (cd "$OC/ACPI" && md5 -r *.aml)
  echo
  echo "# OpenCore"
  nvram 4D1FDA02-38C7-4A6A-9CC6-4BCCA8B30102:opencore-version 2>/dev/null | awk '{print $2}' || true
  echo
  echo "# boot-args"
  $PB -c "Print :NVRAM:Add:7C436110-AB2A-4BBB-A880-FE41995C9F82:boot-args" "$OC/config.plist"
  echo
  echo "# macOS"
  sw_vers
} > "$OUT/MANIFEST.txt"

echo "wrote $OUT/config.plist and $OUT/MANIFEST.txt"
