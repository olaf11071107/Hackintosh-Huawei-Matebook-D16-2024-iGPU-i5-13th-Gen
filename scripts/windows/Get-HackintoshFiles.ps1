<#
  Downloads everything needed to start the Huawei MCLG-XX (i5-13420H, UHD A7A8) hackintosh onto a USB stick.

  Run it in Windows PowerShell on the Huawei, with the USB stick already formatted FAT32:
      powershell -ExecutionPolicy Bypass -File .\Get-HackintoshFiles.ps1 -Drive E

  It never formats or deletes anything. An existing EFI folder on the stick is renamed to EFI.old-<time>.

  What lands on the stick:
    E:\EFI\                       OpenCore 1.0.8 EFI (the repo's NootedGreen-OFF install EFI) + HfsPlus.efi,
                                  with a freshly generated SMBIOS written into config.plist
    E:\com.apple.recovery.boot\   macOS Tahoe recovery image from Apple (needs Python 3)
    E:\Tools\                     SSDTTime, USBToolBox (Windows.exe), macserial.exe
    E:\Hackintosh\                the repo as a zip (for install.sh on macOS later) and SMBIOS-PRIVATE.txt
#>
param(
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z]$')][string]$Drive,
    [switch]$Force,          # allow a drive Windows reports as "Fixed" (some USB SSDs)
    [switch]$SkipRecovery    # don't download the macOS recovery image
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # Invoke-WebRequest is very slow in PS 5.1 with the progress bar
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Add-Type -AssemblyName System.IO.Compression.FileSystem

$OcVer   = '1.0.8'   # must match the OpenCore.efi shipped in the repo's dist/ EFIs
$Smbios  = 'MacBookPro16,2'
$RepoZip = 'https://github.com/dssoftx/Hackintosh-X1Carbon-G11-IrisXe-Tahoe/archive/refs/heads/main.zip'
$OcZip   = "https://github.com/acidanthera/OpenCorePkg/releases/download/$OcVer/OpenCore-$OcVer-RELEASE.zip"
$HfsPlus = 'https://raw.githubusercontent.com/acidanthera/OcBinaryData/master/Drivers/HfsPlus.efi'
$SsdtZip = 'https://github.com/corpnewt/SSDTTime/archive/refs/heads/master.zip'
$UtbExe  = 'https://github.com/USBToolBox/tool/releases/latest/download/Windows.exe'

function Step($msg) { Write-Host "`n== $msg" -ForegroundColor Cyan }

# ── 1. check the stick ───────────────────────────────────────────────────────────────────────────────────────
$Drive = $Drive.ToUpper()
$vol = Get-Volume -DriveLetter $Drive -ErrorAction SilentlyContinue
if (-not $vol) { throw "No volume at ${Drive}:" }
if ($vol.FileSystem -ne 'FAT32') { throw "${Drive}: is $($vol.FileSystem), not FAT32. The firmware can only boot OpenCore from FAT32 (use Rufus for sticks over 32 GB)." }
if ($vol.DriveType -ne 'Removable' -and -not $Force) { throw "${Drive}: is a $($vol.DriveType) drive, not Removable. Re-run with -Force if this really is your USB stick." }
if ($vol.SizeRemaining -lt 3GB) { throw "${Drive}: has $([math]::Round($vol.SizeRemaining/1GB,1)) GB free; need about 3 GB." }
$Usb = "${Drive}:\"
Write-Host "Target: ${Drive}: '$($vol.FileSystemLabel)' $([math]::Round($vol.Size/1GB,1)) GB, $([math]::Round($vol.SizeRemaining/1GB,1)) GB free"

$Stage = Join-Path $env:TEMP "hackintosh-dl-$(Get-Date -Format yyyyMMdd-HHmmss)"
New-Item -ItemType Directory -Force -Path $Stage | Out-Null

function Get-File($url, $name) {
    $out = Join-Path $Stage $name
    Write-Host "   $url"
    Invoke-WebRequest -Uri $url -OutFile $out -UseBasicParsing
    return $out
}

# Extracts only the zip entries under $prefix (skips the rest of the repo and its long paths).
function Expand-ZipPrefix($zip, $prefix, $dest) {
    $z = [IO.Compression.ZipFile]::OpenRead($zip)
    try {
        foreach ($e in $z.Entries) {
            if (-not $e.FullName.StartsWith($prefix) -or $e.FullName -eq $prefix) { continue }
            $target = Join-Path $dest ($e.FullName.Substring($prefix.Length) -replace '/', '\')
            if ($e.FullName.EndsWith('/')) { New-Item -ItemType Directory -Force -Path $target | Out-Null; continue }
            New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($e, $target, $true)
        }
    } finally { $z.Dispose() }
}

# ── 2. download ──────────────────────────────────────────────────────────────────────────────────────────────
Step 'Downloading'
$repo = Get-File $RepoZip 'repo.zip'
$oc   = Get-File $OcZip   'opencore.zip'
$hfs  = Get-File $HfsPlus 'HfsPlus.efi'
$ssdt = Get-File $SsdtZip 'ssdttime.zip'
$utb  = Get-File $UtbExe  'USBToolBox.exe'

# ── 3. EFI ───────────────────────────────────────────────────────────────────────────────────────────────────
Step 'Building the EFI folder on the stick'
$efi = Join-Path $Usb 'EFI'
if (Test-Path $efi) {
    $old = "EFI.old-$(Get-Date -Format yyyyMMdd-HHmmss)"
    Rename-Item $efi $old
    Write-Host "   existing EFI renamed to $old"
}
Expand-ZipPrefix $repo 'Hackintosh-X1Carbon-G11-IrisXe-Tahoe-main/dist/EFI-install/EFI/' $efi
if (-not (Test-Path (Join-Path $efi 'OC\OpenCore.efi'))) { throw 'dist/EFI-install/EFI not found in the repo zip' }
Copy-Item $hfs (Join-Path $efi 'OC\Drivers\HfsPlus.efi')

# ── 4. tools ─────────────────────────────────────────────────────────────────────────────────────────────────
Step 'Copying tools'
$tools = Join-Path $Usb 'Tools'
New-Item -ItemType Directory -Force -Path $tools | Out-Null
Expand-ZipPrefix $ssdt 'SSDTTime-master/' (Join-Path $tools 'SSDTTime')
Copy-Item $utb (Join-Path $tools 'USBToolBox.exe')
Expand-ZipPrefix $oc 'Utilities/macserial/' (Join-Path $tools 'macserial')
Expand-ZipPrefix $oc 'Utilities/macrecovery/' (Join-Path $Stage 'macrecovery')
$hack = Join-Path $Usb 'Hackintosh'
New-Item -ItemType Directory -Force -Path $hack | Out-Null
Copy-Item $repo (Join-Path $hack 'Hackintosh-X1Carbon-G11-IrisXe-Tahoe-main.zip')

# ── 5. SMBIOS ────────────────────────────────────────────────────────────────────────────────────────────────
Step "Generating your own SMBIOS ($Smbios)"
$ms = & (Join-Path $tools 'macserial\macserial.exe') -m $Smbios -g -n 1
$m = [regex]::Match(($ms -join "`n"), '(?m)^\s*([A-Z0-9]{12})\s*\|\s*([A-Z0-9]{17})\s*$')
if (-not $m.Success) { throw "Could not parse macserial output:`n$ms" }
$serial = $m.Groups[1].Value
$mlb    = $m.Groups[2].Value
$uuid   = [guid]::NewGuid().ToString().ToUpper()
$romB   = New-Object byte[] 6; (New-Object Security.Cryptography.RNGCryptoServiceProvider).GetBytes($romB)
$rom    = [Convert]::ToBase64String($romB)

# Plain text replacement, not an XML writer: .NET rewrites the DOCTYPE and <true/> tags in ways OpenCore may reject.
# Each of these keys occurs exactly once in this config.plist (all under PlatformInfo.Generic).
$cfg = Join-Path $efi 'OC\config.plist'
$text = [IO.File]::ReadAllText($cfg)
function Set-Generic($key, $tag, $value) {
    $re = "(<key>$key</key>\s*<$tag>)[^<]*(</$tag>)"
    $n = [regex]::Matches($script:text, $re).Count
    if ($n -ne 1) { throw "expected one <$tag> for $key in config.plist, found $n" }
    $script:text = [regex]::Replace($script:text, $re, { param($m) $m.Groups[1].Value + $value + $m.Groups[2].Value })
}
Set-Generic 'SystemProductName'  'string' $Smbios
Set-Generic 'SystemSerialNumber' 'string' $serial
Set-Generic 'MLB'                'string' $mlb
Set-Generic 'SystemUUID'         'string' $uuid
Set-Generic 'ROM'                'data'   $rom
[IO.File]::WriteAllText($cfg, $text, (New-Object Text.UTF8Encoding($false)))   # no BOM

@"
PRIVATE - never post these anywhere. Reuse the SAME values in every EFI you build for this laptop
(including the accelerated one later), or iCloud/iMessage will see a new Mac each time.

SystemProductName  $Smbios
SystemSerialNumber $serial
MLB                $mlb
SystemUUID         $uuid
ROM (base64)       $rom
"@ | Set-Content -Encoding ASCII (Join-Path $hack 'SMBIOS-PRIVATE.txt')
Write-Host "   written to config.plist and Hackintosh\SMBIOS-PRIVATE.txt"

# ── 6. macOS recovery ────────────────────────────────────────────────────────────────────────────────────────
if (-not $SkipRecovery) {
    Step 'Downloading macOS Tahoe recovery from Apple (about 1-1.5 GB)'
    $py = $null
    if (Get-Command py -ErrorAction SilentlyContinue) { $py = @('py', '-3') }
    elseif ((Get-Command python -ErrorAction SilentlyContinue) -and ((& python --version 2>&1) -match 'Python 3')) { $py = @('python') }
    if (-not $py) {
        Write-Warning 'Python 3 not found, recovery skipped. Install it (winget install -e --id Python.Python.3.12), open a new PowerShell and re-run this script.'
    } else {
        $pyArgs = @($py | Select-Object -Skip 1) + @(
            (Join-Path $Stage 'macrecovery\macrecovery.py'),
            '-b', 'Mac-CFF7D910A743CAAF', '-m', '00000000000000000', '-os', 'latest',
            '-o', (Join-Path $Usb 'com.apple.recovery.boot'), 'download')
        & $py[0] @pyArgs
        if ($LASTEXITCODE -ne 0) { throw "macrecovery failed (exit $LASTEXITCODE)" }
    }
}

Remove-Item -Recurse -Force $Stage
Step 'Done'
Write-Host @"
On the stick now: EFI\, com.apple.recovery.boot\, Tools\, Hackintosh\

Before you boot it, still to do (this EFI's ACPI is the ThinkPad's, not yours):
  1. Tools\SSDTTime\SSDTTime.bat  -> dump DSDT, make SSDT-PLUG / EC / AWAC
  2. Tools\USBToolBox.exe         -> map your ports, build UTBMap.kext
  3. Put those into the EFI and update config.plist.
"@
