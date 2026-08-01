# pack.ps1 : One-click packaging for both platforms (plan section 7.4 / M6)
#
#   Copies the bit-matched vsthost.exe next to a plugin as <plugin>.exe so that
#   same-name auto-loading works. Picks x64 / x86 host by plugin PE machine.
#
#   Usage:
#     Single plugin        & .\tools\pack.ps1 -Plugin "D:\plugins\Foo.dll"
#                          & .\tools\pack.ps1 -Plugin "D:\plugins\Foo.vst3"
#     Shell internal fx    & .\tools\pack.ps1 -Plugin "D:\plugins\WaveShell1-VST3 16.6_x64.vst3" `
#                                               -Internal "Magma StressBox Stereo"
#     Options              -Config Debug|Release (default Release)
#                          -Out "D:\out" (output dir, default = plugin dir)
#
#   Shell internal mode writes "(ShellBase)Internal.exe" + a .ini
#   ([shell2vst] shell=<abs path> name=Internal) for direct internal selection.
param(
    [string]$Plugin = "",       # plugin file (.dll / .vst3; .vst3 may be a dir)
    [string]$Internal = "",     # shell internal effect name (optional)
    [string]$Config = "Release",# Debug / Release
    [string]$Out = ""           # output dir (default = plugin dir)
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent

if (-not $Plugin) { throw "-Plugin is required (plugin .dll / .vst3 path)" }
if (-not (Test-Path $Plugin)) { throw "Plugin not found: $Plugin" }
if ($Config -notmatch "^(Debug|Release)$") { throw "-Config must be Debug or Release" }

# ---- Resolve actual plugin file (.vst3 dir -> find .vst3 file inside) ----
$pluginFile = $Plugin
$item = Get-Item $Plugin
if ($item.PSIsContainer) {
    $f = Get-ChildItem -Path $Plugin -Recurse -Filter *.vst3 -File | Select-Object -First 1
    if (-not $f) { throw "No .vst3 file found inside dir: $Plugin" }
    $pluginFile = $f.FullName
}
$pluginDir = Split-Path $Plugin -Parent
$pluginBase = [System.IO.Path]::GetFileNameWithoutExtension($Plugin)

# ---- Detect plugin bitness (PE Machine) ----
function Get-PEArch([string]$path) {
    $fs = [System.IO.File]::OpenRead($path)
    try {
        $br = New-Object System.IO.BinaryReader($fs)
        $fs.Position = 0x3C
        $peOff = $br.ReadInt32()
        $fs.Position = $peOff + 4
        $machine = $br.ReadUInt16()
        if ($machine -eq 0x8664) { return "x64" }
        if ($machine -eq 0x14c)  { return "x86" }
        return ""
    } finally { $fs.Dispose() }
}
$arch = Get-PEArch $pluginFile
if (-not $arch) { throw "Cannot detect plugin bitness: $pluginFile" }
Write-Host "Plugin bitness: $arch  <-  $pluginFile"

# ---- Pick matching host exe ----
$exePath = if ($arch -eq "x64") {
    Join-Path $root "bin\x64\$Config\vsthost.exe"
} else {
    Join-Path $root "bin\Win32\$Config\vsthost.exe"
}
if (-not (Test-Path $exePath)) { throw "Host not found: $exePath (build $arch / $Config first)" }
$exe = (Resolve-Path $exePath).Path

# ---- Target name ----
function Sanitize([string]$s) { return ($s -replace '[\\/:*?"<>|]', '_') }
$targetName = ""
$needIni = $false
if ($Internal) {
    $targetName = "($(Sanitize $pluginBase))$(Sanitize $Internal).exe"
    $needIni = $true
} else {
    $targetName = "$(Sanitize $pluginBase).exe"
}

$outDir = if ($Out) { $Out } else { $pluginDir }
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$target = Join-Path $outDir $targetName

Copy-Item $exe $target -Force
Write-Host "Created: $target  <-  $exe"

# ---- Shell internal: write sidecar .ini (absolute shell path) ----
if ($needIni) {
    $ini = $target + ".ini"
    $absShell = (Resolve-Path $Plugin).Path   # 始终写绝对路径（主程序不解析相对路径）
    $lines = @(
        "[shell2vst]",
        "shell=$absShell",
        "name=$Internal"
    )
    Set-Content -Path $ini -Value $lines -Encoding Unicode
    Write-Host "Created: $ini"
}

# ---- Bundle the Chinese manual (docs\*.md) into the output dir ----
# (no non-ASCII literals here: PS 5.1 misreads BOM-less UTF-8 scripts)
$manual = Get-ChildItem -Path (Join-Path $root "docs") -Filter *.md -File `
          -ErrorAction SilentlyContinue | Select-Object -First 1
if ($manual) {
    Copy-Item $manual.FullName (Join-Path $outDir $manual.Name) -Force
    Write-Host "Bundled manual: $(Join-Path $outDir $manual.Name)"
}

Write-Host "Done. Double-click $target to auto-load."
