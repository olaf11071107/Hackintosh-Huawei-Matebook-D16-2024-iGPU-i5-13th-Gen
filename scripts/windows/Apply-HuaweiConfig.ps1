<#
  Switches the USB stick's EFI from the ThinkPad's ACPI to the Huawei MCLG-XX's.

  Put config.plist and config-accel.plist in the same folder as this script, then:
      powershell -ExecutionPolicy Bypass -File .\Apply-HuaweiConfig.ps1 -Drive E

  - moves the ThinkPad SSDTs out of E:\EFI\OC\ACPI into E:\Hackintosh\ACPI-thinkpad\
  - copies your SSDTTime tables from E:\Tools\SSDTTime\Results into E:\EFI\OC\ACPI
  - installs the new config.plist (acceleration OFF, for installing macOS), keeping the SMBIOS already on the stick
  - saves config-accel.plist (acceleration ON, for after install.sh) with the same SMBIOS to E:\Hackintosh\
  The old config.plist is kept as E:\Hackintosh\config.plist.before-huawei.
#>
param([Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z]$')][string]$Drive)

$ErrorActionPreference = 'Stop'
$Usb     = "$($Drive.ToUpper()):\"
$Oc      = Join-Path $Usb 'EFI\OC'
$Acpi    = Join-Path $Oc 'ACPI'
$Hack    = Join-Path $Usb 'Hackintosh'
$Results = Join-Path $Usb 'Tools\SSDTTime\Results'
$Here    = $PSScriptRoot
$Tables  = 'SSDT-EC.aml', 'SSDT-USBX.aml', 'SSDT-PLUG-ALT.aml', 'SSDT-RTCAWAC.aml', 'SSDT-XOSI.aml', 'SSDT-PNLF.aml'

function Step($msg) { Write-Host "`n== $msg" -ForegroundColor Cyan }

# ── checks first, so nothing is half-done ────────────────────────────────────────────────────────────────────
foreach ($f in (Join-Path $Here 'config.plist'), (Join-Path $Here 'config-accel.plist'), (Join-Path $Oc 'config.plist')) {
    if (-not (Test-Path $f)) { throw "Missing $f" }
}
foreach ($t in $Tables) { if (-not (Test-Path (Join-Path $Results $t))) { throw "Missing $Results\$t - run SSDTTime first" } }

# The SMBIOS that Get-HackintoshFiles.ps1 wrote. Keys occur once each, all under PlatformInfo.Generic.
$cur = [IO.File]::ReadAllText((Join-Path $Oc 'config.plist'))
function Get-Generic($text, $key, $tag) {
    $m = [regex]::Matches($text, "<key>$key</key>\s*<$tag>([^<]*)</$tag>")
    if ($m.Count -ne 1) { throw "expected one $key in the stick's config.plist, found $($m.Count)" }
    return $m[0].Groups[1].Value.Trim()
}
$smbios = @{}
foreach ($k in @(@('SystemSerialNumber', 'string'), @('MLB', 'string'), @('SystemUUID', 'string'), @('ROM', 'data'))) {
    $smbios[$k[0]] = @($k[1], (Get-Generic $cur $k[0] $k[1]))
}
if (-not $smbios['SystemSerialNumber'][1] -or $smbios['ROM'][1] -eq 'AAAAAAAA') {
    throw "The stick's config.plist has no SMBIOS yet. Run Get-HackintoshFiles.ps1 first."
}
Write-Host "Keeping SMBIOS: serial $($smbios['SystemSerialNumber'][1])"

function Write-WithSmbios($src, $dst) {
    $text = [IO.File]::ReadAllText($src)
    foreach ($key in $smbios.Keys) {
        $tag, $value = $smbios[$key]
        $re = "(<key>$key</key>\s*<$tag>)[^<]*(</$tag>)"
        if ([regex]::Matches($text, $re).Count -ne 1) { throw "expected one $key in $src" }
        $text = [regex]::Replace($text, $re, { param($m) $m.Groups[1].Value + $value + $m.Groups[2].Value })
    }
    [IO.File]::WriteAllText($dst, $text, (New-Object Text.UTF8Encoding($false)))
}

# ── apply ────────────────────────────────────────────────────────────────────────────────────────────────────
Step 'Backing up'
New-Item -ItemType Directory -Force -Path $Hack | Out-Null
# Back up only on the first run, so a re-run can't overwrite the ThinkPad originals with Huawei files.
$bak = Join-Path $Hack 'config.plist.before-huawei'
if (-not (Test-Path $bak)) { Copy-Item (Join-Path $Oc 'config.plist') $bak }
$tp = Join-Path $Hack 'ACPI-thinkpad'
if (-not (Test-Path $tp)) {
    New-Item -ItemType Directory -Path $tp | Out-Null
    Get-ChildItem $Acpi -Filter *.aml | Move-Item -Destination $tp
    Write-Host "   ThinkPad SSDTs moved to $tp"
} else {
    Get-ChildItem $Acpi -Filter *.aml | Remove-Item   # already backed up on an earlier run
}

Step 'Installing your SSDTs'
foreach ($t in $Tables) { Copy-Item (Join-Path $Results $t) $Acpi; Write-Host "   $t" }

Step 'Installing config.plist (acceleration off)'
Write-WithSmbios (Join-Path $Here 'config.plist') (Join-Path $Oc 'config.plist')
Write-WithSmbios (Join-Path $Here 'config-accel.plist') (Join-Path $Hack 'config-accel.plist')
Write-Host "   config-accel.plist saved to $Hack for later"

# every ACPI table the new config points at must exist, or OpenCore fails with no useful message
$new = [IO.File]::ReadAllText((Join-Path $Oc 'config.plist'))
foreach ($m in [regex]::Matches($new, '<key>Path</key>\s*<string>(SSDT-[^<]+\.aml)</string>')) {
    if (-not (Test-Path (Join-Path $Acpi $m.Groups[1].Value))) { throw "config.plist references missing ACPI\$($m.Groups[1].Value)" }
}

Step 'Done'
Write-Host @"
E:\EFI now has your Huawei's ACPI and boots macOS without acceleration.
Next: Tools\USBToolBox.exe to map your USB ports (until then the generic UTBDefault.kext is used).
"@
