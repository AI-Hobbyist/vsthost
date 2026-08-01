# build_wrappers.ps1 : 编译 Shell 独立包装器模板（shell2vst2.dll / shell2vst3.dll）
# 产物输出到 bin\wrapper\（供 --shell2vst / --gen-shell-exes 命令使用）
param()

$ErrorActionPreference = "Stop"
$root = "d:\UserData\Desktop\Project\vsthost"
$vcvars = "d:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
$out = Join-Path $root "bin\wrapper"
New-Item -ItemType Directory -Force -Path $out | Out-Null

$bat = Join-Path $env:TEMP "build_wrappers.bat"
@"
@echo off
call "$vcvars" >nul 2>&1
cd /d "$root"
cl /LD /O2 /nologo /utf-8 /I "$root\VST\vst2sdk" /I "$root\VST\vst2sdk\pluginterfaces\vst2.x" /I "$root\src\wrapper" "$root\src\wrapper\shell2vst2.cpp" /Fe:"$out\shell2vst2.dll" /link /OPT:REF /OPT:ICF
cl /LD /O2 /nologo /utf-8 /EHa /I "$root\VST\vst3sdk" /I "$root\VST\vst3sdk\base" /I "$root\VST\vst3sdk\pluginterfaces" /I "$root\src\wrapper" "$root\src\wrapper\shell2vst3.cpp" "$root\VST\vst3sdk\pluginterfaces\base\funknown.cpp" "$root\VST\vst3sdk\pluginterfaces\base\coreiids.cpp" /Fe:"$out\shell2vst3.dll" /link /OPT:REF /OPT:ICF
"@ | Set-Content -Path $bat -Encoding ASCII

Write-Host "== 编译 wrapper =="
cmd /c "`"$bat`"" 2>&1
$rc = $LASTEXITCODE
Remove-Item $bat -ErrorAction SilentlyContinue
if ($rc -ne 0) { throw "wrapper 编译失败 (rc=$rc)" }

Get-ChildItem $out | Select-Object Name, Length
Write-Host "wrapper 编译完成: $out"
