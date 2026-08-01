# refresh_test_exes.ps1 : 生成/刷新持久测试用快捷方式 exe
#
#   用途：vsthost 单插件宿主的“手动点开验证”测试 exe（Mono / Stereo / 5.1 / 7.1 / Quad）。
#         固定存放于 <项目>\test_exe\，**不要删除**；每次重新编译 vsthost.exe 后运行本脚本即可覆盖为最新版本。
#
#   行为：
#     * 目标 exe 缺失（首次或被人为删除）-> 用 --gen-shell-exes 从 Waves shell 全量生成后挑选拷贝。
#     * 目标 exe 已存在             -> 仅用最新 vsthost.exe 覆盖 exe 本体（.ini 关联不变，秒级完成）。
#
#   用法：& .\tools\refresh_test_exes.ps1   （默认使用 bin\x64\Debug\vsthost.exe）
#         可用 -ExePath 指定其他产物，如 -ExePath "bin\x64\Release\vsthost.exe"
param(
    [string]$ExePath = ""   # 留空 = bin\x64\Debug\vsthost.exe
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent          # 项目根
if (-not $ExePath) { $ExePath = Join-Path $root "bin\x64\Debug\vsthost.exe" }
if (-not (Test-Path $ExePath)) { throw "找不到 vsthost.exe：$ExePath（请先编译）" }
$exe = (Resolve-Path $ExePath).Path

$shell166 = "d:\UserData\Desktop\Project\vsthost\testWave\WaveShell1-VST3 16.6_x64.vst3"
$shell167 = "d:\UserData\Desktop\Project\vsthost\testWave17\WaveShell1-VST3 16.7_x64.vst3"
$out = Join-Path $root "test_exe"

# 目标测试 exe（相对 $out；名称与 --gen-shell-exes 输出一致）
$targets = @(
    "Mono\(WaveShell1-VST3 16.6_x64)Magma StressBox Mono.exe",
    "Stereo\(WaveShell1-VST3 16.6_x64)Magma StressBox Stereo.exe",
    "5.x\(WaveShell1-VST3 16.7_x64)C360 5.1.exe",
    "5.x\(WaveShell1-VST3 16.7_x64)B360 5.1_Quad.exe",
    "5.x\(WaveShell1-VST3 16.7_x64)B360 5.0_Quad.exe",
    "7.x\(WaveShell1-VST3 16.7_x64)Abbey Road Studio 3 7.1_Stereo.exe"
)

$missing = @($targets | Where-Object { -not (Test-Path (Join-Path $out $_)) })

# ---- 首次/缺失：全量生成后挑选 ----
if ($missing.Count -gt 0) {
    Write-Host "目标缺失 $($missing.Count) 个，从 Waves shell 生成中..."
    $gen = Join-Path $out "_gen"
    if (Test-Path $gen) { Remove-Item $gen -Recurse -Force }
    New-Item -ItemType Directory -Force -Path (Join-Path $gen "166") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $gen "167") | Out-Null

    # 注意：PowerShell 5.1 用 & 直接调用 GUI exe 传参异常（exit 128），改用 cmd /c
    function Invoke-Gen($shellPath, $outDir) {
        $cmdLine = "`"$exe`" --gen-shell-exes `"$shellPath`" --out `"$outDir`""
        cmd /c $cmdLine
        if ($LASTEXITCODE -ne 0) { Write-Host "  生成退出码 ${LASTEXITCODE}: $shellPath" }
    }
    Invoke-Gen $shell166 (Join-Path $gen "166")
    Invoke-Gen $shell167 (Join-Path $gen "167")

    foreach ($t in $targets) {
        $genRoot = if ($t -match "16\.6") { Join-Path $gen "166" } else { Join-Path $gen "167" }
        $src = Join-Path $genRoot $t
        if (Test-Path $src) {
            $dst = Join-Path $out $t
            New-Item -ItemType Directory -Force -Path (Split-Path $dst -Parent) | Out-Null
            Copy-Item $src $dst -Force
            Copy-Item "$src.ini" "$dst.ini" -Force
            Write-Host "  已生成: $t"
        } else {
            Write-Host "  [警告] 生成目录中未找到: $t"
        }
    }
    Remove-Item $gen -Recurse -Force -ErrorAction SilentlyContinue
}

# ---- 总是：用最新 vsthost.exe 覆盖 exe 本体（ini 关联不变）----
foreach ($t in $targets) {
    $dst = Join-Path $out $t
    if (Test-Path $dst) {
        Copy-Item $exe $dst -Force
        Write-Host "已刷新: $t  <-  $exe"
    }
}

Write-Host "完成。测试 exe 位于: $out"
